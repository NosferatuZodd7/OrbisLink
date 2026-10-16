# App status

What is done and what is missing, checked against the code — not against
anyone's memory. When something changes, it changes here too.

Last review: v1.1.0.

## Legend

- ✅ done and tested (automatically, against the mock console, or both)
- 🟡 done but still to be confirmed on a real console
- ⬜ to do
- ➖ decided not to do, with the reason next to it

---

## 1. Installing .pkg files

| | |
|---|---|
| ✅ | Reading the pkg header and PARAM.SFO (title, content ID, category, version, size) |
| ✅ | Files above 4 GB (everything is 64-bit) |
| ✅ | Rejecting files that are not PS4 pkg files, with the reason |
| ✅ | Local HTTP server with `Range`/206 requests — the console downloads in pieces |
| ✅ | URLs with an unguessable token and restricted to the console's IP |
| ✅ | Automatic choice of the local IP on the console's subnet |
| ✅ | Port in use → picks another |
| ✅ | Remote Package Installer API (install, progress, already installed, pause, resume, cancel) |
| ✅ | Malformed installer replies (unquoted hexadecimal numbers, `exists` as text) |
| ✅ | Sequential queue with automatic game → patch → DLC order |
| ✅ | Stall detection (20 s without bytes) and pause when the service goes down |
| ✅ | Persistent queue: closing the app halfway does not lose the list |
| ✅ | Skipping titles already installed (optional) |
| ✅ | Dragging files and folders onto the window (looks for .pkg files inside) |
| ✅ | Choosing between installing and uploading over FTP when dropping |
| 🟡 | A 10 GB+ install on a real console |
| ✅ | Installing automatically after the FTP upload (on a PS4 the file stays on the console and is installed from the PC, because the installer only downloads over HTTP; on a PS5 it is installed by its path there) |
| ✅ | Deleting the uploaded copy from the console after installing |
| 🟡 | Installing on a PS5 through etaHEN's DPI v2: its own protocol (a form with `url`, a path on the console or an http:// address; a text reply), told apart from Remote Package Installer's JSON API by asking; checked against etaHEN's source and the fake console, not yet on a real PS5 |
| 🟡 | Installing on a PS5 through Prospero Manager (port 7070) when there is no DPI v2: its package API (`/api/pkg/install` with a path or an address, `/api/pkg/status` until *queued*), checked against its source and the fake console, not yet on a real PS5 |
| ✅ | Error 0x80990015 (a task left by an earlier try): the task is removed and the install asked again |
| ✅ | Official (store) packages told apart from fake ones (the header's type field at 0x04, as PS4 PKG Tool does), and a warning before one goes: it installs only with its licence |
| ✅ | The firmware a package needs (PUBTOOLINFO's sdk_ver, else SYSTEM_VER), shown on the queue card and in the library's details, with a warning when a game or update needs more than the PS4 in use runs (its firmware from Remote Play discovery) |
| 🟡 | PS2 games on CD kept as a raw `.bin`/`.cue` (2352 bytes a sector) packed with the ISO inside them, the image the PS2 emulator reads (the raw image made it crash, CE-34878-0); the package is byte for byte the one made from that ISO, not yet run on a real PS4 |
| ✅ | Organising the packages of a folder of this PC: renamed from a pattern of placeholders, numbered in install order, a folder per game, copies of the same package left alone; shown before anything changes |
| 🟡 | Cheats for an installed game, from GoldHEN's cheat repository (and etaHEN's PS5 one on a PS5): the files for the version installed with their cheats' names, sent to GoldHEN's or etaHEN's cheat folder (and etaHEN's list) and taken off again; checked against the real repositories, etaHEN's source and the fake consoles, not yet on a real console |
| 🟡 | Game patches on a PS4 (illusion0001's archive): each patch for the version installed with its switch, the file GoldHEN's game_patch plugin reads and the switch named by the same hash it works out (checked against the plugin's own code); not yet on a real PS4 |

## 2. FTP

| | |
|---|---|
| ✅ | Connection to the GoldHEN server (2121, anonymous, passive) |
| 🟡 | Connection to etaHEN's FTP server on a PS5 (1337, its own port setting) |
| ✅ | FTP on another port: 1337, 2121 and 21 (and 9090 on a PS5) are asked, and the one that greets as FTP is kept |
| ✅ | List, browse, create folder (one already there is fine), delete, rename |
| ✅ | Move between disks (`/data` ↔ USB), where the console cannot rename: copied through the PC, checked, then the original deleted |
| ✅ | Dropping on *Send over FTP* copies to the folder the Files tab has open |
| ✅ | Upload files with progress and cancellation |
| ✅ | Download files to the PC (menu, double click, or a chosen folder) |
| ✅ | Drag from the window to the desktop (downloads to a local cache first) |
| ✅ | Protected areas read-only, unless "advanced mode" is on |
| ✅ | Limit on simultaneous connections (1 by default, up to 2) |
| ✅ | Automatic reconnection with exponential back-off |
| ✅ | Menu per file and per folder |
| 🟡 | Resuming an interrupted upload (`REST`/`APPE` implemented; GoldHEN's server still has to be confirmed to accept it) |
| ✅ | Downloads (files and whole folders) as cards in the queue; one the console breaks (its 426) goes on from where it stopped (`REST`), while each try gets further; tested against the fake console |
| ✅ | Several files and folders picked at once (click, Ctrl+click, Shift+click) and downloaded, moved or deleted together; a folder deleted with everything inside it, what is inside first |
| ✅ | A file being uploaded listed with its percentage as soon as its upload starts; the folder's totals under the list, and its disk's free space when the server says it (`AVBL`: GoldHEN's and etaHEN's do not, so only the totals show) |
| 🟡 | Save vault: saves a PS4 copied to a USB drive (`PS4/SAVEDATA`), on the console's drives (`/mnt/usbN`) or on a drive plugged into this PC, shown and backed up into the vault folder (the drive is only read); checked with the fake console and in the tests |

## 3. Remote Play

| | |
|---|---|
| ✅ | `chiaki-lib` building without changing a line of the submodule |
| ✅ | Console discovery (987/UDP on the PS4, 9302/UDP on the PS5): state, name, version, running game |
| 🟡 | The network search: every interface's broadcast address, three rounds, every address of a small private network one by one, and the ports of a jailbroken console (2121, 1337, 12800) for one with Remote Play off; checked against the fake console and in the tests, not yet on the network that missed a real PS4 |
| ✅ | Several consoles: each in its own box on the stage, with its state; "Add console" scans the network or takes an IP |
| ✅ | One click connects: checks the console, wakes it if in rest mode, waits until it is ready and connects — or opens the registration |
| 🟡 | Waking a console from rest mode (still to be confirmed on a real console) |
| ✅ | PC registration (8-digit PIN + PSN Account ID), with the console's reason when it refuses |
| ✅ | The Account ID is accepted in hexadecimal, decimal or base64, and the app converts it. The three forms are shown under the field, with a switch to reverse the byte order. Each console keeps its own |
| ✅ | Saving and forgetting registered consoles |
| ✅ | Session with video (graphics card when possible, processor otherwise) |
| ✅ | Sound (Opus → PCM → sound card), with format conversion when the card does not accept the console's 48 kHz |
| ✅ | Keyboard as a controller, with chiaki-ng's default map; every key can be changed on the map and is saved |
| ✅ | Physical controller through SDL (DualShock, DualSense and others) |
| ✅ | Account sign-in PIN, when the console asks for it |
| ✅ | Session end reasons in words, instead of a code |
| ✅ | Discovery, registration, video and sound confirmed on a real PS4 and a real PS5 (1080p at 60 fps) |
| ✅ | Step-by-step log of each attempt (discovery → registration → prepare → connect → first frame), with timings and the exact reason for each failure |
| ✅ | Hardware decoding (d3d11va on Windows, vaapi on Linux, videotoolbox on macOS) with automatic fallback to software |
| ✅ | Microphone: 48 kHz stereo capture, encoded to Opus by chiaki and sent. Off by default and visible while capturing. Confirmed in a voice party on a real PS5 |
| 🟡 | Controller rumble (through SDL; the DualSense adaptive triggers are for later) |
| ✅ | Touchpad through the mouse |
| ✅ | Full screen (F11, Esc to leave) |
| 🟡 | Games, store, save vault and payloads as a page over the picture while a session is on screen, with an opacity slider (70–100 %, kept) and the keyboard the page's while it is open; checked with the demo stage, not yet over a real session |
| ✅ | Choosing resolution, fps and bitrate in the settings |
| ⬜ | Remote Play over PSN (needs a libcurl with WebSockets) |

## 4. Interface

| | |
|---|---|
| ✅ | Single window: stream in the middle, queue and FTP in the side panel |
| ✅ | Buttons, fields and tabs in the app's style instead of Qt's grey |
| ✅ | Service indicators (Remote Play, FTP, installer) with the reason when they are down; on a PS5 without a jailbreak they are grey and say what is needed |
| ✅ | Automatic check of the IP address while typing |
| ✅ | Drag-and-drop overlay with the two choices |
| ✅ | App, installer and shortcut icons |
| ✅ | Notices inside the window |
| ✅ | No operation fails silently: a click that cannot go ahead says why |
| ✅ | Three themes (dark, glass, light), switched in the settings without restarting |
| ✅ | Dialogs are nearly opaque and dim what is behind them |
| ✅ | A button is never narrower than its text |
| ✅ | "Liquid glass" language: stacked translucent surfaces, light edge, soft shadows, 28px corners, floating bar and panels, physics-based motion |
| 🟡 | Real blur of what is behind (needs QtQuick.Effects, Qt 6.5+; built here with 6.4, so the effect is layered) |
| ✅ | The title bar asks Windows for its own translucent material (acrylic in the "glass" theme, mica in the others) and, where there is none, the theme colour or the dark bar. The diagnostics say which one was used |
| 🟡 | Whole window translucent with the system material behind the content — means painting the window transparent, which can come out black with software rendering |
| ✅ | First-run wizard (3 steps, with a live console check); can be reopened from the settings |
| ✅ | System notifications when a long job ends with the window in the background: Windows (`Shell_NotifyIconW`), Linux (org.freedesktop.Notifications over D-Bus), macOS (osascript); elsewhere the window flashes |
| ✅ | Live log window (Ctrl+L), with a filter and Remote Play detail |
| ✅ | Exporting diagnostics to a file, with versions, network, settings (without secrets), the last Remote Play attempt and the end of the log |
| ✅ | English (source language and default) and Portuguese, chosen in the settings; the log and the diagnostics are always in English |
| ⬜ | Shortcut to the queue in full screen |
| ✅ | One queue for every job, in three groups (under way, lit and on top; waiting, with a breathing badge; finished, at the end, with one **Clear finished** for all), totals for the round (percentage, jobs to go, time left from its pace) and a compact rail with the jobs when the panel is closed while they run; the tabs only while there is a card |
| ✅ | A converted package deleted from the output folder once the console has it (a setting keeps them) |
| 🟡 | The console chip's firmware (Remote Play discovery) and the jailbreak's version when its FTP greeting gives it; checked with the fake console — GoldHEN's own greeting still to be seen, and etaHEN's does not give it |
| ✅ | Save vault: **Select all** selects everything shown (then **Select none**), and each game a button for its saves |
| ✅ | Library: packages in the console's places deleted together, after one question |
| ✅ | Payloads and plugins say what they do: the payload library's words, else what is known of the common ones (GoldHEN's plugins, PS4 and PS5 payloads), else the kind of file and its loader |
| 🟡 | Homebrew store with several stores at once: homebrew.page, PKG-Zone (its `store.db`, read through its MD5) and any Homebrew Store added; an app in more than one shows each store's tag and asks which to take it from; a store's packages downloaded (resumed, checked against their MD5) and installed through the queue on a PS4 or a PS5. Checked against PKG-Zone's real database and a fake store with the fake console, not yet on a real console |

## 5. Packaging and CI

| | |
|---|---|
| ✅ | Windows installer (NSIS) with components, firewall rule and shortcuts |
| ✅ | Portable zip for Windows |
| ✅ | .tar.gz for Linux |
| ✅ | mingw cross-compilation for the command line |
| ✅ | Check that the package carries every DLL (Qt, curl, MSVC runtime, FFmpeg, SDL2) |
| ✅ | Detection of missing graphics acceleration, with automatic compatibility mode |
| ✅ | Diagnostics script (`diagnostics.bat`) that collects the logs and the Windows event log |
| ⬜ | AppImage for Linux |
| ➖ | Code signing: decided not to. A certificate costs €200–400/year and this is not sold — the SmartScreen warning can be clicked through |
| ✅ | Update check: reads the GitHub releases, compares by semver, downloads, verifies the SHA-256 and runs the installer |
| ✅ | Stable or testing channel (pre-releases), and the repository is a setting — the project can move without recompiling |
| ✅ | Every push to the `beta` branch publishes a `vX.Y.Z-dev.N` test build, which the testing channel receives; the five most recent are kept |
| ✅ | The Windows dependencies (vcpkg) are cached per vcpkg version, and only after a complete install |
| ✅ | On every push, a look for names `<windows.h>` turns into macros (`scripts/check-win32-names.py`): the Windows build itself only runs on pull requests and by hand |

## 6. Tests

28 automated suites, all running in CI:

| | |
|---|---|
| ✅ | Tolerant JSON, PARAM.SFO, pkg header |
| ✅ | Local HTTP server (including `Range` and IP restriction) |
| ✅ | Install queue (order, stall, pause, persistence) |
| ✅ | FTP listing |
| ✅ | Remote installer protocol against a fake server |
| ✅ | Settings (including consoles, keyboard map and per-console Account ID) |
| ✅ | Keyboard map (defaults, remapping, swapping on conflict) |
| ✅ | Updates: SHA-256 against the NIST vectors, semver ordering, reading the GitHub reply and choosing the channel |
| ✅ | Service checks |
| ✅ | Remote Play discovery against a fake PS4, including the silent periodic check |
| ✅ | Remote Play credentials (including a PS5 saved without the PS5 flag) |
| ✅ | End-to-end integration against the mock console (install, FTP, download back, upload-and-install) |
| ✅ | Interface: the window opens and draws (smoke test) |
| ✅ | Interface: drag and drop follows the cursor and accepts the file |
| ✅ | Cheats and game patches: the repositories' lists (and the oddly named files in them), the cheat files of each format, a patch XML, etaHEN's lists and GoldHEN's patch switch hash (against values from game_patch's own code) |
| ✅ | Homebrew stores: a store's addresses, its MD5 answer, versions as the stores write them, which console an app is for, and reading a `store.db` laid out as PKG-Zone's |
| ⬜ | Remote Play session with synthetic video — a test that does not need a console is missing |

---

## What it takes to call it ready

In order of importance:

1. **What still needs a real console** (marked 🟡): rumble, waking from
   rest mode, and FTP/installing on a PS5 with etaHEN. It is the step no CI
   machine can take.
2. **A session test with synthetic video.** Without it, every change to the
   decoder only shows with a console in front of you.
3. **AppImage — only if Linux matters.** Today the Linux package only has
   the command line. Still to decide whether it is worth it.
