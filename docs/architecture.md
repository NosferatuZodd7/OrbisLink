# Architecture

## Overview

```
┌──────────────────────── OrbisLink ────────────────────────┐
│                                                            │
│  UI (Qt 6 / QML)                                           │
│   StreamArea (chiaki-ng) · DropOverlay · TransferPanel     │
│   FtpBrowser · SettingsDialog                              │
│                                                            │
│  ─────────────── orbislink_core (C++17) ───────────────    │
│   ConsoleManager   service checks                          │
│   PkgInspector     PKG header + PARAM.SFO + ICON0          │
│   LocalHttpServer  serves pkg files to the console (Range) │
│   IInstallerBackend ← RpiClient (API on 12800)             │
│   FtpClient        libcurl, GoldHEN / etaHEN FTP           │
│   InstallQueue     sequential queue, persistence           │
│   SettingsStore    JSON in the data folder                 │
│   common/          json, rotating log, utilities           │
│   net/             sockets, HTTP client, local IP choice   │
│                                                            │
│  ─────────────── orbislink_stream (chiaki-lib) ────────    │
│   discovery · registration · session · credentials         │
└────────────────────────────────────────────────────────────┘
```

## Why the core does not depend on Qt

The specification expected `QTcpServer`/`QNetworkAccessManager`. The core was
written in plain C++17 (plus libcurl) for three practical reasons:

1. **Testability and CI.** The core tests and the integration test run
   without Qt, without a display server and without a console. Qt in every
   core test would mean hundreds of MB of dependencies to test logic that is
   not interface.
2. **The queue and the HTTP server must not depend on the UI event loop.**
   The requirement "the stream must not stutter" is easier to guarantee with
   their own threads than with slots on the UI thread.
3. **The Qt layer stays thin.** The UI hooks into
   `InstallQueue::setListener()`, `ConsoleManager::setListener()` and the
   getters — it only has to re-emit them as Qt signals in an adapter
   (`AppController`, `StreamController`).

What the specification asked for still holds: no network or disk operation
runs on the UI thread, and every module has a clear interface with mocks in
the tests (`IInstallerBackend` is the proof: the queue tests use a fake
installer).

## Threads

| Thread | Owner | What it does |
|---|---|---|
| UI | Qt | only draws and forwards events |
| `LocalHttpServer::acceptThread_` | HTTP server | accepts connections |
| one per HTTP connection | HTTP server | serves one Range; detached, counted in `activeWorkers_` |
| `InstallQueue::worker_` | queue | runs **one** task at a time |
| `ConsoleManager::thread_` | console | checks the services every 10 s |
| FTP calls | caller's thread | limited to 1 (up to 2) by `FtpClient::Slot` |
| chiaki's own threads | Remote Play | session, video, audio, discovery |

Rules: all shared state goes through `std::mutex`; *listeners* are called
**outside** the lock (the UI can re-enter the core without a deadlock); each
module's `stop()` is idempotent and waits for its threads.

## Direct install flow (mode A)

```
file.pkg
   │ PkgInspector.inspect()            checks the magic, reads PARAM.SFO/ICON0
   ▼
InstallQueue.enqueue()                 orders gd → gp → ac by TITLE_ID
   │
   ├─ (optional) RpiClient.isExists()  "Reinstall / Skip"
   │
   ├─ LocalHttpServer.registerFile()   random token → /f/<token>/<name>.pkg
   │
   ├─ RpiClient.installDirect([url])   POST /api/install  → task_id
   │
   ├─ the console sends HEAD + GET with Range to the local server
   │
   ├─ RpiClient.taskProgress(task_id)  POST every second → bytes, ETA, error
   │     · 0 bytes served after 20 s  → "the console cannot reach the PC"
   │     · installer down             → queue paused, task back to Pending
   │
   └─ LocalHttpServer.unregisterFile() the token expires when the task ends
```

## FTP upload flow (mode B)

```
file → FtpClient.upload() → /data/pkg/<name>.pkg (progress and cancellation)
```

"Install after upload" cannot point the installer at the uploaded file: the
installer API only accepts HTTP URLs (see `docs/validation.md` §4.4). So the
file stays on the console as a copy and is installed from the PC, through
the local HTTP server, like a direct install. Optionally the uploaded copy is
deleted afterwards.

## Security

* The HTTP server binds to **one specific interface** (the one that reaches
  the console), never to `0.0.0.0` by default.
* No folder is exposed: every file has a random 16-byte token and any other
  path returns 404.
* `allowedClient` restricts requests to the console's IP (403 for the rest);
  `allowLoopback` exists only for development and tests.
* The token is removed when the task ends, is cancelled or fails.
* System paths on FTP (`/system`, `/system_ex`, `/preinst`, …) are read-only
  unless "Advanced mode" is on.
* `redactSensitive()` strips the Account ID, registration keys, tokens and
  passwords from everything that goes into the log.

## Status

What is done and what is missing, item by item, is in [`status.md`](status.md).

## Remote Play (`src/orbislink/stream/`)

`chiaki-lib` comes in as a library, without a single line changed in the
submodule. `cmake/ChiakiLib.cmake` explains why chiaki-ng's top-level
CMakeLists.txt is not used: it requires a libcurl with WebSockets, which is
only for playing over PSN. The `third-party/` subdirectories (nanopb and
jerasure) and `lib/` are driven directly.

| File | What it does |
|---|---|
| `chiaki_log_bridge` | sends chiaki's log to the same file as everything else, and keeps the console's refusal reason |
| `discovery` | asks the console (987/UDP on the PS4, 9302/UDP on the PS5) and returns state, name, version and target; scans the network; wakes it |
| `credentials` | stores and reads the registered consoles, in a file separate from the settings |
| `registration` | 8-digit PIN + PSN Account ID → registration key and rp_key |
| `session` | connects, receives video and audio, sends the controller state and the microphone, turns the end-of-session reasons into words |

On the Qt side:

| File | What it does |
|---|---|
| `qt/stream_controller` | what QML sees; marshals chiaki's callbacks to the UI thread; the one-click connect |
| `qt/video_bridge` | AVFrame (YUV420P) → QVideoFrame, plane by plane; the conversion to RGB is left to the graphics card |
| `qt/audio_output` | PCM → QAudioSink, with a half-second cap on the queue so latency does not grow |
| `qt/audio_input` | microphone capture for the console |
| `qt/input_map` | keyboard → controller buttons and axes, with chiaki-ng's default map and the user's changes |

`qml/orbislink/StreamVideo.qml` is the only file with `import QtMultimedia`,
and it is loaded by a `Loader`: on a machine without that module only the
video fails, not the whole window.

**Threads.** chiaki has its own. The state, video and audio callbacks arrive
from there; everything that touches QML properties goes through
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.

## Translations

The source language is English. `translations/orbislink_pt_PT.ts` is the
Portuguese translation. Texts born outside Qt (the core and Remote Play) are
marked with `QT_TRANSLATE_NOOP("Messages", ...)` (`common/tr.h`) and
translated when they reach the interface (`qt/translate_message.h`). The log
and the diagnostics report are always in English.
