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
  C++ SDK (see [UsingTheSDK](UsingTheSDK.md)), `.usmap` Mappings, IDA Mappings `.idmap`, and Dumpspace `.json`. The generated C++ SDK avoids MSVC-only calling conventions, so it compiles cleanly under clang/gcc.
- **IDA & Ghidra mappings importer scripts.**  
  Generated `.idmap` mapping files can be imported directly into IDA Pro or Ghidra, automatically restoring global symbols, exec function names, and VTable names. Global pointer references are also detected and renamed, making globals such as GObjects and their pointer slots easy to identify.
- **Built-in C++ SDK verification library.**  
  Every dump also produces `CppSDK/TestSuit/`, a small ready-to-build project that checks the generated C++ SDK that it actually compiles, and that it works correctly when loaded into a live game at runtime. Turn it off with `CppGenerator.bGenerateSDKCompilationTest`.

## Platform Support

| Platform | Entry Point | Deployment |
| --- | --- | --- |
| Android | `Dumper/main_android.cpp` | Injected `.so`, or standalone CLI executable |
| iOS | `Dumper/main_ios.mm` | MobileSubstrate tweak (`.deb`) on jailbroken devices, or a standalone `.dylib` for jailed devices |

> **Architecture Support**
>
> - **ARM64** — Fully supported.
> - **ARM32** — Fully supported.
> - **Emulators** — Use the native `x86_64` or `x86` builds, do not use the ARM builds even if the emulator supports ARM translation.
>
> The dumper must match the target's OS architecture.

## Usage

**iOS - Tweak (`.deb`):**  
Install on a jailbroken device, launch a UE game, then tap the floating button that appears to configure and run a dump.

**iOS - Library (`.dylib`):**  
Inject the built `.dylib` into a UE game's `.ipa`, launch then tap the floating button that appears to configure and run a dump.

**Android - Library (`.so`):**  
Load into the target process, The dump runs automatically after 60 seconds once it's loaded, check logcat "MobileDumper-7" for logs.

**Android - Standalone executable:**  
If a required argument is missing, MobileDumper-7 will ask user input for it.

```text
$ ./MobileDumper-7 --help
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

**Recommended - Suspend the game while dumping:**  
A running game keeps creating objects while it is being dumped, which may cause issues.  
Freeze it by adding `-s/--suspend` to the Android executable args, or the **Suspend Threads** toggle on iOS UI menu.  

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

# Android — standalone executable for a 32-bit game (must match the target's architecture)
./build.sh --platform android --target exe --build release --arch arm32

# Android — standalone executable (emulator, x86_64)
./build.sh --platform android --target exe --build release --arch x86_64

# Android — every architecture at once
./build.sh --platform android --target exe --build release --arch all

# iOS — dylib
./build.sh --platform ios --target dylib --build release

# iOS — theos tweak (requires Theos, $THEOS set)
# --scheme accepts rootless, rootful, or roothide (default: rootless)
./build.sh --platform ios --target theos --build release --scheme rootless
```

On Windows, use `build.cmd` with the same flags. Omit all flags to be prompted interactively.

## Custom Game Profiles

Most games work out of the box without a custom profile. A profile is only needed when a game applies custom encryption to its UE internals — typically to GNames pointers, name strings, or object items.

To add one, create a header in `Dumper/Profile/CustomProfiles/` that inherits from `IProfile` and override only the methods the game requires:

```cpp
// Dumper/Profile/CustomProfiles/Shared/MyGame.h
#pragma once
#include "../../IProfile.h"

class MyGameProfile : public IProfile
{
public:
    // Bundle ID on iOS / package name on Android.
    // The profile is activated automatically when a supported game is detected.
    std::vector<std::string> GetSupportedGames() const override
    {
        return {"com.example.mygame"};
    }

    // Override only what the game encrypts — leave everything else out.
};
```

Then register it in the platform entry point alongside the existing profiles:

- **iOS** — `Dumper/main_ios.mm` → `GetUECustomProfiles()`
- **Android** — `Dumper/main_android.cpp` → `GetUECustomProfiles()`

```cpp
UECustomProfiles.push_back(std::make_shared<MyGameProfile>());
```

### Available overrides

| Method | When to override |
| -------- | ----------------- |
| `GetGObjects()` | GObjects pointer cannot be found automatically |
| `GetGNames()` | GNames pointer cannot be found automatically — see PUBG example below |
| `ResolveGObjectsLayout(...)` | Auto layout detection fails; provide the object array layout manually |
| `ResolveGNamesLayout(...)` | Auto layout detection fails; provide the name container layout manually |
| `OverrideDecryptCallbacks(FDecryptCallbacks&)` | The game encrypts one or more engine values in memory — see Decryption hooks below |
| `OverrideSettings(FSettings&)` | Tweak Generator settings for this game |
| `OverrideInGenOffsets(FInGenOffsets&)` | Force an engine offset the detector gets wrong |
| `OverrideInSKOffsets(FInSDKOffsets&)` | Force a size/offset baked into the generated SDK |
| `GetObjectByIndex(...)` / `GetNameEntryByIndex(...)` / `GetNameEntryString(...)` | The game stores its objects or names in a different structure than the standard layout options can describe |
| `DisableWideNameEntry()` | Wide name entries should be skipped entirely |

### Decryption hooks

Everything a game might encrypt is reached through one struct, `FDecryptCallbacks`
([Dumper/Engine/OffsetFinder/DecryptCallbacks.h](Dumper/Engine/OffsetFinder/DecryptCallbacks.h)).
Install decrypt hooks in `IProfile::OverrideDecryptCallbacks`, every hook defaults to identity, so override only what
the game actually encrypts.

| Group | Hooks |
| --- | --- |
| `Statics` | `GObjects`, `GNames` — the global pointers |
| `FName` | `CompIdx`, `Number` |
| `UObject` | `Flags`, `Index`, `Class`, `Outer` |
| `NameStorage` | `Chunk`, `Entry` — chunk/block and name entry addresses, shared by both name layouts |
| `String` | `UTF8`, `UTF16`, `UTF32` — the name characters |
| `NameArray` | `Chunks`, `NumElements`, `FNameEntry.Index` |
| `NamePool` | `Blocks`, `MaxChunkIndex`, `ByteCursor`, `FNameEntry.Header` |
| `FixedObjects` | `Objects`, `NumObjects`, `MaxObjects`, `FUObjectItem.Object` |
| `ChunkedObjects` | `Objects`, `NumElements`, `MaxElements`, `MaxChunks`, `NumChunks`, `FUObjectItem.Object` |

A FDecryptCallbacks hook can decrypt one value, one array entry, or a block of name characters.

```cpp
void OverrideDecryptCallbacks(FDecryptCallbacks& Callbacks) const override
{
    // one value
    Callbacks.Statics.GNames = [](const FDecryptContext<uintptr_t>& Context) -> uintptr_t
    {
        constexpr uintptr_t Key = 0x5B7C3A91;
        return Context.RawValue ^ Key;
    };

    // array entry
    Callbacks.NameStorage.Chunk = [](const FDecryptIndexedContext<uintptr_t>& Context) -> uintptr_t
    {
        const uint32_t Shift = (Context.Index % 16) + 1;
        return Utils::Memory::Ror(Context.RawValue, Shift);
    };

    // string
    Callbacks.String.UTF8 = [](char* Data, int32 Len)
    {
        for (int32_t i = 0; i + 1 < Len; i += 2)
        {
            const uint16_t Swapped = Utils::Memory::Swap(*reinterpret_cast<uint16_t*>(Data + i));
            std::memcpy(Data + i, &Swapped, sizeof(Swapped));
        }

        for (int32_t i = 0; i < Len; i++)
            Data[i] = static_cast<char>(Data[i] ^ 0x5A);
    };

    // one value
    Callbacks.UObject.Class = [](const FDecryptContext<uintptr_t>& Context) -> uintptr_t
    {
        const uint64_t Key = GMemory->Read<uint64_t>(Context.Address + 0x8);
        return Context.RawValue ^ Utils::Memory::Ror(Key, 5);
    };
}
```

These hooks are what the dumper uses to read an encrypted value correctly. A profile that also
wants the generated SDK to decrypt the same value at runtime sets the matching
`Settings.CppGenerator.DecryptionUtils.DecryptXFuncBody` in `OverrideSettings` — see
[UsingTheSDK.md](UsingTheSDK.md#22-when-the-defaults-are-not-enough) for the full list and the three
profiles that already do this.

### Examples

See [Dumper/Profile/CustomProfiles/](Dumper/Profile/CustomProfiles/) for real profiles exercising every
decrypt hook type above.

## Credits & Thanks

[Dumper-7](https://github.com/Encryqed/Dumper-7)

[UEDumper](https://github.com/Spuckwaffel/UEDumper)
