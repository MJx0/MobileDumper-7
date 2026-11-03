#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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

# Asks a yes/no question, default Y unless $2 says otherwise.
confirm() {
    [[ $AUTO_YES -eq 1 ]] && return 0

    local default="${2:-Y}" hint reply
    if [[ "$default" =~ ^[Yy]$ ]]; then hint="Y/n"; else hint="y/N"; fi
    read -rp "  $1 [$hint] " reply
    reply="${reply:-$default}"
    [[ "$reply" =~ ^[Yy] ]]
}

ALL_ARCHES=(arm64 arm32 x86 x86_64)

TARGET_NAME="$(grep -m1 '^project(' "${SCRIPT_DIR}/CMakeLists.txt" | sed 's/project(\([^ )]*\).*/\1/')"

usage() {
    echo "Usage: $(basename "$0") [OPTIONS]"
    echo ""
    echo "Selectors mirror build.sh — omit them all to clean everything."
    echo ""
    echo "Options:"
    echo "  --platform android|ios         Limit to one platform"
    echo "  --target   exe|lib             Android output"
    echo "             dylib|theos         iOS output"
    echo "  --build    debug|release       Limit to one configuration"
    echo "  --arch     arm64|arm32|x86|x86_64|all"
    echo "                                 Android ABI; iOS is always arm64"
    echo "  -y, --yes                      Don't ask for confirmation before removing anything"
    echo "  -h, --help                     Show this help"
    echo ""
    echo "Without -y, everything that would be removed is listed first and you're asked to confirm."
    echo ""
    echo "Examples:"
    echo "  $(basename "$0")                                  # everything"
    echo "  $(basename "$0") --platform android               # all android builds"
    echo "  $(basename "$0") --platform android --arch arm32  # one ABI"
    echo "  $(basename "$0") --target theos                   # Theos packages + cache"
}

ARG_PLATFORM=""
ARG_TARGET=""
ARG_BUILD=""
ARG_ARCH=""
AUTO_YES=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform) ARG_PLATFORM="$(echo "$2" | tr '[:upper:]' '[:lower:]')"; shift 2 ;;
        --target)   ARG_TARGET="$(echo "$2" | tr '[:upper:]' '[:lower:]')";   shift 2 ;;
        --build)    ARG_BUILD="$(echo "$2" | tr '[:upper:]' '[:lower:]')";    shift 2 ;;
        --arch)     ARG_ARCH="$(echo "$2" | tr '[:upper:]' '[:lower:]')";     shift 2 ;;
        -y|--yes)   AUTO_YES=1; shift ;;
        -h|--help)  usage; exit 0 ;;
        *) err "Unknown option: $1"; echo ""; usage >&2; exit 1 ;;
    esac
done

case "$ARG_PLATFORM" in
    ""|android|ios) ;;
    *) err "Invalid --platform '${ARG_PLATFORM}' (must be android or ios)"; exit 1 ;;
esac

case "$ARG_TARGET" in
    ""|exe|lib|dylib|theos) ;;
    *) err "Invalid --target '${ARG_TARGET}' (android: exe|lib, ios: dylib|theos)"; exit 1 ;;
esac

case "$ARG_BUILD" in
    ""|debug|release) ;;
    *) err "Invalid --build '${ARG_BUILD}' (must be debug or release)"; exit 1 ;;
esac

case "$ARG_ARCH" in
    ""|all|arm64|arm32|x86|x86_64) ;;
    *) err "Invalid --arch '${ARG_ARCH}' (must be arm64, arm32, x86, x86_64 or all)"; exit 1 ;;
esac

platforms=(android ios)
[[ -n "$ARG_PLATFORM" ]] && platforms=("$ARG_PLATFORM")

configs=(debug release)
[[ -n "$ARG_BUILD" ]] && configs=("$ARG_BUILD")

TO_REMOVE=()

plan_dir() {
    # Explicit return 0: under set -e, a bare call to this function would otherwise abort the
    # whole script the first time a candidate directory doesn't exist - the common case.
    [[ -d "$1" ]] && TO_REMOVE+=("$1")
    return 0
}

section "Scanning build directories"

for platform in "${platforms[@]}"; do
    if [[ "$platform" == "android" ]]; then
        targets=(exe lib)
        arches=("${ALL_ARCHES[@]}")
        [[ -n "$ARG_ARCH" && "$ARG_ARCH" != "all" ]] && arches=("$ARG_ARCH")
    else
        targets=(dylib)
        arches=(arm64)
    fi

    if [[ -n "$ARG_TARGET" ]]; then
        # theos has no builds/ directory; it is handled separately below.
        [[ "$ARG_TARGET" == "theos" ]] && continue
        printf '%s\n' "${targets[@]}" | grep -qx "$ARG_TARGET" || continue
        targets=("$ARG_TARGET")
    fi

    for target in "${targets[@]}"; do
        for config in "${configs[@]}"; do
            for arch in "${arches[@]}"; do
                plan_dir "${SCRIPT_DIR}/builds/${platform}/${target}/${config}/${arch}"
            done
        done
    done
done

STAMP_COUNT=0
THEOS_ROOT="${SCRIPT_DIR}/Theos"

if [[ -z "$ARG_PLATFORM" || "$ARG_PLATFORM" == "ios" ]] &&
   [[ -z "$ARG_TARGET"   || "$ARG_TARGET"   == "theos" ]]; then
    plan_dir "${THEOS_ROOT}/packages"
    plan_dir "${THEOS_ROOT}/.theos"

    # Theos writes a .stamp into every object directory so object files can
    # depend on "directory exists" without depending on the directory itself
    # (whose mtime changes on every write and would force rebuilds). Because
    # sources compile from ../../Dumper, those paths resolve outside .theos/
    # and the markers are left stranded in the project tree.
    STAMP_COUNT="$(find "${THEOS_ROOT}" -name '.stamp' -type f 2>/dev/null | wc -l | tr -d ' ')"
fi

if [[ ${#TO_REMOVE[@]} -eq 0 && "$STAMP_COUNT" -eq 0 ]]; then
    ok "Nothing to clean."
    exit 0
fi

section "Will remove"
for d in "${TO_REMOVE[@]}"; do
    info "${d#"${SCRIPT_DIR}/"}"
done
[[ "$STAMP_COUNT" -gt 0 ]] && info "${STAMP_COUNT} .stamp marker(s) under Theos/"

confirm "Remove the above?" Y || { warn "Cancelled."; exit 0; }

REMOVED=0

section "Removing"
for d in "${TO_REMOVE[@]}"; do
    info "rm -rf ${d#"${SCRIPT_DIR}/"}"
    rm -rf "$d"
    REMOVED=$((REMOVED + 1))
done

[[ -d "${SCRIPT_DIR}/builds" ]] && find "${SCRIPT_DIR}/builds" -type d -empty -delete 2>/dev/null || true

if [[ "$STAMP_COUNT" -gt 0 ]]; then
    info "rm .stamp markers (${STAMP_COUNT})"
    find "${THEOS_ROOT}" -name '.stamp' -type f -delete 2>/dev/null || true
    # Bottom-up so a directory tree left holding only markers disappears entirely.
    find "${THEOS_ROOT}" -mindepth 1 -type d -empty -delete 2>/dev/null || true
    REMOVED=$((REMOVED + 1))
fi

ok "Clean complete (${REMOVED} removed)."
