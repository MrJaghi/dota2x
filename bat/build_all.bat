@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM build_all.bat
REM ------------------------------------------------------------
REM Full pipeline:
REM   1. Build KernelDriver (x64)
REM   2. Embed driver.sys  -> mapper/driver_resource.hpp
REM   3. Embed Intel driver (bin\iqvw64e.sys) -> mapper/intel_driver_resource.hpp
REM   4. Build Mapper      (x64)
REM   5. Embed mapper.exe  -> loader/mapper_resource.hpp
REM   6. Build ExternalESP (x64)
REM   7. Embed ExternalESP.exe -> loader/esp_resource.hpp
REM   8. Build Loader      (x64)        <-- must be last
REM
REM Usage:
REM     bat\build_all.bat            (defaults to Release|x64)
REM     bat\build_all.bat Debug
REM
REM You can run this either from the repo root (bat\build_all.bat Release)
REM OR cd into bat\ and run build_all.bat Release -- both work.
REM ============================================================

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "PLATFORM=x64"

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
REM Normalize to absolute path and switch to repo root immediately so
REM sub-scripts don't confuse cmd's label resolution.
pushd "%REPO_ROOT%"
cd /d "%REPO_ROOT%"

set "MSBUILD="
REM ---- Locate MSBuild (VS 2022 / 2019) via vswhere ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD=%%i"
    )
)
if "%MSBUILD%"=="" (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe" (
        set "MSBUILD=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    )
)
if "%MSBUILD%"=="" (
    echo [-] MSBuild not found. Run this from a Visual Studio Developer Command Prompt,
    echo     or install the Visual Studio Build Tools.
    popd
    exit /b 1
)

echo.
echo =======================================================
echo  DragonBurn - Full Build + Embed Pipeline
echo  Config   : %CONFIG% ^| %PLATFORM%
echo  MSBuild  : %MSBUILD%
echo =======================================================
echo.

REM We intentionally call sub-scripts via "cmd /c" instead of plain "call"
REM to avoid the classic cmd.exe bug where, after a child batch uses
REM setlocal/popd/goto, the parent can no longer resolve its own labels
REM ("The system cannot find the batch label specified - build_step").
REM Running each sub-script in a fresh cmd.exe process isolates that state.

set /a STEP_OK=1

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  1/8 Building KernelDriver...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"%MSBUILD%" KernelDriver.vcxproj /t:Build /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo /v:minimal
if errorlevel 1 ( echo [-] Build failed: KernelDriver & goto :fail )
echo [+] KernelDriver built successfully.

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  2/8 Embedding driver.sys into Mapper...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
cmd /c ""%SCRIPT_DIR%embed_driver.bat" %CONFIG%"
if errorlevel 1 goto :fail

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  3/8 Embedding Intel driver into Mapper...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
cmd /c ""%SCRIPT_DIR%embed_intel.bat" %CONFIG%"
if errorlevel 1 goto :fail

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  4/8 Building Mapper...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"%MSBUILD%" Mapper.vcxproj /t:Build /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo /v:minimal
if errorlevel 1 ( echo [-] Build failed: Mapper & goto :fail )
echo [+] Mapper built successfully.

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  5/8 Embedding mapper.exe into Loader...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
cmd /c ""%SCRIPT_DIR%embed_mapper.bat" %CONFIG%"
if errorlevel 1 goto :fail

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  6/8 Building ExternalESP...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"%MSBUILD%" ExternalESP.vcxproj /t:Build /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo /v:minimal
if errorlevel 1 ( echo [-] Build failed: ExternalESP & goto :fail )
echo [+] ExternalESP built successfully.

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  7/8 Embedding ExternalESP.exe into Loader...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
cmd /c ""%SCRIPT_DIR%embed_esp.bat" %CONFIG%"
if errorlevel 1 goto :fail

echo.
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
echo  8/8 Building Loader (final)...
echo ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"%MSBUILD%" Loader.vcxproj /t:Build /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo /v:minimal
if errorlevel 1 ( echo [-] Build failed: Loader & goto :fail )
echo [+] Loader built successfully.

REM --- Offsets are HARDCODED at compile time ---
REM ExternalESP.exe #includes output\offsets.hpp + output\client_dll.hpp
REM verbatim (see esp\Offsets.h), so there is nothing to copy next to
REM Loader.exe anymore. To update offsets after a game patch: re-dump
REM into output\ and re-run this script.
echo.
echo  [*] Offsets are hardcoded into ExternalESP.exe from output\*.hpp at compile time.

echo.
echo =======================================================
echo  [+] All done. Loader output: x64\%CONFIG%\Loader.exe
echo =======================================================
popd
endlocal
exit /b 0

:fail
echo.
echo [-] Pipeline FAILED.
popd
endlocal
exit /b 1
