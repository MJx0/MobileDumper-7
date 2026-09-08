# MobileDumper-7

Mobile-focused dumper for all Unreal Engine games on iOS and Android, based on [Dumper-7](https://github.com/Encryqed/Dumper-7), using [KittyMemory](https://github.com/MJx0/KittyMemory) as the iOS memory backend and [KittyMemoryEx](https://github.com/MJx0/KittyMemoryEx) as the Android memory backend.

## Features

- **Universal memory interface.**  
  Every read/write, on both platforms, goes through one `IMemory` interface, so it's easier to add a new memory backend.
- **Dedicated UE binary analyzer.**  
  [UEAnalyzerKitty](Dumper/UEAnalyzerKitty/) is a UE binary scanner that auto locates `GObjects`/`GNames` when direct symbol/pattern lookup isn't enough but may use more memory.
- **Full offset & layout auto-detection.**  
  Every engine offset is automatically detected from live memory at runtime. Nothing is pinned to a specific Unreal Engine version or a hardcoded offset table.
- **Per-game overrides when you need them.**  
  Most games work out of the box; an `IProfile` interface exists for the rare games that need custom offsets or decryptions.
- **Multiple dump outputs.**  
  C++ SDK, `.usmap` Mappings, IDA Mappings `.idmap`, and Dumpspace `.json`. The generated C++ SDK avoids MSVC-only calling conventions, so it compiles cleanly under clang/gcc.
- **IDA & Ghidra mappings importer scripts.**  
  Generated `.idmap` mapping files can be imported directly into IDA Pro or Ghidra, automatically restoring global symbols, exec function names, and VTable names. Global pointer references are also detected and renamed, making globals such as GObjects and their pointer slots easy to identify.

## Platform Support

| Platform | Entry Point | Deployment |
|---|---|---|
| Android | `Dumper/main_android.cpp` | Injected `.so`, or standalone CLI executable |
| iOS | `Dumper/main_ios.mm` | MobileSubstrate tweak (`.deb`) on jailbroken devices, or a standalone `.dylib` for jailed devices |

> **Architecture Support**

> - **ARM64** — Fully supported.
> - **ARM32** — Partial support; results may vary.
> - **Emulators** — Use the native `x86_64` or `x86` builds; do not use the ARM builds even if the emulator supports ARM translation.

## Usage

**iOS - Tweak (`.deb`):**  
Install on a jailbroken device, launch a UE game, then tap the floating button that appears to configure and run a dump.

**iOS - Library (`.dylib`):**  
Inject the built `.dylib` into a UE game's `.ipa`, launch then tap the floating button that appears to configure and run a dump.

**Android - Library (`.so`):**  
Load into the target process, The dump runs automatically after 60 seconds once it's loaded, check logcat "MobileDumper-7" for logs.

**Android - Standalone executable:**  
If a required argument is missing, MobileDumper7 will ask user input for it.

```text
$ ./MobileDumper7 --help
Usage: MobileDumper-7 [--help] [--version] [--package <name>] [--output <path>] [--dump] [--suspend] [--mem {1,2}]

Optional arguments:
  -h, --help             shows help message and exits
  -v, --version          prints version information and exits
  -p, --package <name>   Specify game package.
  -o, --output <path>    Output directory path.
  -d, --dump             Dump UE library from memory.
  -s, --suspend          Send SIGSTOP to the game while dumping, then SIGCONT.
  -m, --mem {1,2}        Specify memory access type (1: process_vm_readv, 2: pread).
```

## Build

Requires CMake 3.22+, Ninja, and the Android NDK or Xcode, C++20

Clone with `--recurse-submodules` (or run
`git submodule update --init --recursive` afterward if you already cloned without it):

```bash
git clone --recurse-submodules https://github.com/MJx0/MobileDumper-7.git
cd MobileDumper-7
```

```bash
# Android — shared library
./build.sh --platform android --target lib --build release --arch arm64

# Android — standalone executable (device, arm64)
./build.sh --platform android --target exe --build release --arch arm64

# Android — standalone executable (emulator, x86_64)
./build.sh --platform android --target exe --build release --arch x86_64

# iOS — dylib
./build.sh --platform ios --target dylib --build release

# iOS — theos tweak (requires Theos, $THEOS set)
# --scheme accepts rootless, rootful, or roothide (default: rootless)
./build.sh --platform ios --target theos --build release --scheme rootless
```

On Windows, use `build.cmd` with the same flags. Omit all flags to be prompted interactively.

## Credits & Thanks

[Dumper-7](https://github.com/Encryqed/Dumper-7)

[UEDumper](https://github.com/Spuckwaffel/UEDumper)
