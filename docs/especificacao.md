# OrbisLink — Especificação Técnica (versão corrigida)

> Esta é a especificação original com os pontos **[VALIDAR]** resolvidos
> contra as fontes oficiais. Onde a fonte contradiz a versão original,
> seguiu-se a fonte e a alteração está registada em
> [Alterações face à versão original](#alterações-face-à-versão-original) e
> detalhada em [`validacao.md`](validacao.md).

---

## Alterações face à versão original

| # | Onde | Versão original | Corrigido para | Fonte |
|---|---|---|---|---|
| 1 | §2 | "9295 TCP/UDP, 9296/9297 UDP" | 9295/**TCP** (controlo e registo), 9296/UDP (stream), 9297/UDP (senkusha), 987/UDP (descoberta PS4) | `ctrl.c`, `regist.c`, `streamconnection.c`, `senkusha.c`, `discovery.h` do chiaki-ng |
| 2 | §3.3 | "QML, seguindo o que o chiaki-ng usa atualmente [VALIDAR]" | Confirmado: Qt 6 + QML (`Core Gui Concurrent Svg Qml Quick Widgets`) | `gui/CMakeLists.txt` do chiaki-ng |
| 3 | §5.2 | Tipo de pkg em 0x04; tabela de entradas com 7 campos | O tipo real é `content_type` em **0x74**; há também `content_flags` (0x78) e `package_size` (0x430); da entrada de 0x20 bytes só interessam `id` (0x00), `offset` (0x10) e `size` (0x14) | `pkg.h` do Remote Package Installer |
| 4 | §5.4 | Campo de erro `"error"` | `"error_code"`, em **hexadecimal sem aspas** (`{ "status": "fail", "error_code": 0x8002001C }`) | `server.c`, `kick_error_json()` |
| 5 | §5.4 | "Pausar/retomar/cancelar [VALIDAR se existem]" | Existem: `start_task`, `stop_task`, `pause_task`, `resume_task`, `unregister_task` | `server.c`, `s_post_handlers` |
| 6 | §5.4 | — | `is_exists` devolve `"exists"` como **string** `"true"`/`"false"` e `size` em hexadecimal | `server.c`, `handle_api_is_exists()` |
| 7 | §5.6 Modo B, passo 3 | "instalar apontando para o ficheiro local da consola [VALIDAR]" | **Não é possível**: `/api/install` só aceita URLs HTTP. A app informa "Enviado — instala na consola" | `pkg.c`/`http.c` do instalador |
| 8 | §5.5 | Retoma com `REST` [VALIDAR] | Sem fonte que confirme suporte no GoldHEN: a retoma é opcional, desligada por omissão, e confirma `SIZE` antes de usar `APPE` | — (TODO em hardware real) |
| 9 | §7 | Espaço insuficiente [VALIDAR código] | `0x8002001C` (ENOSPC) confirmado; famílias BGFT/AppInstUtil continuam por confirmar | OpenOrbis SDK `errors.h` |
| 10 | §3.3 | `QTcpServer`/`QNetworkAccessManager` no núcleo | Núcleo em C++17 puro + libcurl; Qt só na camada de UI (justificação em [`arquitetura.md`](arquitetura.md)) | decisão de engenharia |

---

## 1. Resumo

O **OrbisLink** é uma aplicação de desktop (Windows prioritário; Linux e
macOS secundários) que junta numa só janela:

1. **Remote Play** de uma PS4 com HEN (GoldHEN) — imagem, som e comando,
   através do chiaki-ng;
2. **arrastar e largar ficheiros .pkg** para a janela, que os envia e
   instala na consola;
3. **cliente FTP** ligado ao servidor FTP do GoldHEN.

### 1.1 Âmbito de uso

A app destina-se a consolas do próprio utilizador, para homebrew e cópias de
jogos que o utilizador possui legalmente. A app **não** inclui, descarrega,
indexa nem sugere fontes de conteúdo.

### 1.2 Não-objetivos

Não executa o jailbreak nem carrega payloads; não descarrega pkg da
internet; não suporta PS5 na v1 (a arquitetura permite acrescentar); não
contorna o registo oficial de Remote Play.

## 2. Viabilidade técnica

| Componente | Solução | Protocolo | Portas (confirmadas) |
|---|---|---|---|
| Remote Play | chiaki-ng (AGPL-3.0, Qt 6 + QML) | Remote Play (Takion) | 987/UDP descoberta PS4; 9295/TCP controlo e registo; 9296/UDP stream; 9297/UDP senkusha |
| FTP | Servidor FTP do GoldHEN | FTP anónimo, PASV | 2121 |
| Instalação remota | Remote Package Installer (flatz) | HTTP/JSON | 12800 |

## 3. Arquitetura

Fork do chiaki-ng, com o núcleo do OrbisLink como biblioteca separada
(`orbislink_core`) e a UI acrescentada ao projeto Qt existente. Licença do
resultado: **AGPL-3.0**. Ver [`arquitetura.md`](arquitetura.md).

## 4. Módulos

`ConsoleManager`, `PkgInspector`+`Sfo`, `LocalHttpServer`,
`IInstallerBackend`/`RpiClient`, `FtpClient`, `InstallQueue`,
`SettingsStore`, mais `common/` (JSON tolerante, log rotativo, utilidades)
e `net/` (sockets, cliente HTTP, escolha de interface local).

## 5. Detalhes por módulo

### 5.1 ConsoleManager

Perfil da consola (nome, IP, portas) e verificação dos serviços de 10 em 10
segundos e antes de cada tarefa:

* **Remote Play** — estado vindo do chiaki-ng (`setRemotePlayState()`);
* **FTP** — ligação TCP à 2121 e leitura do banner (espera-se `220`);
* **Instalador** — qualquer resposta HTTP na 12800 conta como disponível.

Indicadores: 🟢 disponível, 🟡 a verificar, 🔴 indisponível, cada um com
texto de ajuda.

### 5.2 PkgInspector

Lê por offsets, sem carregar o ficheiro (suporta > 4 GB). Cabeçalho PKG
(big-endian) e PARAM.SFO (little-endian) conforme a tabela de
[`validacao.md`](validacao.md) §6 e §7. Saída: `PkgInfo` com `contentId`,
`titleId`, `title`, `category`, `appVersion`, `version`, `kind`,
`contentType`, `contentFlags`, `declaredSize`, `isPatch`, `iconPng`.

Validações: magic inválido → "Não é um pkg PS4 válido."; < 4 KB ou ilegível
→ rejeitado; metadados em falta → aceite, com "(sem título)".

### 5.3 LocalHttpServer

Obrigatório: `Range`/`206`, `HEAD` e `GET`, `Content-Length`,
`Accept-Ranges: bytes`, `Content-Type: application/octet-stream`, ficheiros
> 4 GB, leitura por blocos (1 MB por omissão), várias ligações simultâneas.

Segurança: bind à interface que alcança a consola; nenhuma pasta exposta
(`/f/<token>/<nome>.pkg`, tudo o resto 404); restrição opcional ao IP da
consola (403); token expira no fim da tarefa; porta 8765 por omissão com
procura automática de porta livre.

### 5.4 RpiClient

Cliente da API da porta 12800. Endpoints, corpos e respostas em
[`validacao.md`](validacao.md) §4. Polling de progresso de 1 em 1 s;
timeout de 10 s; 3 tentativas com backoff de 1 s, 2 s e 4 s. Erros
traduzidos por [`error_codes.md`](error_codes.md); códigos desconhecidos
aparecem em hexadecimal. `IInstallerBackend` permite outros instaladores.

### 5.5 FtpClient

libcurl, porta 2121, anónimo, passivo. Listar (parser tolerante de `LIST`
estilo Unix e MS-DOS), enviar e descarregar com progresso e cancelamento,
criar pasta, apagar, mudar nome, tamanho remoto. Uma ligação de cada vez
(configurável até 2); timeout de inatividade 30 s; 3 tentativas.
Atalhos: `/data/`, `/data/pkg/`, `/data/GoldHEN/`, `/user/app/`,
`/mnt/usb0/`. Zonas protegidas só de leitura sem "Modo avançado".

### 5.6 InstallQueue

**Modo A (direto, por omissão):** validar → (opcional) `is_exists` →
registar no servidor HTTP → `install` → polling → remover token.
**Modo B (FTP):** validar → enviar para `/data/pkg/` → informar (ver
correção #7).

Sequencial; ordem automática `gd → gp → ac` dentro do mesmo TITLE_ID;
estados `Pendente → A validar → A enviar/A instalar → Concluído | Erro |
Cancelado`; cancelar, repetir, remover, mover; persistência em JSON com as
tarefas interrompidas a voltarem a "Pendente"; pausa automática quando os
serviços caem.

### 5.7 UI (Fase 3+)

Janela única com o stream ao centro, barra superior com os três
indicadores, painel lateral recolhível (Fila / Ficheiros), DropOverlay com
as duas zonas ("Instalar diretamente" e "Enviar por FTP"), TransferPanel com
ícone, título, tipo, progresso, velocidade e ETA, FtpBrowser e
SettingsView. Português (pt-PT) e Inglês pelo sistema de tradução do Qt.

## 6. Fluxos, 7. Erros, 8. Logs, 9. Segurança

Inalterados face à versão original, com as mensagens de erro de §7 já
implementadas no núcleo (ver [`error_codes.md`](error_codes.md)).
Log rotativo de 5 × 5 MB, `debug` desligado por omissão, dados sensíveis
removidos por `redactSensitive()`.

## 10. Estrutura do repositório

```
src/orbislink/{common,net,pkg,http,installer,ftp,queue,settings,console}
tools/cli/                 orbislink-cli (diagnóstico e testes)
tools/mock-console/        consola falsa (FTP + API 12800) e gerador de pkg
tests/                     testes unitários + integração
docs/                      esta especificação, validacao.md, error_codes.md, arquitetura.md
```

## 11. Testes

Unitários: JSON tolerante, PARAM.SFO, PkgInspector (incluindo > 4 GB),
LocalHttpServer (Range em todas as formas, HEAD, 404, 403, 405, 416,
concorrência), InstallQueue (ordenação, persistência, pausa, erros),
parser de `LIST`, protocolo do instalador, definições e redação de logs.
Integração: `tests/integration/run_integration.py` contra a consola falsa.
Testes manuais em PS4 real: checklist mantida em `README.md`.

## 12. Fases

Ver a tabela de estado em [`arquitetura.md`](arquitetura.md).
