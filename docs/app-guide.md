# OrbisLink — how the app is put together

A map of every screen, every button and what it is expected to do. It is
written for people (testers, translators, contributors) and kept up to date
with the code, so a change can be checked against it: if a button here does
something else in the app, one of the two is wrong.

## What the app does

Three jobs, around one PlayStation at a time — the **console in use**:

1. **Remote Play** — the console's picture, sound and controls on the PC.
2. **Files** — packages (`.pkg`) and any other file to the console, over FTP
   or by direct install, and files back from it.
3. **PS1/PS2 games** — disc images on the PC turned into PS4 packages, then
   (optionally) sent and installed.

## Words used in the app

| Word | Meaning |
|---|---|
| Console in use | The one console every action goes to. Changing it is done from its card, the console chip (top bar) or Settings → Console in use. |
| Saved consoles | Every console added; one of them is in use. |
| Jailbreak | GoldHEN, etaHEN or HEN running on the console. Detected when its FTP server answers; shown in **gold**. |
| FTP | The console's FTP server (jailbreak only). Needed to send files, browse, and to send converted games. |
| Installer | Remote Package Installer (RPI) open on the console. Needed to install. |
| Queue | Everything being sent or installed, one at a time, in order. |
| Classics files | The PS1/PS2 emulator files a converted game needs; downloaded once by the app. |

## The window

```
┌ top bar ─────────────────────────────────────────────────────────────┐
│ logo  console name/IP        [console chip] [Remote Play] [FTP] [Installer]  ⟳ ▣ 🗎 ◐ ⚙ │
├──────────────────────────────────────────────┬───────────────────────┤
│ stage: consoles · Remote Play · PS1/PS2 Games │ side panel: Queue | Files (FTP) │
├──────────────────────────────────────────────┴───────────────────────┤
│ status line: last message                     Local HTTP: … · version │
└──────────────────────────────────────────────────────────────────────┘
```

## Top bar

| Item | What it shows | Click |
|---|---|---|
| Logo, name, IP | The console in use. | — |
| **Back to Remote Play** | Only while a Remote Play session runs and another page is open. | Back to the picture. |
| **PS1/PS2 Games** | Highlighted while that page is open. | Opens / closes the games page. |
| **Console chip** | See *Console chip* below. | See below. |
| Remote Play / FTP / Installer | A dot per service of the console in use: green available, amber checking, red not answering, grey not applicable. Hover: why. | — |
| ⟳ | | Checks the three services now. |
| ▣ | | Shows / hides the side panel (F9). Locked while full screen. |
| 🗎 | | Log and diagnostics (Ctrl+L): log, copy, save a diagnostics file. |
| ◐ | | Dark / system / light theme. |
| ⚙ | | Settings (Ctrl+,). |

### Console chip

Left of the service dots, the console the file actions go to:

| Situation | Looks like | Click |
|---|---|---|
| Console in use answers on FTP (jailbreak) | Its name in **gold**, the jailbreak's name (GoldHEN, etaHEN, HEN) beside it, a glow pulsing slowly. Sending and installing work. | Opens the consoles page. |
| It answers, but only for Remote Play | Its name in the app's blue, pulsing. Remote Play works; sending and installing do not. | Opens the consoles page. |
| It does not answer | Its name faded, "not connected" ("looking…" while checking). | Looks for it again (checks the services now). |
| No console saved | Not shown. | — |

Remote Play is not needed for anything but the picture: converting, sending
and installing only need the console's FTP (and the installer to install).

## Consoles page (home)

One square card per saved console, plus **Add console**. The cards are
centred and wrap onto more rows.

| On a card | Does |
|---|---|
| Card body (console in use) | Remote Play: connects with one click (wakes it from rest mode if needed). |
| Card body (another console) | Makes it the console in use and connects. |
| **Remote Play** | Same as the body. While connecting: **Cancel**. |
| **FTP** | Makes it the console in use and opens the Files tab, without Remote Play. |
| 🔗 | Registers this PC for Remote Play (the console shows an 8-digit PIN). |
| ✕ | Removes the console from the list (asks first). |
| Gold badge | The console has a jailbreak (FTP answers). |

**Add console**: name, IP and type; it is probed straight away.

## Remote Play session

The picture fills the stage. Moving the mouse shows the toolbar:

| Button | Does |
|---|---|
| 🎮 | Keyboard map: which key presses which button. The touchpad key (T) puts a finger on the left half of the touchpad and then clicks — Select in PS2 games. A controller's own touchpad is passed on as it is (left half Select, right half Start). Keyboard and controller can be used together. **Change keys** to rebind (click a key, press the new one; two keys swap); **Reset** goes back to the defaults. |
| Speaker | Mutes / unmutes the console's sound. |
| Microphone | Sends / stops the PC microphone. |
| Full screen | Full screen on / off (F11; Esc leaves the session). |
| **End the session** | Disconnects. |

## Side panel

### Queue tab

Each file being sent or installed is a card: title, size, state, bar,
speed and time left.

| On a card | Does |
|---|---|
| ↑ ↓ | Moves a waiting task up or down. |
| ⟲ | Tries a failed or cancelled task again. |
| ✕ | Cancels a running task; removes a finished one. |

Footer: total speed, what is left of the total, **Pause / Resume** for the whole queue. Each has a fixed place: the button stays on the right whatever the figures say.

Every upload and install is followed here, never in the Files tab: sending
opens this tab, and the file list keeps showing the console's folders (a file
still arriving is listed with "arriving…").

When Remote Package Installer stops answering, the queue pauses and shows
*Waiting for Remote Package Installer*: it checks every few seconds and goes
on by itself when the installer is back (**Try now** forces it).

A converted PS1/PS2 game shows as **one** card with three bars — Convert,
Send, Install — instead of separate queue cards (see *PS1/PS2 Games*);
closing it takes its finished queue tasks along. A package sent and then
installed is one card too (the install's). The tab's number counts cards.

### Files (FTP) tab

The console's files. Needs FTP.

| Action | Does |
|---|---|
| Path field, ↑, ⟳ | Where you are; one level up; list again. |
| Pinned folders (tags) | Click: opens it. ✕: removes it from the top. **Pin this folder** pins the one that is open (also *Pin to the top* in a folder's menu). They start as `/data/`, `/data/pkg/`, `/data/GoldHEN/`, `/user/app/`, `/mnt/usb0/` and are kept in the settings. |
| One click on a folder | Opens it. |
| One click on a file, or ⋮, or right click | The file's menu. |
| 🗑 | Deletes on the console (asks first). |
| Drag a row onto a folder row | Moves it there. |
| Drag a file out of the window | Copies it to where it is dropped (a file over 64 MB is fetched first with *Get it ready to drag*). |
| New folder + **Create** | Makes a folder here. |

Row menu: Open · Pin to the top / Remove from the top (folders) · Use as the upload folder · Download to the desktop ·
Download to… (files and whole folders) · Get it ready to drag · Show the local
copy · Copy the path · Rename… · **Move to…** (a folder tree of the console:
quick access on the left, folders that open on the right, **New folder**,
then **Move here**) · Delete on the console.

## Dropping files on the window

Dragging `.pkg` files (or folders with them) over the window shows two zones:

| Zone | Does | Needs |
|---|---|---|
| **Send over FTP** | Copies them to the folder the Files tab is in. | FTP |
| **Install directly** | The console downloads them from this PC and installs. | Installer |

If a file with the same name is already on the console: **Replace**,
**Keep both** (new name) or **Skip**.

## PS1/PS2 Games page

1. **Choose games folder…** — the folder with the disc images (`.iso`,
   `.bin/.cue`, `.img`, also in sub-folders). The app finds which are PS1 and
   PS2 games and names them from the emulator's title list.
2. The first conversion downloads the Classics files once (about 109 MB);
   **Download now** does it earlier. Nothing else has to be provided.

| Item | Does |
|---|---|
| ‹ | Back to the consoles. |
| Folder chip | Changes the games folder. |
| ⟳ | Looks again. |
| Game card | Opens the game's dialog. |
| ☐ on a card | Selects it; with several selected, **Continue…** opens one dialog for all. |

### Game dialog

Platform, serial, region, file; **Name on the console** (editable); where
packages are saved (**Change…**).

| Button | Does | Needs |
|---|---|---|
| **Convert only** | Makes the package in the output folder. | Nothing (no console). |
| **Convert and install** | Makes it, sends it over FTP to `/data/OrbisLinkFPKG/` on the console (made if missing), installs it, then deletes that copy. The package stays in the output folder. | FTP of the console in use; the installer for the last step (the queue waits for it). |
| **Send and install** | Shown instead when the package was made before and *Use the existing package* is chosen: skips the conversion. | Same as above. |
| **Send disc file** | Sends the disc image itself over FTP. | FTP. |

Without a console connected, the dialog says so, the buttons that send are
off, and **Convert only** still works.

### Conversion card (Queue tab)

Stages: waiting → (emulator files the first time) → cover → converting →
signing → sending → installing → installed. Each of Convert / Send / Install
has its own bar; ✕ cancels while it runs and removes the card when it is
over; ⟲ retries a failed send or install; 📂 opens the output folder.

### How a package is made

PS2 discs exactly as easy-ps2-fpkg makes them: the "Jak v2" emulator as it
comes, its config with the game's serial, its own `param.sfo` with the game's
IDs and name, the official cover by serial (else the emulator's art), the disc
as `image/disc01.iso`. PS1 discs as PS Classics fPKG Builder does.

## Settings

| Section | Holds |
|---|---|
| Consoles | Saved consoles: name, IP, type; add / remove. |
| Account IDs (PSID) | PSN accounts for Remote Play registration. |
| Console in use | Which console; FTP port, user, upload folder, connections, advanced mode (protected system folders); installer port and install options; local HTTP server. |
| Remote Play | Resolution, frame rate, bitrate, hardware decoding, rumble, touchpad from the mouse, full screen on connect. |
| General | Language, theme, updates (channel: stable or testing), advanced options. |

## What needs what

| Action | Console connected? | FTP | Installer | Remote Play registration |
|---|---|---|---|---|
| Convert only | no | — | — | — |
| Convert and install / Send and install | yes | yes | waits for it | — |
| Send disc file, Send over FTP | yes | yes | — | — |
| Install directly | yes | — | yes | — |
| Files tab (browsing also works while an upload runs) | yes | yes | — | — |
| Remote Play | yes | — | — | yes |

## Where it lives in the code

| Area | Files |
|---|---|
| Window, top bar, side panel, drop handling | `qml/orbislink/Main.qml` |
| Consoles page, Remote Play stage, keyboard map dialog | `StreamArea.qml`, `ConsoleCard.qml`, `AddConsoleCard.qml`, `KeyboardMap.qml` |
| Queue and conversion cards | `TransferPanel.qml`, `StageLoader.qml` |
| Files tab | `FtpBrowser.qml`, `FtpFolderPicker.qml` |
| Games page | `GamesView.qml`, `GameCard.qml`, `GameDialog.qml` |
| Everything the QML calls (`app`) | `src/orbislink/qt/app_controller.*` |
| Games (`games`) | `src/orbislink/qt/games_controller.*` |
| Remote Play (`stream`) | `src/orbislink/qt/stream_controller.*` |
| Queue, FTP, installer | `src/orbislink/queue`, `ftp`, `installer` |
| Package builder, disc scanner, Classics files | `src/orbislink/fpkg` |
