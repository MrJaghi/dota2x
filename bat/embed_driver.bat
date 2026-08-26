@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM embed_driver.bat
REM ------------------------------------------------------------
REM Converts the compiled KernelDriver (.sys) into a C++ header
REM (mapper/driver_resource.hpp) so the Mapper project can embed
REM the driver bytes directly.
REM
REM Run from repo root (where DragonBurn.sln lives):
REM     bat\embed_driver.bat [Release|Debug]
REM ============================================================

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
pushd "%REPO_ROOT%"
cd /d "%REPO_ROOT%"

REM WDK output paths vary by VS/SDK version -- probe a few.
set "DRIVER_BIN="
if exist "x64\%CONFIG%\KernelDriver\SharedMemorySpectre.sys" set "DRIVER_BIN=x64\%CONFIG%\KernelDriver\SharedMemorySpectre.sys"
if not defined DRIVER_BIN if exist "x64\%CONFIG%\SharedMemorySpectre.sys" set "DRIVER_BIN=x64\%CONFIG%\SharedMemorySpectre.sys"
if not defined DRIVER_BIN if exist "x64\%CONFIG%\KernelDriver.sys"      set "DRIVER_BIN=x64\%CONFIG%\KernelDriver.sys"
if not defined DRIVER_BIN if exist "x64\%CONFIG%\KernelDriver\KernelDriver.sys" set "DRIVER_BIN=x64\%CONFIG%\KernelDriver\KernelDriver.sys"

if defined DRIVER_BIN goto have_driver
echo [-] Could not find compiled driver .sys under x64\%CONFIG%\
echo     Build KernelDriver first ^(SharedMemorySpectre.sys^), then re-run.
popd
exit /b 1
:have_driver

set "OUT=mapper\driver_resource.hpp"

echo [*] Embedding driver: %DRIVER_BIN% -^> %OUT%
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%bin2h.ps1" ^
    -InputPath  "%DRIVER_BIN%" ^
    -OutputPath "%OUT%" ^
    -Namespace  "driver_resource" ^
    -ArrayName  "driver_bytes"
if not errorlevel 1 goto embed_ok
echo [-] Driver embedding failed.
popd
exit /b 1
:embed_ok

echo [+] Driver embedded. Now ^(re^)build Mapper.
popd
endlocal
