#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Consola falsa para testar o OrbisLink sem uma PS4.

Levanta dois serviços que imitam o que uma PS4 com GoldHEN expõe:

* servidor FTP anónimo (por omissão na porta 2121), com PASV/EPSV;
* API HTTP do Remote Package Installer (por omissão na porta 12800), que
  descarrega mesmo o pkg do servidor HTTP do OrbisLink usando pedidos Range,
  tal como a consola faz.

As respostas replicam as do instalador original (flatz), incluindo as
particularidades que não são JSON válido: números em hexadecimal sem aspas
e o campo "exists" como texto.

Uso:
    python3 mock_console.py [--ftp-port 2121] [--api-port 12800]
                            [--root PASTA] [--print-ports]
"""

from __future__ import annotations

import argparse
import http.server
import json
import os
import shutil
import socket
import socketserver
import struct
import sys
import tempfile
import threading
import time
import urllib.request

# ---------------------------------------------------------------- FTP

class FtpSession(threading.Thread):
    """Uma ligação de controlo FTP."""

    def __init__(self, conn: socket.socket, root: str) -> None:
        super().__init__(daemon=True)
        self.conn = conn
        self.root = os.path.abspath(root)
        self.cwd = "/"
        self.data_listener: socket.socket | None = None
        self.rename_from: str | None = None
        self.rest_offset = 0

    # -- utilitários -------------------------------------------------
    def send(self, line: str) -> None:
        self.conn.sendall((line + "\r\n").encode("utf-8", "replace"))

    def local_path(self, remote: str) -> str:
        if not remote.startswith("/"):
            remote = self.cwd.rstrip("/") + "/" + remote
        parts: list[str] = []
        for part in remote.split("/"):
            if part in ("", "."):
                continue
            if part == "..":
                if parts:
                    parts.pop()
                continue
            parts.append(part)
        return os.path.join(self.root, *parts)

    def open_data(self) -> socket.socket | None:
        if not self.data_listener:
            self.send("425 Sem ligação de dados")
            return None
        self.data_listener.settimeout(15)
        try:
            data, _ = self.data_listener.accept()
        except OSError:
            self.send("425 Falha na ligação de dados")
            return None
        finally:
            self.data_listener.close()
            self.data_listener = None
        return data

    def make_passive(self) -> int:
        if self.data_listener:
            self.data_listener.close()
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        self.data_listener = listener
        return listener.getsockname()[1]

    @staticmethod
    def listing_line(path: str, name: str) -> str:
        stats = os.stat(path)
        is_dir = os.path.isdir(path)
        perms = "drwxr-xr-x" if is_dir else "-rw-r--r--"
        stamp = time.strftime("%b %d %H:%M", time.localtime(stats.st_mtime))
        return f"{perms}   1 ps4      ps4      {stats.st_size:>12} {stamp} {name}"

    # -- ciclo de vida ----------------------------------------------
    def run(self) -> None:
        try:
            self.send("220 GoldHEN FTP Server (mock-console)")
            buffer = b""
            while True:
                chunk = self.conn.recv(4096)
                if not chunk:
                    break
                buffer += chunk
                while b"\r\n" in buffer or b"\n" in buffer:
                    separator = b"\r\n" if b"\r\n" in buffer else b"\n"
                    raw, buffer = buffer.split(separator, 1)
                    line = raw.decode("utf-8", "replace").strip()
                    if not line:
                        continue
                    if not self.handle(line):
                        return
        except OSError:
            pass
        finally:
            if self.data_listener:
                self.data_listener.close()
            self.conn.close()

    def handle(self, line: str) -> bool:
        parts = line.split(" ", 1)
        command = parts[0].upper()
        argument = parts[1] if len(parts) > 1 else ""

        if command in ("USER", "PASS"):
            self.send("230 Sessão iniciada" if command == "PASS" else "331 Indica a palavra-passe")
        elif command == "SYST":
            self.send("215 UNIX Type: L8")
        elif command == "FEAT":
            self.send("211-Extensões suportadas")
            self.send(" SIZE")
            self.send(" REST STREAM")
            self.send(" PASV")
            self.send("211 Fim")
        elif command == "OPTS":
            self.send("200 Ok")
        elif command == "TYPE":
            self.send("200 Tipo definido")
        elif command == "PWD":
            self.send(f'257 "{self.cwd}" é a pasta atual')
        elif command == "CWD":
            target = argument if argument.startswith("/") else self.cwd.rstrip("/") + "/" + argument
            if os.path.isdir(self.local_path(target)):
                self.cwd = "/" + target.strip("/")
                self.send("250 Pasta alterada")
            else:
                self.send("550 Pasta inexistente")
        elif command == "CDUP":
            self.cwd = "/" + "/".join(self.cwd.strip("/").split("/")[:-1])
            self.send("250 Pasta alterada")
        elif command == "PASV":
            port = self.make_passive()
            self.send(f"227 Entering Passive Mode (127,0,0,1,{port >> 8},{port & 0xFF})")
        elif command == "EPSV":
            port = self.make_passive()
            self.send(f"229 Entering Extended Passive Mode (|||{port}|)")
        elif command in ("LIST", "NLST"):
            self.command_list(argument, names_only=command == "NLST")
        elif command == "SIZE":
            path = self.local_path(argument)
            if os.path.isfile(path):
                self.send(f"213 {os.path.getsize(path)}")
            else:
                self.send("550 Ficheiro inexistente")
        elif command == "REST":
            try:
                self.rest_offset = int(argument)
                self.send(f"350 A retomar a partir de {self.rest_offset}")
            except ValueError:
                self.send("501 Offset inválido")
        elif command == "RETR":
            self.command_retr(argument)
        elif command in ("STOR", "APPE"):
            self.command_stor(argument, append=command == "APPE")
        elif command == "DELE":
            path = self.local_path(argument)
            try:
                os.remove(path)
                self.send("250 Ficheiro apagado")
            except OSError:
                self.send("550 Não foi possível apagar")
        elif command == "MKD":
            try:
                os.makedirs(self.local_path(argument), exist_ok=False)
                self.send(f'257 "{argument}" criada')
            except OSError:
                self.send("550 Não foi possível criar")
        elif command == "RMD":
            try:
                os.rmdir(self.local_path(argument))
                self.send("250 Pasta apagada")
            except OSError:
                self.send("550 Não foi possível apagar")
        elif command == "RNFR":
            self.rename_from = self.local_path(argument)
            self.send("350 Indica o novo nome")
        elif command == "RNTO":
            if not self.rename_from:
                self.send("503 Falta o RNFR")
            else:
                try:
                    os.replace(self.rename_from, self.local_path(argument))
                    self.send("250 Nome alterado")
                except OSError:
                    self.send("550 Não foi possível mudar o nome")
                self.rename_from = None
        elif command == "NOOP":
            self.send("200 Ok")
        elif command == "QUIT":
            self.send("221 Adeus")
            return False
        else:
            self.send("502 Comando não suportado")
        return True

    def command_list(self, argument: str, names_only: bool) -> None:
        target = argument.strip()
        if target.startswith("-"):  # ignora flags tipo "-a"
            target = ""
        path = self.local_path(target or self.cwd)
        self.send("150 A abrir a ligação de dados")
        data = self.open_data()
        if not data:
            return
        try:
            if os.path.isdir(path):
                for name in sorted(os.listdir(path)):
                    entry = os.path.join(path, name)
                    line = name if names_only else self.listing_line(entry, name)
                    data.sendall((line + "\r\n").encode("utf-8", "replace"))
            self.send("226 Transferência completa")
        finally:
            data.close()

    def command_retr(self, argument: str) -> None:
        path = self.local_path(argument)
        if not os.path.isfile(path):
            self.send("550 Ficheiro inexistente")
            return
        self.send("150 A enviar o ficheiro")
        data = self.open_data()
        if not data:
            return
        try:
            with open(path, "rb") as handle:
                if self.rest_offset:
                    handle.seek(self.rest_offset)
                    self.rest_offset = 0
                shutil.copyfileobj(handle, data.makefile("wb"))
            self.send("226 Transferência completa")
        finally:
            data.close()

    def command_stor(self, argument: str, append: bool) -> None:
        path = self.local_path(argument)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.send("150 A receber o ficheiro")
        data = self.open_data()
        if not data:
            return
        mode = "ab" if (append or self.rest_offset) else "wb"
        self.rest_offset = 0
        try:
            with open(path, mode) as handle:
                while True:
                    chunk = data.recv(65536)
                    if not chunk:
                        break
                    handle.write(chunk)
            self.send("226 Transferência completa")
        finally:
            data.close()


class FtpServer(threading.Thread):
    def __init__(self, port: int, root: str) -> None:
        super().__init__(daemon=True)
        self.root = root
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", port))
        self.listener.listen(8)
        self.port = self.listener.getsockname()[1]
        self.running = True

    def run(self) -> None:
        while self.running:
            try:
                conn, _ = self.listener.accept()
            except OSError:
                break
            FtpSession(conn, self.root).start()

    def stop(self) -> None:
        self.running = False
        try:
            self.listener.close()
        except OSError:
            pass


# ------------------------------------------------- API do instalador

def read_pkg_title(url: str) -> tuple[str, str]:
    """Lê TITLE/TITLE_ID do PARAM.SFO via pedidos Range, como o instalador real.

    O Remote Package Installer faz exatamente isto antes de registar a tarefa
    (pkg_setup_prerequisites em pkg.c): puxa o cabeçalho, localiza a entrada
    0x1000 e lê o PARAM.SFO. Aqui serve para a consola falsa devolver um
    título realista — e para exercitar o suporte a Range do servidor local.
    """
    def fetch(offset: int, size: int) -> bytes:
        request = urllib.request.Request(url)
        request.add_header("Range", f"bytes={offset}-{offset + size - 1}")
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.read()

    try:
        header = fetch(0, 0x2000)
        if header[0:4] != b"\x7fCNT":
            return ("", "")
        entry_count = struct.unpack_from(">I", header, 0x10)[0]
        table_offset = struct.unpack_from(">I", header, 0x18)[0]
        if entry_count > 4096:
            return ("", "")
        table = fetch(table_offset, entry_count * 0x20)
        for index in range(entry_count):
            base = index * 0x20
            entry_id = struct.unpack_from(">I", table, base)[0]
            if entry_id != 0x1000:  # PARAM.SFO
                continue
            sfo_offset = struct.unpack_from(">I", table, base + 0x10)[0]
            sfo_size = struct.unpack_from(">I", table, base + 0x14)[0]
            sfo = fetch(sfo_offset, sfo_size)
            if sfo[0:4] != b"\x00PSF":
                return ("", "")
            key_table = struct.unpack_from("<I", sfo, 0x08)[0]
            value_table = struct.unpack_from("<I", sfo, 0x0C)[0]
            count = struct.unpack_from("<I", sfo, 0x10)[0]
            values = {}
            for i in range(count):
                base_index = 0x14 + i * 0x10
                key_offset, _fmt, value_size, _max, value_offset = struct.unpack_from(
                    "<HHIII", sfo, base_index)
                key_start = key_table + key_offset
                key_end = sfo.index(b"\0", key_start)
                key = sfo[key_start:key_end].decode("utf-8", "replace")
                raw = sfo[value_table + value_offset:value_table + value_offset + value_size]
                values[key] = raw.split(b"\0")[0].decode("utf-8", "replace")
            return (values.get("TITLE", ""), values.get("TITLE_ID", ""))
    except Exception:  # noqa: BLE001 - o título é só cosmético
        pass
    return ("", "")


class InstallerState:
    """Tarefas de instalação simuladas."""

    def __init__(self, download_dir: str, chunk_size: int = 256 * 1024,
                 chunk_delay: float = 0.0) -> None:
        self.download_dir = download_dir
        self.chunk_size = chunk_size
        self.chunk_delay = chunk_delay
        self.lock = threading.Lock()
        self.tasks: dict[int, dict] = {}
        self.installed: dict[str, int] = {}
        self.next_id = 1

    def start_install(self, urls: list[str]) -> tuple[int, str]:
        with self.lock:
            task_id = self.next_id
            self.next_id += 1
            title, title_id = read_pkg_title(urls[0])
            if not title:
                title = os.path.splitext(os.path.basename(urls[0]))[0]
            task = {
                "id": task_id,
                "urls": urls,
                "transferred": 0,
                "length": 0,
                "error": 0,
                "title": title,
                "title_id": title_id,
                "paused": False,
                "stopped": False,
                "ranges": 0,
            }
            self.tasks[task_id] = task
        threading.Thread(target=self._download, args=(task,), daemon=True).start()
        return task_id, task["title"]

    def _download(self, task: dict) -> None:
        destination = os.path.join(self.download_dir, f"task{task['id']}.pkg")
        try:
            for url in task["urls"]:
                # HEAD para saber o tamanho, como a consola faz antes de puxar
                # as partes do pkg.
                head = urllib.request.Request(url, method="HEAD")
                with urllib.request.urlopen(head, timeout=10) as response:
                    total = int(response.headers.get("Content-Length", "0"))
                    if response.headers.get("Accept-Ranges") != "bytes":
                        raise RuntimeError("servidor sem suporte a Range")
                with self.lock:
                    task["length"] += total

                offset = 0
                with open(destination, "ab") as output:
                    while offset < total:
                        if task["stopped"]:
                            return
                        while task["paused"]:
                            time.sleep(0.05)
                        end = min(offset + self.chunk_size, total) - 1
                        request = urllib.request.Request(url)
                        request.add_header("Range", f"bytes={offset}-{end}")
                        request.add_header("Accept-Encoding", "identity")
                        with urllib.request.urlopen(request, timeout=20) as response:
                            if response.status != 206:
                                raise RuntimeError(
                                    f"esperava 206, veio {response.status}")
                            chunk = response.read()
                        output.write(chunk)
                        offset += len(chunk)
                        if self.chunk_delay:
                            time.sleep(self.chunk_delay)
                        with self.lock:
                            task["transferred"] += len(chunk)
                            task["ranges"] += 1
            with self.lock:
                self.installed[task.get("title_id") or "CUSA00000"] = task["length"]
                task["path"] = destination
        except Exception as error:  # noqa: BLE001 - a consola só devolve um código
            with self.lock:
                task["error"] = 0x80020005  # ORBIS_KERNEL_ERROR_EIO
                task["message"] = str(error)


class InstallerApiHandler(http.server.BaseHTTPRequestHandler):
    state: InstallerState = None  # type: ignore[assignment]
    protocol_version = "HTTP/1.1"

    def log_message(self, *args) -> None:  # silencia o log por linha
        pass

    def _reply(self, body: str) -> None:
        payload = (body + "\n").encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def _fail(self, code: int) -> None:
        # Formato real: hexadecimal sem aspas (não é JSON válido de propósito).
        self._reply('{ "status": "fail", "error_code": 0x%08X }' % code)

    def do_GET(self) -> None:  # noqa: N802
        self._reply('{ "status": "success" }')

    def do_POST(self) -> None:  # noqa: N802
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode("utf-8") if length else "{}"
        try:
            request = json.loads(raw)
        except json.JSONDecodeError:
            self._reply('{ "status": "fail", "error": "Invalid JSON format." }')
            return

        path = self.path.split("?")[0]
        state = InstallerApiHandler.state

        if path == "/api/install":
            if request.get("type") != "direct":
                self._fail(0x80020016)
                return
            packages = request.get("packages") or []
            if not packages:
                self._fail(0x80020016)
                return
            task_id, title = state.start_install(packages)
            self._reply(
                '{ "status": "success", "task_id": %d, "title": "%s" }' % (task_id, title))
        elif path == "/api/get_task_progress":
            task = state.tasks.get(int(request.get("task_id", -1)))
            if not task:
                self._fail(0x80020002)
                return
            with state.lock:
                self._reply(
                    '{ "status": "success", "bits": 0x1, "error": %d, "length": 0x%X, '
                    '"transferred": 0x%X, "length_total": 0x%X, "transferred_total": 0x%X, '
                    '"num_index": 0, "num_total": 1, "rest_sec": 0, "rest_sec_total": 0, '
                    '"preparing_percent": 100, "local_copy_percent": 0 }'
                    % (task["error"], task["length"], task["transferred"],
                       task["length"], task["transferred"]))
        elif path == "/api/is_exists":
            title_id = request.get("title_id", "")
            if title_id in state.installed:
                self._reply('{ "status": "success", "exists": "true", "size": 0x%X }'
                            % state.installed[title_id])
            else:
                self._reply('{ "status": "success", "exists": "false" }')
        elif path in ("/api/stop_task", "/api/pause_task", "/api/resume_task",
                      "/api/start_task", "/api/unregister_task"):
            task = state.tasks.get(int(request.get("task_id", -1)))
            if not task:
                self._fail(0x80020002)
                return
            if path == "/api/stop_task":
                task["stopped"] = True
            elif path == "/api/pause_task":
                task["paused"] = True
            elif path in ("/api/resume_task", "/api/start_task"):
                task["paused"] = False
            self._reply('{ "status": "success" }')
        elif path == "/api/find_task":
            self._reply('{ "status": "success", "task_id": 1 }')
        elif path in ("/api/uninstall_game", "/api/uninstall_patch", "/api/uninstall_ac",
                      "/api/uninstall_theme"):
            state.installed.pop(request.get("title_id", ""), None)
            self._reply('{ "status": "success" }')
        else:
            self.send_error(404, "Not Found")


class ThreadingHttpServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True



class DiscoveryServer:
    """Responde ao pedido de descoberta do Remote Play (987/UDP no PS4).

    O formato é o de uma resposta HTTP dentro de um datagrama, tal como o
    chiaki-ng o interpreta em lib/src/discovery.c: código 200 quando a
    consola está acordada, 620 quando está em repouso.
    """

    def __init__(self, port: int, state: str = "ready", name: str = "PS4 da sala") -> None:
        self.port = port
        self.state = state
        self.name = name
        self._socket: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False

    def start(self) -> None:
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind(("0.0.0.0", self.port))
        self.port = self._socket.getsockname()[1]
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._socket:
            self._socket.close()

    def _response(self) -> bytes:
        if self.state == "standby":
            linhas = ["HTTP/1.1 620 Server Standby"]
        else:
            linhas = ["HTTP/1.1 200 Ok"]
        linhas += [
            "host-id:1122334455AA",
            "host-type:PS4",
            f"host-name:{self.name}",
            "host-request-port:997",
            "device-discovery-protocol-version:00020020",
            "system-version:09000000",
        ]
        if self.state == "ready":
            linhas += ["running-app-name:Bloodborne", "running-app-titleid:CUSA00207"]
        return ("\r\n".join(linhas) + "\r\n").encode("utf-8")

    def _loop(self) -> None:
        while self._running:
            try:
                data, addr = self._socket.recvfrom(2048)
            except OSError:
                return
            if not data or b"SRCH" not in data:
                continue
            try:
                self._socket.sendto(self._response(), addr)
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Consola falsa para o OrbisLink")
    parser.add_argument("--ftp-port", type=int, default=2121)
    parser.add_argument("--api-port", type=int, default=12800)
    parser.add_argument("--root", default=None, help="pasta que faz de sistema de ficheiros da consola")
    parser.add_argument("--slow", type=float, default=0.0, metavar="SEGUNDOS",
                        help="atraso por bloco descarregado (para demonstrações)")
    parser.add_argument("--discovery-port", type=int, default=0, metavar="PORTA",
                        help="responde à descoberta do Remote Play nesta porta "
                             "(987 é a real, mas precisa de privilégios)")
    parser.add_argument("--discovery-state", default="ready", choices=["ready", "standby"],
                        help="estado que a consola falsa diz ter")
    parser.add_argument("--print-ports", action="store_true",
                        help="imprime as portas escolhidas em JSON e continua")
    arguments = parser.parse_args()

    root = arguments.root or tempfile.mkdtemp(prefix="orbislink-mock-")
    os.makedirs(os.path.join(root, "data", "pkg"), exist_ok=True)
    os.makedirs(os.path.join(root, "mnt", "usb0"), exist_ok=True)
    downloads = os.path.join(root, "data", "downloads")
    os.makedirs(downloads, exist_ok=True)

    ftp = FtpServer(arguments.ftp_port, root)
    ftp.start()

    InstallerApiHandler.state = InstallerState(downloads, chunk_delay=arguments.slow)
    api = ThreadingHttpServer(("127.0.0.1", arguments.api_port), InstallerApiHandler)
    api_thread = threading.Thread(target=api.serve_forever, daemon=True)
    api_thread.start()

    discovery = None
    if arguments.discovery_port:
        discovery = DiscoveryServer(arguments.discovery_port, arguments.discovery_state)
        try:
            discovery.start()
        except PermissionError:
            print("Sem permissão para a porta da descoberta; Remote Play desligado.", flush=True)
            discovery = None

    info = {"ftp_port": ftp.port, "api_port": api.server_address[1], "root": root}
    if discovery:
        info["discovery_port"] = discovery.port
    if arguments.print_ports:
        print(json.dumps(info), flush=True)
    else:
        print(f"Consola falsa: FTP em 127.0.0.1:{info['ftp_port']}, "
              f"API em 127.0.0.1:{info['api_port']} (raiz: {root})", flush=True)

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        api.shutdown()
        ftp.stop()
        if discovery:
            discovery.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
