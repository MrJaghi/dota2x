@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM embed_esp.bat
REM ------------------------------------------------------------
REM Converts the compiled ExternalESP (.exe) into a C++ header
REM (loader/esp_resource.hpp) so the Loader can embed it.
REM
REM Run from repo root:
REM     bat\embed_esp.bat [Release|Debug]
REM ============================================================

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
pushd "%REPO_ROOT%"
cd /d "%REPO_ROOT%"

set "ESP_BIN=x64\%CONFIG%\ExternalESP.exe"
if exist "%ESP_BIN%" goto have_esp
echo [-] Could not find ExternalESP.exe at %ESP_BIN%
echo     Build ExternalESP first, then re-run this script.
popd
exit /b 1
:have_esp

set "OUT=loader\esp_resource.hpp"

echo [*] Embedding ESP: %ESP_BIN% -^> %OUT%
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%bin2h.ps1" ^
    -InputPath  "%ESP_BIN%" ^
    -OutputPath "%OUT%" ^
    -Namespace  "esp_resource" ^
    -ArrayName  "esp_bytes"
if not errorlevel 1 goto embed_ok
echo [-] ESP embedding failed.
popd
exit /b 1
:embed_ok

echo [+] ESP embedded. Now build Loader.
popd
endlocal
