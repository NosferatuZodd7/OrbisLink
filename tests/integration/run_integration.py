#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Teste de integração do OrbisLink contra a consola falsa.

Cobre o caminho completo da Instalação direta (pkg → servidor HTTP local com
Range → API do instalador → progresso → conclusão) e as operações de FTP,
sem precisar de uma PS4.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "mock-console"))

import make_test_pkg  # noqa: E402


def child_env() -> dict[str, str]:
    """Força UTF-8 nos processos filhos (o OrbisLink escreve sempre em UTF-8)."""
    environment = dict(os.environ)
    environment["PYTHONUTF8"] = "1"
    environment["PYTHONIOENCODING"] = "utf-8"
    return environment


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def digest(path: str) -> str:
    hasher = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            hasher.update(block)
    return hasher.hexdigest()


class Runner:
    def __init__(self, cli: str) -> None:
        self.cli = cli
        self.failures: list[str] = []

    def check(self, condition: bool, description: str) -> bool:
        print(("[ok]   " if condition else "[FALHA] ") + description, flush=True)
        if not condition:
            self.failures.append(description)
        return condition

    def run(self, *arguments: str, expect: int | None = 0) -> subprocess.CompletedProcess:
        # encoding explícito: em Windows o Python decodificaria a saída em
        # cp1252 e rebentaria nos acentos e nos indicadores 🟢/🔴.
        result = subprocess.run([self.cli, *arguments], capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=180,
                                env=child_env())
        if expect is not None and result.returncode != expect:
            print(f"        comando: {' '.join(arguments)}")
            print(f"        saída  : {result.stdout.strip()}")
            print(f"        erro   : {result.stderr.strip()}")
        return result


def main() -> int:
    # A própria saída deste script leva acentos e emojis.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True)
    parser.add_argument("--mock", required=True)
    arguments = parser.parse_args()

    runner = Runner(arguments.cli)
    workspace = tempfile.mkdtemp(prefix="orbislink-integration-")
    console_root = os.path.join(workspace, "consola")
    os.makedirs(console_root)

    # pkg sintético com ~3 MB, suficiente para vários pedidos Range.
    pkg_path = os.path.join(workspace, "jogo de teste.pkg")
    with open(pkg_path, "wb") as handle:
        handle.write(make_test_pkg.build_pkg("UP0001-CUSA12345_00-ORBISLINKTEST001",
                                             "Jogo de Teste", "gd", "01.00",
                                             3 * 1024 * 1024))
    pkg_digest = digest(pkg_path)

    ftp_port = free_port()
    api_port = free_port()
    http_port = free_port()

    mock = subprocess.Popen(
        [sys.executable, arguments.mock, "--ftp-port", str(ftp_port),
         "--api-port", str(api_port), "--root", console_root, "--print-ports"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", errors="replace", env=child_env())
    try:
        line = mock.stdout.readline()
        info = json.loads(line)
        print(f"consola falsa: {info}", flush=True)

        common = ["--host", "127.0.0.1", "--ftp-port", str(info["ftp_port"]),
                  "--installer-port", str(info["api_port"])]

        # 1. Verificação de serviços
        services = runner.run("services", *common)
        runner.check(services.returncode == 0, "services devolve sucesso")
        runner.check("🟢" in services.stdout, "services mostra pelo menos um serviço disponível")
        runner.check(services.stdout.count("🟢") == 2, "FTP e instalador estão ambos disponíveis")

        # 2. Metadados do pkg
        inspect = runner.run("inspect", pkg_path)
        runner.check("CUSA12345" in inspect.stdout, "inspect lê o TITLE_ID")
        runner.check("Jogo de Teste" in inspect.stdout, "inspect lê o título")

        # 3. Instalação direta ponta a ponta
        install = runner.run("install", *common, "--http-port", str(http_port),
                             "--bind", "127.0.0.1", "--timeout", "120", pkg_path)
        runner.check(install.returncode == 0, "install termina com sucesso")
        runner.check("Concluído" in install.stdout, "a tarefa chega ao estado Concluído")

        downloads = os.path.join(console_root, "data", "downloads")
        received = [os.path.join(downloads, name) for name in os.listdir(downloads)]
        runner.check(len(received) == 1, "a consola falsa recebeu exatamente um pkg")
        if received:
            runner.check(digest(received[0]) == pkg_digest,
                         "o pkg recebido por pedidos Range é idêntico ao original")

        # 4. FTP: envio, listagem e descarga
        remote = "/data/pkg/jogo.pkg"
        put = runner.run("ftp-put", *common, pkg_path, remote)
        runner.check(put.returncode == 0, "ftp-put envia o ficheiro")
        uploaded = os.path.join(console_root, "data", "pkg", "jogo.pkg")
        runner.check(os.path.exists(uploaded) and digest(uploaded) == pkg_digest,
                     "o ficheiro chega íntegro à consola")

        listing = runner.run("ftp-ls", *common, "/data/pkg")
        runner.check("jogo.pkg" in listing.stdout, "ftp-ls mostra o ficheiro enviado")

        downloaded = os.path.join(workspace, "de-volta.pkg")
        get = runner.run("ftp-get", *common, remote, downloaded)
        runner.check(get.returncode == 0 and digest(downloaded) == pkg_digest,
                     "ftp-get traz o ficheiro de volta sem alterações")

        mkdir = runner.run("ftp-mkdir", *common, "/data/pkg/subpasta")
        runner.check(mkdir.returncode == 0
                     and os.path.isdir(os.path.join(console_root, "data", "pkg", "subpasta")),
                     "ftp-mkdir cria a pasta")

        remove = runner.run("ftp-rm", *common, remote)
        runner.check(remove.returncode == 0 and not os.path.exists(uploaded),
                     "ftp-rm apaga o ficheiro")

        # 5. Enviar por FTP e instalar a seguir, numa só passagem
        antes = len(os.listdir(downloads))
        duplo = runner.run("install", *common, "--ftp", "--install-after-upload",
                           "--http-port", str(http_port), "--bind", "127.0.0.1",
                           "--timeout", "120", pkg_path)
        runner.check(duplo.returncode == 0, "install --ftp --install-after-upload termina bem")
        # O nome remoto vem do ficheiro local, com os espaços trocados por
        # underscores (a fila higieniza o nome antes de o enviar).
        enviado = os.path.join(console_root, "data", "pkg", "jogo_de_teste.pkg")
        runner.check(os.path.exists(enviado) and digest(enviado) == pkg_digest,
                     "o pkg fica guardado na consola depois do envio")
        runner.check(len(os.listdir(downloads)) == antes + 1,
                     "e é logo instalado a partir do PC")
        if os.path.exists(enviado):
            os.remove(enviado)

        # 6. O mesmo, mas apagando a cópia da consola no fim
        antes = len(os.listdir(downloads))
        limpo = runner.run("install", *common, "--ftp", "--install-after-upload",
                           "--delete-after-install", "--http-port", str(http_port),
                           "--bind", "127.0.0.1", "--timeout", "120", pkg_path)
        runner.check(limpo.returncode == 0, "install com --delete-after-install termina bem")
        runner.check(len(os.listdir(downloads)) == antes + 1, "instalou a partir do PC")
        runner.check(not os.path.exists(enviado),
                     "e a cópia enviada já não está na consola")

        # 7. Zona protegida continua bloqueada sem modo avançado
        blocked = runner.run("ftp-mkdir", *common, "/system/teste", expect=1)
        runner.check(blocked.returncode != 0 and "Zona protegida" in blocked.stderr,
                     "escrever em /system é recusado sem Modo avançado")
    finally:
        mock.terminate()
        try:
            mock.wait(timeout=10)
        except subprocess.TimeoutExpired:
            mock.kill()

    if runner.failures:
        print(f"\n{len(runner.failures)} verificação(ões) falharam:", flush=True)
        for failure in runner.failures:
            print(f"  - {failure}", flush=True)
        return 1
    print("\nIntegração: tudo passou.", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
