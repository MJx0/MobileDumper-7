@echo off
setlocal EnableDelayedExpansion

:: =============================================================================
::  MobileDumper-7 - deploy to device and dump the live game
::
::  Pushes the standalone executable to the device and runs it against the
::  running game. This is the single deploy mode: a live attach/dump.
::
::  With no --package, deploy.bat lists running installed packages for selection.
::
::  Examples:
::    deploy.bat
::    deploy.bat --arch arm64 --package com.pubg.imobile
::    deploy.bat --arch arm64 --mem 2 --no-suspend
:: =============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "ADB=%SCRIPT_DIR%\adb.exe"
if not exist "%ADB%" set "ADB=adb"

:: ── Defaults (override on the command line, see :usage) ───────────────────────
set "PACKAGE="
set "PACKAGE_SET="
set "ARCH=arm64"
set "ARCH_SET="
:: The dumper appends "\MobileDumper-7" to this, so /sdcard -> /sdcard/MobileDumper-7
set "OUTPUT=/sdcard"
set "MEMTYPE=1"
set "DUMPLIB=N"
set "SUSPEND=Y"
set "MEM_SET="
set "DUMP_SET="
set "SUSPEND_SET="

set "REMOTE_DIR=/data/local/tmp"
set "REMOTE_EXE=%REMOTE_DIR%/MobileDumper-7"

:: ── Argument parsing ──────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :end_parse

if /i "%~1"=="-h"     goto :usage
if /i "%~1"=="--help" goto :usage

if /i "%~1"=="--package" (
    set "PACKAGE=%~2"
    set "PACKAGE_SET=1"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--arch" (
    set "ARCH=%~2"
    set "ARCH_SET=1"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--output" (
    set "OUTPUT=%~2"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--mem" (
    set "MEMTYPE=%~2"
    set "MEM_SET=1"
    shift
    shift
    goto :parse_args
)
if /i "%~1"=="--dump" (
    set "DUMPLIB=Y"
    set "DUMP_SET=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--suspend" (
    set "SUSPEND=Y"
    set "SUSPEND_SET=1"
    shift
    goto :parse_args
)
if /i "%~1"=="--no-suspend" (
    set "SUSPEND=N"
    set "SUSPEND_SET=1"
    shift
    goto :parse_args
)
echo Unknown option: %~1
goto :usage
:end_parse

:: ── Validate ──────────────────────────────────────────────────────────────────
set "ARCH_OK="
for %%A in (arm64 arm32 x86 x86_64) do if /i "%ARCH%"=="%%A" set "ARCH_OK=1"
if not defined ARCH_OK (
    echo Invalid --arch "%ARCH%" ^(arm64, arm32, x86 or x86_64^).
    exit /b 1
)

if not "%MEMTYPE%"=="1" if not "%MEMTYPE%"=="2" (
    echo Invalid --mem "%MEMTYPE%" ^(1 = process_vm_readv, 2 = pread^).
    exit /b 1
)

:: ── Architecture ──────────────────────────────────────────────────────────────
if defined ARCH_SET goto :arch_ready
echo Architecture:
echo   1. arm64
echo   2. arm32
echo   3. x86
echo   4. x86_64
echo   5. Quit
choice /C 12345 /N /M "> " 2>nul
if errorlevel 5 exit /b 0
if errorlevel 4 (
    set "ARCH=x86_64"
) else if errorlevel 3 (
    set "ARCH=x86"
) else if errorlevel 2 (
    set "ARCH=arm32"
) else (
    set "ARCH=arm64"
)
echo.
:arch_ready

set "EXE_BIN=%SCRIPT_DIR%\builds\android\exe\release\%ARCH%\MobileDumper-7"

:: ── Ensure the executable exists (build it on demand) ─────────────────────────
if not exist "%EXE_BIN%" (
    echo Executable for %ARCH% not found. Building it now...
    echo.
    pushd "%SCRIPT_DIR%"
    call "%SCRIPT_DIR%\build.cmd" --platform android --target exe --build release --arch %ARCH%
    set "BUILD_RC=!ERRORLEVEL!"
    popd
    if not "!BUILD_RC!"=="0" (
        echo.
        echo Build failed. Fix the errors above, then run deploy.bat again.
        goto :fail
    )
)
if not exist "%EXE_BIN%" (
    echo Missing "%EXE_BIN%" even after building.
    echo Check that build.cmd can produce the exe target for %ARCH%.
    goto :fail
)

:: ── Dump options ──────────────────────────────────────────────────────────────
if not defined SUSPEND_SET (
    set "_IN="
    set /p "_IN=Suspend the game while dumping? (Y/n) [Y]: "
    if not "!_IN!"=="" set "SUSPEND=!_IN!"
)

if not defined DUMP_SET (
    set "_IN="
    set /p "_IN=Also dump the Unreal library (libUE4.so)? (y/N) [N]: "
    if not "!_IN!"=="" set "DUMPLIB=!_IN!"
)

if not defined MEM_SET (
    set "_IN="
    set /p "_IN=Memory access 1=process_vm_readv, 2=pread [1]: "
    if not "!_IN!"=="" set "MEMTYPE=!_IN!"
)
echo.

:: ── ADB / device ──────────────────────────────────────────────────────────────
if /i "%ADB%"=="adb" (
    where adb >nul 2>&1
    if errorlevel 1 (
        echo adb.exe not found.
        echo Put adb.exe next to deploy.bat or add it to PATH.
        goto :fail
    )
)

"%ADB%" start-server >nul 2>&1
"%ADB%" get-state >nul 2>&1
if errorlevel 1 (
    echo No device detected.
    "%ADB%" devices
    echo Connect the device with USB debugging enabled, then retry.
    goto :fail
)

:: Best effort: adbd root lets us read /proc/<pid>/mem without su.
:: On production builds this fails harmlessly and we fall back to su.
"%ADB%" root >nul 2>&1
"%ADB%" wait-for-device >nul 2>&1

call :detect_root
if "%ROOTMODE%"=="none" (
    echo Warning: no root detected ^(neither adbd root nor su^).
    echo          Reading the game's memory will most likely fail.
    echo.
)

:: ── Target process ────────────────────────────────────────────────────────────
:: When --package was not supplied, let the user select a running package.
if not defined PACKAGE_SET call :find_running_game
if not defined PACKAGE goto :fail

call :find_pid
if not defined PID (
    echo Game "%PACKAGE%" is not running. Start it, then run this again.
    goto :fail
)
echo Target: %PACKAGE% ^(pid %PID%^)
echo.

set "RUN_ARGS=--package %PACKAGE% --output %OUTPUT% --mem %MEMTYPE%"
if /i "%SUSPEND%"=="y" set "RUN_ARGS=%RUN_ARGS% --suspend"
if /i "%DUMPLIB%"=="y" set "RUN_ARGS=%RUN_ARGS% --dump"

:: ── Deploy and run ────────────────────────────────────────────────────────────
echo Pushing "%EXE_BIN%"...
"%ADB%" push "%EXE_BIN%" "%REMOTE_EXE%" >nul
if errorlevel 1 goto :adb_failed
"%ADB%" shell chmod 755 "%REMOTE_EXE%"
if errorlevel 1 goto :adb_failed

"%ADB%" shell mkdir -p "%OUTPUT%" >nul 2>&1

echo.
echo Running: %REMOTE_EXE% %RUN_ARGS%
echo.
call :run_root "%REMOTE_EXE% %RUN_ARGS%"
if errorlevel 1 goto :run_failed

echo.
echo Done. Output: %OUTPUT%/MobileDumper-7
echo.
pause
exit /b 0

:: =============================================================================
::  SUBROUTINES
:: =============================================================================

:: Detects how to get root: "adb" (adbd already root), "su", or "none".
:detect_root
set "ROOTMODE=none"
set "OUT=%TEMP%\md7_root.txt"
"%ADB%" shell id > "%OUT%" 2>nul
findstr /C:"uid=0" "%OUT%" >nul 2>&1
if not errorlevel 1 set "ROOTMODE=adb"
if "%ROOTMODE%"=="none" (
    "%ADB%" shell su -c id > "%OUT%" 2>nul
    findstr /C:"uid=0" "%OUT%" >nul 2>&1
    if not errorlevel 1 set "ROOTMODE=su"
)
del "%OUT%" >nul 2>&1
goto :eof

:: %1 = shell command line to run as root
:run_root
if /i "%ROOTMODE%"=="adb" (
    "%ADB%" shell "%~1"
    goto :eof
)
if /i "%ROOTMODE%"=="su" (
    "%ADB%" shell "su -c '%~1'"
    goto :eof
)
"%ADB%" shell "%~1"
goto :eof

:: List running third-party packages, excluding Android/Google phone packages.
:: No game allowlist or engine/library detection is used.
:find_running_game
set "GAME_COUNT=0"
set "PACKAGE="
set "GAME_LIST=%TEMP%\md7_packages_%RANDOM%.txt"
echo Finding running packages...
"%ADB%" shell "pm list packages -3 | while IFS= read -r line; do package=${line#package:}; case $package in com.google.*|com.android.*|android) continue ;; esac; pids=$(pidof $package); if [ -n \"$pids\" ]; then echo $package $pids; fi; done" <nul > "%GAME_LIST%" 2>nul
if errorlevel 1 (
    del "%GAME_LIST%" >nul 2>&1
    echo Could not list running packages.
    exit /b 1
)
echo Running packages:
for /f "usebackq tokens=1,2" %%G in ("%GAME_LIST%") do (
    set /a GAME_COUNT+=1
    set "GAME_!GAME_COUNT!=%%G"
    echo   !GAME_COUNT!. %%G ^(pid %%H^)
)
del "%GAME_LIST%" >nul 2>&1
if !GAME_COUNT!==0 (
    echo No running packages found. Start the app or pass --package ^<name^>.
    exit /b 1
)
echo   0. Cancel
:select_running_package
set "GAME_CHOICE="
set /p "GAME_CHOICE=Select a package (1-!GAME_COUNT!, 0 to cancel): "
if not defined GAME_CHOICE exit /b 1
if "!GAME_CHOICE!"=="0" exit /b 1
for /l %%I in (1,1,!GAME_COUNT!) do (
    if "!GAME_CHOICE!"=="%%I" set "PACKAGE=!GAME_%%I!"
)
if not defined PACKAGE (
    echo Invalid selection. Choose a listed number.
    goto :select_running_package
)
echo Selected running package: !PACKAGE!
goto :eof

:: Sets PID to the first running pid of %PACKAGE%.
:find_pid
set "PID="
set "PIDRAW="
"%ADB%" shell pidof %PACKAGE% > "%TEMP%\md7_pid.txt" 2>nul
set /p PIDRAW=<"%TEMP%\md7_pid.txt"
del "%TEMP%\md7_pid.txt" >nul 2>&1
if defined PIDRAW for /f "tokens=1" %%P in ("!PIDRAW!") do set "PID=%%P"
goto :eof

:usage
echo Usage: %~n0 [OPTIONS]
echo.
echo   --package ^<name^>   Package (default: select from running packages)
echo   --arch ^<abi^>       arm64, arm32, x86, x86_64 (prompted when omitted)
echo   --output ^<path^>    Device output dir (default: /sdcard)
echo   --mem ^<1^|2^>        1 = process_vm_readv, 2 = pread (default: 1)
echo   --dump             Also dump the Unreal library (libUE4.so)
echo   --suspend          Suspend the game while dumping (default)
echo   --no-suspend       Do not suspend the game
echo   -h, --help         Show this help
echo.
echo Output: ^<output^>/MobileDumper-7
exit /b 0

:run_failed
echo.
echo Dump failed. The dumper log is under %OUTPUT%/MobileDumper-7:
echo   "%ADB%" shell "ls -t %OUTPUT%/MobileDumper-7"
echo   "%ADB%" shell "tail -n 40 %OUTPUT%/MobileDumper-7/^<newest^>/MobileDumper-7.log"
echo If it exited with signal 11, check: "%ADB%" logcat -d -b crash
echo If this was a permission error, make sure the device is rooted (adbd root or su).
goto :fail

:adb_failed
echo.
echo ADB command failed. Check the USB connection and device authorization.
goto :fail

:fail
echo.
pause
exit /b 1
