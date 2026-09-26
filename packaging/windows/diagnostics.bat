@echo off
rem SPDX-License-Identifier: AGPL-3.0-or-later
rem Runs OrbisLink and gathers everything that happened into a single file,
rem so it can be sent to whoever is helping.
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "REPORT=%~dp0diagnostics.txt"
set "EXE=gui\orbislink-gui.exe"
if not exist "%EXE%" set "EXE=orbislink-gui.exe"

echo ==================================================== > "%REPORT%"
echo  OrbisLink - diagnostics >> "%REPORT%"
echo  %DATE% %TIME% >> "%REPORT%"
echo ==================================================== >> "%REPORT%"
echo. >> "%REPORT%"

echo [system] >> "%REPORT%"
ver >> "%REPORT%" 2>&1
echo. >> "%REPORT%"

echo [files] >> "%REPORT%"
if exist "%EXE%" (echo found: %EXE%) else (echo MISSING: %EXE%)>> "%REPORT%"
if exist "orbislink-cli.exe" (echo found: orbislink-cli.exe) else (echo MISSING: orbislink-cli.exe)>> "%REPORT%"
echo. >> "%REPORT%"

echo [command line - if this fails, the problem is not graphical] >> "%REPORT%"
if exist "orbislink-cli.exe" (
  orbislink-cli.exe --help >> "%REPORT%" 2>&1
  echo exit code: !ERRORLEVEL! >> "%REPORT%"
)
echo. >> "%REPORT%"

echo [the app's own report] >> "%REPORT%"
rem The app itself knows how to gather versions, network, service state and
rem the log of the last Remote Play attempt. It exits by itself afterwards.
"%EXE%" --software --print-diagnostics >> "%REPORT%" 2>&1
echo exit code: !ERRORLEVEL! >> "%REPORT%"
echo. >> "%REPORT%"

echo [graphical interface, compatibility mode] >> "%REPORT%"
echo Opening the window. Close it whenever you want to continue...
"%EXE%" --software >> "%REPORT%" 2>&1
echo exit code: !ERRORLEVEL! >> "%REPORT%"
echo. >> "%REPORT%"

echo [app log] >> "%REPORT%"
if exist "%APPDATA%\OrbisLink\orbislink-gui.log" (
  type "%APPDATA%\OrbisLink\orbislink-gui.log" >> "%REPORT%"
) else if exist "gui\orbislink-gui.log" (
  type "gui\orbislink-gui.log" >> "%REPORT%"
) else if exist "orbislink-gui.log" (
  type "orbislink-gui.log" >> "%REPORT%"
) else (
  echo No log found - the app never even started. >> "%REPORT%"
)

echo [Windows event log] >> "%REPORT%"
rem Windows records the module at fault when a program crashes.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Get-WinEvent -FilterHashtable @{LogName='Application'} -MaxEvents 200 -ErrorAction SilentlyContinue | Where-Object { $_.Message -like '*orbislink*' } | Select-Object -First 5 | ForEach-Object { $_.TimeCreated; $_.Message; '---' }" >> "%REPORT%" 2>&1
echo. >> "%REPORT%"

echo.
echo Report written to: %REPORT%
echo Send that file.
echo.
echo With the app open you can do the same with Ctrl+L,
echo using the "Save to the desktop" button.
echo.
start "" notepad "%REPORT%"
endlocal
