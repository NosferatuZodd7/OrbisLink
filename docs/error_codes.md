# Console error codes

The remote installer reports failures as
`{ "status": "fail", "error_code": 0x8XXXXXXX }` (hexadecimal **without
quotes**, see `docs/validation.md`). `RpiClient` reads that code and passes it
through `describeConsoleError()` (`src/orbislink/installer/error_codes.cpp`).

**Rule:** a code is only described when its value is confirmed by an official
source. Unknown codes are shown in hexadecimal, without inventing a meaning.

## Confirmed — libkernel (`0x8002xxxx`)

Source: `OpenOrbis-PS4-Toolchain/include/orbis/_types/errors.h`
(`ORBIS_KERNEL_ERROR_*`, which are the `SCE_KERNEL_ERROR_*` used in the
installer's code).

| Code | Name | Message shown |
|---|---|---|
| `0x80020001` | EPERM | Operation not permitted on the console. |
| `0x80020002` | ENOENT | The console could not find the file or path. |
| `0x80020005` | EIO | Input/output error on the console. |
| `0x8002000C` | ENOMEM | The console ran out of memory. |
| `0x8002000D` | EACCES | Access denied on the console. |
| `0x80020010` | EBUSY | The resource is busy on the console. |
| `0x80020011` | EEXIST | Already exists. |
| `0x80020016` | EINVAL | Invalid request for the console. |
| `0x8002001B` | EFBIG | File too large for the console. |
| **`0x8002001C`** | **ENOSPC** | **Not enough space on the console.** |
| `0x8002001E` | EROFS | Read-only file system. |
| `0x80020023` | EAGAIN | The console asked to try again. |
| `0x80020033` | ENETUNREACH | The console could not reach the network. |
| `0x80020035` | ECONNABORTED | Connection aborted. |
| `0x80020036` | ECONNRESET | The connection was reset by the other side. |
| `0x8002003C` | ETIMEDOUT | The console timed out. |
| `0x8002003D` | ECONNREFUSED | The console could not connect to the PC (check the firewall). |
| `0x80020041` | EHOSTUNREACH | The console cannot reach the PC. |
| `0x80020055` | ECANCELED | The operation was cancelled. |

`isOutOfSpaceError()` returns `true` for `0x8002001C` — it is the code the UI
uses for the "not enough space" message required in §7.

## BGFT (`0x8099xxxx`)

Source: etaHEN's error table, `Source Code/util/include/error_translator.hpp`
(the `SCE_BGFT_ERROR_*` names; its DPI v2 installer reports with it). BGFT
is the console's download and install task service: what both Remote Package
Installer and etaHEN's DPI v2 hand the package to. OrbisLink describes the
ones an install from it can meet:

| Code | Name | Message shown |
|---|---|---|
| `0x80990004` | INVALID_ARGUMENT | The console's installer refused the request as invalid. |
| **`0x80990015`** | **TASK_DUPLICATED** | **The console already has an install task for this package, left by an earlier try.** OrbisLink removes that task and asks again when the installer can find it (`/api/find_task`, `/api/unregister_task`); otherwise: delete it from the console's Downloads list, or restart the console. |
| `0x80990018`, `0x8099008C` | TASK_ENTRY_NOSPC, DOWNLOAD_TASK_ENTRY_NOSPC | The console's download list is full. |
| `0x80990027` | HTTP_STATUS | The PC's server answered with an error. |
| `0x8099002C` | HTTP_RECV_IO | The download from the PC broke off. |
| `0x80990038` | CONTENTID_UNMATCH | The package does not match what the console expected. |
| `0x80990039`, `0x80990085`, `0x8099008D` | DEVICE_NOSPC, DEVICE_NOSPC_KERNEL, DOWNLOAD_DEVICE_NOSPC | Not enough space (`isOutOfSpaceError()`). |
| `0x80990106`, `0x80990107` | DOWNLOAD_DEVICE_EXT_NOSPC, DEVICE_EXT_NOSPC | Not enough space on the extended storage (`isOutOfSpaceError()`). |
| `0x80990045` | HTTP_NOT_CONNECTED | The console could not connect to the PC. |
| `0x80990053` | FILE_BROKEN | The package is damaged. |
| `0x80990079` | UNSUPPORTED_PACKAGE | The console does not take this kind of package. |
| `0x80990082` | NEED_SYSTEM_UPDATE | The package needs a newer system software. |
| `0x80990086` | CONTENT_ALREADY_DOWNLOADING | Already downloading. |
| `0x80990087` | DISC_APPLICATION_ALREADY_INSTALLED | The disc version is installed. |
| `0x80990088` | SAME_APPLICATION_ALREADY_INSTALLED | The same game is already installed. |
| `0x8099008B` | APPLICATION_IS_RUNNING | The game is running: close it first. |

## TODO — families still to confirm

The codes of the installer's other libraries are not published in any
citable source:

* **AppInstUtil** (`sceAppInstUtil*`, `is_exists`, uninstall) — family
  `0x8024xxxx`, **not confirmed**.
* **libhttp** (`SCE_HTTP_ERROR_*`, used when downloading from the PC) — names
  visible in the installer's `http.c`, values not published.

Until there is a source, these codes are shown as
`The console returned an error: 0x8XXXXXXX` and are written to the log.

**How to contribute:** when you catch a new code on real hardware, record the
code, the exact context (endpoint, action) and the source of the confirmation
here before adding it to the table in `error_codes.cpp`.

## Remote Play registration

When the console refuses a registration, it sends an `RP-Application-Reason`
header. The app turns it into a sentence:

| Code | Meaning |
|---|---|
| `0x80108b02` | The console did not recognise the Account ID (wrong account, or bytes in the wrong order). |
| `0x80108b09` | The console rejected the PIN (expired or mistyped). |
| `0x80108b10` | Remote Play is already in use by another device. |
| `0x80108b11` | Incompatible Remote Play version. |
| `0x80108b15` | The console's Remote Play crashed. |

## Errors on OrbisLink's side

These do not come from the console; they are produced locally:

| Situation | Message |
|---|---|
| FTP port closed | FTP unavailable — check that GoldHEN is loaded and FTP is enabled. |
| Port 12800 closed | Remote installer unavailable. Open Remote Package Installer on the console. |
| Task created but 0 bytes served after 20 s | The console could not download from the PC. Check the Windows firewall and that both are on the same network. |
| Invalid pkg magic | Not a valid PS4 pkg. |
| Already installed | Already on the console — skipped. |
| Writing to a protected area | Protected system area: turn on Advanced mode in the settings. |
