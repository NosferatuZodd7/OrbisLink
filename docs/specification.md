# OrbisLink — Technical specification (corrected version)

> This is the original specification with the **[VALIDATE]** points resolved
> against the official sources. Where a source contradicts the original
> version, the source was followed and the change is recorded in
> [Changes from the original version](#changes-from-the-original-version) and
> detailed in [`validation.md`](validation.md).

---

## Changes from the original version

| # | Where | Original version | Corrected to | Source |
|---|---|---|---|---|
| 1 | §2 | "9295 TCP/UDP, 9296/9297 UDP" | 9295/**TCP** (control and registration), 9296/UDP (stream), 9297/UDP (senkusha), 987/UDP (PS4 discovery), 9302/UDP (PS5 discovery) | chiaki-ng's `ctrl.c`, `regist.c`, `streamconnection.c`, `senkusha.c`, `discovery.h` |
| 2 | §3.3 | "QML, following what chiaki-ng currently uses [VALIDATE]" | Confirmed: Qt 6 + QML (`Core Gui Concurrent Svg Qml Quick Widgets`) | chiaki-ng's `gui/CMakeLists.txt` |
| 3 | §5.2 | pkg type at 0x04; entry table with 7 fields | The real type is `content_type` at **0x74**; there is also `content_flags` (0x78) and `package_size` (0x430); of the 0x20-byte entry only `id` (0x00), `offset` (0x10) and `size` (0x14) matter | Remote Package Installer's `pkg.h` |
| 4 | §5.4 | Error field `"error"` | `"error_code"`, in **hexadecimal without quotes** (`{ "status": "fail", "error_code": 0x8002001C }`) | `server.c`, `kick_error_json()` |
| 5 | §5.4 | "Pause/resume/cancel [VALIDATE whether they exist]" | They exist: `start_task`, `stop_task`, `pause_task`, `resume_task`, `unregister_task` | `server.c`, `s_post_handlers` |
| 6 | §5.4 | — | `is_exists` returns `"exists"` as the **string** `"true"`/`"false"` and `size` in hexadecimal | `server.c`, `handle_api_is_exists()` |
| 7 | §5.6 mode B, step 3 | "install pointing at the console's local file [VALIDATE]" | **Not possible**: `/api/install` only accepts HTTP URLs. "Install after upload" installs the same file from the PC instead | the installer's `pkg.c`/`http.c` |
| 8 | §5.5 | Resume with `REST` [VALIDATE] | No source confirms GoldHEN supports it: resume is optional, off by default, and checks `SIZE` before using `APPE` | — (TODO on real hardware) |
| 9 | §7 | Not enough space [VALIDATE code] | `0x8002001C` (ENOSPC) confirmed; the BGFT/AppInstUtil families are still to be confirmed | OpenOrbis SDK `errors.h` |
| 10 | §3.3 | `QTcpServer`/`QNetworkAccessManager` in the core | Core in plain C++17 + libcurl; Qt only in the UI layer (reasons in [`architecture.md`](architecture.md)) | engineering decision |
| 11 | §1.2 | "No PS5 in v1" | Remote Play works on the PS5; FTP and installing on a PS5 use etaHEN's services when they respond | real PS5; etaHEN's documentation |

---

## 1. Summary

**OrbisLink** is a desktop application (Windows first; Linux and macOS
second) that brings together in one window:

1. **Remote Play** for a PS4 with HEN (GoldHEN), and a PS5 — picture, sound
   and controller, through chiaki-ng;
2. **dragging and dropping .pkg files** onto the window, which uploads and
   installs them on the console;
3. an **FTP client** connected to the console's FTP server.

### 1.1 Intended use

The app is for the user's own consoles, for homebrew and copies of games the
user legally owns. The app does **not** include, download, index or suggest
sources of content.

### 1.2 Non-goals

It does not run a jailbreak or load payloads; it does not download pkg files
from the internet; it does not work around the official Remote Play
registration.

## 2. Technical feasibility

| Component | Solution | Protocol | Ports (confirmed) |
|---|---|---|---|
| Remote Play | chiaki-ng (AGPL-3.0, Qt 6 + QML) | Remote Play (Takion) | 987/UDP PS4 discovery; 9302/UDP PS5 discovery; 9295/TCP control and registration; 9296/UDP stream; 9297/UDP senkusha |
| FTP | GoldHEN's FTP server (PS4) / etaHEN's (PS5) | anonymous FTP, PASV | 2121 / 1337 |
| Remote install | Remote Package Installer (flatz) / etaHEN DPI v2 | HTTP/JSON | 12800 |

## 3. Architecture

chiaki-ng's library, with the OrbisLink core as a separate library
(`orbislink_core`) and a Qt Quick interface. Licence of the result:
**AGPL-3.0**. See [`architecture.md`](architecture.md).

## 4. Modules

`ConsoleManager`, `PkgInspector`+`Sfo`, `LocalHttpServer`,
`IInstallerBackend`/`RpiClient`, `FtpClient`, `InstallQueue`,
`SettingsStore`, plus `common/` (tolerant JSON, rotating log, utilities),
`net/` (sockets, HTTP client, local interface choice) and `stream/` (Remote
Play).

## 5. Module details

### 5.1 ConsoleManager

Console profile (name, IP, ports) and service checks every 10 seconds and
before each task:

* **Remote Play** — state coming from chiaki-ng (`setRemotePlayState()`);
* **FTP** — TCP connection to the FTP port and reading the banner (`220`
  expected);
* **Installer** — any HTTP reply on 12800 counts as available.

Indicators: 🟢 available, 🟡 checking, 🔴 unavailable, ⚪ not available on
this console (a PS5 without the service), each with help text.

### 5.2 PkgInspector

Reads by offsets, without loading the file (supports > 4 GB). PKG header
(big-endian) and PARAM.SFO (little-endian) as in the table in
[`validation.md`](validation.md) §6 and §7. Output: `PkgInfo` with
`contentId`, `titleId`, `title`, `category`, `appVersion`, `version`, `kind`,
`contentType`, `contentFlags`, `declaredSize`, `isPatch`, `iconPng`.

Validation: invalid magic → "Not a valid PS4 pkg."; < 4 KB or unreadable →
rejected; missing metadata → accepted, as "(untitled)".

### 5.3 LocalHttpServer

Required: `Range`/`206`, `HEAD` and `GET`, `Content-Length`,
`Accept-Ranges: bytes`, `Content-Type: application/octet-stream`, files
> 4 GB, reading in blocks (1 MB by default), several simultaneous
connections.

Security: binds to the interface that reaches the console; no folder exposed
(`/f/<token>/<name>.pkg`, everything else 404); optional restriction to the
console's IP (403); the token expires when the task ends; port 8765 by
default with an automatic search for a free port.

### 5.4 RpiClient

Client for the API on port 12800. Endpoints, bodies and replies in
[`validation.md`](validation.md) §4. Progress polling every second; 10 s
timeout; 3 attempts with 1 s, 2 s and 4 s back-off. Errors described by
[`error_codes.md`](error_codes.md); unknown codes are shown in hexadecimal.
`IInstallerBackend` allows other installers.

### 5.5 FtpClient

libcurl, anonymous, passive. List (tolerant parser for Unix-style and
MS-DOS-style `LIST`), upload and download with progress and cancellation,
create folder, delete, rename, remote size. One connection at a time
(configurable up to 2); 30 s idle timeout; 3 attempts. Shortcuts: `/data/`,
`/data/pkg/`, `/data/GoldHEN/`, `/user/app/`, `/mnt/usb0/`. Protected areas
read-only without "Advanced mode".

### 5.6 InstallQueue

**Mode A (direct, default):** validate → (optional) `is_exists` → register on
the HTTP server → `install` → polling → remove the token.
**Mode B (FTP):** validate → upload to `/data/pkg/` → optionally install from
the PC (see change #7).

Sequential; automatic `gd → gp → ac` order within the same TITLE_ID; states
`Pending → Validating → Sending/Installing → Done | Error | Cancelled`;
cancel, retry, remove, move; JSON persistence with interrupted tasks going
back to "Pending"; automatic pause when the services go down.

### 5.7 UI

Single window with the stream in the middle, a top bar with the three
indicators, a side panel (Queue / Files), a DropOverlay with the two zones
("Install directly" and "Upload over FTP"), a TransferPanel with icon, title,
type, progress, speed and ETA, the FtpBrowser and the settings. English, with
Portuguese through Qt's translation system.

## 6. Flows, 7. Errors, 8. Logs, 9. Security

Unchanged from the original version, with the §7 error messages implemented
in the core (see [`error_codes.md`](error_codes.md)). Rotating log of
5 × 5 MB, `debug` off by default, sensitive data removed by
`redactSensitive()`.

## 10. Repository layout

```
src/orbislink/{common,net,pkg,http,installer,ftp,queue,settings,console,stream,qt}
qml/                       the interface
tools/cli/                 orbislink-cli (diagnostics and tests)
tools/mock-console/        fake console (FTP + API 12800) and pkg generator
tests/                     unit + integration tests
docs/                      this specification, validation.md, error_codes.md, architecture.md, status.md
```

## 11. Tests

Unit: tolerant JSON, PARAM.SFO, PkgInspector (including > 4 GB),
LocalHttpServer (Range in every form, HEAD, 404, 403, 405, 416,
concurrency), InstallQueue (ordering, persistence, pause, errors), the
`LIST` parser, the installer protocol, settings and log redaction, Remote
Play discovery and credentials, the keyboard map. Integration:
`tests/integration/run_integration.py` against the mock console. Manual
tests on real consoles: checklist kept in `README.md`.

## 12. Phases

See [`status.md`](status.md).
