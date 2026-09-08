#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

log() { echo -e "${CYAN}${BOLD}==> $*${RESET}"; }
ok()  { echo -e "${GREEN}${BOLD}[ok] $*${RESET}"; }
err() { echo -e "${RED}${BOLD}[error] $*${RESET}" >&2; }

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
    echo "  -h, --help                     Show this help"
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

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform) ARG_PLATFORM="$(echo "$2" | tr '[:upper:]' '[:lower:]')"; shift 2 ;;
        --target)   ARG_TARGET="$(echo "$2" | tr '[:upper:]' '[:lower:]')";   shift 2 ;;
        --build)    ARG_BUILD="$(echo "$2" | tr '[:upper:]' '[:lower:]')";    shift 2 ;;
        --arch)     ARG_ARCH="$(echo "$2" | tr '[:upper:]' '[:lower:]')";     shift 2 ;;
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

REMOVED=0

remove_dir() {
    [[ -d "$1" ]] || return 0
    log "  rm -rf ${1#"${SCRIPT_DIR}/"}"
    rm -rf "$1"
    REMOVED=$((REMOVED + 1))
}

log "Cleaning build directories..."

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
                remove_dir "${SCRIPT_DIR}/builds/${platform}/${target}/${config}/${arch}"
            done
        done
    done
done

[[ -d "${SCRIPT_DIR}/builds" ]] && find "${SCRIPT_DIR}/builds" -type d -empty -delete 2>/dev/null || true

if [[ -z "$ARG_PLATFORM" || "$ARG_PLATFORM" == "ios" ]] &&
   [[ -z "$ARG_TARGET"   || "$ARG_TARGET"   == "theos" ]]; then
    log "Cleaning Theos..."

    THEOS_ROOT="${SCRIPT_DIR}/Theos"

    remove_dir "${THEOS_ROOT}/packages"
    remove_dir "${THEOS_ROOT}/.theos"

    # Theos writes a .stamp into every object directory so object files can
    # depend on "directory exists" without depending on the directory itself
    # (whose mtime changes on every write and would force rebuilds). Because
    # sources compile from ../../Dumper, those paths resolve outside .theos/
    # and the markers are left stranded in the project tree.
    STAMP_COUNT="$(find "${THEOS_ROOT}" -name '.stamp' -type f 2>/dev/null | wc -l | tr -d ' ')"
    if [[ "${STAMP_COUNT}" -gt 0 ]]; then
        log "  rm .stamp markers (${STAMP_COUNT})"
        find "${THEOS_ROOT}" -name '.stamp' -type f -delete 2>/dev/null || true
        # Bottom-up so a directory tree left holding only markers disappears entirely.
        find "${THEOS_ROOT}" -mindepth 1 -type d -empty -delete 2>/dev/null || true
        REMOVED=$((REMOVED + 1))
    fi
fi

if [[ "$REMOVED" -eq 0 ]]; then
    ok "Nothing to clean."
else
    ok "Clean complete (${REMOVED} removed)."
fi
