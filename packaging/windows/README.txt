OrbisLink @VERSION@ — Remote Play, pkg install and FTP for PS4 and PS5
======================================================================

WHAT THIS VERSION INCLUDES

Two things:

  "OrbisLink" — the window: Remote Play, installing pkg files by dragging
  them onto the window, and a browser for the console's FTP, from which
  files can be brought to the PC (by dragging them out, or from each file's
  menu).

  "orbislink-cli.exe" — the same in the command line, for those who prefer
  it, and for automation.

BEFORE YOU START, ON THE CONSOLE

PS4:
1. GoldHEN loaded (the FTP server listens on port 2121).
2. Remote Package Installer open and IN THE FOREGROUND while you send
   commands. Once the install has started you can minimise it.

PS5:
1. For Remote Play, nothing beyond enabling Remote Play.
2. For FTP and installing: a jailbreak with etaHEN, with FTP=1 (port 1337)
   and DPI_v2=1 (port 12800) in its config.ini.

In both cases, the PC and the console on the same network.

REMOTE PLAY

On a PS4: Settings > Remote Play Connection Settings > Enable, then
"Add Device", which shows an 8-digit PIN.
On a PS5: Settings > System > Remote Play > Link Device, signed in with the
account you will use.

In OrbisLink: click the console's box. If this PC is not registered yet,
the registration opens and asks for the PIN and the PSN Account ID. The
Account ID can be pasted in decimal (as PlayStation's site shows it),
hexadecimal or base64. The PIN lasts a few minutes; if it fails, ask the
console for a new one.

After that, one click connects: if the console is in rest mode, OrbisLink
wakes it, waits until it is ready and connects.

During the stream the keyboard acts as a controller — the "Keys" button
shows the map, and every key can be changed. Esc ends the session. A real
controller (DualShock or DualSense over USB or Bluetooth) also works.

Remote Play uses ports 987/UDP (PS4), 9302/UDP (PS5), 9295/TCP and
9296-9297/UDP. They are outgoing connections from the PC to the console:
the Windows firewall does not usually get in the way.

USING THE COMMAND LINE

Open the Start menu > OrbisLink > "OrbisLink (command line)".
In the examples, replace 192.168.1.42 with your console's IP.

  Check that the services answer:
    orbislink-cli.exe services --host 192.168.1.42

  See what is inside a pkg:
    orbislink-cli.exe inspect "D:\games\Game.pkg"

  Install (the PC serves the file, the console downloads and installs it):
    orbislink-cli.exe install --host 192.168.1.42 "D:\games\Game.pkg"

  Several at once (the game > patch > DLC order is automatic):
    orbislink-cli.exe install --host 192.168.1.42 "Game.pkg" "Patch.pkg" "DLC.pkg"

  FTP:
    orbislink-cli.exe ftp-ls  --host 192.168.1.42 /data/pkg
    orbislink-cli.exe ftp-put --host 192.168.1.42 "D:\games\Game.pkg" /data/pkg/Game.pkg
    orbislink-cli.exe ftp-get --host 192.168.1.42 /data/pkg/Game.pkg "D:\Game.pkg"

  Full list of commands:
    orbislink-cli.exe --help

IF THE WINDOW DOES NOT OPEN

On machines without graphics acceleration - Windows Sandbox, virtual
machines, remote desktop - Qt cannot create the drawing context and the
window never appears.

OrbisLink detects this by itself: if a start never draws anything, the next
start switches to software rendering automatically. In other words, just
open it a second time.

To avoid waiting, use the shortcut:
  Start menu > OrbisLink > "OrbisLink (compatibility mode)"

or, in the command line:
  orbislink-gui.exe --software

Either way, there is always a log of what happened in:
  %APPDATA%\OrbisLink\orbislink-gui.log

If you ask for help, send that file: it says exactly where it stopped.

IF DRAG AND DROP DOES NOT WORK

If the cursor shows the "forbidden" sign and the panel asking "Drop to..."
does not appear, OrbisLink is running as administrator.

Windows does not let you drag files from Explorer - which runs without
elevated privileges - onto a window that has them. It blocks the messages
without telling anyone, so it looks as if the feature disappeared.

Close OrbisLink and open it from the normal shortcut (desktop or Start
menu). Do not open it with "Run as administrator", and there is no need:
OrbisLink does not need any privileges to work.

OrbisLink warns when this happens, and the diagnostics have a "Privileges"
line that confirms it.

FIREWALL

The console downloads the pkg files from a server running on your PC. If
Windows blocks incoming connections, the install stays at 0 bytes and
OrbisLink warns "The console could not download from the PC".

The installer creates a firewall rule for OrbisLink on private networks. If
you turned it off, or if the network is marked as public, allow the app when
Windows asks, or create the rule by hand.

WHERE THE FILES ARE

  Settings and logs:      %APPDATA%\OrbisLink
  Interface log:          %APPDATA%\OrbisLink\orbislink-gui.log
  Program:                the folder you installed to (by default
                          C:\Program Files\OrbisLink)

The log never stores the PSN Account ID or registration keys.

INTENDED USE

For your own consoles, with homebrew and copies of games you legally own.
The app does not include, download, index or suggest sources of content,
and it does not jailbreak anything: a PS4 must already be running GoldHEN,
and a PS5 needs etaHEN for FTP and installing.

LICENCE

AGPL-3.0-or-later. See LICENSE.txt.
Source code: @REPO_URL@
