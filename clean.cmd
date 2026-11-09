@echo off
:: Re-launches itself so every exit path pauses before the window closes; MD7_NO_PAUSE skips this when another of our scripts calls it.
IF "%~1"=="--md7-inner" (
    SHIFT /1
    GOTO :main
)

cmd /c ""%~f0" --md7-inner %*"
SET "_MD7_EC=%ERRORLEVEL%"
IF NOT DEFINED MD7_NO_PAUSE (
    echo.
    pause
)
EXIT /B %_MD7_EC%

:main
setlocal enabledelayedexpansion

:: -- ANSI color setup (Windows 10 / 11) ---------------------------------------
FOR /F "delims=" %%E IN ('echo prompt $E^| cmd') DO SET "ESC=%%E"
SET "_CYAN=%ESC%[36;1m"
SET "_GREEN=%ESC%[32;1m"
SET "_YELLOW=%ESC%[33;1m"
SET "_RED=%ESC%[31;1m"
SET "_RESET=%ESC%[0m"

:: -- Script directory ----------------------------------------------------------
SET "SCRIPT_DIR=%~dp0"
IF "%SCRIPT_DIR:~-1%"=="\" SET "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: -- Argument parsing ----------------------------------------------------------
SET "ARG_TARGET="
SET "ARG_BUILD="
SET "ARG_ARCH="
SET "AUTO_YES=0"

:parse_args
IF "%~1"=="" GOTO :end_parse
IF /I "%~1"=="--platform" (
    IF /I NOT "%~2"=="android" (
        CALL :err "Invalid --platform '%~2' (only android is supported on Windows)"
        EXIT /B 1
    )
    SHIFT /1
    SHIFT /1
    GOTO :parse_args
)
IF /I "%~1"=="--target" (
    SET "ARG_TARGET=%~2"
    SHIFT /1
    SHIFT /1
    GOTO :parse_args
)
IF /I "%~1"=="--build" (
    SET "ARG_BUILD=%~2"
    SHIFT /1
    SHIFT /1
    GOTO :parse_args
)
IF /I "%~1"=="--arch" (
    SET "ARG_ARCH=%~2"
    SHIFT /1
    SHIFT /1
    GOTO :parse_args
)
IF /I "%~1"=="-y" (
    SET "AUTO_YES=1"
    SHIFT /1
    GOTO :parse_args
)
IF /I "%~1"=="--yes" (
    SET "AUTO_YES=1"
    SHIFT /1
    GOTO :parse_args
)
IF /I "%~1"=="--help" (
    CALL :usage
    EXIT /B 0
)
IF /I "%~1"=="-h" (
    CALL :usage
    EXIT /B 0
)
CALL :err "Unknown option: %~1"
echo.
CALL :usage
EXIT /B 1
:end_parse

:: -- Validate ------------------------------------------------------------------
IF NOT "!ARG_TARGET!"=="" (
    SET "_OK="
    IF /I "!ARG_TARGET!"=="exe"    SET "_OK=1"
    IF /I "!ARG_TARGET!"=="lib" SET "_OK=1"
    IF NOT DEFINED _OK (
        CALL :err "Invalid --target '!ARG_TARGET!' (must be exe or lib)"
        EXIT /B 1
    )
)

IF NOT "!ARG_BUILD!"=="" (
    SET "_OK="
    IF /I "!ARG_BUILD!"=="debug"   SET "_OK=1"
    IF /I "!ARG_BUILD!"=="release" SET "_OK=1"
    IF NOT DEFINED _OK (
        CALL :err "Invalid --build '!ARG_BUILD!' (must be debug or release)"
        EXIT /B 1
    )
)

IF NOT "!ARG_ARCH!"=="" (
    SET "_OK="
    FOR %%A IN (arm64 arm32 x86 x86_64 all) DO (
        IF /I "!ARG_ARCH!"=="%%A" SET "_OK=1"
    )
    IF NOT DEFINED _OK (
        CALL :err "Invalid --arch '!ARG_ARCH!' (must be arm64, arm32, x86, x86_64 or all)"
        EXIT /B 1
    )
)

:: An unset selector means "every value", so the default is a full clean.
SET "_TARGETS=exe lib"
IF NOT "!ARG_TARGET!"=="" SET "_TARGETS=!ARG_TARGET!"

SET "_CONFIGS=debug release"
IF NOT "!ARG_BUILD!"=="" SET "_CONFIGS=!ARG_BUILD!"

SET "_ARCHES=arm64 arm32 x86 x86_64"
IF NOT "!ARG_ARCH!"=="" IF /I NOT "!ARG_ARCH!"=="all" SET "_ARCHES=!ARG_ARCH!"

:: -- builds\android\<target>\<config>\<arch> ----------------------------------
SET /A _COUNT=0

FOR %%T IN (!_TARGETS!) DO (
    FOR %%C IN (!_CONFIGS!) DO (
        FOR %%A IN (!_ARCHES!) DO (
            IF EXIST "!SCRIPT_DIR!\builds\android\%%T\%%C\%%A" SET /A _COUNT+=1
        )
    )
)

IF !_COUNT! EQU 0 (
    CALL :ok "Nothing to clean."
    EXIT /B 0
)

SET /A _FOUND=0

CALL :section "Will remove"

FOR %%T IN (!_TARGETS!) DO (
    FOR %%C IN (!_CONFIGS!) DO (
        FOR %%A IN (!_ARCHES!) DO (
            CALL :plan_dir "!SCRIPT_DIR!\builds\android\%%T\%%C\%%A" "builds\android\%%T\%%C\%%A"
        )
    )
)

CALL :confirm "Remove the above?" Y
IF NOT "!CONFIRM_RESULT!"=="1" (
    CALL :warn "Cancelled."
    EXIT /B 0
)

SET /A _REMOVED=0

CALL :section "Removing"

FOR %%T IN (!_TARGETS!) DO (
    FOR %%C IN (!_CONFIGS!) DO (
        FOR %%A IN (!_ARCHES!) DO (
            CALL :remove_dir "!SCRIPT_DIR!\builds\android\%%T\%%C\%%A" "builds\android\%%T\%%C\%%A"
        )
    )
)

CALL :ok "Clean complete (!_REMOVED! removed)."
EXIT /B 0

:: =============================================================================
:: SUBROUTINES
:: =============================================================================

:usage
echo Usage: %~n0 [OPTIONS]
echo.
echo Selectors mirror build.cmd ^- omit them all to clean everything.
echo.
echo Options:
echo   --platform android        Target platform (Android only on Windows)
echo   --target   exe^|lib        Output type
echo   --build    debug^|release  Build configuration
echo   --arch     arm64^|arm32^|x86^|x86_64^|all   Android ABI
echo   -y, --yes                 Don't ask for confirmation before removing anything
echo   -h, --help                Show this help
echo.
echo Without -y, everything that would be removed is listed first and you're asked to confirm.
echo.
echo Examples:
echo   %~n0
echo   %~n0 --target exe
echo   %~n0 --arch arm32
GOTO :EOF

:plan_dir
IF EXIST "%~1" (
    CALL :info "%~2"
    SET /A _FOUND+=1
)
GOTO :EOF

:remove_dir
IF EXIST "%~1" (
    CALL :info "rmdir /s /q %~2"
    rmdir /s /q "%~1"
    SET /A _REMOVED+=1
)
GOTO :EOF

:log
echo !_CYAN!==^> %~1!_RESET!
GOTO :EOF

:section
echo.
echo !_CYAN!+-- %~1!_RESET!
GOTO :EOF

:info
echo !_CYAN!^|!_RESET!   %~1
GOTO :EOF

:ok
echo !_GREEN![ok] %~1!_RESET!
GOTO :EOF

:warn
echo !_YELLOW![warn] %~1!_RESET!
GOTO :EOF

:err
echo !_RED![error] %~1!_RESET! 1>&2
GOTO :EOF

:: Asks a yes/no question (%~1, default %~2). Sets CONFIRM_RESULT to 1/0; shows [Y/n]/[y/N].
:: Always "yes" under -y.
:confirm
IF "!AUTO_YES!"=="1" ( SET "CONFIRM_RESULT=1" & GOTO :EOF )
SET "_CDEFAULT=%~2"
IF "!_CDEFAULT!"=="" SET "_CDEFAULT=Y"
IF /I "!_CDEFAULT!"=="Y" ( SET "_CHINT=Y/n" ) ELSE ( SET "_CHINT=y/N" )
SET "_CREPLY="
SET /P "_CREPLY=  %~1 [!_CHINT!] "
IF "!_CREPLY!"=="" SET "_CREPLY=!_CDEFAULT!"
IF /I "!_CREPLY:~0,1!"=="Y" ( SET "CONFIRM_RESULT=1" ) ELSE ( SET "CONFIRM_RESULT=0" )
GOTO :EOF
