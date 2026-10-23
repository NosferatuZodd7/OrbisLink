// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Names <windows.h> turns into macros, defined here the way it does (the
// wide-character forms), so that a build on Linux trips over them as the
// Windows build would: a struct called CopyFile, a function called
// GetObject, an enum value called ERROR all become something else there.
// Used by scripts/check-win32-names.py; never part of a real build.
#pragma once

// winbase.h, fileapi.h, processenv.h…
#define CopyFile CopyFileW
#define CopyFileEx CopyFileExW
#define MoveFile MoveFileW
#define MoveFileEx MoveFileExW
#define DeleteFile DeleteFileW
#define CreateFile CreateFileW
#define CreateDirectory CreateDirectoryW
#define RemoveDirectory RemoveDirectoryW
#define GetFileAttributes GetFileAttributesW
#define SetFileAttributes SetFileAttributesW
#define FindFirstFile FindFirstFileW
#define FindNextFile FindNextFileW
#define GetTempPath GetTempPathW
#define GetTempFileName GetTempFileNameW
#define GetFullPathName GetFullPathNameW
#define GetCurrentDirectory GetCurrentDirectoryW
#define SetCurrentDirectory SetCurrentDirectoryW
#define GetModuleFileName GetModuleFileNameW
#define GetModuleHandle GetModuleHandleW
#define LoadLibrary LoadLibraryW
#define GetEnvironmentVariable GetEnvironmentVariableW
#define SetEnvironmentVariable SetEnvironmentVariableW
#define ExpandEnvironmentStrings ExpandEnvironmentStringsW
#define GetCommandLine GetCommandLineW
#define CreateProcess CreateProcessW
#define CreateEvent CreateEventW
#define CreateMutex CreateMutexW
#define CreateSemaphore CreateSemaphoreW
#define OpenEvent OpenEventW
#define OpenMutex OpenMutexW
#define FormatMessage FormatMessageW
#define OutputDebugString OutputDebugStringW
#define GetUserName GetUserNameW
#define GetComputerName GetComputerNameW
#define GetVersionEx GetVersionExW
#define ReportEvent ReportEventW
#define StartService StartServiceW
#define OpenService OpenServiceW
#define GetCurrentTime() GetTickCount()
#define Yield()
#define CopyMemory RtlCopyMemory
#define MoveMemory RtlMoveMemory
#define FillMemory RtlFillMemory
#define ZeroMemory RtlZeroMemory

// winuser.h, wingdi.h, commdlg.h, shellapi.h, mmsystem.h, winspool.h
#define LoadString LoadStringW
#define LoadImage LoadImageW
#define LoadIcon LoadIconW
#define LoadCursor LoadCursorW
#define LoadBitmap LoadBitmapW
#define MessageBox MessageBoxW
#define SendMessage SendMessageW
#define PostMessage PostMessageW
#define GetMessage GetMessageW
#define PeekMessage PeekMessageW
#define DispatchMessage DispatchMessageW
#define CreateWindow CreateWindowW
#define CreateWindowEx CreateWindowExW
#define CreateDialog CreateDialogW
#define DialogBox DialogBoxW
#define RegisterClass RegisterClassW
#define GetClassName GetClassNameW
#define GetWindowText GetWindowTextW
#define SetWindowText SetWindowTextW
#define GetWindowLong GetWindowLongW
#define SetWindowLong SetWindowLongW
#define GetProp GetPropW
#define SetProp SetPropW
#define RemoveProp RemovePropW
#define CharUpper CharUpperW
#define CharLower CharLowerW
#define DrawText DrawTextW
#define TextOut TextOutW
#define CreateFont CreateFontW
#define GetObject GetObjectW
#define StartDoc StartDocW
#define FindText FindTextW
#define ReplaceText ReplaceTextW
#define ChooseColor ChooseColorW
#define ChooseFont ChooseFontW
#define GetOpenFileName GetOpenFileNameW
#define GetSaveFileName GetSaveFileNameW
#define PrintDlg PrintDlgW
#define ShellExecute ShellExecuteW
#define PlaySound PlaySoundW
#define AddJob AddJobW
#define GetJob GetJobW
#define SetJob SetJobW
#define AddForm AddFormW
#define GetForm GetFormW
#define SetForm SetFormW
#define DeleteForm DeleteFormW
#define OpenPrinter OpenPrinterW
#define AddPrinter AddPrinterW

// Plain words that are macros there too.
#define ERROR 0
#define DELETE (0x00010000L)
#define ABSOLUTE 1
#define RELATIVE 2
#define TRANSPARENT 1
#define OPAQUE 2
#define IGNORE 0
#define INFINITE 0xFFFFFFFF
#define NO_ERROR 0L
#define IN
#define OUT
#define OPTIONAL
#define NEAR
#define FAR
#define PASCAL
#define CALLBACK
#define WINAPI
#define near
#define far
// Not here: "interface" (objbase.h) and "small" (rpcndr.h), which the build
// leaves out with WIN32_LEAN_AND_MEAN (CMakeLists.txt).
