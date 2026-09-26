; SPDX-License-Identifier: AGPL-3.0-or-later
; Instalador do OrbisLink para Windows x64.
; Construído com NSIS (makensis), a partir de Linux ou de Windows.

Unicode true
SetCompressor /SOLID lzma

!define APP_NAME "OrbisLink"
!define APP_PUBLISHER "Projeto OrbisLink"
; O endereço do projecto não está escrito aqui: vem de quem compila
; (-DAPP_URL), e no CI é o repositório onde a compilação correu. Assim uma
; bifurcação deste projecto aponta para si própria sem editar nada.
!ifndef APP_URL
  !define APP_URL ""
!endif
!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
!endif
; O VIProductVersion do Windows exige exatamente X.X.X.X numérico, por isso
; não pode ser o APP_VERSION (que pode ser "0.1.0-dev" ou "v1.2.3").
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
VIAddVersionKey "FileDescription" "OrbisLink — instalação de pkg e FTP para PS4 com GoldHEN"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "AGPL-3.0-or-later"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"

!define MUI_ICON "${SOURCE_DIR}\orbislink.ico"
!define MUI_UNICON "${SOURCE_DIR}\orbislink.ico"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_ALLLANGUAGES

!insertmacro MUI_PAGE_LICENSE "${SOURCE_DIR}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!ifdef INCLUDE_GUI
; ATENCAO: nao pode ser um MUI_FINISHPAGE_RUN directo para o executavel.
;
; Este instalador corre elevado (RequestExecutionLevel admin, por causa da
; regra de firewall). Um programa lancado a partir daqui herda a elevacao,
; e o Windows NAO deixa arrastar ficheiros do Explorador (que corre sem
; elevacao) para uma janela elevada. E o UIPI a bloquear as mensagens, sem
; erro nenhum: o cursor mostra o sinal de proibido e mais nada.
;
; Lancado daqui, o OrbisLink ficaria com o arrastar e largar morto, e a
; funcionar quando aberto pelo atalho.
;
; A solucao sem plugins e pedir ao explorer.exe que o lance: ele corre com
; os privilegios do utilizador, e o filho herda os dele e nao os nossos.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION AbrirSemElevacao
!define MUI_FINISHPAGE_RUN_TEXT "Abrir o OrbisLink"
!endif
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\LEIA-ME.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Abrir as instruções"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "PortugueseBR"

!ifdef INCLUDE_GUI
Function AbrirSemElevacao
  ; O explorer.exe corre sem elevacao; o que ele lanca fica igual a ele.
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\gui\orbislink-gui.exe"'
FunctionEnd
!endif
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "O OrbisLink precisa de uma versão de 64 bits do Windows."
    Abort
  ${EndIf}
FunctionEnd

; Se o executável estiver bloqueado é porque o OrbisLink está aberto. Instalar
; assim deixa ficheiros por substituir e a aplicação passa a misturar versões,
; o que dá estoiros difíceis de perceber. Mais vale parar aqui.
Function EnsureNotRunning
  IfFileExists "$INSTDIR\gui\orbislink-gui.exe" 0 livre
  verificar:
    ClearErrors
    FileOpen $0 "$INSTDIR\gui\orbislink-gui.exe" a
    IfErrors bloqueado
    FileClose $0
    Goto livre
  bloqueado:
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
      "O OrbisLink está aberto.$\r$\n$\r$\nFecha-o antes de continuar: instalar por cima \
deixaria ficheiros de duas versões misturados, e a aplicação deixaria de arrancar." \
      IDRETRY verificar
    Abort
  livre:
FunctionEnd

Section "OrbisLink (necessário)" SEC_CORE
  SectionIn RO
  Call EnsureNotRunning
  SetOutPath "$INSTDIR"
  File "${SOURCE_DIR}\orbislink-cli.exe"
  File "${SOURCE_DIR}\LICENSE.txt"
  File "${SOURCE_DIR}\LEIA-ME.txt"
  File "${SOURCE_DIR}\diagnostico.bat"
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
Section "Interface gráfica" SEC_GUI
  ; Apaga a versão anterior antes de copiar: assim nunca ficam ficheiros de
  ; duas versões diferentes lado a lado.
  RMDir /r "$INSTDIR\gui"
  ; Fica numa subpasta própria: traz as DLLs do Qt e o desinstalador remove-a
  ; inteira, sem tocar em mais nada.
  SetOutPath "$INSTDIR\gui"
  File /r "${SOURCE_DIR}\gui\*.*"
  SetOutPath "$INSTDIR"
SectionEnd
!endif

Section "Atalhos no Menu Iniciar" SEC_SHORTCUTS
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
!ifdef INCLUDE_GUI
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\OrbisLink.lnk" \
    "$INSTDIR\gui\orbislink-gui.exe" "" "$INSTDIR\gui\orbislink-gui.exe" 0
  ; Para máquinas sem aceleração gráfica: Windows Sandbox, máquinas virtuais,
  ; ambiente de trabalho remoto. A aplicação também deteta isto sozinha ao
  ; segundo arranque, mas assim não é preciso esperar.
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\OrbisLink (modo compatível).lnk" \
    "$INSTDIR\gui\orbislink-gui.exe" "--software" "$INSTDIR\gui\orbislink-gui.exe" 0
  CreateShortcut "$DESKTOP\OrbisLink.lnk" \
    "$INSTDIR\gui\orbislink-gui.exe" "" "$INSTDIR\gui\orbislink-gui.exe" 0
!endif
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\OrbisLink (linha de comandos).lnk" \
    "$WINDIR\System32\cmd.exe" '/K "cd /d $\"$INSTDIR$\" && orbislink-cli.exe --help"' \
    "$INSTDIR\orbislink-cli.exe" 0
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Instruções.lnk" "$INSTDIR\LEIA-ME.txt"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Diagnóstico.lnk" "$INSTDIR\diagnostico.bat"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Desinstalar.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Regra de firewall para o servidor HTTP local" SEC_FIREWALL
  ; Sem esta regra a consola não consegue descarregar os pkg do PC.
  ; A regra é limitada ao executável do OrbisLink e às redes privadas.
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
    DetailPrint "Não foi possível criar a regra de firewall (código $0). \
      Cria-a à mão se a consola não conseguir descarregar do PC."
  ${EndIf}
SectionEnd

Section /o "Acrescentar ao PATH do sistema" SEC_PATH
  ; Deixa chamar "orbislink-cli" de qualquer pasta.
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

LangString DESC_CORE ${LANG_PORTUGUESEBR} "O executável do OrbisLink e a licença."
LangString DESC_CORE ${LANG_ENGLISH} "The OrbisLink executable and the license."
LangString DESC_SHORTCUTS ${LANG_PORTUGUESEBR} "Atalhos no Menu Iniciar."
LangString DESC_SHORTCUTS ${LANG_ENGLISH} "Start Menu shortcuts."
!ifdef INCLUDE_GUI
LangString DESC_GUI ${LANG_PORTUGUESEBR} "A janela do OrbisLink, com o arrastar e largar e a fila."
LangString DESC_GUI ${LANG_ENGLISH} "The OrbisLink window, with drag and drop and the queue."
!endif
LangString DESC_FIREWALL ${LANG_PORTUGUESEBR} \
  "Autoriza ligações de entrada para o OrbisLink em redes privadas. Sem isto, a consola não consegue descarregar os pkg do PC."
LangString DESC_FIREWALL ${LANG_ENGLISH} \
  "Allows inbound connections to OrbisLink on private networks. Without it the console cannot download packages from the PC."
LangString DESC_PATH ${LANG_PORTUGUESEBR} "Permite chamar orbislink-cli de qualquer pasta."
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
  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (modo compatível).lnk"
  Delete "$DESKTOP\OrbisLink.lnk"

  Delete "$INSTDIR\orbislink-cli.exe"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\LEIA-ME.txt"
  Delete "$INSTDIR\diagnostico.bat"
  Delete "$INSTDIR\diagnostico.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\OrbisLink (linha de comandos).lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Instruções.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Diagnóstico.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Desinstalar.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  DeleteRegKey HKLM "Software\${APP_NAME}"
  ; As definições do utilizador em %APPDATA%\OrbisLink não são apagadas.
SectionEnd
