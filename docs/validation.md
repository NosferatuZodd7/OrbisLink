# Validation of the points marked **[VALIDATE]**

Every point in the specification marked **[VALIDATE]** was confirmed against
the official source code of the project concerned before being implemented.
Where the source differs from the specification, **the source was followed**
and the specification was corrected (see `docs/specification.md`).

Date of the check: 2026-09-21.

---

## 1. Remote Play ports (chiaki-ng)

| Port | Use | Source |
|---|---|---|
| 987/UDP | PS4 discovery | `lib/include/chiaki/discovery.h`: `#define CHIAKI_DISCOVERY_PORT_PS4 987` |
| 9302/UDP | PS5 discovery | same source: `CHIAKI_DISCOVERY_PORT_PS5 9302` |
| 9303–9319/UDP | Local ports discovery is sent from | `CHIAKI_DISCOVERY_PORT_LOCAL_MIN/MAX` |
| 9295/TCP | Control session | `lib/src/ctrl.c`: `#define SESSION_CTRL_PORT 9295` |
| 9295/TCP | Registration (PIN + Account ID) | `lib/src/regist.c`: `#define REGIST_PORT 9295` |
| 9296/UDP | Stream (video/audio/controller) | `lib/src/streamconnection.c`: `#define STREAM_CONNECTION_PORT 9296` |
| 9297/UDP | Senkusha (MTU/RTT measurement before the stream) | `lib/src/senkusha.c`: `#define SENKUSHA_PORT 9297` |

**Correction to the specification:** the table in section 2 said "9295
TCP/UDP, 9296/9297 UDP". Correct: 9295 **TCP** (control and registration),
9296/UDP (stream) and 9297/UDP (senkusha).

## 2. chiaki-ng's interface

chiaki-ng's `gui/CMakeLists.txt` requires
`Qt6 COMPONENTS Core Gui Concurrent Svg Qml Quick Widgets` and builds
`src/qml/qml.qrc`, `qmlmainwindow.cpp`, `qmlbackend.cpp`, `qmlsettings.cpp`.
**Confirmed: the current UI is Qt 6 + QML** (with `Widgets` still present).

## 3. GoldHEN's FTP server

GoldHEN's `README.md`: "FTP Server on **2121** port". The same README also
confirms "BinLoader Server on 9090 port", "Klog Server on 3232 port" and
"Internal pkg installation support (`/data/pkg`)".
**Confirmed: port 2121 and `/data/pkg` as the natural destination for pkg
files.**

On the PS5, etaHEN's FTP server listens on **1337** and is off by default
(`FTP=1` in `config.ini`); its DPI v2 installer listens on **12800**
(`DPI_v2=1`) and takes `POST /api/install` with a list of package URLs, like
the PS4 installer below.

## 4. Remote Package Installer API (flatz), port 12800

Source: `server.c` (the `s_post_handlers` table), `main.c`
(`#define SERVER_PORT (12800)`) and the project's `README`.

Existing endpoints (all POST, JSON body):

| Endpoint | Body | Reply |
|---|---|---|
| `/api/install` | `{"type":"direct","packages":[url, ...]}` or `{"type":"ref_pkg_url","url":...}` | `{ "status": "success", "task_id": <n>, "title": "..." }` |
| `/api/is_exists` | `{"title_id":"CUSA12345"}` | `{ "status": "success", "exists": "true", "size": 0x1A2B }` |
| `/api/get_task_progress` | `{"task_id":<n>}` | see below |
| `/api/find_task` | `{"content_id":"...","sub_type":<n>}` | `{ "status": "success", "task_id": <n> }` |
| `/api/start_task`, `/api/stop_task`, `/api/pause_task`, `/api/resume_task`, `/api/unregister_task` | `{"task_id":<n>}` | `{ "status": "success" }` |
| `/api/uninstall_game`, `/api/uninstall_patch` | `{"title_id":"CUSA12345"}` | `{ "status": "success" }` |
| `/api/uninstall_ac`, `/api/uninstall_theme` | `{"content_id":"..."}` | `{ "status": "success" }` |

**Corrections to the specification:**

1. **Pause/resume/cancel really exist** (the specification had a [VALIDATE]
   doubting it): `start_task`, `stop_task`, `pause_task`, `resume_task`,
   `unregister_task`.
2. **The error field is called `error_code`, not `error`:**
   `kick_error_json()` writes `{ "status": "fail", "error_code": 0x%08X }`.
   (There is a second format, `{ "status": "fail", "error": "text" }`, used by
   `kick_error()` for malformed requests — `RpiClient` handles both.)
3. **The replies are not valid JSON.** Numbers come in hexadecimal without
   quotes (`0x8002001C`) and `exists` comes as the **string**
   `"true"`/`"false"`. That is why the parser in `src/orbislink/common/json.h`
   accepts `0x…` literals and there is `Json::toLooseBool()`.
4. **`/api/install` only accepts HTTP URLs.** `pkg_setup_prerequisites()` in
   `pkg.c` downloads the parts over HTTP (`http.c` uses `sceHttp*` with a
   `Range` header and only accepts `200`/`206`). **The installer cannot be
   asked to install a local path on the console** — so "Install after
   upload" (§5.6, mode B) installs the same file from the PC instead.
5. **`/api/get_task_progress` fields** (all hexadecimal except the marked
   ones): `status`, `bits`, `error` (signed int), `length`, `transferred`,
   `length_total`, `transferred_total`, `num_index` (dec), `num_total` (dec),
   `rest_sec` (dec), `rest_sec_total` (dec), `preparing_percent` (dec),
   `local_copy_percent` (dec).
6. **Task sub-types** (README): `Game=6, AC=7, Patch=8, License=9`.
7. **The installer must be in the foreground on the console** while it
   receives commands (the README warns that the PS4 suspends background apps
   and the network stops working). Once the task has started, it can be
   minimised.

## 5. `Range` requirement on the local HTTP server

The installer's `http.c` builds the header
`Range: bytes=<offset>-<offset+size-1>` and `Accept-Encoding: identity`, and
`do_request()` only accepts `status_code == 200 || status_code == 206`.
**Confirmed: without Range support the install fails** — hence the dedicated
tests in `tests/test_local_http_server.cpp`.

## 6. PS4 PKG format (big-endian)

Source: the Remote Package Installer's `pkg.h` (`struct pkg_header`,
`struct pkg_table_entry`).

| Offset | Size | Field |
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
| — | 0x2000 | header size |

Table entry (0x20 bytes): `id` (0x00), `offset` (0x10), `size` (0x14).
IDs: `0x1000` = PARAM.SFO, `0x1200` = ICON0.PNG.

**Correction to the specification:** the field at 0x04 ("pkg type" in the
original version) is not used by the installer; the real content type is at
**0x74** (`content_type`). The specification also did not mention
`content_flags` (0x78) or `package_size` (0x430), both used by OrbisLink.

`content_type`: `0x1A` GD (app/patch/remaster), `0x1B` AC (DLC/theme),
`0x1C` AL (DLC without data), `0x1E` DP (delta patch).

`pkg_is_patch()` classifies as a patch if `content_flags` has `0x00100000`
(FIRST_PATCH) or `0x40000000` (SUBSEQUENT_PATCH) — that is how OrbisLink
tells a game from a patch, instead of relying on CATEGORY alone.

## 7. PARAM.SFO (little-endian)

Source: the same project's `sfo.c`.

Header (0x14): magic `"\0PSF"` (0x00), `version` (0x04),
`key_table_offset` (0x08), `value_table_offset` (0x0C), `entry_count` (0x10).
Index (0x10 per entry): `key_offset` u16 (0x00), `format` u16 (0x02),
`size` u32 (0x04), `max_size` u32 (0x08), `value_offset` u32 (0x0C).
Formats: `0x0004` special string, `0x0204` NUL-terminated string, `0x0404`
uint32.

**Correction to the specification:** the header field order in the original
version ("version, key_table_start, data_table_start, number of entries") is
right, but the exact names/offsets are the ones above.

## 8. `CATEGORY` codes

`gd` (game/app), `gp` (patch), `ac` (additional content) are the ones used in
practice and OrbisLink accepts them, but **the primary classification uses
the header's `content_type`/`content_flags`**, which is what the installer
itself uses (`server.c` maps `PS4GD`/`PS4AC`/`PS4AL`/`PS4DP`). No citable
official source was found for the full list of `CATEGORY` values, so
CATEGORY is only used as a tie-breaker.

## 9. FTP upload resume (`REST`/`APPE`)

There is no published documentation of GoldHEN's FTP server confirming
`REST` support for uploads. The implementation (`FtpClient::upload`) makes
resuming **optional and off by default**: it is only tried when the caller
asks for `resume=true`, and then it first checks the remote size with `SIZE`
before using `APPE`. If the server refuses, the operation fails with the
server's message and can be repeated from the start.
**TODO:** confirm on real hardware (checklist in the README).

## 10. "Not enough space" error code

Confirmed only for the libkernel family:
`ORBIS_KERNEL_ERROR_ENOSPC = 0x8002001C`
(source: `OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h`).
The specific BGFT/AppInstUtil codes the installer returns are not published
in a citable source — see `docs/error_codes.md`.
