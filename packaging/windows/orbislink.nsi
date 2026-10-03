; SPDX-License-Identifier: AGPL-3.0-or-later
; OrbisLink installer for Windows x64.
; Built with NSIS (makensis), from Linux or from Windows.

Unicode true
SetCompressor /SOLID lzma

!define APP_NAME "OrbisLink"
!define APP_PUBLISHER "OrbisLink project"
; The project's address is not written here: it comes from whoever builds
; (-DAPP_URL), and in CI it is the repository the build ran in. That way a
; fork of this project points at itself without editing anything.
!ifndef APP_URL
  !define APP_URL ""
!endif
!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
!endif
; Windows' VIProductVersion needs exactly X.X.X.X, numeric, so it cannot be
; APP_VERSION (which may be "0.1.0-dev" or "v1.2.3").
!ifndef APP_VERSION_NUMERIC
  !define APP_VERSION_NUMERIC "0.0.0.0"
!endif
!ifndef SOURCE_DIR
  !define SOURCE_DIR "..\..\dist\windows"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "..\..\dist\OrbisLink-${APP_VERSION}-setup.exe"
!endif

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinMessages.nsh"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"
RequestExecutionLevel admin

VIProductVersion "${APP_VERSION_NUMERIC}"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "OrbisLink — Remote Play, pkg install and FTP for PS4 and PS5"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "AGPL-3.0-or-later"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"

!define MUI_ICON "${SOURCE_DIR}\orbislink.ico"
!define MUI_UNICON "${SOURCE_DIR}\orbislink.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_LICENSE "${SOURCE_DIR}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!ifdef INCLUDE_GUI
; CAREFUL: this cannot be a MUI_FINISHPAGE_RUN straight to the executable.
;
; This installer runs elevated (RequestExecutionLevel admin, because of the
; firewall rule). A program launched from here inherits the elevation, and
; Windows does NOT let you drag files from Explorer (which runs without
; elevation) onto an elevated window. UIPI blocks the messages without any
; error: the cursor shows the "forbidden" sign and nothing else.
;
; Launched from here, OrbisLink would have drag and drop dead, and working
; when opened from the shortcut.
;
; The plugin-free fix is to ask explorer.exe to launch it: it runs with the
; user's privileges, and the child inherits those and not ours.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION OpenWithoutElevation
!define MUI_FINISHPAGE_RUN_TEXT "Open OrbisLink"
!endif
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Open the instructions"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

!ifdef INCLUDE_GUI
Function OpenWithoutElevation
  ; explorer.exe runs without elevation; what it launches is the same.
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\gui\orbislink-gui.exe"'
FunctionEnd
!endif

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "OrbisLink needs a 64-bit version of Windows."
    Abort
  ${EndIf}
FunctionEnd

; If the executable is locked, OrbisLink is open. Installing like that leaves
; files unreplaced and the app ends up mixing versions, which causes crashes
; that are hard to understand. Better to stop here.
Function EnsureNotRunning
  IfFileExists "$INSTDIR\gui\orbislink-gui.exe" 0 free
  check:
    ClearErrors
    FileOpen $0 "$INSTDIR\gui\orbislink-gui.exe" a
    IfErrors locked
    FileClose $0
    Goto free
  locked:
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
      "OrbisLink is open.$\r$\n$\r$\nClose it before continuing: installing over it \
would leave files from two versions mixed together, and the app would stop starting." \
      IDRETRY check
    Abort
  free:
FunctionEnd

Section "OrbisLink (required)" SEC_CORE
  SectionIn RO
  Call EnsureNotRunning
  SetOutPath "$INSTDIR"
  File "${SOURCE_DIR}\orbislink-cli.exe"
  File "${SOURCE_DIR}\LICENSE.txt"
  File "${SOURCE_DIR}\LICENSE-Inter.txt"
  File "${SOURCE_DIR}\LICENSE-Lucide.txt"
  File "${SOURCE_DIR}\LICENSE-LibOrbisPkg.txt"
  File "${SOURCE_DIR}\README.txt"
  File "${SOURCE_DIR}\diagnostics.bat"
  ; Files that older versions installed under Portuguese names.
  Delete "$INSTDIR\LEIA-ME.txt"
  Delete "$INSTDIR\diagnostico.bat"
  Delete "$INSTDIR\diagnostico.txt"
  File "${SOURCE_DIR}\orbislink.ico"

  WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\${APP_NAME}" "Version" "${APP_VERSION}"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "DisplayIcon" "$\"$INSTDIR\orbislink.ico$\""
!if "${APP_URL}" != ""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "URLInfoAbout" "${APP_URL}"
!endif
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "InstallLocation" "$\"$INSTDIR$\""
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "NoRepair" 1

  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

!ifdef INCLUDE_GUI
Section "Graphical interface" SEC_GUI
  ; Deletes the previous version before copying: that way files from two
  ; different versions never sit side by side.
  RMDir /r "$INSTDIR\gui"
  ; It has its own subfolder: it carries the Qt DLLs and the uninstaller
  ; removes it whole, without touching anything else.
  SetOutPath "$INSTDIR\gui"
  File /r "${SOURCE_DIR}\gui\*.*"
  SetOutPath "$INSTDIR"
SectionEnd
!endif

Section "Start Menu shortcuts" SEC_SHORTCUTS
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  ; Shortcuts that older versions created under Portuguese names.
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (modo compatível).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (command line).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Instructions.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Diagnostics.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
  ; Names older versions used.
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (linha de comandos).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Instruções.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Diagnóstico.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Desinstalar.lnk"
!ifdef INCLUDE_GUI
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\OrbisLink.lnk" \
    "$INSTDIR\gui\orbislink-gui.exe" "" "$INSTDIR\gui\orbislink-gui.exe" 0
  ; For machines without graphics acceleration: Windows Sandbox, virtual
  ; machines, remote desktop. The app also detects this by itself on the
  ; second start, but this way there is no need to wait.
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\OrbisLink (compatibility mode).lnk" \
    "$INSTDIR\gui\orbislink-gui.exe" "--software" "$INSTDIR\gui\orbislink-gui.exe" 0
  CreateShortcut "$DESKTOP\OrbisLink.lnk" \
    "$INSTDIR\gui\orbislink-gui.exe" "" "$INSTDIR\gui\orbislink-gui.exe" 0
!endif
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\OrbisLink (command line).lnk" \
    "$WINDIR\System32\cmd.exe" '/K "cd /d $\"$INSTDIR$\" && orbislink-cli.exe --help"' \
    "$INSTDIR\orbislink-cli.exe" 0
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Instructions.lnk" "$INSTDIR\README.txt"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Diagnostics.lnk" "$INSTDIR\diagnostics.bat"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Firewall rule for the local HTTP server" SEC_FIREWALL
  ; Without this rule the console cannot download the pkg files from the PC.
  ; The rule is limited to the OrbisLink executable and to private networks.
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="OrbisLink"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="OrbisLink" \
    dir=in action=allow program="$INSTDIR\orbislink-cli.exe" enable=yes \
    profile=private protocol=TCP localport=any'
  Pop $0
!ifdef INCLUDE_GUI
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="OrbisLink (interface)"'
  Pop $1
  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="OrbisLink (interface)" \
    dir=in action=allow program="$INSTDIR\gui\orbislink-gui.exe" enable=yes \
    profile=private protocol=TCP localport=any'
  Pop $1
!endif
  ${If} $0 != 0
    DetailPrint "Could not create the firewall rule (code $0). \
      Create it by hand if the console cannot download from the PC."
  ${EndIf}
SectionEnd

Section /o "Add to the system PATH" SEC_PATH
  ; Lets you run "orbislink-cli" from any folder.
  ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
  ${If} $0 == ""
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
      "Path" "$INSTDIR"
  ${Else}
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
      "Path" "$0;$INSTDIR"
  ${EndIf}
  SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=2000
SectionEnd

LangString DESC_CORE ${LANG_ENGLISH} "The OrbisLink executable and the license."
LangString DESC_SHORTCUTS ${LANG_ENGLISH} "Start Menu shortcuts."
!ifdef INCLUDE_GUI
LangString DESC_GUI ${LANG_ENGLISH} "The OrbisLink window, with drag and drop and the queue."
!endif
LangString DESC_FIREWALL ${LANG_ENGLISH} \
  "Allows inbound connections to OrbisLink on private networks. Without it the console cannot download packages from the PC."
LangString DESC_PATH ${LANG_ENGLISH} "Lets you run orbislink-cli from any folder."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_CORE} $(DESC_CORE)
!ifdef INCLUDE_GUI
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_GUI} $(DESC_GUI)
!endif
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_SHORTCUTS} $(DESC_SHORTCUTS)
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_FIREWALL} $(DESC_FIREWALL)
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_PATH} $(DESC_PATH)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="OrbisLink"'
  Pop $0
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="OrbisLink (interface)"'
  Pop $0

  RMDir /r "$INSTDIR\gui"
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (compatibility mode).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (modo compatível).lnk"
  Delete "$DESKTOP\OrbisLink.lnk"

  Delete "$INSTDIR\orbislink-cli.exe"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\LICENSE-Inter.txt"
  Delete "$INSTDIR\LICENSE-Lucide.txt"
  Delete "$INSTDIR\LICENSE-LibOrbisPkg.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\diagnostics.bat"
  Delete "$INSTDIR\diagnostics.txt"
  Delete "$INSTDIR\LEIA-ME.txt"
  Delete "$INSTDIR\diagnostico.bat"
  Delete "$INSTDIR\diagnostico.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (command line).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Instructions.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Diagnostics.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
  ; Names older versions used.
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (linha de comandos).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Instruções.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Diagnóstico.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Desinstalar.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
  ; The user's settings in %APPDATA%\OrbisLink are not deleted.
SectionEnd
