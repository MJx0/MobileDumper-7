#!/usr/bin/env bash
#
# Android (ADB):
# Builds, pushes and executes the dumper executable to dump a live game
# On success, it pulls dump to Tests/Android/<package>/
#
# iOS (Theos):
# Builds and installs the dumper tweak onto a jailbroken device over Theos.
#
# ./deploy.sh
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'

log()     { echo -e "${CYAN}${BOLD}==> $*${RESET}"; }
ok()      { echo -e "${GREEN}${BOLD}[ok] $*${RESET}"; }
warn()    { echo -e "${YELLOW}${BOLD}[warn] $*${RESET}"; }
err()     { echo -e "${RED}${BOLD}[error] $*${RESET}" >&2; }
section() { echo; echo -e "${CYAN}${BOLD}┌── $1${RESET}"; }
info()    { echo -e "${CYAN}│${RESET}   $*"; }

usage() {
	echo "Usage: $(basename "$0")"
	echo ""
	echo "No flags - everything is asked interactively:"
	echo "  Android: architecture, whether to suspend the game while dumping, whether to"
	echo "           rebuild the dumper first."
	echo "  iOS:     Theos packaging scheme, device IP and SSH port, whether to clean first."
	echo ""
}

[[ "${1:-}" == "-h" || "${1:-}" == "--help" ]] && { usage; exit 0; }
[[ $# -gt 0 ]] && { err "Unknown option: $1"; usage; exit 1; }

# Asks a yes/no question, default Y unless $2 says otherwise; shows [Y/n]/[y/N], not the bare letter.
confirm() {
	local default="${2:-Y}" hint reply
	if [[ "$default" =~ ^[Yy]$ ]]; then hint="Y/n"; else hint="y/N"; fi
	read -rp "  $1 [$hint] " reply
	reply="${reply:-$default}"
	[[ "$reply" =~ ^[Yy] ]]
}

# ── Deployment target ─────────────────────────────────────────────────────────────────────────

section "Deployment target"
info "1) Android Executable (ADB)"
info "2) iOS Theos"
read -rp "  Select target [1-2]: " TARGET_CHOICE

case "$TARGET_CHOICE" in
	1) TARGET="android" ;;
	2) TARGET="theos" ;;
	*) err "Invalid selection"; exit 1 ;;
esac

# ── Android: ADB dump ────────────────────────────────────────────────────────────

deploy_android() {
	local build_type="release"

	section "Architecture"
	info "1) arm32   - a 32-bit game on mobile"
	info "2) arm64   - a 64-bit game on mobile"
	info "3) x86     - a 32-bit game on emulator"
	info "4) x86_64  - a 64-bit game on emulator"
	read -rp "  Select architecture [1-4]: " arch_choice

	local arch
	case "$arch_choice" in
		1) arch="arm32" ;;
		2) arch="arm64" ;;
		3) arch="x86" ;;
		4) arch="x86_64" ;;
		*) err "Invalid selection"; exit 1 ;;
	esac

	section "Suspend while dumping"
	info "A running game keeps creating objects while it's being dumped, which can cause issues."
	local suspend=""
	confirm "Suspend the game while dumping?" Y && suspend="--suspend"

	section "Dumper build"
	local local_exe="$HERE/builds/android/exe/$build_type/$arch/MobileDumper-7"
	local need_build=0

	if [[ -f "$local_exe" ]]; then
		info "Existing build: $local_exe"
		# BSD stat/date (macOS) and GNU stat/date (Linux) disagree on both flags, including what
		# -r even means for date, so this branches on which stat dialect actually works here.
		local built_at
		if built_at="$(stat -f '%m' "$local_exe" 2>/dev/null)"; then
			built_at="$(date -r "$built_at" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "unknown")"
		else
			built_at="$(date -d "@$(stat -c '%Y' "$local_exe")" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "unknown")"
		fi
		info "Built: $built_at"
		confirm "Rebuild fresh before deploying?" N && need_build=1
	else
		warn "No existing build at $local_exe"
		need_build=1
	fi

	if [[ $need_build -eq 1 ]]; then
		info "Building ($arch, $build_type)..."
		"$HERE/build.sh" --platform android --target exe --build "$build_type" --arch "$arch"

		if [[ ! -f "$local_exe" ]]; then
			err "Build finished but $local_exe still isn't there"
			exit 1
		fi
	fi

	ok "Using: $local_exe"

	section "Root access"
	# Prefer `adb root` (emulators, userdebug builds run adbd as root already); fall back to `su -c`.
	local root_mode=""

	# -n (no stdin) on every probe here: these run before the one real interactive command
	# below, and without it each adb shell call quietly steals bytes meant for that prompt.
	current_uid() {
		adb shell -n "id -u" 2>/dev/null | tr -d '\r\n '
	}

	if [[ "$(current_uid)" == "0" ]]; then
		root_mode="direct"
	else
		adb root >/dev/null 2>&1 || true
		adb wait-for-device

		for _ in 1 2 3 4 5; do
			[[ "$(current_uid)" == "0" ]] && break
			sleep 1
		done

		if [[ "$(current_uid)" == "0" ]]; then
			root_mode="direct"
		elif adb shell -n "su -c 'id -u'" 2>/dev/null | tr -d '\r\n ' | grep -q '^0$'; then
			root_mode="su"
		else
			err "No root access available - neither 'adb root' nor 'su -c' got uid 0"
			exit 1
		fi
	fi

	info "Root access: $([[ "$root_mode" == "direct" ]] && echo "adb root" || echo "su -c")"

	# -n: these are one-shot commands, never interactive, and must not touch the stdin that's
	# reserved for the dumper's own prompts below.
	run_root() {
		if [[ "$root_mode" == "direct" ]]; then
			adb shell -n "$1"
		else
			adb shell -n "su -c '$1'"
		fi
	}

	local remote_exe="/data/local/tmp/MobileDumper-7-${arch}"
	local remote_output="/data/local/tmp/md7_dumps"
	local log_file
	log_file="$(mktemp -t md7_deploy)"

	run_root "rm -rf ${remote_output}" >/dev/null 2>&1 || true

	section "Push & run"
	info "Installing: $local_exe"
	info "       ->   $remote_exe"
	adb push "$local_exe" "$remote_exe"
	run_root "chmod 755 ${remote_exe}"

	# --package, --output and --mem are left out on purpose: the dumper itself prompts for
	# whichever of these is missing, with its own process picker and menus - duplicating that
	# here would just be a second, worse CLI on top of the real one.
	local dump_command="${remote_exe} --output ${remote_output} --dump ${suspend}"

	info "Running the dumper - answer its prompts below:"
	echo

	if [[ "$root_mode" == "direct" ]]; then
		adb shell -t "$dump_command" | tee "$log_file"
	else
		adb shell -t "su -c '${dump_command}'" | tee "$log_file"
	fi

	# Not anchored to column 0: the dumper's own log lines are prefixed with a color escape code.
	local package
	package="$(grep -a 'Process Name: ' "$log_file" | tail -1 | sed -E 's/.*Process Name: //' | tr -d '\r')"

	if [[ -z "$package" ]]; then
		err "Couldn't find the dumped package name in the dumper's own output"
		rm -f "$log_file"
		exit 1
	fi

	local remote_zip
	remote_zip="$(run_root "ls ${remote_output}/MobileDumper-7/*.zip" 2>/dev/null | tr -d '\r')"

	if [[ -z "$remote_zip" ]]; then
		err "No dump zip produced under ${remote_output} - check the dumper output above"
		rm -f "$log_file"
		exit 1
	fi

	rm -f "$log_file"

	local local_zip
	local_zip="$(mktemp -t md7_dump).zip"

	section "Pull dump"
	info "Pulling ${remote_zip}"
	run_root "chmod 644 \"${remote_zip}\""
	adb pull "$remote_zip" "$local_zip"

	info "Cleaning up remote output"
	run_root "rm -rf ${remote_output}"

	local dest_dir="$HERE/Tests/Android/$package"

	if [[ -d "$dest_dir" ]]; then
		info "Replacing existing $dest_dir"
		rm -rf "$dest_dir"
	fi

	mkdir -p "$dest_dir"

	info "Unpacking into $dest_dir"
	unzip -q "$local_zip" -d "$dest_dir"
	rm -f "$local_zip"

	ok "Dump ready: $dest_dir"
	ok "Build TestSuit: $dest_dir/CppSDK/TestSuit/build.sh"
}

# ── iOS: Theos install ───────────────────────────────────────────────────────────
#
# Builds and installs the dumper's own Theos tweak (Theos/Makefile), the iOS counterpart
# to pushing the Android executable above.

deploy_theos() {
	section "Theos"
	if [[ -z "${THEOS:-}" ]]; then
		err "\$THEOS is not set - install Theos and set \$THEOS first"
		exit 1
	fi
	info "\$THEOS = $THEOS"

	section "Packaging scheme"
	info "1) rootless"
	info "2) rootful"
	info "3) roothide"
	read -rp "  Select scheme [1-3]: " scheme_choice

	local scheme
	case "$scheme_choice" in
		1) scheme="rootless" ;;
		2) scheme="rootful" ;;
		3) scheme="roothide" ;;
		*) err "Invalid selection"; exit 1 ;;
	esac

	section "Device"
	read -rp "  Device IP to install onto (THEOS_DEVICE_IP): " device_ip

	if [[ -z "$device_ip" ]]; then
		err "No device IP given"
		exit 1
	fi

	read -rp "  SSH port [22]: " device_port
	device_port="${device_port:-22}"

	info "Checking $device_ip:$device_port is reachable..."
	if command -v nc >/dev/null 2>&1 && ! nc -z -w 3 "$device_ip" "$device_port" 2>/dev/null; then
		warn "Couldn't reach $device_ip:$device_port - check the device is on the same network,"
		warn "unlocked, and that SSH (OpenSSH via Sileo/Zebra) is actually running on it."
		confirm "Try the install anyway?" N || exit 1
	fi

	section "Build & install"
	info "Scheme: $scheme"
	info "Target: $device_ip:$device_port"

	if confirm "Clean build first (removes .theos/ and packages/)?" N; then
		info "Cleaning..."
		(cd "$HERE/Theos" && make clean >/dev/null 2>&1) || true
	fi

	info "Building and installing - this can take a few minutes the first time..."
	# "install" alone fails: Theos's internal-install-check requires a package built in the
	# *same* invocation (it reads a last-built-package marker that only package:: refreshes),
	# so install has to be chained after package, not run as its own separate `make` call.
	(cd "$HERE/Theos" && THEOS_DEVICE_IP="$device_ip" THEOS_DEVICE_PORT="$device_port" THEOS_PACKAGE_SCHEME="$scheme" make package install)

	ok "Installed onto $device_ip:$device_port"
}

case "$TARGET" in
	android) deploy_android ;;
	theos)   deploy_theos ;;
esac
