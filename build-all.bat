@echo off
setlocal enabledelayedexpansion

pushd "%~dp0"

if not exist godot-cpp\ (
    echo godot-cpp\ missing. Run:
    echo   git clone --recursive https://github.com/godotengine/godot-cpp.git
    popd
    exit /b 1
)

set SCONS_EXTRA=
if "%USE_MINGW%"=="1" set SCONS_EXTRA=use_mingw=yes

echo === Windows (native) ===
if "%SPOUT_SDK%"=="" (
    echo WARNING: SPOUT_SDK not set; Spout backend will not link.
    echo          See README.md#Installing-Spout.
)
scons -j%NUMBER_OF_PROCESSORS% platform=windows target=template_debug   arch=x86_64 %SCONS_EXTRA% %*
if errorlevel 1 goto :fail
scons -j%NUMBER_OF_PROCESSORS% platform=windows target=template_release arch=x86_64 %SCONS_EXTRA% %*
if errorlevel 1 goto :fail

echo.
echo === Linux (WSL2 cross) ===
where wsl >nul 2>&1
if errorlevel 1 (
    echo SKIP: wsl not on PATH. Install WSL2 with a Linux distro.
) else (
    wsl -- ./build-all.sh
    if errorlevel 1 goto :fail
)

echo.
echo === macOS ===
echo SKIP: Windows -^> macOS cross-compile is not practical.
echo       Build on a Mac or use a macOS GitHub Actions runner.

echo.
echo Built:
dir /b demo\addons\spectator_stream_exporter\bin\

popd
endlocal
exit /b 0

:fail
echo.
echo Build failed.
popd
endlocal
exit /b 1
