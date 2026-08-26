@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM embed_intel.bat
REM ------------------------------------------------------------
REM Converts bin\iqvw64e.sys into mapper\intel_driver_resource.hpp
REM so the Mapper can load the vulnerable Intel NAL driver without
REM requiring an external .sys next to Mapper.exe.
REM
REM Run from repo root:
REM     bat\embed_intel.bat
REM
REM Expected SHA256 of iqvw64e.sys:
REM   4429f32db1cc70567919d7d47b844a91cf1329a6cd116f582305f3b7b60cd60b
REM ============================================================

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
REM Switch to repo root BEFORE any pushd/goto so returning to the parent
REM build_all.bat works even when build_all was invoked from inside bat\.
pushd "%REPO_ROOT%"
cd /d "%REPO_ROOT%"

set "INTEL_BIN=bin\iqvw64e.sys"
if exist "%INTEL_BIN%" goto have_intel
echo [-] Could not find %INTEL_BIN%
echo.
echo     Place the vulnerable Intel Network Adapter diagnostic driver
echo     ^(iqvw64e.sys^) into bin\iqvw64e.sys then re-run this script.
echo.
echo     The mapper can still run without embedding -- it will look for
echo     iqvw64e.sys next to Mapper.exe, in the working directory, and
echo     in ^%%SystemRoot^%%\System32\drivers.
popd
exit /b 1
:have_intel

set "OUT=mapper\intel_driver_resource.hpp"

echo [*] Embedding Intel driver: %INTEL_BIN% -^> %OUT%
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%bin2h.ps1" ^
    -InputPath  "%INTEL_BIN%" ^
    -OutputPath "%OUT%" ^
    -Namespace  "intel_driver_resource" ^
    -ArrayName  "driver_bytes"
if not errorlevel 1 goto embed_ok
echo [-] Intel driver embedding failed.
popd
exit /b 1
:embed_ok

echo [+] Intel driver embedded. Rebuild Mapper for the change to take effect.
popd
endlocal
