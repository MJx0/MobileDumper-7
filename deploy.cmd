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

::
:: Android (ADB):
:: Builds, pushes and executes the dumper executable to dump a live game
:: On success, it pulls dump to Tests\Android\<package>\
::
:: iOS (Theos):
:: iOS Theos installs are macOS/Linux only - see deploy.sh.
::
:: deploy.cmd

:: The dumper's own interactive menus (process picker, memory-access-type picker) write
:: UTF-8 box-drawing characters; without this the console's OEM codepage misdecodes them
:: into mojibake.
chcp 65001 >NUL

:: -- ANSI color setup (Windows 10 / 11) ---------------------------------------
FOR /F "delims=" %%E IN ('echo prompt $E^| cmd') DO SET "ESC=%%E"
SET "_CYAN=%ESC%[36;1m"
SET "_GREEN=%ESC%[32;1m"
SET "_YELLOW=%ESC%[33;1m"
SET "_RED=%ESC%[31;1m"
SET "_RESET=%ESC%[0m"

SET "HERE=%~dp0"
IF "%HERE:~-1%"=="\" SET "HERE=%HERE:~0,-1%"

IF /I "%~1"=="-h" ( CALL :usage & EXIT /B 0 )
IF /I "%~1"=="--help" ( CALL :usage & EXIT /B 0 )
IF NOT "%~1"=="" ( CALL :err "Unknown option: %~1" & CALL :usage & EXIT /B 1 )

:: -- Deployment target ---------------------------------------------------------

CALL :section "Deployment target"
CALL :info "1) Android Executable (ADB)"
CALL :info "2) iOS Theos"
SET /P "TARGET_CHOICE=  Select target [1-2]: "

IF "!TARGET_CHOICE!"=="2" (
	CALL :err "iOS Theos deployment is macOS/Linux only - use deploy.sh"
	EXIT /B 1
)
IF NOT "!TARGET_CHOICE!"=="1" (
	CALL :err "Invalid selection"
	EXIT /B 1
)

:: -- Architecture --------------------------------------------------------------

CALL :section "Architecture"
CALL :info "1) arm32   - a 32-bit game on mobile"
CALL :info "2) arm64   - a 64-bit game on mobile"
CALL :info "3) x86     - a 32-bit game on emulator"
CALL :info "4) x86_64  - a 64-bit game on emulator"
SET "_ARCH_CHOICE="
SET /P "_ARCH_CHOICE=  Select architecture [1-4]: "

SET "ARCH="
IF "!_ARCH_CHOICE!"=="1" SET "ARCH=arm32"
IF "!_ARCH_CHOICE!"=="2" SET "ARCH=arm64"
IF "!_ARCH_CHOICE!"=="3" SET "ARCH=x86"
IF "!_ARCH_CHOICE!"=="4" SET "ARCH=x86_64"
IF "!ARCH!"=="" ( CALL :err "Invalid selection" & EXIT /B 1 )

:: -- Suspend while dumping ----------------------------------------------------

CALL :section "Suspend while dumping"
CALL :info "A running game keeps creating objects while it's being dumped, which can cause issues."
CALL :confirm "Suspend the game while dumping?" Y
SET "SUSPEND="
IF "!CONFIRM_RESULT!"=="1" SET "SUSPEND=--suspend"

:: -- Dumper build: prompt to build it if missing or on request ----------------

CALL :section "Dumper build"

SET "BUILD_TYPE=release"
SET "LOCAL_EXE=%HERE%\builds\android\exe\%BUILD_TYPE%\%ARCH%\MobileDumper-7"
SET "NEED_BUILD=0"

IF EXIST "%LOCAL_EXE%" (
	CALL :info "Existing build: %LOCAL_EXE%"

	REM %%~tT is batch-native, no PowerShell needed - avoids a console-font reset some setups
	REM trigger the first time a PowerShell child process attaches to a legacy conhost window.
	SET "_BUILT_AT="
	FOR %%T IN ("%LOCAL_EXE%") DO SET "_BUILT_AT=%%~tT"
	IF "!_BUILT_AT!"=="" SET "_BUILT_AT=unknown"
	CALL :info "Built: !_BUILT_AT!"

	CALL :confirm "Rebuild fresh before deploying?" N
	IF "!CONFIRM_RESULT!"=="1" SET "NEED_BUILD=1"
) ELSE (
	CALL :warn "No existing build at %LOCAL_EXE%"
	SET "NEED_BUILD=1"
)

IF "!NEED_BUILD!"=="1" (
	CALL :info "Building (%ARCH%, %BUILD_TYPE%)..."
	SET "MD7_NO_PAUSE=1"
	CALL "%HERE%\build.cmd" --platform android --target exe --build %BUILD_TYPE% --arch %ARCH%
	IF NOT EXIST "%LOCAL_EXE%" (
		CALL :err "Build finished but %LOCAL_EXE% still isn't there"
		EXIT /B 1
	)
)

CALL :ok "Using: %LOCAL_EXE%"

:: -- Root access: prefer `adb root`, fall back to `su -c` ---------------------

CALL :section "Root access"

SET "ROOT_MODE="
SET "_UID="
FOR /F "delims=" %%U IN ('adb shell -n "id -u" 2^>NUL') DO SET "_UID=%%U"

IF "!_UID!"=="0" (
	SET "ROOT_MODE=direct"
) ELSE (
	adb root >NUL 2>&1
	adb wait-for-device

	CALL :wait_for_root

	IF "!_UID!"=="0" (
		SET "ROOT_MODE=direct"
	) ELSE (
		SET "_UID="
		FOR /F "delims=" %%U IN ('adb shell -n "su -c 'id -u'" 2^>NUL') DO SET "_UID=%%U"
		IF "!_UID!"=="0" (
			SET "ROOT_MODE=su"
		) ELSE (
			CALL :err "No root access available - neither 'adb root' nor 'su -c' got uid 0"
			EXIT /B 1
		)
	)
)

IF "!ROOT_MODE!"=="direct" ( CALL :info "Root access: adb root" ) ELSE ( CALL :info "Root access: su -c" )

:: -- Push & run ---------------------------------------------------------------

SET "REMOTE_EXE=/data/local/tmp/MobileDumper-7-%ARCH%"
SET "REMOTE_OUTPUT=/data/local/tmp/md7_dumps"

CALL :run_root "rm -rf %REMOTE_OUTPUT%" >NUL 2>&1

CALL :section "Push and run"
CALL :info "Installing: %LOCAL_EXE%"
CALL :info "       to    %REMOTE_EXE%"
adb push "%LOCAL_EXE%" "%REMOTE_EXE%"
IF !ERRORLEVEL! NEQ 0 EXIT /B 1

CALL :run_root "chmod 755 %REMOTE_EXE%"

:: --package, --output and --mem are left out on purpose - the dumper prompts for whichever
:: is missing, with its own process picker and menus.
SET "DUMP_COMMAND=%REMOTE_EXE% --output %REMOTE_OUTPUT% --dump %SUSPEND%"

CALL :info "Running the dumper - answer its prompts below:"
ECHO.

:: Piping this through anything (even just to tee a log) buffers by line, so the dumper's
:: final prompt - which has no trailing newline on purpose - wouldn't appear until an extra
:: Enter forced a newline through. Run it straight to the console instead.
IF "!ROOT_MODE!"=="direct" (
	adb shell -t "!DUMP_COMMAND!"
) ELSE (
	adb shell -t "su -c '!DUMP_COMMAND!'"
)

SET "REMOTE_ZIP="
IF "!ROOT_MODE!"=="direct" (
	FOR /F "delims=" %%Z IN ('adb shell "ls %REMOTE_OUTPUT%/MobileDumper-7/*.zip" 2^>NUL') DO SET "REMOTE_ZIP=%%Z"
) ELSE (
	FOR /F "delims=" %%Z IN ('adb shell "su -c 'ls %REMOTE_OUTPUT%/MobileDumper-7/*.zip'" 2^>NUL') DO SET "REMOTE_ZIP=%%Z"
)

IF "!REMOTE_ZIP!"=="" (
	CALL :err "No dump zip produced under %REMOTE_OUTPUT% - check the dumper output above"
	EXIT /B 1
)

:: The dumper names the zip <package>_v<version>_<timestamp>.zip (Generator::SetupDumperFolder) -
:: package names don't contain underscores, so the first underscore-delimited token is the package.
SET "ZIP_BASENAME="
FOR %%F IN ("!REMOTE_ZIP!") DO SET "ZIP_BASENAME=%%~nF"
SET "PACKAGE="
FOR /F "tokens=1 delims=_" %%P IN ("!ZIP_BASENAME!") DO SET "PACKAGE=%%P"

IF "!PACKAGE!"=="" (
	CALL :err "Couldn't parse the package name from !REMOTE_ZIP!"
	EXIT /B 1
)

SET "LOCAL_ZIP=%TEMP%\md7_dump_%PACKAGE%.zip"
IF EXIST "%LOCAL_ZIP%" DEL /F /Q "%LOCAL_ZIP%"

:: -- Pull dump ----------------------------------------------------------------

CALL :section "Pull dump"
CALL :info "Pulling !REMOTE_ZIP!"
CALL :run_root "chmod 644 !REMOTE_ZIP!"
adb pull "!REMOTE_ZIP!" "%LOCAL_ZIP%"
IF !ERRORLEVEL! NEQ 0 EXIT /B 1

CALL :info "Cleaning up remote output"
CALL :run_root "rm -rf %REMOTE_OUTPUT%"

SET "DEST_DIR=%HERE%\Tests\Android\%PACKAGE%"

IF EXIST "%DEST_DIR%" (
	CALL :info "Replacing existing %DEST_DIR%"
	rmdir /s /q "%DEST_DIR%"
)

:: rmdir can leave NTFS in a brief "pending delete" state - retry mkdir past it.
SET "_MKDIR_TRY=0"
:mkdir_retry
mkdir "%DEST_DIR%" 2>NUL
IF EXIST "%DEST_DIR%" GOTO :mkdir_done
SET /A _MKDIR_TRY+=1
IF !_MKDIR_TRY! LSS 5 (
	CALL :sleep_sec 1
	GOTO :mkdir_retry
)
:mkdir_done

CALL :info "Unpacking into %DEST_DIR%"
:: tar avoids Expand-Archive's 260-char MAX_PATH failures on deep SDK paths.
tar -xf "%LOCAL_ZIP%" -C "%DEST_DIR%"
IF !ERRORLEVEL! NEQ 0 EXIT /B 1

DEL /F /Q "%LOCAL_ZIP%"

CALL :ok "Dump ready: %DEST_DIR%"
CALL :ok "Build TestSuit: %DEST_DIR%\CppSDK\TestSuit\build.cmd"
EXIT /B 0

:: =============================================================================
:: SUBROUTINES
:: =============================================================================

:: timeout.exe needs a real console, and PowerShell's Start-Sleep can reset a legacy
:: conhost window's font the first time it attaches - ping sidesteps both. The first
:: reply goes out immediately, so N+1 pings covers N seconds.
:sleep_sec
SET /A "_SLEEP_N=%~1+1"
ping -n !_SLEEP_N! 127.0.0.1 >NUL
GOTO :EOF

:: A GOTO loop inside a parenthesized IF/ELSE block corrupts cmd.exe's parsing
:: of later ^-escapes in that same block, so this loop lives in its own
:: subroutine rather than inline where the root-access block calls it.
:wait_for_root
SET "_TRY=0"
:wait_for_root_loop
SET "_UID="
FOR /F "delims=" %%U IN ('adb shell -n "id -u" 2^>NUL') DO SET "_UID=%%U"
IF "!_UID!"=="0" GOTO :EOF
SET /A _TRY+=1
IF !_TRY! LSS 5 (
	CALL :sleep_sec 1
	GOTO :wait_for_root_loop
)
GOTO :EOF

:usage
echo Usage: %~n0
echo.
echo No flags - everything is asked interactively: architecture, whether to suspend the game
echo while dumping, and whether to rebuild the dumper first.
echo.
echo iOS Theos deployment is macOS/Linux only - see deploy.sh.
GOTO :EOF

:: Asks a yes/no question (%~1, default %~2). Sets CONFIRM_RESULT to 1/0; shows [Y/n]/[y/N].
:confirm
SET "_CDEFAULT=%~2"
IF "!_CDEFAULT!"=="" SET "_CDEFAULT=Y"
IF /I "!_CDEFAULT!"=="Y" ( SET "_CHINT=Y/n" ) ELSE ( SET "_CHINT=y/N" )
SET "_CREPLY="
SET /P "_CREPLY=  %~1 [!_CHINT!] "
IF "!_CREPLY!"=="" SET "_CREPLY=!_CDEFAULT!"
IF /I "!_CREPLY:~0,1!"=="Y" ( SET "CONFIRM_RESULT=1" ) ELSE ( SET "CONFIRM_RESULT=0" )
GOTO :EOF

:: Runs %~1 as root on the device, through whichever ROOT_MODE was detected above. Never
:: interactive, so -n keeps it from touching stdin reserved for the dumper's own prompts.
:run_root
IF "!ROOT_MODE!"=="direct" (
	adb shell -n "%~1"
) ELSE (
	adb shell -n "su -c '%~1'"
)
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
