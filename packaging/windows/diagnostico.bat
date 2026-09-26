@echo off
rem SPDX-License-Identifier: AGPL-3.0-or-later
rem Corre o OrbisLink e junta tudo o que aconteceu num so ficheiro,
rem para poder ser enviado a quem esta a ajudar.
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "RELATORIO=%~dp0diagnostico.txt"
set "EXE=gui\orbislink-gui.exe"
if not exist "%EXE%" set "EXE=orbislink-gui.exe"

echo ==================================================== > "%RELATORIO%"
echo  OrbisLink - diagnostico >> "%RELATORIO%"
echo  %DATE% %TIME% >> "%RELATORIO%"
echo ==================================================== >> "%RELATORIO%"
echo. >> "%RELATORIO%"

echo [sistema] >> "%RELATORIO%"
ver >> "%RELATORIO%" 2>&1
echo. >> "%RELATORIO%"

echo [ficheiros] >> "%RELATORIO%"
if exist "%EXE%" (echo encontrado: %EXE%) else (echo EM FALTA: %EXE%)>> "%RELATORIO%"
if exist "orbislink-cli.exe" (echo encontrado: orbislink-cli.exe) else (echo EM FALTA: orbislink-cli.exe)>> "%RELATORIO%"
echo. >> "%RELATORIO%"

echo [linha de comandos - se isto falhar, o problema nao e grafico] >> "%RELATORIO%"
if exist "orbislink-cli.exe" (
  orbislink-cli.exe --help >> "%RELATORIO%" 2>&1
  echo codigo de saida: !ERRORLEVEL! >> "%RELATORIO%"
)
echo. >> "%RELATORIO%"

echo [relatorio interno da aplicacao] >> "%RELATORIO%"
rem A propria aplicacao sabe reunir versoes, rede, estado dos servicos e o
rem diario da ultima tentativa de Remote Play. Sai sozinha a seguir.
"%EXE%" --software --print-diagnostics >> "%RELATORIO%" 2>&1
echo codigo de saida: !ERRORLEVEL! >> "%RELATORIO%"
echo. >> "%RELATORIO%"

echo [interface grafica, modo compativel] >> "%RELATORIO%"
echo A abrir a janela. Fecha-a quando quiseres para continuar...
"%EXE%" --software >> "%RELATORIO%" 2>&1
echo codigo de saida: !ERRORLEVEL! >> "%RELATORIO%"
echo. >> "%RELATORIO%"

echo [registo da aplicacao] >> "%RELATORIO%"
if exist "%APPDATA%\OrbisLink\orbislink-gui.log" (
  type "%APPDATA%\OrbisLink\orbislink-gui.log" >> "%RELATORIO%"
) else if exist "gui\orbislink-gui.log" (
  type "gui\orbislink-gui.log" >> "%RELATORIO%"
) else if exist "orbislink-gui.log" (
  type "orbislink-gui.log" >> "%RELATORIO%"
) else (
  echo Nenhum registo encontrado - a aplicacao nem chegou a arrancar. >> "%RELATORIO%"
)

echo [registo de eventos do Windows] >> "%RELATORIO%"
rem O Windows guarda o modulo culpado quando um programa estoira.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Get-WinEvent -FilterHashtable @{LogName='Application'} -MaxEvents 200 -ErrorAction SilentlyContinue | Where-Object { $_.Message -like '*orbislink*' } | Select-Object -First 5 | ForEach-Object { $_.TimeCreated; $_.Message; '---' }" >> "%RELATORIO%" 2>&1
echo. >> "%RELATORIO%"

echo.
echo Relatorio escrito em: %RELATORIO%
echo Envia esse ficheiro.
echo.
echo Com a aplicacao aberta tambem da para fazer o mesmo por Ctrl+L,
echo no botao "Guardar no ambiente de trabalho".
echo.
start "" notepad "%RELATORIO%"
endlocal
