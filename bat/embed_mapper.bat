@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM embed_mapper.bat
REM ------------------------------------------------------------
REM Converts the compiled Mapper (.exe) into a C++ header
REM (loader/mapper_resource.hpp) so the Loader can embed it.
REM
REM Run from repo root:
REM     bat\embed_mapper.bat [Release|Debug]
REM ============================================================

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
pushd "%REPO_ROOT%"
cd /d "%REPO_ROOT%"

set "MAPPER_BIN=x64\%CONFIG%\Mapper.exe"
if exist "%MAPPER_BIN%" goto have_mapper
echo [-] Could not find Mapper.exe at %MAPPER_BIN%
echo     Build Mapper first, then re-run this script.
popd
exit /b 1
:have_mapper

set "OUT=loader\mapper_resource.hpp"

echo [*] Embedding mapper: %MAPPER_BIN% -^> %OUT%
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%bin2h.ps1" ^
    -InputPath  "%MAPPER_BIN%" ^
    -OutputPath "%OUT%" ^
    -Namespace  "mapper_resource" ^
    -ArrayName  "mapper_bytes"
if not errorlevel 1 goto embed_ok
echo [-] Mapper embedding failed.
popd
exit /b 1
:embed_ok

echo [+] Mapper embedded. Now build ExternalESP then embed it.
popd
endlocal
