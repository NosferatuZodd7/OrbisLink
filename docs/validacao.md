# Validação dos pontos marcados **[VALIDAR]**

Cada ponto da especificação marcado com **[VALIDAR]** foi confirmado em
código-fonte oficial do projeto referido antes de ser implementado. Onde a
fonte difere da especificação, **seguiu-se a fonte** e a especificação foi
corrigida (ver `docs/especificacao.md`).

Data da verificação: 2026-09-21.

---

## 1. Portas do Remote Play (chiaki-ng)

| Porta | Uso | Fonte |
|---|---|---|
| 987/UDP | Descoberta de PS4 | `lib/include/chiaki/discovery.h`: `#define CHIAKI_DISCOVERY_PORT_PS4 987` |
| 9302/UDP | Descoberta de PS5 (fora do âmbito da v1) | mesma fonte: `CHIAKI_DISCOVERY_PORT_PS5 9302` |
| 9303–9319/UDP | Portas locais de onde a descoberta é enviada | `CHIAKI_DISCOVERY_PORT_LOCAL_MIN/MAX` |
| 9295/TCP | Sessão de controlo | `lib/src/ctrl.c`: `#define SESSION_CTRL_PORT 9295` |
| 9295/TCP | Registo (PIN + Account ID) | `lib/src/regist.c`: `#define REGIST_PORT 9295` |
| 9296/UDP | Stream (vídeo/áudio/comando) | `lib/src/streamconnection.c`: `#define STREAM_CONNECTION_PORT 9296` |
| 9297/UDP | Senkusha (medição de MTU/RTT antes do stream) | `lib/src/senkusha.c`: `#define SENKUSHA_PORT 9297` |

**Correção à especificação:** a tabela da secção 2 dizia "9295 TCP/UDP,
9296/9297 UDP". O correto é: 9295 **TCP** (controlo e registo), 9296/UDP
(stream) e 9297/UDP (senkusha).

## 2. Interface do chiaki-ng

`gui/CMakeLists.txt` do chiaki-ng exige
`Qt6 COMPONENTS Core Gui Concurrent Svg Qml Quick Widgets` e compila
`src/qml/qml.qrc`, `qmlmainwindow.cpp`, `qmlbackend.cpp`, `qmlsettings.cpp`.
**Confirmado: a UI atual é Qt 6 + QML** (com `Widgets` ainda presente).

## 3. Servidor FTP do GoldHEN

`README.md` do GoldHEN: "FTP Server on **2121** port". O mesmo README
confirma ainda "BinLoader Server on 9090 port", "Klog Server on 3232 port" e
"Internal pkg installation support (`/data/pkg`)".
**Confirmado: porta 2121 e a pasta `/data/pkg` como destino natural dos pkg.**

## 4. API do Remote Package Installer (flatz), porta 12800

Fonte: `server.c` (tabela `s_post_handlers`), `main.c`
(`#define SERVER_PORT (12800)`) e o `README` do projeto.

Endpoints existentes (todos POST, corpo JSON):

| Endpoint | Corpo | Resposta |
|---|---|---|
| `/api/install` | `{"type":"direct","packages":[url, ...]}` ou `{"type":"ref_pkg_url","url":...}` | `{ "status": "success", "task_id": <n>, "title": "..." }` |
| `/api/is_exists` | `{"title_id":"CUSA12345"}` | `{ "status": "success", "exists": "true", "size": 0x1A2B }` |
| `/api/get_task_progress` | `{"task_id":<n>}` | ver abaixo |
| `/api/find_task` | `{"content_id":"...","sub_type":<n>}` | `{ "status": "success", "task_id": <n> }` |
| `/api/start_task`, `/api/stop_task`, `/api/pause_task`, `/api/resume_task`, `/api/unregister_task` | `{"task_id":<n>}` | `{ "status": "success" }` |
| `/api/uninstall_game`, `/api/uninstall_patch` | `{"title_id":"CUSA12345"}` | `{ "status": "success" }` |
| `/api/uninstall_ac`, `/api/uninstall_theme` | `{"content_id":"..."}` | `{ "status": "success" }` |

**Correções à especificação:**

1. **Pausar/retomar/cancelar existem mesmo** (a especificação punha um
   [VALIDAR] a duvidar): `start_task`, `stop_task`, `pause_task`,
   `resume_task`, `unregister_task`.
2. **O campo de erro chama-se `error_code`, não `error`:**
   `kick_error_json()` escreve
   `{ "status": "fail", "error_code": 0x%08X }`.
   (Há um segundo formato, `{ "status": "fail", "error": "texto" }`, usado
   por `kick_error()` para pedidos malformados — o `RpiClient` trata os dois.)
3. **As respostas não são JSON válido.** Os números vêm em hexadecimal sem
   aspas (`0x8002001C`) e `exists` vem como **string** `"true"`/`"false"`.
   Por isso o parser em `src/orbislink/common/json.h` aceita literais `0x…`
   e existe `Json::toLooseBool()`.
4. **`/api/install` só aceita URLs HTTP.** `pkg_setup_prerequisites()` em
   `pkg.c` descarrega as partes por HTTP (`http.c` usa `sceHttp*` com
   cabeçalho `Range` e aceita apenas `200`/`206`). **Não é possível pedir ao
   instalador que instale um caminho local da consola** — logo, a opção
   "Instalar após upload" (§5.6, Modo B) não pode ser implementada como
   estava escrita. O OrbisLink informa o utilizador em vez de falhar.
5. **Campos de `/api/get_task_progress`** (todos em hexadecimal exceto os
   marcados): `status`, `bits`, `error` (int com sinal), `length`,
   `transferred`, `length_total`, `transferred_total`, `num_index` (dec),
   `num_total` (dec), `rest_sec` (dec), `rest_sec_total` (dec),
   `preparing_percent` (dec), `local_copy_percent` (dec).
6. **Sub-tipos de tarefa** (README): `Game=6, AC=7, Patch=8, License=9`.
7. **O instalador tem de estar em primeiro plano na consola** enquanto
   recebe comandos (o README avisa que a PS4 suspende a app em segundo plano
   e a rede deixa de funcionar). Depois de a tarefa arrancar, já pode ser
   minimizado.

## 5. Requisito de `Range` no servidor HTTP local

`http.c` do instalador monta o cabeçalho
`Range: bytes=<offset>-<offset+size-1>` e `Accept-Encoding: identity`, e
`do_request()` só aceita `status_code == 200 || status_code == 206`.
**Confirmado: sem suporte a Range a instalação falha** — daí os testes
dedicados em `tests/test_local_http_server.cpp`.

## 6. Formato PKG da PS4 (big-endian)

Fonte: `pkg.h` do Remote Package Installer (`struct pkg_header`,
`struct pkg_table_entry`).

| Offset | Tamanho | Campo |
|---|---|---|
| 0x00 | 4 | magic `7F 43 4E 54` (`"\x7FCNT"`) |
| 0x10 | 4 | `entry_count` |
| 0x14 | 2 | `sc_entry_count` |
| 0x18 | 4 | `entry_table_offset` |
| 0x40 | 0x24 (36) | `content_id` (ASCII) |
| 0x74 | 4 | `content_type` |
| 0x78 | 4 | `content_flags` |
| 0x430 | 8 | `package_size` |
| 0xFE0 | 0x20 | `digest` |
| — | 0x2000 | tamanho do cabeçalho |

Entrada da tabela (0x20 bytes): `id` (0x00), `offset` (0x10), `size` (0x14).
IDs: `0x1000` = PARAM.SFO, `0x1200` = ICON0.PNG.

**Correção à especificação:** o campo em 0x04 ("Tipo de pkg" na versão
original) não é usado pelo instalador; o tipo de conteúdo real está em
**0x74** (`content_type`). A especificação também não mencionava
`content_flags` (0x78) nem `package_size` (0x430), ambos usados pelo
OrbisLink.

`content_type`: `0x1A` GD (app/patch/remaster), `0x1B` AC (DLC/tema),
`0x1C` AL (DLC sem dados), `0x1E` DP (patch delta).

`pkg_is_patch()` classifica como patch se `content_flags` tiver
`0x00100000` (FIRST_PATCH) ou `0x40000000` (SUBSEQUENT_PATCH) — é assim que
o OrbisLink distingue jogo de patch, em vez de confiar apenas no CATEGORY.

## 7. PARAM.SFO (little-endian)

Fonte: `sfo.c` do mesmo projeto.

Cabeçalho (0x14): magic `"\0PSF"` (0x00), `version` (0x04),
`key_table_offset` (0x08), `value_table_offset` (0x0C), `entry_count` (0x10).
Índice (0x10 por entrada): `key_offset` u16 (0x00), `format` u16 (0x02),
`size` u32 (0x04), `max_size` u32 (0x08), `value_offset` u32 (0x0C).
Formatos: `0x0004` string especial, `0x0204` string com NUL, `0x0404` uint32.

**Correção à especificação:** a ordem dos campos do cabeçalho na versão
original ("versão, key_table_start, data_table_start, número de entradas")
está certa, mas os nomes/offsets exatos são os acima.

## 8. Códigos `CATEGORY`

`gd` (jogo/app), `gp` (patch), `ac` (conteúdo adicional) são os usados na
prática e o OrbisLink aceita-os, mas **a classificação primária é feita pelo
`content_type`/`content_flags` do cabeçalho**, que é o que o próprio
instalador usa (`server.c` mapeia `PS4GD`/`PS4AC`/`PS4AL`/`PS4DP`). Não foi
encontrada fonte oficial citável para a lista completa de valores de
`CATEGORY`; por isso o CATEGORY só é usado como desempate.

## 9. Retoma de upload por FTP (`REST`/`APPE`)

Não há documentação publicada do servidor FTP do GoldHEN que confirme
suporte a `REST` em uploads. A implementação (`FtpClient::upload`) faz a
retoma **opcional e desligada por omissão**: só é tentada quando o chamador
pede `resume=true`, e nesse caso confirma primeiro o tamanho remoto com
`SIZE` antes de usar `APPE`. Se o servidor recusar, a operação falha com a
mensagem do servidor e pode ser repetida do início.
**TODO:** confirmar em hardware real (checklist §11.3).

## 10. Código de erro de "espaço insuficiente"

Confirmado apenas para a família do libkernel:
`ORBIS_KERNEL_ERROR_ENOSPC = 0x8002001C`
(fonte: `OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h`).
Os códigos específicos do BGFT/AppInstUtil que o instalador devolve não
estão publicados em fonte citável — ver `docs/error_codes.md`.
