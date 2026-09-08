@echo off
setlocal enabledelayedexpansion

:: ── ANSI color setup (Windows 10 / 11) ───────────────────────────────────────
FOR /F "delims=" %%E IN ('echo prompt $E^| cmd') DO SET "ESC=%%E"
SET "_CYAN=%ESC%[36;1m"
SET "_GREEN=%ESC%[32;1m"
SET "_RED=%ESC%[31;1m"
SET "_RESET=%ESC%[0m"

:: ── Script directory ──────────────────────────────────────────────────────────
SET "SCRIPT_DIR=%~dp0"
IF "%SCRIPT_DIR:~-1%"=="\" SET "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: ── Project name from CMakeLists.txt ─────────────────────────────────────────
SET "TARGET_NAME="
FOR /F "tokens=2 delims=()" %%P IN ('findstr /B /I "project(" "%SCRIPT_DIR%\CMakeLists.txt" 2^>NUL') DO (
    FOR /F "tokens=1" %%Q IN ("%%P") DO SET "TARGET_NAME=%%Q"
)
IF "!TARGET_NAME!"=="" (
    CALL :err "Could not read project name from CMakeLists.txt"
    EXIT /B 1
)

:: ── Config defaults ───────────────────────────────────────────────────────────
SET "NDK_HOST_TAG=windows-x86_64"

:: ── Argument parsing ──────────────────────────────────────────────────────────
SET "ARG_PLATFORM="
SET "ARG_TARGET="
SET "ARG_BUILD="
SET "ARG_ARCH="

:parse_args
IF "%~1"=="" GOTO :end_parse
IF /I "%~1"=="--platform" (
    SET "ARG_PLATFORM=%~2"
    SHIFT
    SHIFT
    GOTO :parse_args
)
IF /I "%~1"=="--target" (
    SET "ARG_TARGET=%~2"
    SHIFT
    SHIFT
    GOTO :parse_args
)
IF /I "%~1"=="--arch" (
    SET "ARG_ARCH=%~2"
    SHIFT
    SHIFT
    GOTO :parse_args
)
IF /I "%~1"=="--type" (
    CALL :err "--type was renamed to --target (exe^|lib)"
    EXIT /B 1
)
IF /I "%~1"=="--build" (
    SET "ARG_BUILD=%~2"
    SHIFT
    SHIFT
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

:: ── Validate --platform ───────────────────────────────────────────────────────
SET "PLATFORM="
IF NOT "!ARG_PLATFORM!"=="" (
    IF /I "!ARG_PLATFORM!"=="android" (
        SET "PLATFORM=Android"
    ) ELSE (
        CALL :err "Invalid --platform '!ARG_PLATFORM!' (only android is supported on Windows)"
        EXIT /B 1
    )
)

:: ── Validate --build ──────────────────────────────────────────────────────────
SET "BTYPE="
IF NOT "!ARG_BUILD!"=="" (
    IF /I "!ARG_BUILD!"=="debug"   SET "BTYPE=Debug"
    IF /I "!ARG_BUILD!"=="release" SET "BTYPE=Release"
    IF "!BTYPE!"=="" (
        CALL :err "Invalid --build '!ARG_BUILD!' (must be debug or release)"
        EXIT /B 1
    )
)

:: ── Main ─────────────────────────────────────────────────────────────────────
echo.
echo   !TARGET_NAME! Build  (Windows ^— Android only)
echo.

:: Platform
IF "!PLATFORM!"=="" (
    echo Platform:
    echo   1. Android
    echo   2. Quit
    CHOICE /C 12 /N /M "> " 2>NUL
    IF !ERRORLEVEL! EQU 2 EXIT /B 0
    IF !ERRORLEVEL! EQU 1 SET "PLATFORM=Android"
    echo.
)

:: Resolve --type now that platform is known
SET "TARGET="
IF NOT "!ARG_TARGET!"=="" (
    IF /I "!ARG_TARGET!"=="lib" SET "TARGET=lib"
    IF /I "!ARG_TARGET!"=="exe"    SET "TARGET=exe"
    IF /I "!ARG_TARGET!"=="theos" (
        CALL :err "--target theos is only valid on macOS (iOS builds are not supported on Windows)"
        EXIT /B 1
    )
    IF /I "!ARG_TARGET!"=="dylib" (
        CALL :err "--target dylib is only valid on macOS (iOS builds are not supported on Windows)"
        EXIT /B 1
    )
    IF "!TARGET!"=="" (
        CALL :err "Invalid --target '!ARG_TARGET!' (must be exe or lib)"
        EXIT /B 1
    )
)

:: Target
IF "!TARGET!"=="" (
    echo Target:
    echo   1. lib
    echo   2. exe
    echo   3. Quit
    CHOICE /C 123 /N /M "> " 2>NUL
    IF !ERRORLEVEL! EQU 3 EXIT /B 0
    IF !ERRORLEVEL! EQU 2 SET "TARGET=exe"
    IF !ERRORLEVEL! EQU 1 SET "TARGET=lib"
    echo.
)

:: Build type
IF "!BTYPE!"=="" (
    echo Build type:
    echo   1. Release
    echo   2. Debug
    echo   3. Quit
    CHOICE /C 123 /N /M "> " 2>NUL
    IF !ERRORLEVEL! EQU 3 EXIT /B 0
    IF !ERRORLEVEL! EQU 2 SET "BTYPE=Debug"
    IF !ERRORLEVEL! EQU 1 SET "BTYPE=Release"
    echo.
)

SET "BUILD_TYPE=!BTYPE!"

:: Architecture
SET "ARCH="
IF NOT "!ARG_ARCH!"=="" (
    FOR %%A IN (arm64 arm32 x86 x86_64 all) DO (
        IF /I "!ARG_ARCH!"=="%%A" SET "ARCH=%%A"
    )
    IF "!ARCH!"=="" (
        CALL :err "Invalid --arch '!ARG_ARCH!' (must be arm64, arm32, x86, x86_64 or all)"
        EXIT /B 1
    )
)

IF "!ARCH!"=="" (
    echo Architecture:
    echo   1. arm64
    echo   2. arm32
    echo   3. x86
    echo   4. x86_64
    echo   5. all
    echo   6. Quit
    CHOICE /C 123456 /N /M "> " 2>NUL
    IF !ERRORLEVEL! EQU 6 EXIT /B 0
    IF !ERRORLEVEL! EQU 5 SET "ARCH=all"
    IF !ERRORLEVEL! EQU 4 SET "ARCH=x86_64"
    IF !ERRORLEVEL! EQU 3 SET "ARCH=x86"
    IF !ERRORLEVEL! EQU 2 SET "ARCH=arm32"
    IF !ERRORLEVEL! EQU 1 SET "ARCH=arm64"
    echo.
)

echo.
CALL :build_android "!TARGET!"
IF !ERRORLEVEL! NEQ 0 EXIT /B 1

CALL :ok "Done."
EXIT /B 0

:: ═════════════════════════════════════════════════════════════════════════════
:: SUBROUTINES
:: ═════════════════════════════════════════════════════════════════════════════

:usage
echo Usage: %~n0 [OPTIONS]
echo.
echo Options:
echo   --platform android        Target platform (Android only on Windows)
echo   --target   exe^|lib        Output type
echo   --build    debug^|release  Build configuration (default: release)
echo   --arch     arm64^|arm32^|x86^|x86_64^|all   Android ABI (default: arm64)
echo   -h, --help                Show this help
echo.
echo Omit any option to be prompted interactively.
echo.
echo Examples:
echo   %~n0 --platform android --target lib --build release --arch arm64
echo   %~n0 --platform android --target exe --arch all
echo   %~n0
GOTO :EOF

:log
echo !_CYAN!==> %~1!_RESET!
GOTO :EOF

:ok
echo !_GREEN![ok] %~1!_RESET!
GOTO :EOF

:err
echo !_RED![error] %~1!_RESET! 1>&2
GOTO :EOF

:: ── NDK detection ─────────────────────────────────────────────────────────────
:: Mirrors .vscode/clangd-selector.cmd. Sets FOUND_NDK to the NDK root, or empty.
:find_ndk
SET "FOUND_NDK="
SET "_NDK_TMP="

FOR %%V IN (ANDROID_NDK_HOME ANDROID_NDK NDK_HOME) DO (
    IF NOT DEFINED _NDK_TMP (
        IF DEFINED %%V SET "_NDK_TMP=!%%V!"
    )
)

IF NOT DEFINED _NDK_TMP (
    FOR %%S IN (ANDROID_HOME ANDROID_SDK_ROOT ANDROID_SDK_HOME ANDROID_SDK) DO (
        IF NOT DEFINED _NDK_TMP (
            IF DEFINED %%S (
                SET "_SDK_TMP=!%%S!"
                IF EXIST "!_SDK_TMP!\ndk" CALL :pick_newest_ndk
            )
        )
    )
)

IF NOT DEFINED _NDK_TMP GOTO :EOF

IF EXIST "!_NDK_TMP!\build\cmake\android.toolchain.cmake" SET "FOUND_NDK=!_NDK_TMP!"
GOTO :EOF

:: Picks the highest NDK version under !_SDK_TMP!\ndk.
:: Batch has no version-aware sort, and `dir /O:N` is alphabetical, which ranks
:: 9.x above 27.x. PowerShell sorts on [version]; plain dir is the fallback.
:pick_newest_ndk
SET "_NDK_LATEST="

FOR /F "delims=" %%D IN ('powershell -NoProfile -Command ^
    "Get-ChildItem -Directory '!_SDK_TMP!\ndk' ^| Sort-Object { [version]($_.Name -replace '^^(\d+\.\d+\.\d+).*','$1') } ^| Select-Object -Last 1 -ExpandProperty Name" 2^>NUL') DO SET "_NDK_LATEST=%%D"

IF NOT DEFINED _NDK_LATEST (
    FOR /F "delims=" %%D IN ('dir /B /O:N "!_SDK_TMP!\ndk" 2^>NUL') DO SET "_NDK_LATEST=%%D"
)

IF DEFINED _NDK_LATEST SET "_NDK_TMP=!_SDK_TMP!\ndk\!_NDK_LATEST!"
GOTO :EOF

:: ── Preset-driven Android build ───────────────────────────────────────────────
:: %1 = arch (arm64|arm32|x86|x86_64)   %2 = target (lib|exe)
:build_android_one
SETLOCAL
SET "_ARCH=%~1"
SET "_TARGET=%~2"

:: Presets keep the canonical ABI spelling; only arm32 differs from the CLI value.
SET "_PSEG=!_ARCH!"
IF /I "!_ARCH!"=="arm32" SET "_PSEG=armeabi-v7a"

SET "_PRESET=android-!_PSEG!"
IF /I "!_TARGET!"=="exe" SET "_PRESET=!_PRESET!-exe"
IF /I "!BUILD_TYPE!"=="Debug" SET "_PRESET=!_PRESET!-debug"

IF /I "!BUILD_TYPE!"=="Debug" ( SET "_CFG=debug" ) ELSE ( SET "_CFG=release" )
SET "_BDIR=!SCRIPT_DIR!\builds\android\!_TARGET!\!_CFG!\!_ARCH!"

CALL :log "Building Android !_TARGET! — arch=!_ARCH!, type=!BUILD_TYPE!, preset=!_PRESET!"

cmake --preset "!_PRESET!"
IF !ERRORLEVEL! NEQ 0 (
    CALL :err "Configure failed for preset: !_PRESET!"
    ENDLOCAL & EXIT /B 1
)

cmake --build --preset "!_PRESET!"
IF !ERRORLEVEL! NEQ 0 (
    CALL :err "Build failed for preset: !_PRESET!"
    ENDLOCAL & EXIT /B 1
)

IF /I "!_TARGET!"=="exe" (
    SET "_ARTIFACT=!_BDIR!\!TARGET_NAME!"
) ELSE (
    SET "_ARTIFACT=!_BDIR!\lib!TARGET_NAME!.so"
)

IF NOT EXIST "!_ARTIFACT!" (
    CALL :err "Artifact not found after build: !_ARTIFACT!"
    ENDLOCAL & EXIT /B 1
)
CALL :ok "Android !_TARGET! (!_ARCH!): !_ARTIFACT!"

SET "_OBJCOPY=!FOUND_NDK!\toolchains\llvm\prebuilt\!NDK_HOST_TAG!\bin\llvm-objcopy.exe"
IF EXIST "!_OBJCOPY!" (
    "!_OBJCOPY!" --only-keep-debug "!_ARTIFACT!" "!_ARTIFACT!.debug"
    CALL :ok "Android symbols: !_ARTIFACT!.debug"
) ELSE (
    CALL :err "llvm-objcopy not found — symbols not extracted"
)

ENDLOCAL & EXIT /B 0

:: ── Android build driver ──────────────────────────────────────────────────────
:: %1 = target (lib|exe)
:build_android
where ninja >NUL 2>&1
IF !ERRORLEVEL! NEQ 0 (
    CALL :err "Ninja is required — the CMake presets pin the Ninja generator."
    CALL :err "  winget install Ninja-build.Ninja"
    EXIT /B 1
)

CALL :find_ndk
IF NOT DEFINED FOUND_NDK (
    CALL :err "Android NDK not found. Set ANDROID_NDK_HOME, or install via Android Studio SDK Manager."
    EXIT /B 1
)
CALL :log "NDK: !FOUND_NDK!"

:: The presets resolve their toolchain through $env{ANDROID_NDK_HOME}.
SET "ANDROID_NDK_HOME=!FOUND_NDK!"

IF /I "!ARCH!"=="all" (
    FOR %%A IN (arm64 arm32 x86 x86_64) DO (
        CALL :build_android_one %%A "%~1"
        IF !ERRORLEVEL! NEQ 0 (
            CALL :err "Android build failed for arch: %%A"
            EXIT /B 1
        )
    )
) ELSE (
    CALL :build_android_one "!ARCH!" "%~1"
    IF !ERRORLEVEL! NEQ 0 EXIT /B 1
)
EXIT /B 0
