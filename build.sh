#!/usr/bin/env bash
set -euo pipefail

host_tag()
{
    case "$(uname -s)" in
        Darwin) echo "darwin-x86_64" ;;
        Linux)  echo "linux-x86_64"  ;;
        *)      return 1             ;;
    esac
}

find_ndk_root()
{
    local ndk=""

    for v in ANDROID_NDK_HOME ANDROID_NDK NDK_HOME
    do
        if [[ -n "${!v:-}" ]]; then
            ndk="${!v}"
            break
        fi
    done

    if [[ -z "$ndk" ]]; then

        for sdk in ANDROID_HOME ANDROID_SDK_ROOT ANDROID_SDK_HOME ANDROID_SDK
        do
            if [[ -n "${!sdk:-}" ]] && [[ -d "${!sdk}/ndk" ]]; then
                ndk="$(ls -1 "${!sdk}/ndk" | sort -V | tail -1)"
                ndk="${!sdk}/ndk/$ndk"
                break
            fi
        done
    fi

    [[ -z "$ndk" ]] && return 1

    [[ -f "$ndk/build/cmake/android.toolchain.cmake" ]] && echo "$ndk"
}

find_xcode_bin()
{
    local tool="$1"
    local c

    c="$(xcrun --find "$tool" 2>/dev/null || true)"

    if [[ -n "$c" && -x "$c" ]]; then
        echo "$c"
        return
    fi

    for c in \
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/$tool" \
        "/Library/Developer/CommandLineTools/usr/bin/$tool"
    do
        [[ -x "$c" ]] && {
            echo "$c"
            return
        }
    done
}

arch_to_preset()
{
    case "$1" in
        arm64)  echo "arm64"       ;;
        arm32)  echo "armeabi-v7a" ;;   # presets keep the canonical ABI spelling
        x86)    echo "x86"         ;;
        x86_64) echo "x86_64"      ;;
        *)      return 1           ;;
    esac
}

ALL_ARCHES=(arm64 arm32 x86 x86_64)

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

log()  { echo -e "${CYAN}${BOLD}==> $*${RESET}"; }
ok()   { echo -e "${GREEN}${BOLD}[ok] $*${RESET}"; }
err()  { echo -e "${RED}${BOLD}[error] $*${RESET}" >&2; }

require_ninja() {
    if command -v ninja >/dev/null 2>&1; then
        return 0
    fi
    err "Ninja is required — the CMake presets pin the Ninja generator."
    err "  macOS: brew install ninja"
    err "  Linux: apt install ninja-build"
    return 1
}

# Mirrors the binaryDir scheme in CMakePresets.json: builds/<platform>/<target>/<config>/<arch>
build_dir_for() {
    local cfg
    cfg="$(echo "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]')"
    echo "${SCRIPT_DIR}/builds/$1/$2/${cfg}/$3"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TARGET_NAME=$(grep -m1 '^project(' "${SCRIPT_DIR}/CMakeLists.txt" | sed 's/project(\([^ )]*\).*/\1/')
if [[ -z "${TARGET_NAME}" ]]; then
    err "Could not read project name from CMakeLists.txt"
    exit 1
fi

BUILD_TYPE="Release"

THEOS_DIR="${SCRIPT_DIR}/Theos"

XCODE_CLANG="$(find_xcode_bin clang || true)"
XCODE_CLANGXX="$(find_xcode_bin clang++ || true)"

build_ios_theos() {
    log "Building iOS Theos Tweak (.deb)${SCHEME:+ — scheme=${SCHEME}}"

    if [[ -z "${THEOS:-}" ]]; then
        err "THEOS env var is not set (e.g. export THEOS=~/theos)"
        return 1
    fi
    if [[ ! -d "${THEOS}" ]]; then
        err "THEOS dir not found: ${THEOS}"
        return 1
    fi

    # Theos/Makefile sets THEOS_PACKAGE_SCHEME with ?= (default: roothide).
    # Command-line assignments always override ?=, so passing --scheme here
    # takes effect; omitting it lets the Makefile default apply.
    local scheme_args=()
    if [[ -n "${SCHEME_SET:-}" ]]; then
        scheme_args=("THEOS_PACKAGE_SCHEME=${SCHEME}")
    fi

    if [[ "${BUILD_TYPE}" == "Debug" ]]; then
        (cd "${THEOS_DIR}" && make package DEBUG=1 "${scheme_args[@]+"${scheme_args[@]}"}")
    else
        (cd "${THEOS_DIR}" && make package FINALPACKAGE=1 "${scheme_args[@]+"${scheme_args[@]}"}")
    fi

    local deb
    deb=$(ls -t "${THEOS_DIR}/packages/"*.deb 2>/dev/null | head -1 || true)
    if [[ -n "${deb}" ]]; then
        ok "iOS package: ${deb}"
    else
        err "No .deb found in Theos/packages/ after build"
        return 1
    fi

    # The Makefile's after-package rule moves the bundle out of .theos/obj and
    # renames it to match the package, so it is looked up from the deb's name.
    local dsym="${deb%.deb}.dSYM"
    if [[ -d "${dsym}" ]]; then
        ok "iOS dSYM:   ${dsym}"
        return 0
    fi

    # A rebuild with no source changes does not relink, so no fresh dSYM is
    # produced — and because the rule moves rather than copies, the previous one
    # is no longer in .theos/obj either. Fall back to whatever symbols exist,
    # flagged, since they may not correspond to this exact package.
    local fallback
    fallback=$(ls -td "${THEOS_DIR}/packages/"*.dSYM 2>/dev/null | head -1 || true)

    if [[ -z "${fallback}" ]]; then
        fallback=$(find "${THEOS_DIR}/.theos" -name "*.dSYM" -type d 2>/dev/null | head -1 || true)
    fi

    if [[ -n "${fallback}" ]]; then
        log "dSYM ${dsym##*/} absent — falling back to ${fallback##*/}, which may predate this build"
        ok "iOS dSYM:   ${fallback}"
    else
        err "dSYM not found at ${dsym} — symbols not extracted"
    fi
}

build_ios_cmake() {
    log "Building iOS library (.dylib) — type=${BUILD_TYPE}"

    require_ninja || return 1

    if [[ -z "${XCODE_CLANG}" || -z "${XCODE_CLANGXX}" ]]; then
        err "Xcode command-line tools not found. Run: xcode-select --install"
        return 1
    fi

    local preset="ios-arm64"
    [[ "${BUILD_TYPE}" == "Debug" ]] && preset+="-debug"

    local build_dir
    build_dir="$(build_dir_for ios dylib arm64)"

    log "Preset: ${preset}"

    # A preset cannot run xcrun, so the resolved toolchain is layered on top.
    # Without this CMake falls back to the /usr/bin/c++ driver shim and the
    # XCODE_CLANG check above would be validating something never actually used.
    if ! cmake --preset "${preset}" \
        -DCMAKE_C_COMPILER="${XCODE_CLANG}" \
        -DCMAKE_CXX_COMPILER="${XCODE_CLANGXX}"; then
        err "Configure failed for preset: ${preset}"
        return 1
    fi

    if ! cmake --build --preset "${preset}"; then
        err "Build failed for preset: ${preset}"
        return 1
    fi

    local dylib="${build_dir}/lib${TARGET_NAME}.dylib"
    if [[ -f "${dylib}" ]]; then
        ok "iOS binary: ${dylib} ($(du -sh "${dylib}" | cut -f1))"
    else
        err ".dylib not found after build: ${dylib}"
        return 1
    fi

    local dsym="${dylib}.dSYM"
    if dsymutil "${dylib}" -o "${dsym}" 2>/dev/null; then
        ok "iOS dSYM:   ${dsym}"
    else
        err "dsymutil failed — symbols not extracted"
    fi
}

build_android_one() {
    local arch="$1"
    local target="$2"

    local preset="android-$(arch_to_preset "${arch}")"
    [[ "${target}" == "exe" ]] && preset+="-exe"
    [[ "${BUILD_TYPE}" == "Debug" ]] && preset+="-debug"

    local build_dir
    build_dir="$(build_dir_for android "${target}" "${arch}")"

    log "Building Android ${target} — arch=${arch}, type=${BUILD_TYPE}, preset=${preset}"

    if ! cmake --preset "${preset}"; then
        err "Configure failed for preset: ${preset}"
        return 1
    fi

    if ! cmake --build --preset "${preset}"; then
        err "Build failed for preset: ${preset}"
        return 1
    fi

    local artifact
    if [[ "${target}" == "exe" ]]; then
        artifact="${build_dir}/${TARGET_NAME}"
    else
        artifact="${build_dir}/lib${TARGET_NAME}.so"
    fi

    if [[ -f "${artifact}" ]]; then
        ok "Android ${target} (${arch}): ${artifact} ($(du -sh "${artifact}" | cut -f1))"
    else
        err "Artifact not found after build: ${artifact}"
        return 1
    fi

    local tag
    if ! tag="$(host_tag)"; then
        err "Unsupported host OS — symbols not extracted"
        return 0
    fi

    local objcopy="${NDK_PATH}/toolchains/llvm/prebuilt/${tag}/bin/llvm-objcopy"
    if [[ -f "${objcopy}" ]]; then
        "${objcopy}" --only-keep-debug "${artifact}" "${artifact}.debug"
        ok "Android symbols: ${artifact}.debug"
    else
        err "llvm-objcopy not found — symbols not extracted"
    fi
}

build_android() {
    local target="$1"

    require_ninja || return 1

    if ! NDK_PATH="$(find_ndk_root)"; then
        err "Android NDK not found. Set ANDROID_NDK_HOME or install via:"
        err "  brew install --cask android-ndk"
        err "  or Android Studio SDK Manager"
        return 1
    fi

    # The presets resolve their toolchain through $env{ANDROID_NDK_HOME}.
    export ANDROID_NDK_HOME="${NDK_PATH}"
    log "NDK: ${NDK_PATH}"

    local arches=()
    if [[ "${ARCH}" == "all" ]]; then
        arches=("${ALL_ARCHES[@]}")
    else
        arches=("${ARCH}")
    fi

    for a in "${arches[@]}"; do
        if ! build_android_one "${a}" "${target}"; then
            err "Android build failed for arch: ${a}"
            return 1
        fi
    done
}

usage() {
    echo "Usage: $(basename "$0") [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --platform android|ios         Target platform"
    echo "  --target   exe|lib             Android output"
    echo "             dylib|theos         iOS output"
    echo "  --build    debug|release       Build configuration (default: release)"
    echo "  --arch     arm64|arm32|x86|x86_64|all"
    echo "                                 Android ABI (default: arm64); iOS is always arm64"
    echo "  --scheme   rootless|rootful|roothide"
    echo "                                 Theos packaging scheme (--platform ios --target theos only)."
    echo "                                 Default: whatever Theos/Makefile hardcodes (currently rootless)."
    echo "  -h, --help                     Show this help"
    echo ""
    echo "Omit any option to be prompted interactively."
    echo ""
    echo "Examples:"
    echo "  $(basename "$0") --platform android --target lib --build release --arch arm64"
    echo "  $(basename "$0") --platform android --target exe --arch all"
    echo "  $(basename "$0") --platform ios --target theos"
    echo "  $(basename "$0") --platform ios --target theos --scheme rootful"
    echo "  $(basename "$0") --platform ios"
}

ARG_PLATFORM=""
ARG_TARGET=""
ARG_BUILD=""
ARG_ARCH=""
ARG_SCHEME=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform) ARG_PLATFORM="$(echo "$2" | tr '[:upper:]' '[:lower:]')"; shift 2 ;;
        --target)   ARG_TARGET="$(echo "$2" | tr '[:upper:]' '[:lower:]')";   shift 2 ;;
        --build)    ARG_BUILD="$(echo "$2" | tr '[:upper:]' '[:lower:]')";    shift 2 ;;
        --arch)     ARG_ARCH="$(echo "$2" | tr '[:upper:]' '[:lower:]')";     shift 2 ;;
        --scheme)   ARG_SCHEME="$(echo "$2" | tr '[:upper:]' '[:lower:]')";   shift 2 ;;
        --type)     err "--type was renamed to --target (exe|lib for android, dylib|theos for ios)"; exit 1 ;;
        -h|--help)  usage; exit 0 ;;
        *) err "Unknown option: $1"; echo ""; usage >&2; exit 1 ;;
    esac
done

PLATFORM=""
BTYPE=""
TARGET=""
ARCH=""

if [[ -n "$ARG_PLATFORM" ]]; then
    case "$ARG_PLATFORM" in
        android) PLATFORM="Android" ;;
        ios)     PLATFORM="iOS" ;;
        *) err "Invalid --platform '${ARG_PLATFORM}' (must be android or ios)"; exit 1 ;;
    esac
fi

if [[ -n "$ARG_BUILD" ]]; then
    case "$ARG_BUILD" in
        debug)   BTYPE="Debug" ;;
        release) BTYPE="Release" ;;
        *) err "Invalid --build '${ARG_BUILD}' (must be debug or release)"; exit 1 ;;
    esac
fi

PS3=$'\n> '

echo ""
echo "  ${TARGET_NAME} Build"
echo ""

if [[ -z "$PLATFORM" ]]; then
    echo "Platform:"
    select PLATFORM in "Android" "iOS" "Quit"; do
        [[ -n "$PLATFORM" ]] && break
    done
    [[ "$PLATFORM" == "Quit" ]] && exit 0
    echo ""
fi

if [[ -n "$ARG_TARGET" ]]; then
    case "$PLATFORM/$ARG_TARGET" in
        "Android/lib") TARGET="lib" ;;
        "Android/exe")    TARGET="exe" ;;
        "iOS/dylib")      TARGET="dylib" ;;
        "iOS/theos")      TARGET="theos" ;;
        "Android/dylib"|"Android/theos")
            err "--target '${ARG_TARGET}' is only valid for --platform ios (use exe or lib)"; exit 1 ;;
        "iOS/exe"|"iOS/lib")
            err "--target '${ARG_TARGET}' is only valid for --platform android (use dylib or theos)"; exit 1 ;;
        *) err "Invalid --target '${ARG_TARGET}' (android: exe|lib, ios: dylib|theos)"; exit 1 ;;
    esac
fi

if [[ -z "$TARGET" ]]; then
    echo "Target:"
    if [[ "$PLATFORM" == "Android" ]]; then
        select TARGET in "lib" "exe" "Quit"; do
            [[ -n "$TARGET" ]] && break
        done
    else
        select TARGET in "dylib" "theos" "Quit"; do
            [[ -n "$TARGET" ]] && break
        done
    fi
    [[ "$TARGET" == "Quit" ]] && { err "Cancelled."; exit 1; }
    echo ""
fi

if [[ -z "$BTYPE" ]]; then
    echo "Build type:"
    select BTYPE in "Release" "Debug" "Quit"; do
        [[ -n "$BTYPE" ]] && break
    done
    [[ "$BTYPE" == "Quit" ]] && { err "Cancelled."; exit 1; }
    echo ""
fi

BUILD_TYPE="$BTYPE"

if [[ "$PLATFORM" == "iOS" ]]; then
    if [[ -n "$ARG_ARCH" && "$ARG_ARCH" != "arm64" ]]; then
        err "--arch '${ARG_ARCH}' is not valid for iOS (always arm64)"
        exit 1
    fi
    ARCH="arm64"
elif [[ "$TARGET" != "theos" ]]; then
    if [[ -n "$ARG_ARCH" ]]; then
        case "$ARG_ARCH" in
            arm64|arm32|x86|x86_64|all) ARCH="$ARG_ARCH" ;;
            *) err "Invalid --arch '${ARG_ARCH}' (must be arm64, arm32, x86, x86_64 or all)"; exit 1 ;;
        esac
    else
        echo "Architecture:"
        select ARCH in "arm64" "arm32" "x86" "x86_64" "all" "Quit"; do
            [[ -n "$ARCH" ]] && break
        done
        [[ "$ARCH" == "Quit" ]] && { err "Cancelled."; exit 1; }
        echo ""
    fi
fi

# Theos packaging scheme — iOS theos target only. Left unset (SCHEME_SET empty),
# build_ios_theos falls back to whatever Theos/Makefile hardcodes.
SCHEME=""
SCHEME_SET=""
if [[ -n "$ARG_SCHEME" ]]; then
    if [[ "$PLATFORM" != "iOS" || "$TARGET" != "theos" ]]; then
        err "--scheme is only valid for --platform ios --target theos"
        exit 1
    fi
    case "$ARG_SCHEME" in
        rootless) SCHEME="rootless" ;;
        roothide) SCHEME="roothide" ;;
        rootful)  SCHEME="" ;;   # Theos' traditional/legacy scheme is the empty value.
        *) err "Invalid --scheme '${ARG_SCHEME}' (must be rootless, rootful or roothide)"; exit 1 ;;
    esac
    SCHEME_SET=1
fi

echo ""
case "$PLATFORM/$TARGET" in
    "Android/lib") build_android lib ;;
    "Android/exe")    build_android exe ;;
    "iOS/dylib")      build_ios_cmake ;;
    "iOS/theos")      build_ios_theos ;;
esac

ok "Done."
