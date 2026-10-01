# Using the SDK

## 1. Adding the SDK to your project

1. Copy the generated `CppSDK` folder into your project.
2. Include `SDK.hpp` to get everything, or include single package headers
   (`SDK/Engine_classes.hpp`) for much faster compile times.
3. Compile `SDK/Basic.cpp`, plus every `SDK/<Package>_functions.cpp` whose functions you call.
4. Build with **C++20**. clang and gcc are both supported.

## 2. Implementing the accessors

`SDK/Basic.cpp` ships with five stubs that all return `0`/`nullptr`. **The SDK does nothing until you
implement them.**

```c++
uintptr_t      InSDKUtils::GetImageBase();
uintptr_t      InSDKUtils::GetGNames();
TUObjectArray* InSDKUtils::GetGObjects();
std::string    InSDKUtils::GetNameByIndex(int32 Index);
UObject*       InSDKUtils::GetObjectByIndex(int32 Index);
```

Everything in `Offsets::` is added to `GetImageBase()`:

| Platform | `GetImageBase()` must return |
| --- | --- |
| Android | load address of the UE library |
| iOS | ASLR slide of the UE executable (`_dyld_get_image_vmaddr_slide(...)`) |

```c++
uintptr_t InSDKUtils::GetGNames()
{
    return GetImageBase() + Offsets::GNames;
}

TUObjectArray* InSDKUtils::GetGObjects()
{
    return reinterpret_cast<TUObjectArray*>(GetImageBase() + Offsets::GObjects);
}
```

`GetObjectByIndex` and `GetNameByIndex` depend on the layout the dumper detected for your game.

If you dump the same game often, fill these bodies into `CppGenerator::GetImageBaseFuncBody` and
relatives in [Dumper/Settings.h](Dumper/Settings.h) so every SDK is generated ready to build.

## 3. Using the SDK

### 3.1 Retrieving instances

```c++
SDK::UObject* Obj1 = SDK::UObject::FindObject<SDK::UObject>("ClassName Package.Outer.ObjectName");
SDK::UObject* Obj2 = SDK::UObject::FindObjectFast<SDK::UObject>("ObjectName");

/* No offset required, these replace GWorld/GEngine */
SDK::UWorld*  World  = SDK::UWorld::GetWorld();
SDK::UEngine* Engine = SDK::UEngine::GetEngine();
```

### 3.2 Calling functions

```c++
/* Non-static functions need an instance */
float OutX, OutY;
MyController->GetMousePosition(&OutX, &OutY);

/* Static functions are called on their DefaultObject automatically */
SDK::FName Name = SDK::UKismetStringLibrary::Conv_StringToName(L"DemoNetDriver");
```

### 3.3 Checking a UObject's type

```c++
/* Fastest, but limited to base types */
const bool bIsActor = Obj->IsA(SDK::EClassCastFlags::Actor);

/* Ideal for native classes */
const bool bIsPawn = Obj->IsA(SDK::APawn::StaticClass());

/* Works for every class, including Blueprint classes */
const bool bIsBP = Obj->IsA(SDK::ABP_Pawn_C::StaticName());
```

### 3.4 Casting

Unreal relies heavily on inheritance, so base-class pointers often hold child instances. Check the
type before casting:

```c++
if (MyController->Pawn->IsA(SDK::AGameSpecificPawn::StaticClass()))
{
    SDK::AGameSpecificPawn* Pawn = static_cast<SDK::AGameSpecificPawn*>(MyController->Pawn);
    Pawn->GameSpecificVariable = 30;
}
```

### 3.5 Iterating GObjects

```c++
SDK::TUObjectArray* GObjects = SDK::InSDKUtils::GetGObjects();

for (int32 i = 0; i < GObjects->Num(); i++)
{
    SDK::UObject* Obj = SDK::InSDKUtils::GetObjectByIndex(i);

    if (!Obj || Obj->IsDefaultObject())
        continue;

    if (Obj->IsA(SDK::EClassCastFlags::Pawn))
        printf("%s\n", Obj->GetFullName().c_str());
}
```

## 4. Code

An injected library that walks the persistent level:

```c++
#include <pthread.h>
#include <unistd.h>

#include "SDK/Engine_classes.hpp"

// Basic.cpp and Engine_functions.cpp are compiled into this library

static void* MainThread(void*)
{
    /* Give the engine time to initialise before touching GObjects */
    sleep(20);

    SDK::UWorld* World = SDK::UWorld::GetWorld();
    if (!World || !World->PersistentLevel)
        return nullptr;

    /* Every pointer along a chain should be null-checked */
    SDK::APlayerController* Controller = World->OwningGameInstance->LocalPlayers[0]->PlayerController;

    SDK::TArray<SDK::AActor*>& Actors = World->PersistentLevel->Actors;

    for (SDK::AActor* Actor : Actors)
    {
        if (!Actor || !Actor->IsA(SDK::EClassCastFlags::Pawn))
            continue;

        SDK::APawn* Pawn = static_cast<SDK::APawn*>(Actor);
        // Use Pawn here
    }

    return nullptr;
}

__attribute__((constructor)) static void OnLoad()
{
    pthread_t Thread;
    pthread_create(&Thread, nullptr, MainThread, nullptr);
}
```

## Notes

- Everything lives in `namespace SDK`, configurable via `CppGenerator::SDKNamespaceName`.
- A failing `static_assert` from `Assertions.inl` means the dumped layout disagrees with the
  compiler, so memory reads/writes would land on the wrong bytes.
