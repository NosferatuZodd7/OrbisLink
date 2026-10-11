# OrbisLink — how the app is put together

A map of every screen, every button and what it is expected to do. It is
written for people (testers, translators, contributors) and kept up to date
with the code, so a change can be checked against it: if a button here does
something else in the app, one of the two is wrong.

## What the app does

Its jobs, around one PlayStation at a time — the **console in use**:

1. **Remote Play** — the console's picture, sound and controls on the PC.
2. **Files** — packages (`.pkg`) and any other file to the console, over FTP
   or by direct install, and files back from it.
3. **PS1/PS2 games** — disc images on the PC turned into PS4 packages, then
   (optionally) sent and installed.
4. **Save vault** — every save on the console read and backed up on the PC,
   kept as the PS4 keeps saves on a USB drive, and sent to any console of
   the same PSN account (PS4 or PS5).
5. **Homebrew store** — the PS5 homebrew apps of the homebrew.page catalog,
   looked at and installed on a jailbroken PS5 in one click.

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
│ stage: consoles · Remote Play · Games · Saves       │ side panel: Queue | Files (FTP) │
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
| 💿 | Games: the console's library and the PS1/PS2 converter; lit while that page is open. | Opens / closes the games page. |
| 🏪 | The homebrew store; lit while that page is open (only in builds with the store). | Opens / closes the store. |
| 🗄 | The save vault; lit while that page is open. | Opens / closes the save vault. |
| ⚡ | The payload manager; lit while that page is open. | Opens / closes the payloads page. |
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
| 👤 Account ⌄ (console in use) | The account (PSID) Remote Play goes in with on this console. A click lists the saved accounts — the one in use ticked, the ones this PC is not registered with on this console marked — and **Manage accounts…** (Settings → Account IDs). The choice is kept for this console. |

One click, one request: after a click the card takes no other until the
console has answered (Remote Play connected or failed, FTP found or not).

**Which account.** Connecting uses the account shown on the card. Only
when the console has none of the saved accounts yet, and two or more are
saved, connecting first asks which one; with one, that one is used. Each account has its own registration on each
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

Video apps (YouTube, Netflix…) and other protected screens are not sent over
Remote Play. While the console shows one, the picture stays dark with a
notice saying so; the session carries on, and the picture comes back once
the console goes back to a game or the home screen (PS button).

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
Send, Install — instead of separate queue cards (see *Games*);
closing it takes its finished queue tasks along. A package sent and then
installed is one card too (the install's). The tab's number counts cards.

Installs and removals from the **Homebrew store** have their cards here too,
under *Homebrew store*: the app's icon, the PS5, the stage, the bar, the
percentage, the speed, how much of how much and the time left; ✕ cancels one
under way or removes a finished one, **Clear finished** clears them all.

Whatever is installed, from wherever, is followed here: a package already on
the console (*Install* in the console library, *Install on the console* in the
Files tab) is a queue card *Install on the console*, its bar following the
installer's task, or else how much of the package the console has copied
into `/user/app/<TITLE ID>` (`/user/patch/` for an update); a disc converted
from the library is a conversion card; what the library's other buttons set
going — putting an app on the home screen, running a payload — has a card
under *Console library*, with a moving bar while it runs and what came of it
after.

On a PS5 whose installer is etaHEN's **DPI v2** (it only starts installs and
reports nothing back), a direct install is followed by what the PC has served:
the card is done once the console has taken the whole package, and the
console finishes the install on its own (its notifications show it).

A task the console still keeps from an earlier try (error 0x80990015,
*TASK_DUPLICATED*) is removed and the install asked for again, when the
installer can find it (Remote Package Installer's API); otherwise the card
says to delete it from the console's Downloads list.

### Files (FTP) tab

The console's files. Needs FTP.

| Action | Does |
|---|---|
| Path field, ↑, ⟳ | Where you are; one level up; list again. A folder the console does not have (the PS4's `/data/pkg/` on a PS5) opens the nearest one up that it has, and the status line says so. |
| Pinned folders (tags) | Click: opens it. ✕: removes it from the top. **Pin this folder** pins the one that is open (also *Pin to the top* in a folder's menu). A PS4 and a PS5 each keep their own, in the settings: a PS4's start as `/data/`, `/data/pkg/`, `/data/GoldHEN/`, `/user/app/`, `/mnt/usb0/`; a PS5's as `/data/`, `/data/etaHEN/`, `/data/homebrew/`, `/mnt/usb0/`, `/mnt/ext0/`. |
| One click on a folder | Opens it. |
| One click on a file, or ⋮, or right click | The file's menu. |
| 🗑 | Deletes on the console (asks first). |
| Drag a row onto a folder row | Moves it there. |
| Drag a file out of the window | Copies it to where it is dropped (a file over 64 MB is fetched first with *Get it ready to drag*). |
| New folder + **Create** | Makes a folder here. |

Row menu: Open · Pin to the top / Remove from the top (folders) · **Install on the console** (a `.pkg` on a
PS5: installed from where it is, as Prospero Manager's *already on the PS5* does) · Use as the upload folder · Download to the desktop ·
Download to… (files and whole folders) · Get it ready to drag · Show the local
copy · Copy the path · Rename… · **Move to…** (a folder tree of the console:
quick access on the left, folders that open on the right, **New folder**,
then **Move here**) · Delete on the console.

## Dropping files on the window

Dragging `.pkg` files (or folders with them) over the window shows two zones:

| Zone | Does | Needs |
|---|---|---|
| **Send over FTP** | Copies them to the folder the Files tab is in. Any other file dropped (a disc image for the USB drive, a payload) is copied as it is; only a damaged `.pkg` is turned away. | FTP |
| **Install directly** | The console downloads them from this PC and installs. | Installer |

On a PS5, a package sent with *install after sending* (and a converted game's
**Send and install**) is installed from where it was put, by its path on the
console — nothing is sent twice. If the console's installer does not take a
path, it is installed from this PC instead.

If a file with the same name is already on the console: **Replace**,
**Keep both** (new name) or **Skip**.

## Games page

Two tabs, kept apart, as converting is a choice and not a step:

- **All games** (it opens on this one): the console library, everything in
  the console's `OrbisLinkFPKG` folders with what each thing can do.
- **PS1/PS2 converter** (optional): PS1/PS2 discs on this PC made into PS4
  packages. Only in builds with the converter.

| Item | Does |
|---|---|
| ‹ | Back to the consoles. |
| ⟳ (All games) | Reads the console's folders again. |
| Folder chip, ⟳ (converter) | Changes the games folder; looks again. |

### All games (the console library)

One folder, `OrbisLinkFPKG`, where games and apps of any kind are put as they
come, over FTP or by plugging in a drive: `/data/OrbisLinkFPKG` in the
console's memory (made by the app the first time the tab opens),
`/mnt/usb0`–`/mnt/usb7/OrbisLinkFPKG` on USB drives, `/mnt/ext0`–`/mnt/ext1/OrbisLinkFPKG`
on the extended storage. The tab reads them over FTP, says what each thing
is, and gives it the one button that makes it playable. Nothing is fetched
whole to find out: a package's header and a disc's `SYSTEM.CNF` are read in a
few small pieces.

What is there is listed in four groups, each under its heading with how many
it holds: **Games and apps** (game and app packages, PS1/PS2 discs, PS5 images
and app folders), **Updates and add-ons** (patch, add-on and theme packages),
**Payloads**, and **Other files** (archives, and what could not be read).

| It is | Shown as | The button |
|---|---|---|
| `.pkg` | Its title, icon, title ID, version (from its `PARAM.SFO`); **Installed** when `/user/app/<TITLE ID>` exists (an update: `/user/patch/<TITLE ID>`; an add-on: its label in `/user/addcont/<TITLE ID>`). | **Install** (PS5): the console installs it from where it is, followed in the Queue tab. A PS4 installs packages on the console from Debug Settings → Package Installer, which the row says. |
| `.iso`, `.img`, `.bin/.cue` (a `.bin` over 32 MB; a `.cue` shows its track as one) | PS1 or PS2 disc, serial, name from the emulator's list (once it is here), from the disc itself. | **Convert…**, a menu: **Convert and install** — the disc is brought to this PC, made into a PS4 package (as in the converter), sent back to the same folder and installed; **Only convert** — the same, without the install: the package stays in the folder, listed next to the disc, to install when you want. The copy on the PC is deleted after. Its card in the Queue tab adds the step *Bringing it from the console*. |
| `.ffpkg`, `.exfat`, `.ffpfs` | PS5 image, title ID from its name. | **Put on the home screen** (PS5): moved into the folder ShadowMountPlus mounts from on the same drive (`/data/homebrew`, `/mnt/usbN/homebrew`, `/mnt/extN/homebrew`; a move there is instant), then ShadowMountPlus is asked to scan, or started again. |
| A folder with `sce_sys/param.json` (PS5) or `sce_sys/param.sfo` (PS4) | Its name, icon and title ID from there. Other folders are not listed. | **Put on the home screen**, the same way. |
| `.elf`, `.lua`, `.js`, `.jar`, a small `.bin` | Payload. | **Run**: sent to the console's loader for it. |
| `.zip`, `.7z`, `.rar` | Archive. | None: unpack it on the PC first. |

Every row has the same columns: the picture, what it is, the button (the
same width on every row; **Again** on what is installed) and, at the right
edge, 🗑, which deletes the file (after asking; what was installed from it
stays installed). Search, and chips: All, **Not installed**, and one per
group. The list is read again by itself once something set going from it has
finished (a package put back, an install).

### PS1/PS2 converter

1. **Choose games folder…** — the folder with the disc images (`.iso`,
   `.bin/.cue`, `.img`, also in sub-folders). The app finds which are PS1 and
   PS2 games and names them from the emulator's title list.
2. The first conversion downloads the Classics files once (about 109 MB);
   **Download now** does it earlier. Nothing else has to be provided.

| Item | Does |
|---|---|
| Game card | Opens the game's dialog. |
| ☐ on a card | Selects it; with several selected, **Continue…** opens one dialog for all. |

### Game dialog

Platform, serial, region, file; **Name on the console** (editable); where
packages are saved (**Change…**).

| Button | Does | Needs |
|---|---|---|
| **Convert only** | Makes the package in the output folder. | Nothing (no console). |
| **Convert and install** | Makes it, sends it over FTP to the folder chosen under *On the console* (made if missing) and installs it. A PS4 installs it from this PC and the copy there is then deleted; a PS5 installs it from where it was put, and the copy stays until you delete it. The package stays in the output folder. | FTP of the console in use; the installer for the last step (the queue waits for it). |
| **Send and install** | Shown instead when the package was made before and *Use the existing package* is chosen: skips the conversion. | Same as above. |
| **Send disc file** | Sends the disc image itself over FTP. | FTP. |

**On the console:** where the package goes — **Its memory**
(`/data/OrbisLinkFPKG/`), a **USB drive** (`/mnt/usb0/OrbisLinkFPKG/`) or the
**Extended storage** (`/mnt/ext0/OrbisLinkFPKG/`). The dialog asks the
console which drives it has, and marks *not found* the one that shows
nothing (still selectable: an empty drive shows nothing too). The choice is
kept in the settings.

Without a console connected, the dialog says so, the buttons that send are
off, and **Convert only** still works.

### What the console takes, and from where

| You have | What happens | From |
|---|---|---|
| A PS4 package (`.pkg`, fake-signed) | Installed by the installer: Remote Package Installer on a PS4, etaHEN's DPI v2 on a PS5 (PS4 packages on a PS5 need etaHEN with kstuff). | This PC (*Install directly*), or a copy on the console: its memory, a USB drive, the extended storage (PS5: **Install on the console** in the Files tab, or *Send and install*). |
| A PS1/PS2 disc (`.iso`, `.bin/.cue`, `.img`) | Converted here into a PS4 package, then as above. The image itself is not installable; *Send disc file* only copies it. | Same. |
| A PS5 game as a folder, or as an image (`.ffpkg`, `.exfat`, `.ffpfs`) | Not installed: ShadowMountPlus mounts it and puts it on the home screen. Copy it over FTP into a folder it looks in. | `/data/homebrew`, `/mnt/usb0`–`/mnt/usb7` and their `homebrew` folders, `/mnt/ext0`, `/mnt/ext1` and theirs (ShadowMountPlus's own list, first level of sub-folders). |
| A homebrew app from the store | Its folder goes to `/data/homebrew`; ShadowMountPlus registers it. | The Homebrew store page. |

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

## Homebrew store

The PS5 homebrew apps of the homebrew.page catalog — the one ProsperoStore
reads — and installing them on a jailbroken PS5 (etaHEN's FTP on, kstuff and
ShadowMountPlus running). The catalog is used only when its signature
(Ed25519, either of the two keys the catalog publishes) and the SHA-256 of
each of its files check out; a catalog older than one already seen is
refused. When the site does not answer, the last catalog kept is shown, and
the header says so. Each app's icon is downloaded once.

| Item | Does |
|---|---|
| PS5 chip (header) | The PS5 apps go to: the one in use when it is a PS5, else the first PS5 whose FTP answers. With several PS5s, a click lists them to pick another. |
| Search · chips | Search by name, author or title ID · All, by kind (apps, games, tools…), **Installed**, **Updates**. |
| A card | Icon, name, author, version, size, and **Installed** / **Update** / **Coming soon**. A click opens it. |
| The app's window | What it is (description), version, size, title ID, date, licence; **On the console**: whether it stays in the app sandbox, uses the network, starts helper payloads (as the catalog's scan found); **What's new**; links to its source code, catalog page and release. |
| **Install on <PS5>** · **Update on <PS5>** · **Install again** | Downloads the app's ZIP, checks it against the catalog's SHA-256 (a mismatch is not installed), copies its folder to a staging folder on the PS5 (`/data/orbislink/staging`), sets everything in it to 777 (the console starts an app only then: CE-107750-0 otherwise; a server that cannot is said so), then moves it to `/data/homebrew/<TITLE ID>`. An update keeps the copy it replaces in `/data/orbislink/previous/<TITLE ID>`, and what the user put in the app's folder — games, saves, settings: what the new archive does not have — moves on into the new folder, as copying the new version over the old one would keep it. ShadowMountPlus puts it on the home screen: at once when its API lets the network in, else at its next scan (every 15 seconds). |
| After an install | ShadowMountPlus is asked to look at once (its API); when its API does not let the network in, ShadowMountPlus is **started again**: its file on the console (etaHEN's payloads or plugins, the autoloader, PLDMGR), else the payload library's, goes to the ELF loader (9021). A new ShadowMountPlus asks the one running to stop and scans everything at once. |
| **Not on the home screen?** (an installed app) | Reads ShadowMountPlus's log on the PS5 (`/data/shadowmount/debug.log`, over FTP) and says what it did with the app: registered it, saw it while it was still changing, could not read its `param.json`, had it refused by the console (with the code), gave up after failed tries, kept another copy with the same title ID, or never saw it (not running, or no log); the log lines about it are shown. It also asks ShadowMountPlus to look again, retrying what it gave up on — when its API lets the network in. Every install asks the same. |
| **Remove** | Asks ShadowMountPlus to uninstall it (when its API lets the network in), then deletes its folder. Its saves stay. Without the API, its icon goes when ShadowMountPlus removes missing games, or by deleting it on the console. |
| Progress | On the card, at the foot of the app's window and as a card in the Queue tab: the stage (downloading, copying to the PS5, putting it in place, starting ShadowMountPlus again), the percentage, the speed over the last seconds, how much of how much and the time left. The copy counts the app's files as they reach the PS5. |
| **Cancel** | Stops an install under way, mid-file too; nothing in place changes. |

No package is built: on the PS5 the app folder is the install, which is why
apps the catalog offers only as disc images are listed but not installed.
Building a PS5 fake package (fpkg) is not something any open tool does yet.

## Payloads

What a jailbroken console keeps in its payload and plugin folders, read
over its FTP, for the console in use or another picked in the header. What
the page shows follows the console: a PS4 is GoldHEN's, a PS5 etaHEN's and
the autoloader's.

| Console | Folder | What it is | Starts by itself when switched on |
|---|---|---|---|
| PS4 | `/data/GoldHEN/payloads` | `goldhen.bin` is GoldHEN itself: the PPPwn loader starts it at every boot (marked **the jailbreak**; deleting it asks twice as clearly). | — |
| PS4 | `/data/payloads` | The payload library Payload Guest lists. | — |
| PS4 | `/data/GoldHEN/plugins` | `.prx` plugins. | Loads with every game: its line in `[default]` of `/data/GoldHEN/plugins.ini` says `=true` (the per-game sections are left alone). |
| PS5 | `/data/etaHEN/payloads`, `/data/etaHEN/plugins` | etaHEN's payloads (`.elf`) and plugins (`.plugin`). | Starts when etaHEN loads: a `<name>.auto_start` file sits next to it, which is how etaHEN marks it. |
| PS5 | `/data/ps5_autoloader` | The autoloader's files. | Listed in its `autoload.txt` (one file name per line, in order; `!<ms>` lines wait between them). Switching one off takes its wait out with it. |
| PS5 | `/data/pldmgr/payloads` | PLK's Payload Manager (PLDMGR): a folder per payload, a `.json` of its details beside each file (added files get a folder of their own; deleting or renaming takes the `.json` along). | Listed in `/data/pldmgr/autoload.txt`, the same format: it starts when the Payload Manager loads. |

| Item | Does |
|---|---|
| Console chip (header) | The console shown; with several, a click lists them (those without FTP say so). |
| **Send from this PC…** | Runs a payload from this PC on the console now, without copying it: choose the file, the port is filled in for it (PS4: GoldHEN's BinLoader, 9090, on in GoldHEN's settings; PS5: ELF to 9021 — etaHEN's loader or elfldr —, `.bin` to the exploit's 9020, `.lua` 9026, `.jar` 9025, `.js` 50000) and can be changed. |
| ⟳ | Reads the console again. |
| **Add…** (each folder) | Copies files from this PC into it; a folder the console does not have yet is made. |
| Switch (each file) | Whether it starts by itself, as the table above says. |
| ▶ | Runs that payload now: fetched from the console and sent to its loader port. |
| ⬇ · ✎ · 🗑 | Downloads it to this PC's downloads folder · renames it (its auto-start follows: the marker, its line in `autoload.txt` or `plugins.ini`) · deletes it, with its auto-start, after asking. |
| **Settings files** · **Edit** | `/data/GoldHEN/plugins.ini` (PS4); `/data/etaHEN/config.ini`, `/data/ps5_autoloader/autoload.txt`, `/data/pldmgr/autoload.txt` and `/data/pldmgr/pldmgr_config.txt` (PS5), opened as text; **Save on the console** writes it back (a file not there yet is made). |
| **What the console answered** | What a payload sent prints, as the PS5's ELF loader passes it back (GoldHEN's BinLoader says nothing); **Clear** empties it. |
| **Order and waits…** (the autoloader, PLDMGR) | The autoload list as steps: ↑ ↓ change the order, *wait … ms* is the pause before that one (`!<ms>` in the file; 1000 is one second), ✕ takes it out of the list, **Add a file from the folder** puts one at the end. **Save on the console** writes the list back; comment lines in it stay at the top. |

A PLDMGR payload's version, from its `.json`, shows beside its name.

Without FTP the page says so; payloads from this PC can still be sent.

### Library (PS5)

The community's PS5 payloads: the list PLK's Payload Manager reads
(itsPLK/ps5-payloads-mirror), each payload's latest release mirrored, with
its SHA-256 and where it comes from. The list is read when the tab first
opens (⟳ reads it again) and kept: when the site does not answer, the last
one is shown and said so. A payload is downloaded once to this PC and used
only if its SHA-256 matches the list.

| Item | Does |
|---|---|
| Search · chips | By name or description · All, by category, **Installed**, **Updates**. |
| A row | Name, version, description, category and date; **Installed**, or **Update: v1.10 → v1.11** when a copy on the console is older (its version from PLDMGR's `.json`, else from its file name). *in …* says which folders have it. |
| ↗ | Its author's page. |
| ▶ | Runs it now, without installing it: downloaded (or this PC's copy) and sent to the loader port for it (ELF 9021, `.bin` 9020). What it prints shows above the list. |
| **Install…** · **Update…** · **Install again…** | Puts it in a folder on the console: etaHEN's payloads, the autoloader or PLDMGR (in PLDMGR, in a folder of its name with the same `.json` PLDMGR writes). An older copy in that folder is replaced, and whether it started by itself carries over (its marker, or its place in `autoload.txt`). Needs FTP. |

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
| Homebrew store: look at the apps | no (internet) | — | — | — |
| Homebrew store: install, update, remove | a PS5 | yes | — | — |
| Payloads: see, add, rename, delete, auto-start, edit settings | yes | yes | — | — |
| Payloads: run one now | yes | to run one from the console | — | — |
| Payloads library: look, run one now | internet | — | — | — |
| Console library: see what is there, put on the home screen, run | yes | yes | — | — |
| Console library: install a package, convert and install a disc | yes | yes | yes (waits for it) | — |
| Console library: only convert a disc (the package stays in its folder) | yes | yes | — | — |
| Payloads library: install, update | yes | yes | — | — |

## Where it lives in the code

| Area | Files |
|---|---|
| Window, top bar, side panel, drop handling | `qml/orbislink/Main.qml` |
| Consoles page, Remote Play stage, keyboard map dialog | `StreamArea.qml`, `ConsoleCard.qml`, `AddConsoleCard.qml`, `KeyboardMap.qml` |
| Queue and conversion cards | `TransferPanel.qml`, `JobCard.qml` (store and library cards), `StageLoader.qml` |
| Files tab | `FtpBrowser.qml`, `FtpFolderPicker.qml` |
| Games page | `GamesView.qml`, `GameCard.qml`, `GameDialog.qml` |
| Console library (`consoleLibrary`) | `ConsoleLibrary.qml`, `src/orbislink/qt/library_controller.*`, `src/orbislink/library/console_library.*` (kinds, drives, `param.json`/`param.sfo`, reads in blocks), `src/orbislink/qt/shadowmount_control.*` (asking ShadowMountPlus to scan, or starting it again) |
| Everything the QML calls (`app`) | `src/orbislink/qt/app_controller.*` |
| Games (`games`) | `src/orbislink/qt/games_controller.*` |
| Save vault (`saves`) | `SavesView.qml`, `src/orbislink/qt/saves_controller.*`, `src/orbislink/saves` |
| Homebrew store (`store`) | `StoreView.qml`, `src/orbislink/qt/store_controller.*`, `src/orbislink/store` |
| Payloads (`payloads`) | `PayloadsView.qml`, `src/orbislink/qt/payloads_controller.*`, `src/orbislink/payloads/payload_layout.*` (folders, `autoload.txt`, `plugins.ini`), `src/orbislink/net/payload_sender.*` (to a loader port) |
| Remote Play (`stream`) | `src/orbislink/qt/stream_controller.*` |
| Queue, FTP, installer | `src/orbislink/queue`, `ftp`, `installer` |
| Package builder, disc scanner, Classics files | `src/orbislink/fpkg` |
