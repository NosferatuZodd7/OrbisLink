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
4. **Save vault** — every save on the console read and backed up on the PC,
   kept as the PS4 keeps saves on a USB drive, and sent to any console of
   the same PSN account (PS4 or PS5).

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
┌ top bar ───────────────────────────────────────────────────────────────────┐
│ logo  name/IP      [console chip] [🎮 FTP 📦] │ [▶ 💿 🗄] │ ⟳ ▣ 🗎 [☾ ◐ ☀ ✨] ⚙ │
├────────────────────────────────────────────────────┬───────────────────────┤
│ stage: consoles · Remote Play · PS1/PS2 · Saves     │ side panel: Queue | Files (FTP) │
├────────────────────────────────────────────────────┴───────────────────────┤
│ status line: last message                      Local HTTP: … · version     │
└────────────────────────────────────────────────────────────────────────────┘
```

## Top bar

Three groups, apart: what is (the console and its services), where to go
(the pages) and the tools. Everything is an icon, with its name and what it
means in the tooltip. In a narrow window the console's name and address on
the left go (the console chip still shows it); the window cannot be made
narrower than where the rest fits.

| Item | What it shows | Click |
|---|---|---|
| Logo, name, IP | The console in use. | — |
| **Console chip** | See *Console chip* below. | See below. |
| 🎮 · **FTP** · 📦 | Remote Play, FTP (the word in small bold letters) and the installer, each with a dot at the corner: green available (with a soft ring), amber checking (it breathes), red not answering, grey not applicable. Hover: the name and why. | — |
| ▶ | Only while a Remote Play session runs and another page is open. | Back to the picture. |
| 💿 | PS1/PS2 games; lit while that page is open. | Opens / closes the games page. |
| 🗄 | The save vault; lit while that page is open. | Opens / closes the save vault. |
| ⟳ | | Checks the three services now. |
| ▣ | | Shows / hides the side panel (F9). Locked while full screen. |
| 🗎 | | Log and diagnostics (Ctrl+L): log, copy, save a diagnostics file. |
| ☾ ◐ ☀ ✨ | Only one is lit. | Dark, glass or light: the plain theme (custom colours step aside, kept for ✨). ✨ Custom: the saved look chosen in Settings → Personalisation (or the colours set aside); with none yet, opens that page. |
| ⚙ | | Settings (Ctrl+,). |

### Console chip

Left of the services, the console the file actions go to:

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
| **Remote Play** | Same as the body. While connecting: **Cancel** (only this button cancels, and only after a moment, so a double click never cancels). |
| **FTP** | Makes it the console in use and opens the Files tab, without Remote Play. |
| 🔗 | Registers this PC for Remote Play (the console shows an 8-digit PIN). |
| ✕ | Removes the console from the list (asks first). |
| Gold badge | The console has a jailbreak (FTP answers). |

One click, one request: after a click the card takes no other until the
console has answered (Remote Play connected or failed, FTP found or not).

**Which account.** With two or more Account IDs saved, connecting first asks
which one to use (the one this console used last is chosen already); with
one, that one is used. Each account has its own registration on each
console and its own Remote Play PIN — a session never uses another
account's. An account not registered on the console yet goes to
registration first, with its own 8-digit PIN filled in when it has one
saved. When the console asks for the account's passcode while connecting,
the 4-digit PIN saved for that account is sent once; if it has none, or the
console refuses it, the PIN is asked for.

**Add console**: name, IP and type; it is probed straight away.

## Remote Play session

The picture fills the stage. Moving the mouse shows the toolbar:

| Button | Does |
|---|---|
| 🎮 | Keyboard map: which key presses which button. The touchpad key (T) puts a finger on the left half of the touchpad and then clicks — Select in PS2 games. A controller's own touchpad is passed on as it is (left half Select, right half Start). Keyboard and controller can be used together. Live: a key held on the keyboard, or a button held on the controller, lights up its key and its part of the controller drawing. **Change keys** to rebind: click a key and press the new one; with a controller connected, click a button on the controller drawing and press the controller button that should do it (L2/R2 are triggers and stay). Two keys, or two buttons, swap. **Reset** goes back to the defaults of both. |
| Speaker | Mutes / unmutes the console's sound. |
| ▭ 16:9 / 4:3 / Fill | The picture's shape, one click to the next: as the console sends it (16:9), 4:3 with bars at the sides (PS1/PS2 games come stretched), or stretched to the window. Kept for next time. |
| Microphone | Sends / stops the PC microphone. |
| Full screen | Full screen on / off (F11; Esc leaves the session). |
| **End the session** | Disconnects. |

## Side panel

### Queue tab

Each file being sent or installed is a card: title, size, state, bar,
speed and time left.

| On a card | Does |
|---|---|
| ↑ ↓ | Moves a waiting task up or down; greyed out when it cannot go that way (never past the task under way), hidden when it cannot move at all. |
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

## Save vault

A place on this PC with a copy of every save, to put back whatever a console
loses and to take a save from one console to another of the same PSN
account (a PS4 game's save goes from a PS4 to a PS5 and back). It needs the
console's FTP (GoldHEN, etaHEN); without it, the page shows what the PC
holds. Backing up only reads the console; sending writes the save where the
console keeps it.

Opening the page reads a console — the one in use, or another picked in the
header: every user, every game with saves, and each save with its icon, its
name, what the game wrote about it (chapter, level, percentage…) where the
console lets that be read, its size and where it stands:

| Badge | Means |
|---|---|
| ✓ On the PC | The console's save is the one backed up (or the copy sent there). |
| ↺ Changed since the backup | The console's save is not the PC's: the game wrote to it after the last backup, or it is that console's own save of that name. |
| ↓ The PC has a newer one | The console has the copy of an earlier backup; a newer one came from another console. **Back up** leaves it out (it would put the earlier save over the newer one): send the PC's copy instead. |
| ⇧ Not backed up | Only on the console. |
| 🗄 Only on the PC | Not on this console: it lost it, or it was never there (another console's). |

| Item | Does |
|---|---|
| Console chip (header) | The console whose saves are shown. With more than one console, a click lists them all (with PS4/PS5, and "no FTP" for those whose FTP does not answer) to show another one's saves. |
| **Back up all to PC** | Backs up every save that is not backed up or changed, in one click. |
| **Send what's missing** | Sends back every save this console had and lost (see below). |
| Owners | One chip per owner — the PSN account (PSID) — named after the Account ID kept in the app, else the console's name for its user (`username.dat`), else the PSID; or one per console user whose PSID is not known yet. **Link…** says which of the Account IDs kept in the app (Settings → Account IDs) a console user is. Most of the time it is not needed: a console user's PSID is read from the console's own list of saves, and from the saves themselves where the console lets them be read. A save whose PSID is still not known asks for the link when it is backed up, then goes. With one owner chosen, everything on the page is that owner's. |
| Filters | All · Not backed up · Changed (and older) · Only on the PC. |
| Click on a save · **Select all** | Selects; the top strip then offers **Back up to PC**, **Send to <console>** and **Delete…** for the selection. |
| **Send to <console>** | Named after the console it sends to: the one shown, or another picked with the arrow next to it (only consoles whose FTP answers). Always asks first, naming the console and its type: the game has to be closed. For each owner, it shows who gets the saves there: the user of that console with the same PSID. When none is known, it lists that console's users whose PSID is not known, to say which one it is (kept from then on). Each save replaces that user's copy with the latest backup and is checked to have arrived whole (an error otherwise); one the console does not list is also added to its list of saves (below). On a PS5, a PS4 game's saves are those of its PS4 version (CUSA); its PS5 version keeps saves of its own. |
| **Delete…** | From the PC only (it warns when a save is not on the console, as the PC's may be its only copy). |
| Folder chip · ⚙ | Opens the folder on the PC · changes it. |
| ⟳ | Reads the console again. |

PS1/PS2 games converted to packages keep their memory cards as ordinary
PS4 saves, so they are in the vault like any other game.

The vault is laid out as the PS4 lays out the saves it copies to a USB
drive:

```
<vault>/PS4/SAVEDATA/<PSID>/<game>/<save>       the encrypted image
<vault>/PS4/SAVEDATA/<PSID>/<game>/<save>.bin   its key
<vault>/.vault/<PSID>/<game>/<save>/            names, dates, icon, the 4 backups before,
                                                and which console users have which copy
```

The PSID is the owner's PSN account in 16 hex digits, as the PS4 names it.
One save of a game is one save whatever console it is on: the PS4's and the
PS5's copies of it are the same backup. Saves copied in by hand from a PS4's
USB drive (its `PS4` folder into the vault folder) show up as well. Backups
made by earlier versions move to this layout once their PSID is known.

Sending a save a console does not list: each console keeps a list of each
user's saves (`/system_data/savedata/<user>/db/user/savedata.db`, SQLite —
a PS5 keeps its list of PS4 saves there too), and a save whose files are
there but has no row in it is not shown. So, after sending the files, the
app reads that list, adds the missing rows — the same row Apollo Save Tool
adds when it creates a save: game, save, names, size in 32 KiB blocks,
owner's account and user — checks the result with SQLite and writes it
back, reading it again to make sure it is exactly the edited one (otherwise
the console's own goes back). A row already there is only no longer marked
broken. The list as the console had it is kept first in
`<vault>/.vault/console-lists/<user>/` (the last 5). If a save does not show
yet, restart the console. A PS5 user that never had a PS4 save has no such
list yet: start a PS4 game there once and save, then send again.

What the consoles allow, and why the page works this way:

- A save is two files on the console, `sdimg_<name>` (the encrypted image,
  with the save's own param.sfo inside) and `<name>.bin` (its key); the
  system's own copy of them (`sce_bu_<name>`) is not kept.
- The game open on the console in use (Remote Play's discovery says which)
  is left alone: a save copied while its game writes it comes out
  corrupted, either way.
- A save counts as on the console only with its own image and key: with
  only the system's copies (`sce_bu_`) left, the console shows it broken,
  and it is offered for sending back.
- Saves are kept as the console has them: encrypted, for the account they
  came from. They go only to a console user of that account (the same PSID),
  on any console.

## Settings

| Section | Holds |
|---|---|
| Consoles | Saved consoles: name, IP, type; add / remove. |
| Account IDs (PSID) | PSN accounts for Remote Play: name, Account ID and its own **Remote Play PIN** (optional: 4 digits, the account's passcode, sent when the console asks for it; 8 digits, the pairing PIN, filled in when the account registers — the console shows a new one each time). Each row says which consoles the account is registered on. Editing one account's PIN never changes another's. |
| Console in use | Which console; FTP port, user, upload folder, connections, advanced mode (protected system folders); installer port and install options; local HTTP server. |
| Remote Play | Resolution, frame rate, bitrate, hardware decoding, rumble, touchpad from the mouse, full screen on connect. |
| Personalisation | The app's colours, on top of the theme picked in the top bar: accent, jailbreak (GoldHEN gold), available, checking, error, window background, panels, text. A click on a colour opens a picker (square, hue strip, hex code, suggestions); ↺ puts the theme's colour back. **Saved looks**: give the theme and colours in use a name to keep them; **Apply** brings one back (it is then the one ✨ in the top bar brings), 🗑 deletes it. Changes show at once. |
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
| Save vault: back up, send to a console | yes | yes | — | — |
| Save vault: see or delete what the PC holds | no | — | — | — |

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
| Save vault (`saves`) | `SavesView.qml`, `src/orbislink/qt/saves_controller.*`, `src/orbislink/saves` |
| Remote Play (`stream`) | `src/orbislink/qt/stream_controller.*` |
| Queue, FTP, installer | `src/orbislink/queue`, `ftp`, `installer` |
| Package builder, disc scanner, Classics files | `src/orbislink/fpkg` |
