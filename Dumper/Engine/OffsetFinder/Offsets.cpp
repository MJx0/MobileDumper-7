#include "Offsets.h"

#include <cstring>
#include <random>

#include "../../Architecture/IArchDecoder.h"
#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Utils.h"
#include "../Unreal/NameArray.h"
#include "../Unreal/ObjectArray.h"

#include "OffsetFinder.h"

uintptr_t GObjects = 0;
uintptr_t GNames   = 0;
FLayouts GLayouts{};
FInGenOffsets GOffsets{};
FInSDKOffsets GInSDKOffsets{};

bool FInGenOffsets::Init(std::string& OutErrorString)
{
#define kSET_UE_OFFSET(Off, Val)                                                           \
	{                                                                                      \
		if (this->Off == OffsetFinder::OffsetNotFound)                                     \
		{                                                                                  \
			this->Off = Val;                                                               \
			GLogger.FmtWrite(ELogLevel::Info, "{} = 0x{:X}\n", #Off, uint32_t(this->Off)); \
		}                                                                                  \
	}

#define kINIT_UE_OFFSET(Off, Required, Init, OverrideIfInvalidVal)                                                            \
	{                                                                                                                         \
		GLogger.FmtWrite(ELogLevel::Info, "Initializing {}...\n", #Off);                                                      \
                                                                                                                              \
		if (this->Off == OffsetFinder::OffsetNotFound)                                                                        \
		{                                                                                                                     \
			Init;                                                                                                             \
		}                                                                                                                     \
                                                                                                                              \
		if (this->Off == OffsetFinder::OffsetNotFound && OverrideIfInvalidVal >= 0)                                           \
		{                                                                                                                     \
			GLogger.FmtWrite(ELogLevel::Info, "Overriding Invalid ( {} )...\n", #Off);                                        \
			this->Off = OverrideIfInvalidVal;                                                                                 \
		}                                                                                                                     \
                                                                                                                              \
		if (Required && this->Off == OffsetFinder::OffsetNotFound)                                                            \
		{                                                                                                                     \
			OutErrorString = fmt::format("Failed to initialize required offset ( {} )", #Off);                                \
			GLogger.FmtWrite(ELogLevel::Error, "{}\n", OutErrorString);                                                       \
			return false;                                                                                                     \
		}                                                                                                                     \
                                                                                                                              \
		if (this->Off == OffsetFinder::OffsetNotFound)                                                                        \
		{                                                                                                                     \
			GLogger.FmtWrite(ELogLevel::Warning, "Couldn't find \"{}\" but it's not required.\n", #Off, uint32_t(this->Off)); \
		}                                                                                                                     \
		else                                                                                                                  \
		{                                                                                                                     \
			GLogger.FmtWrite(ELogLevel::Info, "{} = 0x{:X}\n", #Off, uint32_t(this->Off));                                    \
		}                                                                                                                     \
	}

	// Objects validation
	{
		GLogger.FmtWrite(ELogLevel::Info, "Finding UObject.Index...\n");
		this->Init_UObject_Index();
		GLogger.FmtWrite(ELogLevel::Info, "UObject.Index = 0x{:X}\n", (uint32_t)this->UObject.Index);
		if (this->UObject.Index == OffsetFinder::OffsetNotFound)
		{
			OutErrorString = fmt::format("Failed to find required offset ( UObject.Index )\nMaybe ObjObjects pointer or layout is incorrect!");
			return false;
		}
	}

	kSET_UE_OFFSET(UObject.Vft, 0);
	kINIT_UE_OFFSET(UObject.Flags, true, Init_UObject_Flags(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UObject.Class, true, Init_UObject_Class(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UObject.Outer, true, Init_UObject_Outer(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UObject.Name, true, Init_UObject_Name(), OffsetFinder::OffsetNotFound);

	{
		GLogger.FmtWrite(ELogLevel::Info, "(Early) Initializing FName...\n");
		this->PreInit_FName();
		GLogger.FmtWrite(ELogLevel::Info, "(Early) FName.CompIdx = 0x{:X}\n", (uint32_t)GOffsets.FName.CompIdx);
		GLogger.FmtWrite(ELogLevel::Info, "(Early) FName.Number = 0x{:X}\n", (uint32_t)GOffsets.FName.Number);
		GLogger.FmtWrite(ELogLevel::Info, "(Early) FName.SizeOf = 0x{:X}\n", (uint32_t)GOffsets.FName.SizeOf);
	}

	/* CastFlags is needed by Init_UStruct_Children(); find early, then re-find authoritatively after FProperty detection. */
	if (this->UClass.CastFlags == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Info, "(Early) initializing UClass.CastFlags for UStruct.Children...\n");
		Init_UClass_CastFlags();
	}

	GLogger.FmtWrite(ELogLevel::Info, "(Early) UClass.CastFlags = 0x{:X}\n", uint32_t(this->UClass.CastFlags));

	kINIT_UE_OFFSET(UStruct.Children, true, Init_UStruct_Children(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(UField.Next, true, Init_UField_Next(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(UStruct.SuperStruct, true, Init_UStruct_SuperStruct(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UStruct.Size, true, Init_UStruct_Size(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UStruct.MinAlignment, true, Init_UStruct_MinAlignment(), OffsetFinder::OffsetNotFound);

	this->UClass.CastFlags = OffsetFinder::OffsetNotFound;
	kINIT_UE_OFFSET(UClass.CastFlags, true, Init_UClass_CastFlags(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(UStruct.StructBaseChain, false, Init_UStruct_StructBaseChain(), OffsetFinder::OffsetNotFound);

	if (InternalSettings::bUseFProperty)
	{
		GLogger.FmtWrite(ELogLevel::Info, "Game uses FProperty system\n");
		kINIT_UE_OFFSET(UStruct.ChildProperties, true, Init_UStruct_ChildProperties(), OffsetFinder::OffsetNotFound);

		kSET_UE_OFFSET(FField.Vft, 0);

		kINIT_UE_OFFSET(FField.Class, true, Init_FField_Class(), OffsetFinder::OffsetNotFound);
		kINIT_UE_OFFSET(FField.Owner, true, Init_FField_Owner(), OffsetFinder::OffsetNotFound);
		kINIT_UE_OFFSET(FField.Next, true, Init_FField_Next(), OffsetFinder::OffsetNotFound);
		kINIT_UE_OFFSET(FField.Name, true, Init_FField_Name(), OffsetFinder::OffsetNotFound);
		kINIT_UE_OFFSET(FField.EditorOnlyMetadata, false, Init_FField_EditorOnlyMetaData(), OffsetFinder::OffsetNotFound);

		kINIT_UE_OFFSET(FFieldClass.CastFlags, true, Init_FFieldClass_CastFlags(), OffsetFinder::OffsetNotFound);
		kINIT_UE_OFFSET(FFieldClass.Name, true, Init_FFieldClass_Name(), OffsetFinder::OffsetNotFound);
	}

	kINIT_UE_OFFSET(UClass.ClassDefaultObject, true, Init_UClass_ClassDefaultObject(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UClass.ImplementedInterfaces, false, Init_UClass_InitImplementedInterfaces(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(UEnum.Names, true, Init_UEnum_Names(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UEnum.UnderlyingType, false, Init_UEnum_UnderlayingType(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(UFunction.FunctionFlags, true, Init_UFunction_FunctionFlags(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UFunction.NumParams, false, Init_UFunction_NumParams(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UFunction.ParamSize, false, Init_UFunction_ParamSize(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(UFunction.ExecFunction, true, Init_UFunction_ExecFunction(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(Property.ElementSize, true, Init_Property_ElementSize(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(Property.ArrayDim, true, Init_Property_ArrayDim(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(Property.Offset_Internal, true, Init_Property_OffsetInternal(), OffsetFinder::OffsetNotFound);
	kINIT_UE_OFFSET(Property.PropertyFlags, true, Init_Property_PropertyFlags(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(BoolProperty.Base, true, Init_BoolProperty_Base(), OffsetFinder::OffsetNotFound);

	kINIT_UE_OFFSET(EnumProperty.Base, true, Init_EnumProperty_Base(), BoolProperty.Base);

	kSET_UE_OFFSET(Property.SizeOf, EnumProperty.Base);

	kINIT_UE_OFFSET(ObjectProperty.PropertyClass, true, Init_ObjectProperty_PropertyClass(), Property.SizeOf);

	kINIT_UE_OFFSET(ByteProperty.Enum, false, Init_ByteProperty_Enum(), Property.SizeOf);

	kINIT_UE_OFFSET(StructProperty.Struct, true, Init_StructProperty_Struct(), Property.SizeOf);

	kINIT_UE_OFFSET(DelegateProperty.SignatureFunction, true, Init_DelegateProperty_SignatureFunction(), Property.SizeOf);

	kINIT_UE_OFFSET(ArrayProperty.Inner, true, Init_ArrayProperty_Inner(), Property.SizeOf);

	kINIT_UE_OFFSET(SetProperty.ElementProp, true, Init_SetProperty_ElementProp(), Property.SizeOf);

	kINIT_UE_OFFSET(MapProperty.Base, true, Init_MapProperty_Base(), Property.SizeOf);

	{
		GLogger.FmtWrite(ELogLevel::Info, "(Late) Initializing FName...\n");
		this->PostInit_FName();
	}

	kSET_UE_OFFSET(FieldPathProperty.FieldClass, Property.SizeOf);

	kSET_UE_OFFSET(OptionalProperty.ValueProperty, Property.SizeOf);

	kSET_UE_OFFSET(ClassProperty.MetaClass, ObjectProperty.PropertyClass + sizeof(void*));

	kSET_UE_OFFSET(FInstancedStruct.ScriptStruct, 0x00);
	kSET_UE_OFFSET(FInstancedStruct.StructMemory, sizeof(void*));

	return true;

#undef kINIT_UE_OFFSET
}

/* UObject */
bool FInGenOffsets::Init_UObject_Flags()
{
	this->UObject.Flags = OffsetFinder::OffsetNotFound;

	constexpr auto EnumFlagValueToSearch = 0x43;

	/* We're looking for a commonly occuring flag and this number basically defines the minimum number that counts ad "commonly occuring". */
	constexpr auto MinNumFlagValuesRequiredAtOffset = 0xA0;

	for (int i = 0; i < 0x20; i++)
	{
		int Offset = 0x0;
		while (Offset != OffsetFinder::OffsetNotFound)
		{
			void* Ptr = ObjectArray::GetByIndex(i).GetAddress();

			// Look for 0x43 in this object, as it is a really common value for UObject::Flags
			Offset = OffsetFinder::FindOffset(std::vector{std::pair{Ptr, EnumFlagValueToSearch}}, Offset, 0x40);

			if (Offset == OffsetFinder::OffsetNotFound)
				break; // Early exit

			/* We're looking for a common flag. To check if the flag  is common we're checking the first 0x100 objects to see how often the flag occures at this offset. */
			int32 NumObjectsWithFlagAtOffset = 0x0;

			int Counter = 0;
			for (UEObject Obj : ObjectArray())
			{
				// Only check the (possible) flags of the first 0x100 objects
				if (Counter++ == 0x100)
					break;

				const int32 TypedValueAtOffset = GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Obj.GetAddress()) + Offset);

				if (TypedValueAtOffset == EnumFlagValueToSearch)
					NumObjectsWithFlagAtOffset++;
			}

			if (NumObjectsWithFlagAtOffset > MinNumFlagValuesRequiredAtOffset)
			{
				this->UObject.Flags = Offset;
				return true;
			}
		}
	}

	return false;
}

bool FInGenOffsets::Init_UObject_Index()
{
	this->UObject.Index = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, int32_t>> Infos;

	Infos.emplace_back(ObjectArray::GetByIndex(0x055).GetAddress(), 0x055);
	Infos.emplace_back(ObjectArray::GetByIndex(0x123).GetAddress(), 0x123);

	this->UObject.Index = OffsetFinder::FindOffset<4>(Infos, sizeof(void*)); // Skip VTable
	return this->UObject.Index != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UObject_Class()
{
	this->UObject.Class = OffsetFinder::OffsetNotFound;

	/* Checks for a pointer that points to itself in the end. The UObject::Class pointer of "Class CoreUObject.Class" will point to "Class CoreUObject.Class". */
	auto IsValidCyclicUClassPtrOffset = [](const uint8_t* ObjA, const uint8_t* ObjB, int32_t ClassPtrOffset)
	{
		/* Will be advanced before they are used. */
		const uint8_t* NextClassA = ObjA;
		const uint8_t* NextClassB = ObjB;

		for (int MaxLoopCount = 0; MaxLoopCount < 0x10; MaxLoopCount++)
		{
			const uint8_t* CurrentClassA = NextClassA;
			const uint8_t* CurrentClassB = NextClassB;

			NextClassA = reinterpret_cast<const uint8_t*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(NextClassA) + ClassPtrOffset));
			NextClassB = reinterpret_cast<const uint8_t*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(NextClassB) + ClassPtrOffset));

			/* If this was UObject::Class it would never be invalid. The pointer would simply point to itself.*/
			if (!NextClassA || !NextClassB || !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(NextClassA)) || !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(NextClassB)))
				return false;

			if (CurrentClassA == NextClassA && CurrentClassB == NextClassB)
				return true;
		}

		return false;
	};

	const uint8_t* const ObjA = static_cast<const uint8_t*>(ObjectArray::GetByIndex(0x055).GetAddress());
	const uint8_t* const ObjB = static_cast<const uint8_t*>(ObjectArray::GetByIndex(0x123).GetAddress());

	int32_t Offset = 0;
	while (Offset != OffsetFinder::OffsetNotFound)
	{
		Offset = OffsetFinder::GetValidPointerOffset<true>(ObjA, ObjB, Offset + sizeof(void*), 0x50);

		if (IsValidCyclicUClassPtrOffset(ObjA, ObjB, Offset))
		{
			this->UObject.Class = Offset;
			return true;
		}
	}

	return false;
}

bool FInGenOffsets::Init_UObject_Name()
{
	this->UObject.Name = OffsetFinder::OffsetNotFound;

	if (this->UObject.Class == OffsetFinder::OffsetNotFound || this->UObject.Index == OffsetFinder::OffsetNotFound || this->UObject.Outer == OffsetFinder::OffsetNotFound || this->UObject.Flags == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_UObject_Name: One or more required offsets are not set.\n");
		return false;
	}

	auto IsPotentiallyValidOffset = [this](int32 Offset) -> bool
	{
		// Make sure 0x4 aligned Offsets are neither the start, nor the middle of a pointer-member. Irrelevant for 32-bit, because the 2nd check will be 0x2 aligned then.
		return Offset != this->UObject.Class && Offset != (this->UObject.Class + int32(sizeof(void*) / 2)) && Offset != this->UObject.Outer && Offset != (this->UObject.Outer + int32(sizeof(void*) / 2)) && Offset != this->UObject.Flags && Offset != this->UObject.Index && Offset != this->UObject.Vft && Offset != (this->UObject.Vft + int32(sizeof(void*) / 2));
	};

	this->UObject.Name = OffsetFinder::FindNameOffsetForSomeClass(IsPotentiallyValidOffset, ObjectArray().begin(), ObjectArray().end(), ObjectArray::Num());
	return this->UObject.Name != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UObject_Outer()
{
	this->UObject.Outer = OffsetFinder::OffsetNotFound;

	if (this->UObject.Class == OffsetFinder::OffsetNotFound || this->UObject.Index == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_UObject_Outer: One or more required offsets are not set.\n");
		return false;
	}

	int32_t LowestFoundOffset = 0xFFFF;

	const int32 NumObjects = ObjectArray::Num();
	if (NumObjects < 2)
		return false;

	std::mt19937 Rng(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ObjectArray::GetByIndex(0).GetAddress())));
	const int32_t UpperBound = (NumObjects - 1 < 0x3FF) ? (NumObjects - 1) : 0x3FF;
	std::uniform_int_distribution<int32_t> Dist(0, UpperBound);

	// loop a few times in case we accidentally choose a UPackage (which doesn't have an Outer) to find Outer
	for (int i = 0; i < 0x10; i++)
	{
		int32_t Offset = 0;

		const int32_t IndexA = Dist(Rng);
		int32_t IndexB       = Dist(Rng);
		if (IndexB == IndexA)
			IndexB = (IndexA + 1) % (UpperBound + 1);

		const void* ObjA = ObjectArray::GetByIndex(IndexA).GetAddress();
		const void* ObjB = ObjectArray::GetByIndex(IndexB).GetAddress();

		while (Offset != OffsetFinder::OffsetNotFound)
		{
			Offset = OffsetFinder::GetValidPointerOffset(ObjA, ObjB, Offset + sizeof(void*), 0x50);

			// Make sure we didn't re-find the Class offset or Index (if the Index filed is a valid pionter for some ungodly reason).
			if (Offset != this->UObject.Class && Offset != this->UObject.Index)
				break;
		}

		if (Offset != OffsetFinder::OffsetNotFound && Offset < LowestFoundOffset)
			LowestFoundOffset = Offset;
	}

	this->UObject.Outer = LowestFoundOffset == 0xFFFF ? OffsetFinder::OffsetNotFound : LowestFoundOffset;
	return this->UObject.Outer != OffsetFinder::OffsetNotFound;
}

void FInGenOffsets::PreInit_FName()
{
	this->FName.CompIdx = 0x0;

	/*
	 * Scan all objects and accumulate three raw int32 signals at fixed offsets from each FName address.
	 * No field-gap arithmetic (Outer - Name) is used; Name can be at any position in UObject.
	 *
	 * V0 = CompIdx (always at +0x00)
	 * V4 = DisplayIdx (case-pres), Number (standard), or next-UObject-field (outline-only)
	 * V8 = Number (case-pres), or next-UObject-field (standard / case-pres+outline)
	 *
	 * nEq04  : V0 == V4 → true for case-preserving (CompIdx == DisplayIdx for ~all objects)
	 * nLow04 : V4 ∈ [1,4] → high when a real Number field lives at +0x04
	 * nLow08 : V8 ∈ [1,4] → high when a real Number field lives at +0x08 (case-preserving)
	 */
	int32 NumSampled = 0;
	int32 nEq04      = 0;
	int32 nLow04     = 0;
	int32 nLow08     = 0;

	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (!Obj)
			continue;

		const uintptr_t NameAddr = reinterpret_cast<uintptr_t>(Obj.GetFName().GetAddress());

		const int32 V0 = GMemory->Read<int32>(NameAddr);
		const int32 V4 = GMemory->Read<int32>(NameAddr + 0x4);
		const int32 V8 = GMemory->Read<int32>(NameAddr + 0x8);

		if (V0 <= 0)
			continue;

		nEq04  += (V0 == V4) ? 1 : 0;
		nLow04 += (V4 >= 1 && V4 <= 4) ? 1 : 0;
		nLow08 += (V8 >= 1 && V8 <= 4) ? 1 : 0;
		NumSampled++;
	}

	if (NumSampled == 0)
		return;

	/*
	 * Games without FNAME_OUTLINE_NUMBER have a min. percentage of 6% of all object-names for which
	 * FName::Number is in a [1...4] range. On games with FNAME_OUTLINE_NUMBER the integer at that
	 * offset is random data, landing in [1,4] about 2% (or less) of the time.
	 * The 3% threshold gives both cases a buffer zone.
	 *
	 * PostInit_FName() provides a reliable correction via reflected property ElementSize
	 * for edge cases where the object pool is too small for the statistics to be decisive.
	 */
	const bool bCasePreserving = (nEq04 > NumSampled / 2);
	const int32 Threshold      = static_cast<int32>(NumSampled * 0.03f);

	if (bCasePreserving && nLow08 > Threshold) /* WITH_CASE_PRESERVING_NAME */
	{
		InternalSettings::bUseCasePreservingName = true;

		if (this->FName.Number == OffsetFinder::OffsetNotFound)
			this->FName.Number = 0x8;
		this->FName.SizeOf = 0xC;
	}
	else if (bCasePreserving) /* WITH_CASE_PRESERVING_NAME + FNAME_OUTLINE_NUMBER */
	{
		InternalSettings::bUseCasePreservingName = true;
		InternalSettings::bUseOutlineNumberName  = true;

		if (this->FName.Number == OffsetFinder::OffsetNotFound)
			this->FName.Number = OffsetFinder::OffsetNotFound;
		this->FName.SizeOf = 0x8;
	}
	else if (nLow04 > Threshold) /* Default */
	{
		if (this->FName.Number == OffsetFinder::OffsetNotFound)
			this->FName.Number = 0x4;
		this->FName.SizeOf = 0x8;
	}
	else /* FNAME_OUTLINE_NUMBER */
	{
		InternalSettings::bUseOutlineNumberName = true;

		if (this->FName.Number == OffsetFinder::OffsetNotFound)
			this->FName.Number = OffsetFinder::OffsetNotFound;
		this->FName.SizeOf = 0x4;
	}
}

void FInGenOffsets::PostInit_FName()
{
	const UEClass PlayerStart = ObjectArray::FindClassFast("PlayerStart");

	const int32 FNameSize = PlayerStart.FindMember("PlayerStartTag").GetSize();

	/* Nothing to do for us, everything is fine! */
	if (this->FName.SizeOf == FNameSize)
	{
		GLogger.FmtWrite(ELogLevel::Info, "PostInit_FName: FName.SizeOf is already correct, nothing to do.\n");
		return;
	}

	const uint8* NameAddress = static_cast<const uint8*>(PlayerStart.GetFName().GetAddress());

	const int32 FNameFirstInt /* ComparisonIndex */        = GMemory->Read<int32>(reinterpret_cast<uintptr_t>(NameAddress));
	const int32 FNameSecondInt /* [Number/DisplayIndex] */ = GMemory->Read<int32>(reinterpret_cast<uintptr_t>(NameAddress) + 0x4);

	if (FNameSize == 0x8 && FNameFirstInt == FNameSecondInt) /* WITH_CASE_PRESERVING_NAME + FNAME_OUTLINE_NUMBER */
	{
		InternalSettings::bUseCasePreservingName = true;
		InternalSettings::bUseOutlineNumberName  = true;

		this->FName.Number = -0x1;
		this->FName.SizeOf = 0x8;
	}
	else if (FNameSize > 0x8) /* WITH_CASE_PRESERVING_NAME */
	{
		InternalSettings::bUseOutlineNumberName  = false;
		InternalSettings::bUseCasePreservingName = true;

		this->FName.Number = FNameFirstInt == FNameSecondInt ? 0x8 : 0x4;
		this->FName.SizeOf = 0xC;
	}
	else if (FNameSize == 0x4) /* FNAME_OUTLINE_NUMBER */
	{
		InternalSettings::bUseOutlineNumberName  = true;
		InternalSettings::bUseCasePreservingName = false;

		this->FName.Number = -0x1;
		this->FName.SizeOf = 0x4;
	}
	else /* Default */
	{
		InternalSettings::bUseOutlineNumberName  = false;
		InternalSettings::bUseCasePreservingName = false;

		this->FName.Number = 0x4;
		this->FName.SizeOf = 0x8;
	}

	GLogger.FmtWrite(ELogLevel::Info, "(Late) FName.CompIdx = 0x{:X}\n", (uint32_t)GOffsets.FName.CompIdx);
	GLogger.FmtWrite(ELogLevel::Info, "(Late) FName.Number = 0x{:X}\n", (uint32_t)GOffsets.FName.Number);
	GLogger.FmtWrite(ELogLevel::Info, "(Late) FName.SizeOf = 0x{:X}\n", (uint32_t)GOffsets.FName.SizeOf);
}

bool FInGenOffsets::Init_UField_Next()
{
	this->UField.Next = OffsetFinder::OffsetNotFound;

	if (this->UObject.Index == OffsetFinder::OffsetNotFound || this->UObject.Name == OffsetFinder::OffsetNotFound ||
	    this->UObject.Flags == OffsetFinder::OffsetNotFound || this->UObject.Outer == OffsetFinder::OffsetNotFound ||
	    this->UObject.Class == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_UField_Next: One or more required offsets are not set.\n");
		return false;
	}

	const void* KismetSystemLibraryChild = ObjectArray::FindObjectFast<UEStruct>("KismetSystemLibrary").GetChild().GetAddress();
	const void* KismetStringLibraryChild = ObjectArray::FindObjectFast<UEStruct>("KismetStringLibrary").GetChild().GetAddress();

	const auto HighestUObjectOffset = std::max({this->UObject.Index, this->UObject.Name, this->UObject.Flags, this->UObject.Outer, this->UObject.Class});

	this->UField.Next = OffsetFinder::GetValidPointerOffset(KismetSystemLibraryChild, KismetStringLibraryChild, Utils::Align(HighestUObjectOffset + 0x4, static_cast<int>(sizeof(void*))), 0x60);
	return this->UField.Next != OffsetFinder::OffsetNotFound;
}


bool FInGenOffsets::Init_FField_Name()
{
	this->FField.Name = OffsetFinder::OffsetNotFound;

	UEFField GuidChild   = ObjectArray::FindStructFast("Guid").GetChildProperties();
	UEFField VectorChild = ObjectArray::FindStructFast("Vector").GetChildProperties();

	for (int32 Off = 0; Off < 0x40; Off += 4)
	{
		std::string GuidChildName   = GuidChild.GetName();
		std::string VectorChildName = VectorChild.GetName();

		if ((GuidChildName == "A" || GuidChildName == "D") && (VectorChildName == "X" || VectorChildName == "Z"))
		{
			this->FField.Name = Off;
			return true;
		}
	}

	auto IsPotentiallyValidOffset = [this](int32 Offset) -> bool
	{
		// Make sure 0x4 aligned Offsets are neither the start, nor the middle of a pointer-member. Irrelevant for 32-bit, because the 2nd check will be 0x2 aligned then.
		return Offset != this->FField.Class && Offset != (this->FField.Class + int32(sizeof(void*) / 2)) && Offset != this->FField.Owner && Offset != (this->FField.Owner + int32(sizeof(void*) / 2)) && Offset != this->FField.Next && Offset != (this->FField.Next + int32(sizeof(void*) / 2)) && Offset != this->FField.Vft && Offset != int32(this->FField.Vft + (sizeof(void*) / 2));
	};

	AllFFieldIterator TmpIt;

	this->FField.Name = OffsetFinder::FindNameOffsetForSomeClass(IsPotentiallyValidOffset, TmpIt.begin(), TmpIt.end(), ObjectArray::Num());
	return this->FField.Name != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_FField_Class()
{
	this->FField.Class = OffsetFinder::OffsetNotFound;

	const UEFField GuidChild   = ObjectArray::FindStructFast("Guid").GetChildProperties();
	const UEFField VectorChild = ObjectArray::FindStructFast("Vector").GetChildProperties();

	this->FField.Class = OffsetFinder::GetValidPointerOffset<false>(GuidChild.GetAddress(), VectorChild.GetAddress(), sizeof(void*), 0x30, true);
	return this->FField.Class != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_FField_Owner()
{
	this->FField.Owner = OffsetFinder::OffsetNotFound;

	if (this->FField.Class == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_FField_Owner: One or more required offsets are not set.\n");
		return false;
	}

	if (!InternalSettings::bUseFProperty)
		return false;

	const UEFField GuidChild   = ObjectArray::FindStructFast("Guid").GetChildProperties();
	const UEFField VectorChild = ObjectArray::FindStructFast("Vector").GetChildProperties();
	const UEFField ColorChild  = ObjectArray::FindStructFast("Color").GetChildProperties();

	if (!GuidChild || !VectorChild || !ColorChild)
		return false;

	const uintptr_t GuidAddr   = reinterpret_cast<uintptr_t>(ObjectArray::FindStructFast("Guid").GetAddress());
	const uintptr_t VectorAddr = reinterpret_cast<uintptr_t>(ObjectArray::FindStructFast("Vector").GetAddress());
	const uintptr_t ColorAddr  = reinterpret_cast<uintptr_t>(ObjectArray::FindStructFast("Color").GetAddress());

	if (!GuidAddr || !VectorAddr || !ColorAddr)
		return false;

	for (int32 Off = static_cast<int32>(sizeof(void*)); Off <= 0x38; Off += static_cast<int32>(sizeof(void*)))
	{
		if (Off == this->FField.Class)
			continue;

		const uintptr_t ValA = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(GuidChild.GetAddress()) + Off);
		const uintptr_t ValB = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(VectorChild.GetAddress()) + Off);
		const uintptr_t ValC = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(ColorChild.GetAddress()) + Off);

		// Old FFieldVariant { void* Container; bool bIsUObject; }: Container holds the raw UObject pointer.
		if (ValA == GuidAddr && ValB == VectorAddr && ValC == ColorAddr)
		{
			InternalSettings::bUseMaskForFieldOwner = false;
			this->FField.Owner                      = Off;
			return true;
		}

		// New FFieldVariant (UE 5.1.1+): Container = ptr | isUObject; low bit flags UObject ownership.
		// UObject pointers are pointer-aligned so bit 0 is always 0, making this test unambiguous.
		constexpr uintptr_t kIsUObjectBit = 0x1;
		if ((ValA & ~kIsUObjectBit) == GuidAddr && (ValB & ~kIsUObjectBit) == VectorAddr && (ValC & ~kIsUObjectBit) == ColorAddr)
		{
			InternalSettings::bUseMaskForFieldOwner = true;
			this->FField.Owner                      = Off;
			return true;
		}
	}

	return false;
}

bool FInGenOffsets::Init_FField_Next()
{
	this->FField.Next = OffsetFinder::OffsetNotFound;

	if (this->FField.Class == OffsetFinder::OffsetNotFound || this->FField.Owner == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_FField_Next: One or more required offsets are not set.\n");
		return false;
	}

	const void* GuidChildren   = ObjectArray::FindStructFast("Guid").GetChildProperties().GetAddress();
	const void* VectorChildren = ObjectArray::FindStructFast("Vector").GetChildProperties().GetAddress();

	for (int32 Off = static_cast<int32>(sizeof(void*)); Off <= 0x48; Off += static_cast<int32>(sizeof(void*)))
	{
		// Class and Owner are also valid vtable-bearing pointers — skip both to avoid false positives.
		if (Off == this->FField.Class || Off == this->FField.Owner)
			continue;

		const uintptr_t ValA = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(GuidChildren) + Off);
		const uintptr_t ValB = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(VectorChildren) + Off);

		const bool bAValid = GMemory->IsAddressReadable(ValA) && GMemory->IsAddressReadable(GMemory->Read<uintptr_t>(ValA));
		const bool bBValid = GMemory->IsAddressReadable(ValB) && GMemory->IsAddressReadable(GMemory->Read<uintptr_t>(ValB));

		if (bAValid && bBValid)
		{
			this->FField.Next = Off;
			return true;
		}
	}

	return false;
}

bool FInGenOffsets::Init_FField_EditorOnlyMetaData()
{
	this->FField.EditorOnlyMetadata = OffsetFinder::OffsetNotFound;

	const UEFField GuidChild1 = ObjectArray::FindStructFast("Guid").GetChildProperties();
	const UEFField GuidChild2 = GuidChild1.GetNext();

	auto IsPotentiallyValidOffset = [this](int32 Offset) -> bool
	{
		// Make sure 0x4 aligned Offsets are neither the start, nor the middle of a pointer-member. Irrelevant for 32-bit, because the 2nd check will be 0x2 aligned then.
		return Offset != this->FField.Class && Offset != (this->FField.Class + int32(sizeof(void*) / 2)) && Offset != this->FField.Owner && Offset != (this->FField.Owner + int32(sizeof(void*) / 2)) && Offset != this->FField.Next && Offset != (this->FField.Next + int32(sizeof(void*) / 2)) && Offset != this->FField.Vft && Offset != (this->FField.Vft + int32(sizeof(void*) / 2)) && Offset != this->FField.Name && Offset != (this->FField.Name + this->FName.SizeOf);
	};

	int32 StartingOffset = 0x8;

	// Only pay attention to the 0x8 aligned size-options of FName, since the pair in the TMap is 0x8 aligned because of FString
	struct alignas(0x4) Name08Byte
	{
		uint8 Pad[0x08];
	};
	struct alignas(0x4) Name16Byte
	{
		uint8 Pad[0x10];
	};

	static auto AreValidMetadataMaps = []<typename NameType>(uintptr_t Addr1, uintptr_t Addr2) -> bool
	{
		TMap<NameType, FString> Map1 = GMemory->Read<TMap<NameType, FString>>(Addr1);
		TMap<NameType, FString> Map2 = GMemory->Read<TMap<NameType, FString>>(Addr2);

		if (!Map1.IsValid() || !Map2.IsValid())
			return false;

		auto GetFirstFString = [](const TMap<NameType, FString>& Map) -> FString
		{
			for (int32 i = 0; i < Map.NumAllocated(); i++)
			{
				if (!Map.GetAllocationFlags()[i])
					continue;
				return GMemory->Read<FString>(Map.GetElementAddress(i) + sizeof(NameType));
			}
			return {};
		};

		return GetFirstFString(Map1).IsValid() && GetFirstFString(Map2).IsValid();
	};

	while (true)
	{
		if (!IsPotentiallyValidOffset(StartingOffset))
		{
			StartingOffset += sizeof(void*);
			continue;
		}

		const int32 Offset = OffsetFinder::GetValidPointerOffset<false>(GuidChild1.GetAddress(), GuidChild2.GetAddress(), StartingOffset, 0x40);
		StartingOffset     = Offset + sizeof(void*);

		if (Offset == OffsetFinder::OffsetNotFound)
			break;

		if (!IsPotentiallyValidOffset(Offset))
			continue;

		const uintptr_t RemoteAddr1 = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(GuidChild1.GetAddress()) + Offset);
		const uintptr_t RemoteAddr2 = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(GuidChild2.GetAddress()) + Offset);

		if (!RemoteAddr1 || !RemoteAddr2 || !GMemory->IsAddressReadable(RemoteAddr1) || !GMemory->IsAddressReadable(RemoteAddr2))
			continue;

		// Read as Name08Byte for structural validity checks (TMap header layout is NameType-independent)
		TMap<Name08Byte, FString> LocalMap1 = GMemory->Read<TMap<Name08Byte, FString>>(RemoteAddr1);
		TMap<Name08Byte, FString> LocalMap2 = GMemory->Read<TMap<Name08Byte, FString>>(RemoteAddr2);

		if (!LocalMap1.IsValid() || !LocalMap2.IsValid())
			continue;

		if (LocalMap1.Num() <= 0 || LocalMap2.Num() <= 0)
			continue;

		if (LocalMap1.Num() >= 0x10 || LocalMap2.Num() >= 0x10)
			continue;

		auto GetDataPtrOfArrayInMap = [](const auto& Map) -> const void*
		{
			// TMap's backing TArray sits at offset 0x0, so the first pointer in the map is
			// that TArray's Data member.
			return *reinterpret_cast<const void* const*>(&Map);
		};

		if (!GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(GetDataPtrOfArrayInMap(LocalMap1))) || !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(GetDataPtrOfArrayInMap(LocalMap2))))
			continue;

		if (this->FName.SizeOf <= 0x8)
		{
			if (AreValidMetadataMaps.template operator()<Name08Byte>(RemoteAddr1, RemoteAddr2))
			{
				this->FField.EditorOnlyMetadata = Offset;
				return true;
			}
		}
		else
		{
			if (AreValidMetadataMaps.template operator()<Name16Byte>(RemoteAddr1, RemoteAddr2))
			{
				this->FField.EditorOnlyMetadata = Offset;
				return true;
			}
		}
	}

	return false;
}

bool FInGenOffsets::Init_FFieldClass_CastFlags()
{
	this->FFieldClass.CastFlags = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, EClassCastFlags>> Infos;

	const UEFField GuidChild   = ObjectArray::FindStructFast("Guid").GetChildProperties();
	const UEFField ColourChild = ObjectArray::FindStructFast("Color").GetChildProperties();

	Infos.push_back({GuidChild.GetClass().GetAddress(), EClassCastFlags::Field | EClassCastFlags::Property | EClassCastFlags::NumericProperty | EClassCastFlags::IntProperty});
	Infos.push_back({ColourChild.GetClass().GetAddress(), EClassCastFlags::Field | EClassCastFlags::Property | EClassCastFlags::NumericProperty | EClassCastFlags::ByteProperty});

	this->FFieldClass.CastFlags = OffsetFinder::FindOffset(Infos, sizeof(void*), 0x30);
	return this->FFieldClass.CastFlags != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_FFieldClass_Name()
{
	this->FFieldClass.Name = OffsetFinder::OffsetNotFound;

	std::vector<uintptr_t> Classes;
	auto TryAdd = [&](const char* StructName)
	{
		const UEFField Child = ObjectArray::FindStructFast(StructName).GetChildProperties();
		if (!Child)
			return;
		const void* Addr = Child.GetClass().GetAddress();
		if (Addr && GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Addr)))
			Classes.push_back(reinterpret_cast<uintptr_t>(Addr));
	};

	TryAdd("Guid");
	TryAdd("Vector");
	TryAdd("Color");

	if (Classes.empty())
		return false;

	for (int32 Off = 0x00; Off <= int32(sizeof(void*) * 5); Off += 0x04)
	{
		bool bAllValid = true;
		for (const uintptr_t& Entry : Classes)
		{
			const int32 CmpIdx = GMemory->Read<int32>(Entry + Off);
			if (CmpIdx <= 0)
			{
				bAllValid = false;
				break;
			}

			const std::string Name = NameArray::GetNameEntry(CmpIdx).GetString();
			if (Name.size() < 5 || Name.find("Property") == std::string::npos)
			{
				bAllValid = false;
				break;
			}
		}

		if (bAllValid)
		{
			this->FFieldClass.Name = Off;
			return true;
		}
	}

	return false;
}


// This function assumes that the EnumObj passed in is valid and that the values of the enum are starting at 0
void InializeUEnumSettings(const void* EnumObj, const uint32_t UEnumNumValuesOffset)
{
	constexpr uintptr_t UE5EnumDynamicAllocationTag = 0x1;

	{
		// On UE5.6+ there are two arrays, one for just the FName*/UTF8Char* and one for just int64* values. Check if the array before NumValues contains just Values or TPair<Name, Value>.
		const uintptr_t PossibleValueArrayTaggedPtr = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(EnumObj) + UEnumNumValuesOffset - sizeof(void*));
		const uintptr_t PossibleValueArrayAddr      = PossibleValueArrayTaggedPtr & ~UE5EnumDynamicAllocationTag;

		if (GMemory->IsAddressReadable(PossibleValueArrayAddr) && GMemory->IsAddressReadable(PossibleValueArrayAddr + sizeof(int64)) && GMemory->IsAddressReadable(PossibleValueArrayAddr + 2 * sizeof(int64)))
		{
			if (GMemory->Read<int64>(PossibleValueArrayAddr) == 0 && GMemory->Read<int64>(PossibleValueArrayAddr + sizeof(int64)) == 1 && GMemory->Read<int64>(PossibleValueArrayAddr + 2 * sizeof(int64)) == 2)
			{
				InternalSettings::bIsNewUE5EnumNamesContainer = true;
				return;
			}
		}
	}

	using ValueType = std::conditional_t<sizeof(void*) == 0x8, int64, int32>;
	struct Name08Byte
	{
		uint8 Pad[0x08];
	};
	struct Name16Byte
	{
		uint8 Pad[0x10];
	};

	const uint8* ArrayAddress = static_cast<const uint8*>(EnumObj) + UEnumNumValuesOffset - 0x8;

	auto InitEnumSettings = []<typename NameType>(const TArray<TPair<NameType, ValueType>>& ArrayOfNameValuePairs)
	{
		if (ArrayOfNameValuePairs[1].Second == 1)
			return;

		// Fallback: the full-width value read didn't look like a sequential index, so check
		// whether only the low byte of that same slot holds one (the rare pre-UE4.15-era
		// byte-sized enum value packed into an otherwise wider slot).
		if (static_cast<uint8_t>(ArrayOfNameValuePairs[1].Second) == 1 && static_cast<uint8_t>(ArrayOfNameValuePairs[2].Second) == 2)
		{
			InternalSettings::bIsSmallEnumValue = true;
			return;
		}

		InternalSettings::bIsEnumNameOnly = true;
	};

	if (InternalSettings::bUseCasePreservingName)
	{
		InitEnumSettings(GMemory->Read<TArray<TPair<Name16Byte, ValueType>>>(reinterpret_cast<uintptr_t>(ArrayAddress)));
	}
	else
	{
		InitEnumSettings(GMemory->Read<TArray<TPair<Name08Byte, ValueType>>>(reinterpret_cast<uintptr_t>(ArrayAddress)));
	}
}

bool FInGenOffsets::Init_UEnum_Names()
{
	this->UEnum.Names = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, int32_t>> Infos;

	Infos.push_back({ObjectArray::FindObjectFast("ENetRole", EClassCastFlags::Enum).GetAddress(), 0x5});
	Infos.push_back({ObjectArray::FindObjectFast("ETraceTypeQuery", EClassCastFlags::Enum).GetAddress(), 0x22});

	int UEnumNumValuesOffset = OffsetFinder::FindOffset(Infos);

	if (UEnumNumValuesOffset == OffsetFinder::OffsetNotFound)
	{
		Infos[0] = {ObjectArray::FindObjectFast("EAlphaBlendOption", EClassCastFlags::Enum).GetAddress(), 0x10};
		Infos[1] = {ObjectArray::FindObjectFast("EUpdateRateShiftBucket", EClassCastFlags::Enum).GetAddress(), 0x8};

		UEnumNumValuesOffset = OffsetFinder::FindOffset(Infos);
	}

	if (UEnumNumValuesOffset == OffsetFinder::OffsetNotFound)
		return false;

	InializeUEnumSettings(Infos[0].first, UEnumNumValuesOffset);

	this->UEnum.Names = UEnumNumValuesOffset - static_cast<int32>(sizeof(void*));
	return true;
}

bool FInGenOffsets::Init_UEnum_UnderlayingType()
{
	this->UEnum.UnderlyingType = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, UEEnum::EUnderlyingType>> Infos;
	Infos.push_back({ObjectArray::FindObjectFast("ENetRole", EClassCastFlags::Enum).GetAddress(), UEEnum::EUnderlyingType::uint8});
	Infos.push_back({ObjectArray::FindObjectFast("ETraceTypeQuery", EClassCastFlags::Enum).GetAddress(), UEEnum::EUnderlyingType::uint8});

	int UEnumUnderlayingTypeOffset = OffsetFinder::FindOffset(Infos, OffsetFinder::OffsetFinderMinValue, 0xA0);

	if (UEnumUnderlayingTypeOffset == OffsetFinder::OffsetNotFound)
	{
		Infos[0] = {ObjectArray::FindObjectFast("EAlphaBlendOption", EClassCastFlags::Enum).GetAddress(), UEEnum::EUnderlyingType::uint8};
		Infos[1] = {ObjectArray::FindObjectFast("EUpdateRateShiftBucket", EClassCastFlags::Enum).GetAddress(), UEEnum::EUnderlyingType::uint8};

		UEnumUnderlayingTypeOffset = OffsetFinder::FindOffset(Infos, OffsetFinder::OffsetFinderMinValue, 0xA0);
	}

	InternalSettings::bHasUnderlayingTypeInUEnum = UEnumUnderlayingTypeOffset != OffsetFinder::OffsetNotFound;

	this->UEnum.UnderlyingType = UEnumUnderlayingTypeOffset;
	return this->UEnum.UnderlyingType != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UStruct_SuperStruct()
{
	this->UStruct.SuperStruct = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, void*>> Infos;

	Infos.push_back({ObjectArray::FindObjectFast("Struct").GetAddress(), ObjectArray::FindObjectFast("Field").GetAddress()});
	Infos.push_back({ObjectArray::FindObjectFast("Class").GetAddress(), ObjectArray::FindObjectFast("Struct").GetAddress()});

	// Thanks to the ue4 dev who decided UStruct should be spelled Ustruct
	if (Infos[0].first == nullptr)
		Infos[0].first = Infos[1].second = ObjectArray::FindObjectFast("struct").GetAddress();

	this->UStruct.SuperStruct = OffsetFinder::FindOffset(Infos);
	return this->UStruct.SuperStruct != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UStruct_Children()
{
	this->UStruct.Children = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, void*>> Infos;

	if (ObjectArray::FindObject("ObjectProperty Engine.Controller.TransformComponent", EClassCastFlags::ObjectProperty))
	{
		Infos.push_back({ObjectArray::FindObjectFast("Vector").GetAddress(), ObjectArray::FindObjectFastInOuter("X", "Vector").GetAddress()});
		Infos.push_back({ObjectArray::FindObjectFast("Vector4").GetAddress(), ObjectArray::FindObjectFastInOuter("X", "Vector4").GetAddress()});
		Infos.push_back({ObjectArray::FindObjectFast("Vector2D").GetAddress(), ObjectArray::FindObjectFastInOuter("X", "Vector2D").GetAddress()});
		Infos.push_back({ObjectArray::FindObjectFast("Guid").GetAddress(), ObjectArray::FindObjectFastInOuter("A", "Guid").GetAddress()});

		this->UStruct.Children = OffsetFinder::FindOffset(Infos, 0x14);
		return this->UStruct.Children != OffsetFinder::OffsetNotFound;
	}

	Infos.push_back({ObjectArray::FindObjectFast("PlayerController").GetAddress(), ObjectArray::FindObjectFastInOuter("WasInputKeyJustReleased", "PlayerController").GetAddress()});
	Infos.push_back({ObjectArray::FindObjectFast("Controller").GetAddress(), ObjectArray::FindObjectFastInOuter("UnPossess", "Controller").GetAddress()});

	InternalSettings::bUseFProperty = true;

	this->UStruct.Children = OffsetFinder::FindOffset(Infos);
	return this->UStruct.Children != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UStruct_ChildProperties()
{
	this->UStruct.ChildProperties = OffsetFinder::OffsetNotFound;

	const void* ObjA = ObjectArray::FindStructFast("Color").GetAddress();
	const void* ObjB = ObjectArray::FindStructFast("Guid").GetAddress();

	const int32 PtrSize = static_cast<int32>(sizeof(void*));

	auto IsUObject = [this](uintptr_t Val) -> bool
	{
		const int32 Idx = GMemory->Read<int32>(Val + this->UObject.Index);
		if (Idx < 0 || Idx >= ObjectArray::Num())
			return false;
		return ObjectArray::GetByIndex(Idx).GetAddress() == reinterpret_cast<void*>(Val);
	};

	for (int32 Off = PtrSize; Off <= 0x80; Off += PtrSize)
	{
		const uintptr_t ValA = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(ObjA) + Off);
		const uintptr_t ValB = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(ObjB) + Off);

		if (!GMemory->IsAddressReadable(ValA) || !GMemory->IsAddressReadable(ValB))
			continue;

		if (IsUObject(ValA) || IsUObject(ValB))
			continue;

		const uintptr_t DerefA = GMemory->Read<uintptr_t>(ValA);
		const uintptr_t DerefB = GMemory->Read<uintptr_t>(ValB);

		if (!GMemory->IsAddressReadable(DerefA) || !GMemory->IsAddressReadable(DerefB))
			continue;

		// FStructBaseChain::StructBaseChainArray is self-referential:
		// StructBaseChainArray[0]->StructBaseChainArray == StructBaseChainArray.
		// An FField* has a vtable at offset 0 whose first entry is a function pointer, not the FField itself.
		if (GMemory->Read<uintptr_t>(DerefA) == ValA || GMemory->Read<uintptr_t>(DerefB) == ValB)
			continue;

		this->UStruct.ChildProperties = Off;
		return true;
	}

	return false;
}

bool FInGenOffsets::Init_UStruct_Size()
{
	this->UStruct.Size = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, int32_t>> Infos;

	Infos.push_back({ObjectArray::FindObjectFast("Color").GetAddress(), 0x04});
	Infos.push_back({ObjectArray::FindObjectFast("Guid").GetAddress(), 0x10});

	this->UStruct.Size = OffsetFinder::FindOffset(Infos);
	return this->UStruct.Size != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UStruct_MinAlignment()
{
	this->UStruct.MinAlignment = OffsetFinder::OffsetNotFound;

	// UStruct.Size is already resolved (Init_UStruct_Size runs first), so bound the search to
	// UStruct's own byte range - UField's end to UStruct's end - instead of a wide blind range.
	// Doesn't assume any fixed field ordering, so it holds even if a game reorders fields.
	UEStruct Struct = ObjectArray::FindStructFast("Struct");
	if (!Struct)
		Struct = ObjectArray::FindStructFast("struct");

	if (!Struct || !Struct.GetSuper())
		return false;

	const int32 UStructStart = Struct.GetSuper().GetStructSize();
	const int32 UStructEnd   = Struct.GetStructSize();

	std::vector<std::pair<void*, int16_t>> Infos;
	Infos.push_back({ObjectArray::FindObjectFast("Transform").GetAddress(), 0x10});
	Infos.push_back({ObjectArray::FindObjectFast("Quat").GetAddress(), 0x10});
	Infos.push_back({ObjectArray::FindObjectFast("InterpCurveLinearColor").GetAddress(), sizeof(void*)});
	Infos.push_back({ObjectArray::FindObjectFast("InterpCurveQuat").GetAddress(), sizeof(void*)});
	Infos.push_back({ObjectArray::FindObjectFast("Matrix").GetAddress(), 0x10});

	int32 Score = 0, TotalConsidered = 0;
	this->UStruct.MinAlignment = OffsetFinder::FindOffsetByScore(Infos, UStructStart, UStructEnd, &Score, &TotalConsidered, {this->UStruct.Size});

	if (this->UStruct.MinAlignment != OffsetFinder::OffsetNotFound && Score < TotalConsidered)
		GLogger.FmtWrite(ELogLevel::Warning, "Init_UStruct_MinAlignment: Only {}/{} reference entries agreed at 0x{:X}\n", Score, TotalConsidered, this->UStruct.MinAlignment);

	return this->UStruct.MinAlignment != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UStruct_StructBaseChain()
{
	this->UStruct.StructBaseChain = OffsetFinder::OffsetNotFound;

	/* FStructBaseChain was added in UE4.22. */
	UEStruct Struct = ObjectArray::FindStructFast("Struct");
	if (!Struct)
		Struct = ObjectArray::FindStructFast("struct");

	if (!Struct || !Struct.GetSuper())
		return false;

	const int32 UStructStart = Struct.GetSuper().GetStructSize();
	const int32 UStructEnd   = Struct.GetStructSize();

	auto CountSuperClasses = [](const UEStruct InStruct) -> int32
	{
		int32 Count = 0;

		UEStruct CurrentSuper = InStruct.GetSuper();
		while (CurrentSuper)
		{
			Count++;
			CurrentSuper = CurrentSuper.GetSuper();
		}

		return Count;
	};

	std::vector<std::pair<void*, int32_t>> Infos;

	UEStruct APlayerController = ObjectArray::FindClassFast("PlayerController");
	UEStruct AActor            = ObjectArray::FindClassFast("Actor");

	Infos.push_back({Struct.GetAddress(), CountSuperClasses(Struct)});
	Infos.push_back({APlayerController.GetAddress(), CountSuperClasses(APlayerController)});
	Infos.push_back({AActor.GetAddress(), CountSuperClasses(AActor)});

	constexpr auto FStructBaseChainSize = Utils::Align(sizeof(void*) + sizeof(int32_t), alignof(void*));

	// FStructBaseChain::NumStructBasesInChainMinusOne is at offset sizeof(void*), after StructBaseChainArray
	const int32 CountOffset = OffsetFinder::FindOffset<sizeof(void*)>(Infos, UStructStart, UStructEnd - FStructBaseChainSize);
	if (CountOffset == OffsetFinder::OffsetNotFound)
		return false;

	this->UStruct.StructBaseChain = CountOffset - static_cast<int32>(sizeof(void*));
	return this->UStruct.StructBaseChain != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UFunction_FunctionFlags()
{
	this->UFunction.FunctionFlags = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, EFunctionFlags>> Infos;

	UEStruct(ObjectArray::FindObjectFast("WasInputKeyJustPressed", EClassCastFlags::Function).GetAddress()).GetStructSize();

	Infos.push_back({ObjectArray::FindObjectFast("WasInputKeyJustPressed", EClassCastFlags::Function).GetAddress(), EFunctionFlags::Final | EFunctionFlags::Native | EFunctionFlags::Public | EFunctionFlags::BlueprintCallable | EFunctionFlags::BlueprintPure | EFunctionFlags::Const});
	Infos.push_back({ObjectArray::FindObjectFast("ToggleSpeaking", EClassCastFlags::Function).GetAddress(), EFunctionFlags::Exec | EFunctionFlags::Native | EFunctionFlags::Public});
	Infos.push_back({ObjectArray::FindObjectFast("SwitchLevel", EClassCastFlags::Function).GetAddress(), EFunctionFlags::Exec | EFunctionFlags::Native | EFunctionFlags::Public});

	// Some games don't have APlayerController::SwitchLevel(), so we replace it with APlayerController::FOV() which has the same FunctionFlags
	if (Infos[2].first == nullptr)
		Infos[2].first = ObjectArray::FindObjectFast("FOV", EClassCastFlags::Function).GetAddress();

	int32 Ret = OffsetFinder::FindOffset(Infos);

	if (Ret == OffsetFinder::OffsetNotFound)
	{
		for (auto& [_, Flags] : Infos)
			Flags |= EFunctionFlags::RequiredAPI;

		Ret = OffsetFinder::FindOffset(Infos);
	}

	this->UFunction.FunctionFlags = Ret;
	return this->UFunction.FunctionFlags != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UFunction_NumParams()
{
	this->UFunction.NumParams = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, uint8>> Infos;

	Infos.push_back({ObjectArray::FindObjectFast("WasInputKeyJustPressed", EClassCastFlags::Function).GetAddress(), 2});
	Infos.push_back({ObjectArray::FindObjectFast("ToggleSpeaking", EClassCastFlags::Function).GetAddress(), 1});
	Infos.push_back({ObjectArray::FindObjectFast("SwitchLevel", EClassCastFlags::Function).GetAddress(), 1});

	// Some games don't have APlayerController::SwitchLevel(), so we replace it with APlayerController::FOV() which has the same FunctionFlags
	if (Infos[2].first == nullptr)
		Infos[2] = {ObjectArray::FindObjectFast("FOV", EClassCastFlags::Function).GetAddress(), 1};

	this->UFunction.NumParams = OffsetFinder::FindOffset<1>(Infos);

	return this->UFunction.NumParams != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UFunction_ParamSize()
{
	this->UFunction.ParamSize = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, uint16>> Infos;

	Infos.push_back({ObjectArray::FindObjectFast("WasInputKeyJustPressed", EClassCastFlags::Function).GetAddress(), 1 + ObjectArray::FindStructFast("Key").GetStructSize()});
	Infos.push_back({ObjectArray::FindObjectFast("ToggleSpeaking", EClassCastFlags::Function).GetAddress(), 1});
	Infos.push_back({ObjectArray::FindObjectFast("SwitchLevel", EClassCastFlags::Function).GetAddress(), sizeof(FString)});

	// Some games don't have APlayerController::SwitchLevel(), so we replace it with APlayerController::FOV() which has the same FunctionFlags
	if (Infos[2].first == nullptr)
		Infos[2] = {ObjectArray::FindObjectFast("FOV", EClassCastFlags::Function).GetAddress(), 4};

	this->UFunction.ParamSize = OffsetFinder::FindOffset<2>(Infos);

	return this->UFunction.ParamSize != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UFunction_ExecFunction()
{
	this->UFunction.ExecFunction = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, EFunctionFlags>> Infos;

	uintptr_t WasInputKeyJustPressed = reinterpret_cast<uintptr_t>(ObjectArray::FindObjectFast("WasInputKeyJustPressed", EClassCastFlags::Function).GetAddress());
	uintptr_t ToggleSpeaking         = reinterpret_cast<uintptr_t>(ObjectArray::FindObjectFast("ToggleSpeaking", EClassCastFlags::Function).GetAddress());
	uintptr_t SwitchLevel_Or_FOV     = reinterpret_cast<uintptr_t>(ObjectArray::FindObjectFast("SwitchLevel", EClassCastFlags::Function).GetAddress());

	// Some games don't have APlayerController::SwitchLevel(), so we replace it with APlayerController::FOV() which has the same FunctionFlags
	if (SwitchLevel_Or_FOV == 0)
		SwitchLevel_Or_FOV = reinterpret_cast<uintptr_t>(ObjectArray::FindObjectFast("FOV", EClassCastFlags::Function).GetAddress());

	UEStruct Struct = ObjectArray::FindStructFast("Struct");
	if (!Struct)
		Struct = ObjectArray::FindStructFast("struct");

	if (!Struct)
		return false;

	const int32 UFunctionStart = Struct.GetStructSize();

	const ModuleInfo ModInfo = GMemory->GetUnrealModule();
	for (int i = UFunctionStart; i < 0x140; i += sizeof(void*))
	{
		uintptr_t A = GMemory->Read<uintptr_t>(WasInputKeyJustPressed + i);
		uintptr_t B = GMemory->Read<uintptr_t>(ToggleSpeaking + i);
		uintptr_t C = GMemory->Read<uintptr_t>(SwitchLevel_Or_FOV + i);

		if (ModInfo.Contains(A) && ModInfo.Contains(B) && ModInfo.Contains(C))
		{
			this->UFunction.ExecFunction = i;
			return true;
		}
	}

	return false;
}

bool FInGenOffsets::Init_UClass_CastFlags()
{
	this->UClass.CastFlags = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, EClassCastFlags>> Infos;

	Infos.push_back({ObjectArray::FindObjectFast("Actor").GetAddress(), EClassCastFlags::Actor});
	Infos.push_back({ObjectArray::FindObjectFast("Class").GetAddress(), EClassCastFlags::Field | EClassCastFlags::Struct | EClassCastFlags::Class});

	this->UClass.CastFlags = OffsetFinder::FindOffset(Infos);
	return this->UClass.CastFlags != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UClass_ClassDefaultObject()
{
	this->UClass.ClassDefaultObject = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, void*>> Infos;

	Infos.push_back({ObjectArray::FindClassFast("Object").GetAddress(), ObjectArray::FindObjectFast("Default__Object").GetAddress()});
	Infos.push_back({ObjectArray::FindClassFast("Field").GetAddress(), ObjectArray::FindObjectFast("Default__Field").GetAddress()});

	int MaxOffset = ObjectArray::FindClassFast("Class").GetStructSize();
	if (MaxOffset <= 0x100)
		MaxOffset = 0x400;

	this->UClass.ClassDefaultObject = OffsetFinder::FindOffset(Infos, sizeof(void*) * 5, MaxOffset);
	return this->UClass.ClassDefaultObject != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_UClass_InitImplementedInterfaces()
{
	this->UClass.ImplementedInterfaces = OffsetFinder::OffsetNotFound;

	if (this->UClass.ClassDefaultObject == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "FindImplementedInterfacesOffset: UClass.ClassDefaultObject is not set.\n");
		return false;
	}

	UEClass Interface_AssetUserDataClass = ObjectArray::FindClassFast("Interface_AssetUserData");

	const uint8_t* ActorComponentClassPtr = reinterpret_cast<const uint8_t*>(ObjectArray::FindClassFast("ActorComponent").GetAddress());

	int MaxOffset = ObjectArray::FindClassFast("Class").GetStructSize();
	if (MaxOffset <= 0x100)
		MaxOffset = 0x400;

	for (int i = this->UClass.ClassDefaultObject + sizeof(void*); i < MaxOffset; i += sizeof(void*))
	{
		const auto ActorArray = GMemory->Read<TArray<FImplementedInterface>>(reinterpret_cast<uintptr_t>(ActorComponentClassPtr) + i);

		if (ActorArray.IsValid() && GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(ActorArray.GetDataPtr())))
		{
			if (ActorArray[0].InterfaceClass == Interface_AssetUserDataClass)
			{
				this->UClass.ImplementedInterfaces = i;
				return true;
			}
		}
	}

	return false;
}

bool FInGenOffsets::Init_Property_ElementSize()
{
	this->Property.ElementSize = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, int32_t>> Infos;

	UEStruct Guid = ObjectArray::FindStructFast("Guid");

	Infos.push_back({Guid.FindMember("A").GetAddress(), 0x04});
	Infos.push_back({Guid.FindMember("C").GetAddress(), 0x04});
	Infos.push_back({Guid.FindMember("D").GetAddress(), 0x04});

	this->Property.ElementSize = OffsetFinder::FindOffset(Infos);
	return this->Property.ElementSize != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_Property_ArrayDim()
{
	this->Property.ArrayDim = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, int32_t>> Infos;

	UEStruct Guid = ObjectArray::FindStructFast("Guid");

	Infos.push_back({Guid.FindMember("A").GetAddress(), 0x01});
	Infos.push_back({Guid.FindMember("C").GetAddress(), 0x01});
	Infos.push_back({Guid.FindMember("D").GetAddress(), 0x01});

	if (this->Property.ElementSize == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_Property_ArrayDim: Property.ElementSize is not set.\n");
		return false;
	}

	const int32_t FFieldMax = std::max({FField.Vft,
	                                    FField.Class,
	                                    FField.Owner,
	                                    FField.Next,
	                                    FField.Name,
	                                    FField.EditorOnlyMetadata});

	const int32_t MinOffset = InternalSettings::bUseFProperty ? FFieldMax + 4 : this->UField.Next + sizeof(void*);
	const int32_t MaxOffset = this->Property.ElementSize + 0x10;

	this->Property.ArrayDim = OffsetFinder::FindOffset(Infos, MinOffset, MaxOffset);
	return this->Property.ArrayDim != OffsetFinder::OffsetNotFound;
}

bool FInGenOffsets::Init_Property_PropertyFlags()
{
	this->Property.PropertyFlags = OffsetFinder::OffsetNotFound;

	const UEStruct Guid  = ObjectArray::FindStructFast("Guid");
	const UEStruct Color = ObjectArray::FindStructFast("Color");

	constexpr EPropertyFlags CommonFlags =
	    EPropertyFlags::Edit |
	    EPropertyFlags::ZeroConstructor |
	    EPropertyFlags::SaveGame |
	    EPropertyFlags::IsPlainOldData |
	    EPropertyFlags::HasGetValueTypeHash;

	constexpr EPropertyFlags GuidFlags  = CommonFlags;
	constexpr EPropertyFlags ColorFlags = CommonFlags | EPropertyFlags::BlueprintVisible;

	constexpr std::array<EPropertyFlags, 4> FlagVariants = {
	    EPropertyFlags::NoDestructor,
	    EPropertyFlags::NoDestructor | EPropertyFlags::NativeAccessSpecifierPublic,
	    EPropertyFlags::None,
	    EPropertyFlags::NativeAccessSpecifierPublic};

	std::vector<std::pair<void*, EPropertyFlags>> Infos = {{Guid.FindMember("A").GetAddress(), EPropertyFlags::None},
	                                                       {Color.FindMember("R").GetAddress(), EPropertyFlags::None}};

	if (Infos[1].first == nullptr) [[unlikely]]
		Infos[1].first = Color.FindMember("r").GetAddress();

	if (Infos[0].first == nullptr || Infos[1].first == nullptr)
		return false;

	for (const EPropertyFlags Variant : FlagVariants)
	{
		Infos[0].second = GuidFlags | Variant;
		Infos[1].second = ColorFlags | Variant;

		const int FlagsOffset = OffsetFinder::FindOffset(Infos);

		if (FlagsOffset != OffsetFinder::OffsetNotFound)
		{
			this->Property.PropertyFlags = FlagsOffset;
			return true;
		}
	}

	return false;
}

bool FInGenOffsets::Init_Property_OffsetInternal()
{
	this->Property.Offset_Internal = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, int32_t>> Infos;

	const UEStruct Color = ObjectArray::FindStructFast("Color");
	const UEStruct Guid  = ObjectArray::FindStructFast("Guid");

	Infos.push_back({Color.FindMember("B").GetAddress(), 0x00});
	Infos.push_back({Color.FindMember("G").GetAddress(), 0x01});
	Infos.push_back({Guid.FindMember("C").GetAddress(), 0x08});

	// Thanks to the ue5 dev who decided FColor::R should be spelled FColor::r
	if (Infos[2].first == nullptr) [[unlikely]]
		Infos[2].first = Color.FindMember("r").GetAddress();

	this->Property.Offset_Internal = OffsetFinder::FindOffset(Infos);
	return this->Property.Offset_Internal != OffsetFinder::OffsetFinderMinValue;
}

bool FInGenOffsets::Init_BoolProperty_Base()
{
	this->BoolProperty.Base = OffsetFinder::OffsetNotFound;

	std::vector<std::pair<void*, uint8_t>> Infos;

	UEClass Engine = ObjectArray::FindClassFast("Engine");
	Infos.push_back({Engine.FindMember("bIsOverridingSelectedColor").GetAddress(), 0xFF});
	Infos.push_back({Engine.FindMember("bEnableOnScreenDebugMessagesDisplay").GetAddress(), 0b00000010});
	Infos.push_back({ObjectArray::FindClassFast("PlayerController").FindMember("bAutoManageActiveCameraTarget").GetAddress(), 0xFF});

	const int32 MinOffset = this->Property.Offset_Internal != OffsetFinder::OffsetNotFound ? this->Property.Offset_Internal : OffsetFinder::OffsetFinderMinValue;
	const int32 Ret       = OffsetFinder::FindOffset<1>(Infos, MinOffset);
	if (Ret == OffsetFinder::OffsetNotFound)
		return false;

	this->BoolProperty.Base = Ret - 0x3;

	return true;
}

bool FInGenOffsets::Init_EnumProperty_Base()
{
	this->EnumProperty.Base = OffsetFinder::OffsetNotFound;

	if (this->BoolProperty.Base == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_EnumProperty_Base: BoolProperty.Base is not set.\n");
		return false;
	}

	this->EnumProperty.Base = this->BoolProperty.Base;

	std::vector<std::pair<void*, const void*>> Infos;

	const void* ComponentCreationMethod = ObjectArray::FindObjectFast("EComponentCreationMethod", EClassCastFlags::Enum).GetAddress();
	const void* AutoPossessAI           = ObjectArray::FindObjectFast("EAutoPossessAI", EClassCastFlags::Enum).GetAddress();

	if (!ComponentCreationMethod || !AutoPossessAI)
		return true;

	void* CreationMethodMember = ObjectArray::FindClassFast("ActorComponent").FindMember("CreationMethod", EClassCastFlags::EnumProperty).GetAddress();
	void* AutoPossessAIMember  = ObjectArray::FindClassFast("Pawn").FindMember("AutoPossessAI", EClassCastFlags::EnumProperty).GetAddress();

	// UE4.15 and below don't have EnumProperty
	if (!CreationMethodMember || !AutoPossessAIMember)
		return true;

	Infos.push_back({CreationMethodMember, ComponentCreationMethod});
	Infos.push_back({AutoPossessAIMember, AutoPossessAI});

	// EnumProperty::Enum is the 2nd member after 'NumericProperty UnderlayingType'
	const int32 MinOffset = this->Property.Offset_Internal != OffsetFinder::OffsetNotFound ? this->Property.Offset_Internal : OffsetFinder::OffsetFinderMinValue;
	const int32 Ret       = OffsetFinder::FindOffset(Infos, MinOffset);
	if (Ret == OffsetFinder::OffsetNotFound)
		return true;

	this->EnumProperty.Base = Ret - static_cast<int32>(sizeof(void*));
	return true;
}

bool FInGenOffsets::Init_Property_SizeOf()
{
	this->Property.SizeOf = OffsetFinder::OffsetNotFound;

	if (this->BoolProperty.Base == OffsetFinder::OffsetNotFound)
		Init_BoolProperty_Base();
	if (this->EnumProperty.Base == OffsetFinder::OffsetNotFound && this->BoolProperty.Base != OffsetFinder::OffsetNotFound)
		Init_EnumProperty_Base();

	if (this->EnumProperty.Base == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_Property_SizeOf: One or more required offsets are not set.\n");
		return false;
	}

	this->Property.SizeOf = this->EnumProperty.Base;
	return true;
}

bool FInGenOffsets::Init_ObjectProperty_PropertyClass()
{
	this->ObjectProperty.PropertyClass = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_ObjectProperty_PropertyClass: Property.SizeOf is not set.\n");
		return false;
	}

	this->ObjectProperty.PropertyClass = this->Property.SizeOf;

	std::vector<std::pair<void*, void*>> Infos;

	const UEClass Controller = ObjectArray::FindClassFast("Controller");
	Infos.push_back({Controller.FindMember("PlayerState").GetAddress(), ObjectArray::FindClassFast("PlayerState").GetAddress()});
	Infos.push_back({Controller.FindMember("Pawn").GetAddress(), ObjectArray::FindClassFast("Pawn").GetAddress()});
	Infos.push_back({ObjectArray::FindClassFast("World").FindMember("PersistentLevel").GetAddress(), ObjectArray::FindClassFast("Level").GetAddress()});

	const int32 MinOffset = this->Property.Offset_Internal != OffsetFinder::OffsetNotFound ? this->Property.Offset_Internal : OffsetFinder::OffsetFinderMinValue;
	const int32 Ret       = OffsetFinder::FindOffset(Infos, MinOffset);
	if (Ret != OffsetFinder::OffsetNotFound)
		this->ObjectProperty.PropertyClass = Ret;

	return true;
}

bool FInGenOffsets::Init_ByteProperty_Enum()
{
	this->ByteProperty.Enum = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_ByteProperty_Enum: Property.SizeOf is not set.\n");
		return false;
	}

	this->ByteProperty.Enum = this->Property.SizeOf;

	std::vector<std::pair<void*, const void*>> Infos;

	const void* CollisionResponseEnum = ObjectArray::FindObjectFast("ECollisionResponse", EClassCastFlags::Enum).GetAddress();

	const UEStruct CollisionResponseContainer = ObjectArray::FindStructFast("CollisionResponseContainer");

	if (!CollisionResponseEnum || !CollisionResponseContainer)
		return true;

	const void* GameTraceChannel1 = CollisionResponseContainer.FindMember("GameTraceChannel1", EClassCastFlags::ByteProperty).GetAddress();
	const void* GameTraceChannel2 = CollisionResponseContainer.FindMember("GameTraceChannel2", EClassCastFlags::ByteProperty).GetAddress();

	if (!GameTraceChannel1 || !GameTraceChannel2)
		return true;

	Infos.push_back({const_cast<void*>(GameTraceChannel1), CollisionResponseEnum});
	Infos.push_back({const_cast<void*>(GameTraceChannel2), CollisionResponseEnum});

	const int32 MinOffset = this->Property.Offset_Internal != OffsetFinder::OffsetNotFound ? this->Property.Offset_Internal : OffsetFinder::OffsetFinderMinValue;
	const int32 Ret       = OffsetFinder::FindOffset(Infos, MinOffset);
	if (Ret != OffsetFinder::OffsetNotFound)
		this->ByteProperty.Enum = Ret;

	return true;
}

bool FInGenOffsets::Init_StructProperty_Struct()
{
	this->StructProperty.Struct = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_StructProperty_Struct: Property.SizeOf is not set.\n");
		return false;
	}

	this->StructProperty.Struct = this->Property.SizeOf;

	std::vector<std::pair<void*, const void*>> Infos;

	const void* VectorClass = ObjectArray::FindStructFast("Vector").GetAddress();

	if (VectorClass == nullptr)
		VectorClass = ObjectArray::FindClassFast("vector").GetAddress();

	const UEStruct TwoVectorsStruct = ObjectArray::FindStructFast("TwoVectors");

	if (!VectorClass || !TwoVectorsStruct)
		return true;

	const void* V1 = TwoVectorsStruct.FindMember("v1", EClassCastFlags::StructProperty).GetAddress();
	const void* V2 = TwoVectorsStruct.FindMember("v2", EClassCastFlags::StructProperty).GetAddress();

	if (!V1 || !V2)
		return true;

	Infos.push_back({const_cast<void*>(V1), VectorClass});
	Infos.push_back({const_cast<void*>(V2), VectorClass});

	const int32 MinOffset = this->Property.Offset_Internal != OffsetFinder::OffsetNotFound ? this->Property.Offset_Internal : OffsetFinder::OffsetFinderMinValue;
	const int32 Ret       = OffsetFinder::FindOffset(Infos, MinOffset);
	if (Ret != OffsetFinder::OffsetNotFound)
		this->StructProperty.Struct = Ret;

	return true;
}

bool FInGenOffsets::Init_DelegateProperty_SignatureFunction()
{
	this->DelegateProperty.SignatureFunction = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_DelegateProperty_SignatureFunction: Property.SizeOf is not set.\n");
		return false;
	}

	this->DelegateProperty.SignatureFunction = this->Property.SizeOf;

	std::vector<std::pair<void*, const void*>> Infos;

	const void* DelegateSignature = ObjectArray::FindObjectFast("TimerDynamicDelegate__DelegateSignature", EClassCastFlags::Function).GetAddress();

	const UEStruct TwoVectorsStruct = ObjectArray::FindStructFast("TwoVectors");

	if (!DelegateSignature || !TwoVectorsStruct)
		return true;

	const void* Delegate1 = ObjectArray::FindObjectFast<UEFunction>("K2_GetTimerElapsedTimeDelegate", EClassCastFlags::Function).FindMember("Delegate", EClassCastFlags::DelegateProperty).GetAddress();
	const void* Delegate2 = ObjectArray::FindObjectFast<UEFunction>("K2_GetTimerRemainingTimeDelegate", EClassCastFlags::Function).FindMember("Delegate", EClassCastFlags::DelegateProperty).GetAddress();

	if (!Delegate1 || !Delegate2)
		return true;

	Infos.push_back({const_cast<void*>(Delegate1), DelegateSignature});
	Infos.push_back({const_cast<void*>(Delegate2), DelegateSignature});

	const int32 MinOffset = this->Property.Offset_Internal != OffsetFinder::OffsetNotFound ? this->Property.Offset_Internal : OffsetFinder::OffsetFinderMinValue;
	const int32 Ret       = OffsetFinder::FindOffset(Infos, MinOffset);
	if (Ret != OffsetFinder::OffsetNotFound)
		this->DelegateProperty.SignatureFunction = Ret;

	return true;
}

bool FInGenOffsets::Init_ArrayProperty_Inner()
{
	this->ArrayProperty.Inner = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_ArrayProperty_Inner: Property.SizeOf is not set.\n");
		return false;
	}

	this->ArrayProperty.Inner = this->Property.SizeOf;

	if (InternalSettings::bUseFProperty)
	{
		if (const UEProperty Property = ObjectArray::FindClassFast("GameViewportClient").FindMember("DebugProperties", EClassCastFlags::ArrayProperty))
		{
			void* AddressToCheck = reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Property.GetAddress()) + this->Property.SizeOf));

			if (!GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(AddressToCheck)))
			{
				this->ArrayProperty.Inner = this->Property.SizeOf + sizeof(void*);
			}
		}
	}

	return true;
}

bool FInGenOffsets::Init_SetProperty_ElementProp()
{
	this->SetProperty.ElementProp = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_SetProperty_ElementProp: Property.SizeOf is not set.\n");
		return false;
	}

	this->SetProperty.ElementProp = this->Property.SizeOf;

	if (InternalSettings::bUseFProperty)
	{
		if (const auto Object = ObjectArray::FindStructFast("LevelCollection").FindMember("Levels", EClassCastFlags::SetProperty))
		{
			const void* AddressToCheck = reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object.GetAddress()) + Property.SizeOf));

			if (!GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(AddressToCheck)))
			{
				this->SetProperty.ElementProp = this->Property.SizeOf + sizeof(void*);
			}
		}
	}

	return true;
}

bool FInGenOffsets::Init_MapProperty_Base()
{
	this->MapProperty.Base = OffsetFinder::OffsetNotFound;

	if (this->Property.SizeOf == OffsetFinder::OffsetNotFound)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Init_MapProperty_Base: Property.SizeOf is not set.\n");
		return false;
	}

	this->MapProperty.Base = this->Property.SizeOf;

	if (InternalSettings::bUseFProperty)
	{
		if (const auto Object = ObjectArray::FindClassFast("UserDefinedEnum").FindMember("DisplayNameMap", EClassCastFlags::MapProperty))
		{
			const void* AddressToCheck = reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object.GetAddress()) + Property.SizeOf));

			if (!GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(AddressToCheck)))
			{
				this->MapProperty.Base = this->Property.SizeOf + sizeof(void*);
			}
		}
	}

	return true;
}

/* InSDK -> ULevel */
void FInSDKOffsets::InitLevelActorsOffset()
{
	this->ULevel.Actors = OffsetFinder::OffsetNotFound;

	UEObject Level = nullptr;
	uintptr_t Lvl  = 0x0;

	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject) || !Obj.IsA(EClassCastFlags::Level))
			continue;

		Level = Obj;
		Lvl   = reinterpret_cast<uintptr_t>(Obj.GetAddress());
		break;
	}

	if (Lvl == 0x0)
		return;

	/*
	class ULevel : public UObject
	{
	    FURL URL;
	    TArray<AActor*> Actors;
	    TArray<AActor*> GCActors;
	};

	SearchStart = sizeof(UObject) + sizeof(FURL)
	SearchEnd = offsetof(ULevel, OwningWorld)
	*/
	UEClass UObjectClass = ObjectArray::FindClassFast("Object");
	if (!UObjectClass)
		UObjectClass = ObjectArray::FindClassFast("object");

	const UEStruct FURLStruct = ObjectArray::FindObjectFast<UEStruct>("URL", EClassCastFlags::Struct);

	const UEProperty Level_OwningWorldProperty = Level.GetClass().FindMember("OwningWorld");

	if (!UObjectClass || !FURLStruct || !Level_OwningWorldProperty)
		return;

	const int32 SearchStart = UObjectClass.GetStructSize() + FURLStruct.GetStructSize();
	const int32 SearchEnd   = Level_OwningWorldProperty.GetOffset();

	for (int i = SearchStart; i <= (SearchEnd - 0x10); i += sizeof(void*))
	{
		const auto ActorArray = GMemory->Read<TArray<void*>>(Lvl + i);

		if (ActorArray.IsValid() && GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(ActorArray.GetDataPtr())))
		{
			this->ULevel.Actors = i;
			return;
		}
	}
}

void FInSDKOffsets::InitDatatableRowMapOffset()
{
	this->UDataTable.RowMap = OffsetFinder::OffsetNotFound;

	const UEClass DataTable = ObjectArray::FindClassFast("DataTable");

	constexpr int32 UObjectOuterSize = sizeof(void*);
	constexpr int32 RowStructSize    = sizeof(void*);

	if (!DataTable)
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitDatatableRowMapOffset: [DataTable] Couldn't find \"DataTable\" class, assuming default layout.\n");
		this->UDataTable.RowMap = (GOffsets.UObject.Outer + UObjectOuterSize + RowStructSize);
		return;
	}

	UEProperty RowStructProp = DataTable.FindMember("RowStruct", EClassCastFlags::ObjectProperty);

	if (!RowStructProp)
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitDatatableRowMapOffset: [DataTable] Couldn't find \"RowStruct\" property, assuming default layout.\n");
		this->UDataTable.RowMap = (GOffsets.UObject.Outer + UObjectOuterSize + RowStructSize);
		return;
	}

	this->UDataTable.RowMap = RowStructProp.GetOffset() + RowStructProp.GetSize();
}

void FInSDKOffsets::InitUEngineAndUWorld()
{
	std::unordered_map<std::string, uintptr_t> Offsets = {
	    {"Engine", 0},
	    {"World", 0},
	};
	OffsetFinder::FindStaticOffsets(Offsets);

	this->Statics.GEngine = static_cast<int64>(Offsets["Engine"]);
	this->Statics.GWorld  = static_cast<int64>(Offsets["World"]);
}

void FInSDKOffsets::InitFText()
{
	/* ---- Detection tuning ---- */
	constexpr int32 kMaxTextCandidates = 512;    // populated FText addresses gathered before searching
	constexpr int32 kProbeCandidates   = 32;     // candidates used by the cheap shortlisting pass
	constexpr int32 kMinAcceptHits     = 4;      // decoded strings required to accept a pair outright
	constexpr int32 kMaxStringScan     = 0x80;   // how far into the text data to look for the FString
	constexpr int32 kMaxTextDataScan   = 0x20;   // how far into an FText to look for the text data pointer
	constexpr int32 kMaxFStringLen     = 0x1000; // largest plausible FString ArrayNum
	constexpr int32 kPtrSize           = static_cast<int32>(sizeof(void*));

	int32 FTextSize = 0;

	const UEFunction Conv_StringToText = ObjectArray::FindObjectFast<UEFunction>("Conv_StringToText", EClassCastFlags::Function);
	if (Conv_StringToText)
	{
		for (UEProperty Prop : Conv_StringToText.GetProperties())
		{
			if (Prop.HasPropertyFlags(EPropertyFlags::ReturnParm))
			{
				FTextSize = Prop.GetSize();
				break;
			}
		}
	}

	if (!FTextSize)
	{
		const int MaxNumObjectsConsidered = ObjectArray::Num();
		int NumObjectsConsidered          = 0;

		for (UEObject Obj : ObjectArray())
		{
			if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
				break;

			if (!Obj.IsA(EClassCastFlags::Struct))
				continue;

			for (UEProperty Prop : Obj.Cast<UEStruct>().GetProperties())
			{
				if (Prop.IsA(EClassCastFlags::TextProperty))
				{
					FTextSize = Prop.GetSize();
					break;
				}
			}

			if (FTextSize)
				break;
		}
	}

	if (!FTextSize)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitFText: FText size could not be determined!\n");
		return;
	}

	this->FText.Size = FTextSize;

	/* Every TextProperty offset of every class, so no text field on an object is missed. */
	std::unordered_map<void*, std::vector<int32>> TextPropOffsetsByClass;

	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (!Obj.IsA(EClassCastFlags::Class))
			continue;

		UEClass Class = Obj.Cast<UEClass>();
		for (UEProperty Prop : Class.GetProperties())
		{
			if (Prop.IsA(EClassCastFlags::TextProperty))
				TextPropOffsetsByClass[Class.GetAddress()].push_back(Prop.GetOffset());
		}
	}

	if (TextPropOffsetsByClass.empty())
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitFText: No class declares a TextProperty!\n");
		return;
	}

	/*
	 * Sampled with a stride across the whole array rather than sequentially from index
	 * 0. Sequential order is creation order, so the low indices are saturated with
	 * engine classes, CDOs and template objects - their TextProperty fields are the
	 * default-constructed empty FText singleton, never real text. A cap on how many
	 * objects to visit therefore stopped before ever reaching a runtime instance (a
	 * spawned widget, an active dialogue line) that actually holds localized text.
	 */
	constexpr int32 kMaxObjectsToScan = 20000; // objects visited, spread across the whole array
	constexpr int32 kMaxRawCandidates = 4096;  // populated addresses gathered before filtering

	std::vector<uintptr_t> RawCandidates;
	RawCandidates.reserve(kMaxRawCandidates);

	const int32 NumObjects = ObjectArray::Num();
	const int32 Stride     = std::max(1, NumObjects / kMaxObjectsToScan);

	int32 ObjectsScanned = 0;
	for (int32 Index = 0; Index < NumObjects && static_cast<int32>(RawCandidates.size()) < kMaxRawCandidates; Index += Stride)
	{
		const UEObject Instance = ObjectArray::GetByIndex(Index);
		if (!Instance)
			continue;

		ObjectsScanned++;

		const uintptr_t InstanceAddr = reinterpret_cast<uintptr_t>(Instance.GetAddress());

		/* The full chain, so fields inherited at any depth are sampled. */
		for (UEClass Class = Instance.GetClass(); Class; Class = Class.GetSuper().Cast<UEClass>())
		{
			const auto It = TextPropOffsetsByClass.find(Class.GetAddress());
			if (It == TextPropOffsetsByClass.end())
				continue;

			for (int32 Offset : It->second)
			{
				const uintptr_t TextAddr = InstanceAddr + Offset;

				bool bPopulated = false;
				for (int32 Slot = 0; Slot + kPtrSize <= FTextSize && !bPopulated; Slot += kPtrSize)
					bPopulated = GMemory->IsAddressReadable(GMemory->Read<uintptr_t>(TextAddr + Slot));

				if (bPopulated)
					RawCandidates.push_back(TextAddr);
			}
		}
	}

	if (RawCandidates.empty())
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitFText: No populated FText instances found!\n");
		return;
	}

	/*
	 * FText::GetEmpty() returns one process-wide shared instance, so every field that
	 * was never localized - which most TextProperty fields on most objects are -
	 * points at the exact same TextData value. That value is virtually always the
	 * single most common one in the pool, so discarding it leaves only instances that
	 * were actually assigned real text, without needing to know FTextData's true
	 * layout (which varies across engine versions) to tell them apart.
	 */
	std::unordered_map<uintptr_t, int32> FirstPointerHits;
	FirstPointerHits.reserve(RawCandidates.size());
	for (uintptr_t Addr : RawCandidates)
		FirstPointerHits[GMemory->Read<uintptr_t>(Addr)]++;

	uintptr_t DominantValue = 0;
	int32 DominantCount     = 0;
	for (const auto& [Value, Count] : FirstPointerHits)
	{
		if (Count > DominantCount)
		{
			DominantValue = Value;
			DominantCount = Count;
		}
	}

	std::vector<uintptr_t> TextCandidates;
	TextCandidates.reserve(std::min(static_cast<int32>(RawCandidates.size()), kMaxTextCandidates));
	for (uintptr_t Addr : RawCandidates)
	{
		if (static_cast<int32>(TextCandidates.size()) >= kMaxTextCandidates)
			break;

		// Excluded everywhere a genuinely distinct value exists; kept only if every
		// candidate found turned out to share it, since a shared value is still better
		// evidence than none.
		if (GMemory->Read<uintptr_t>(Addr) == DominantValue && DominantCount < static_cast<int32>(RawCandidates.size()))
			continue;

		TextCandidates.push_back(Addr);
	}

	GLogger.FmtWrite(ELogLevel::Info,
	                 "InitFText: {} Populated FText instance(s) from {} objects across {} class(es), {} after excluding the shared empty value (seen {} times)\n",
	                 RawCandidates.size(),
	                 ObjectsScanned,
	                 TextPropOffsetsByClass.size(),
	                 TextCandidates.size(),
	                 DominantCount);

	if (TextCandidates.empty())
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitFText: Every populated instance shared the same value; none were distinguishable as real text.\n");
		return;
	}

	/*
	 * Game text is routinely non-ASCII, so a decoded string is rejected only for control
	 * characters rather than for leaving a printable range that CJK would fail.
	 */
	auto IsPlausibleText = [](const auto& Str) -> bool
	{
		if (Str.empty())
			return false;

		for (auto Ch : Str)
		{
			const uint32 Code = static_cast<uint32>(Ch);
			if (Code < 0x20 && Code != '\n' && Code != '\r' && Code != '\t')
				return false;
		}

		return true;
	};

	/*
	 * Decodes the string at a candidate offset pair and reports whether it round-trips.
	 *
	 * FString stores ArrayNum including the null terminator, so reading ArrayNum
	 * characters must yield exactly ArrayNum - 1 before the first NUL. That only holds
	 * when the buffer really is an FString of that length, which is a far stronger
	 * signal than a readable pointer and a plausible-looking count.
	 */
	auto TryDecodeText = [&IsPlausibleText](uintptr_t TextAddr, int32 TextDataOffset, int32 StringOffset, bool bTryChar16, std::wstring* OutText) -> bool
	{
		const uintptr_t TextData = GMemory->Read<uintptr_t>(TextAddr + TextDataOffset);
		if (!GMemory->IsAddressReadable(TextData))
			return false;

		const uintptr_t StringAddr = TextData + StringOffset;
		if (!GMemory->IsAddressReadable(StringAddr, sizeof(uintptr_t) + 2 * sizeof(int32)))
			return false;

		const uintptr_t Data = GMemory->Read<uintptr_t>(StringAddr);
		const int32 ArrayNum = GMemory->Read<int32>(StringAddr + sizeof(uintptr_t));
		const int32 ArrayMax = GMemory->Read<int32>(StringAddr + sizeof(uintptr_t) + sizeof(int32));

		if (ArrayNum < 2 || ArrayNum > ArrayMax || ArrayMax > kMaxFStringLen || !GMemory->IsAddressReadable(Data))
			return false;

		const int32 CharCount = ArrayNum - 1;

		if (bTryChar16)
		{
			const std::u16string Str = GMemory->ReadUTF16(Data, ArrayNum);
			if (static_cast<int32>(Str.length()) != CharCount || !IsPlausibleText(Str))
				return false;

			if (OutText)
				*OutText = Utils::String::UTF16ToWString(Str);
		}
		else
		{
			const std::u32string Str = GMemory->ReadUTF32(Data, ArrayNum);
			if (static_cast<int32>(Str.length()) != CharCount || !IsPlausibleText(Str))
				return false;

			if (OutText)
				*OutText = Utils::String::UTF32ToWString(Str);
		}

		return true;
	};

	/*
	 * FText and FName share TCHAR, so the width the analyzer already derived from name
	 * strings is tried first and only overturned by strictly stronger evidence.
	 */
	const bool bPreferChar16 = InternalSettings::bUseChar16String;

	struct FPairScore
	{
		int32 TextDataOffset = -1;
		int32 StringOffset   = -1;
		int32 Char16Hits     = 0;
		int32 Char32Hits     = 0;

		int32 TotalHits() const { return Char16Hits + Char32Hits; }
		bool bIsChar16() const { return Char16Hits >= Char32Hits; }
	};

	auto ScorePair = [&](int32 TextDataOffset, int32 StringOffset, int32 Limit) -> FPairScore
	{
		FPairScore Score;
		Score.TextDataOffset = TextDataOffset;
		Score.StringOffset   = StringOffset;

		const int32 Count = std::min(static_cast<int32>(TextCandidates.size()), Limit);
		for (int32 i = 0; i < Count; i++)
		{
			if (TryDecodeText(TextCandidates[i], TextDataOffset, StringOffset, bPreferChar16, nullptr))
				bPreferChar16 ? Score.Char16Hits++ : Score.Char32Hits++;
			else if (TryDecodeText(TextCandidates[i], TextDataOffset, StringOffset, !bPreferChar16, nullptr))
				bPreferChar16 ? Score.Char32Hits++ : Score.Char16Hits++;
		}

		return Score;
	};

	/* Shortlist cheaply, then score only the pairs that showed something. */
	FPairScore Best;
	const int32 TextDataLimit = std::min(FTextSize, kMaxTextDataScan);

	for (int32 TextDataOffset = 0; TextDataOffset + kPtrSize <= TextDataLimit; TextDataOffset += kPtrSize)
	{
		for (int32 StringOffset = 0; StringOffset <= kMaxStringScan; StringOffset += kPtrSize)
		{
			if (ScorePair(TextDataOffset, StringOffset, kProbeCandidates).TotalHits() == 0)
				continue;

			const FPairScore Full = ScorePair(TextDataOffset, StringOffset, static_cast<int32>(TextCandidates.size()));
			if (Full.TotalHits() > Best.TotalHits())
				Best = Full;
		}
	}

	if (Best.TotalHits() == 0)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitFText: No (TextData, InTextDataString) pair decoded a string - dumping sample candidates for diagnosis.\n");

		/*
		 * Every offset combination failed against every candidate, which the blind
		 * search alone cannot explain further - it means either the assumed FTextData
		 * layout (a raw FString at some offset) does not hold for this engine build, or
		 * the candidates found are all genuinely empty text. Dumping the raw bytes lets
		 * that be told apart by inspection instead of guessed at.
		 */
		auto DumpMemoryRegion = [](uintptr_t Address, int32 Bytes, const char* Indent) -> void
		{
			std::vector<uint8_t> Block(static_cast<size_t>(Bytes), 0);
			if (!GMemory->IsAddressReadable(Address, static_cast<size_t>(Bytes)) || !GMemory->ReadBytes(Address, Block.data(), Block.size()))
			{
				GLogger.FmtWrite(ELogLevel::Info, "{}<unreadable at 0x{:X}>\n", Indent, Address);
				return;
			}

			for (int32 Offset = 0; Offset + kPtrSize <= Bytes; Offset += kPtrSize)
			{
				uintptr_t Qword = 0;
				memcpy(&Qword, Block.data() + static_cast<size_t>(Offset), sizeof(uintptr_t));

				std::string Ascii;
				for (int32 i = 0; i < kPtrSize; i++)
				{
					const uint8 Ch = Block[static_cast<size_t>(Offset + i)];
					Ascii.push_back((Ch >= 0x20 && Ch <= 0x7E) ? static_cast<char>(Ch) : '.');
				}

				GLogger.FmtWrite(ELogLevel::Info, "{}+0x{:03X}  0x{:016X}  {}  {}\n", Indent, Offset, Qword, GMemory->IsAddressReadable(Qword) ? "R" : "-", Ascii);
			}
		};

		constexpr int32 kDumpCandidates = 3;    // sample instances dumped
		constexpr int32 kDumpTextBytes  = 0x30; // bytes of the FText struct itself
		constexpr int32 kDumpDataBytes  = 0x60; // bytes of whatever each pointer inside it leads to

		for (int32 i = 0; i < std::min(kDumpCandidates, static_cast<int32>(TextCandidates.size())); i++)
		{
			const uintptr_t TextAddr = TextCandidates[i];
			GLogger.FmtWrite(ELogLevel::Info, "[FText] Candidate {} at 0x{:X}\n", i, TextAddr);
			DumpMemoryRegion(TextAddr, kDumpTextBytes, "[FText]   ");

			for (int32 Offset = 0; Offset + kPtrSize <= kDumpTextBytes; Offset += kPtrSize)
			{
				const uintptr_t Target = GMemory->Read<uintptr_t>(TextAddr + Offset);
				if (!GMemory->IsAddressReadable(Target))
					continue;

				GLogger.FmtWrite(ELogLevel::Info, "[FText]   Follow +0x{:02X} -> 0x{:X}\n", Offset, Target);
				DumpMemoryRegion(Target, kDumpDataBytes, "[FText]     ");
			}
		}

		GLogger.FmtWrite(ELogLevel::Error, "InitFText: Using default offsets.\n");
		return;
	}

	this->FText.TextData         = Best.TextDataOffset;
	this->FText.InTextDataString = Best.StringOffset;

	if (Best.bIsChar16() != bPreferChar16)
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitFText: FText decoded as UTF{} but names indicated UTF{}, switching.\n", Best.bIsChar16() ? "16" : "32", bPreferChar16 ? "16" : "32");
		InternalSettings::bUseChar16String = Best.bIsChar16();
	}

	std::wstring Sample;
	for (uintptr_t TextAddr : TextCandidates)
	{
		if (TryDecodeText(TextAddr, Best.TextDataOffset, Best.StringOffset, Best.bIsChar16(), &Sample))
			break;
	}

	const ELogLevel Level = Best.TotalHits() >= kMinAcceptHits ? ELogLevel::Info : ELogLevel::Warning;
	GLogger.FmtWrite(Level, "InitFText: TextData+0x{:X}, InTextDataString+0x{:X} decoded {}/{} instance(s) as UTF{}.\n", Best.TextDataOffset, Best.StringOffset, Best.TotalHits(), TextCandidates.size(), Best.bIsChar16() ? "16" : "32");

	if (!Sample.empty())
		GLogger.FmtWrite(ELogLevel::Info, "InitFText: Sample text \"{}\"\n", Utils::String::WStringToString(Sample));
}

void FInSDKOffsets::InitTDelegateSize()
{
	/* If the AudioComponent class or the OnQueueSubtitles member weren't found, fallback to looping GObjects and looking for a Delegate. */
	auto OnPropertyNotFound = [&]() -> void
	{
		const int MaxNumObjectsConsidered = ObjectArray::Num();
		int NumObjectsConsidered          = 0;

		for (UEObject Obj : ObjectArray())
		{
			if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
				break;

			if (!Obj.IsA(EClassCastFlags::Struct))
				continue;

			for (UEProperty Prop : Obj.Cast<UEClass>().GetProperties())
			{
				if (Prop.IsA(EClassCastFlags::DelegateProperty))
				{
					this->DelegateProperty.SizeOf = Prop.GetSize();
					return;
				}
			}
		}
	};

	const UEClass AudioComponentClass = ObjectArray::FindClassFast("AudioComponent");

	if (!AudioComponentClass)
		return OnPropertyNotFound();

	const UEProperty OnQueueSubtitlesProp = AudioComponentClass.FindMember("OnQueueSubtitles", EClassCastFlags::DelegateProperty);

	if (!OnQueueSubtitlesProp)
		return OnPropertyNotFound();

	this->DelegateProperty.SizeOf = OnQueueSubtitlesProp.GetSize();
}

void FInSDKOffsets::InitFFieldPathSize()
{
	if (!InternalSettings::bUseFProperty)
		return;

	/* If the SetFieldPathPropertyByName function or the Value parameter weren't found, fallback to looping GObjects and looking for a Delegate. */
	auto OnPropertyNotFound = [&]() -> void
	{
		const int MaxNumObjectsConsidered = ObjectArray::Num();
		int NumObjectsConsidered          = 0;

		for (UEObject Obj : ObjectArray())
		{
			if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
				break;

			if (!Obj.IsA(EClassCastFlags::Struct))
				continue;

			for (UEProperty Prop : Obj.Cast<UEClass>().GetProperties())
			{
				if (Prop.IsA(EClassCastFlags::FieldPathProperty))
				{
					this->FieldPathProperty.SizeOf = Prop.GetSize();
					return;
				}
			}
		}
	};

	const UEFunction SetFieldPathPropertyByNameFunc = ObjectArray::FindObjectFast<UEFunction>("SetFieldPathPropertyByName", EClassCastFlags::Function);

	if (!SetFieldPathPropertyByNameFunc)
		return OnPropertyNotFound();

	const UEProperty ValueParamProp = SetFieldPathPropertyByNameFunc.FindMember("Value", EClassCastFlags::FieldPathProperty);

	if (!ValueParamProp)
		return OnPropertyNotFound();

	this->FieldPathProperty.SizeOf = ValueParamProp.GetSize();
}

void FInSDKOffsets::InitTMulticastInlineDelegateSize()
{
	/* If the AudioComponent class or the OnQueueSubtitles member weren't found, fallback to looping GObjects and looking for a Delegate. */
	auto OnPropertyNotFound = [&]() -> void
	{
		const int MaxNumObjectsConsidered = ObjectArray::Num();
		int NumObjectsConsidered          = 0;

		for (UEObject Obj : ObjectArray())
		{
			if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
				break;

			if (!Obj.IsA(EClassCastFlags::Struct))
				continue;

			for (UEProperty Prop : Obj.Cast<UEClass>().GetProperties())
			{
				if (Prop.IsA(EClassCastFlags::MulticastInlineDelegateProperty))
				{
					this->MulticastInlineDelegateProperty.SizeOf = Prop.GetSize();
					return;
				}
			}
		}
	};

	const UEClass EmitterClass = ObjectArray::FindClassFast("Emitter");

	if (!EmitterClass)
		return OnPropertyNotFound();

	const UEProperty OnParticleSpawn = EmitterClass.FindMember("OnParticleSpawn", EClassCastFlags::MulticastDelegateProperty);

	if (!OnParticleSpawn)
		return OnPropertyNotFound();

	this->MulticastInlineDelegateProperty.SizeOf = OnParticleSpawn.GetSize();
}

void FInSDKOffsets::InitProcessEvent()
{
	/* ---- Detection tuning ---- */
	constexpr int32 kMaxVTableEntries = 100;   // virtual slots probed
	constexpr int32 kScanBytes        = 0x200; // bytes of each virtual decoded before next-function detection kicks in
	constexpr int32 kMinScore         = 4;     // distinct signals required to accept a slot

	// ProcessEvent may reference the outer FUObjectArray or the ObjObjects sub-struct
	// embedded within it, so anchors are matched by proximity rather than equality.
	constexpr uintptr_t kObjectArraySlack = 0x10;

	if (!GArchDecoder)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitProcessEvent: no architecture decoder is active!\n");
		return;
	}

	/*
	 * Any UObject inherits ProcessEvent at the same vtable slot, but not every live
	 * object is an equally trustworthy source for it: an Actor or Blueprint-generated
	 * class can carry extra virtuals ahead of the inherited UObject ones on some
	 * engine configurations, and the first object encountered in ObjectArray order is
	 * unpredictable. UEngine and UWorld are always native, non-Blueprint singleton
	 * classes, so their CDO's vtable is a reliable stand-in for "the real UObject
	 * vtable layout" without hardcoding an index.
	 */
	auto GetClassVTable = [](const char* ClassName) -> uintptr_t
	{
		const UEClass Class = ObjectArray::FindClassFast(ClassName);
		if (!Class)
			return 0;

		const UEObject Cdo = Class.GetDefaultObject();
		if (!Cdo)
			return 0;

		const uintptr_t ObjAddr = reinterpret_cast<uintptr_t>(Cdo.GetAddress());
		if (!GMemory->IsAddressReadable(ObjAddr))
			return 0;

		const uintptr_t Candidate = GMemory->Read<uintptr_t>(ObjAddr);
		return GMemory->IsAddressReadable(Candidate, sizeof(void*) * 8) ? Candidate : 0;
	};

	uintptr_t VTable = GetClassVTable("Engine");
	if (VTable == 0)
		VTable = GetClassVTable("World");

	if (VTable == 0)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitProcessEvent: could not read the UEngine or UWorld vtable!\n");
		return;
	}

	const int64_t ExpUObjectIndex  = GOffsets.UObject.Index;
	const int64_t ExpFunctionFlags = GOffsets.UFunction.FunctionFlags;
	const int64_t ExpStructSize    = GOffsets.UStruct.Size;
	const int64_t ExpParamSize     = GOffsets.UFunction.ParamSize;
	const int64_t ExpChildren      = GOffsets.UStruct.ChildProperties != -1 ? GOffsets.UStruct.ChildProperties : GOffsets.UStruct.Children;

	int64_t ExpItemSize       = -1;
	uintptr_t ExpObjectsField = 0;
	if (GLayouts.ObjectsLayout)
	{
		if (GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked)
		{
			const FChunkedUObjectArrayLayout* Layout = static_cast<const FChunkedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
			ExpItemSize                              = Layout->FUObjectItem.Size;
			ExpObjectsField                          = GObjects + static_cast<uintptr_t>(Layout->Objects);
		}
		else
		{
			const FFixedUObjectArrayLayout* Layout = static_cast<const FFixedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
			ExpItemSize                            = Layout->FUObjectItem.Size;
			ExpObjectsField                        = GObjects + static_cast<uintptr_t>(Layout->Objects);
		}
	}

	/*
	 * When the object array was reached by dereferencing a static, the code references
	 * that static rather than the array, so its address is an anchor in its own right.
	 */
	const uintptr_t ExpStaticAddr = this->Statics.GObjects != 0 ? GMemory->GetUnrealModule().OffsetToAddress(this->Statics.GObjects) : 0;

	/* An exported symbol settles the slot outright, so only its index must be found. */
	const uintptr_t SymbolAddr = GMemory->FindUnrealSymbol("_ZN7UObject12ProcessEventEP9UFunctionPv");

	if (SymbolAddr == 0 && ExpUObjectIndex == -1 && ExpFunctionFlags == -1 && ExpStructSize == -1)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitProcessEvent: no symbol and no reflected offsets to match against!\n");
		return;
	}

	auto IsNearAnchor = [ExpObjectsField, ExpStaticAddr](uintptr_t Address) -> bool
	{
		if (Address == 0)
			return false;

		auto WithinSlack = [Address](uintptr_t Anchor) -> bool
		{
			if (Anchor == 0)
				return false;

			return Address + kObjectArraySlack >= Anchor && Address <= Anchor + kObjectArraySlack;
		};

		return WithinSlack(GObjects) || WithinSlack(ExpObjectsField) || WithinSlack(ExpStaticAddr);
	};

	auto ReferencesObjectArray = [&IsNearAnchor](uintptr_t Address) -> bool
	{
		if (IsNearAnchor(Address))
			return true;

		// The materialized value is frequently the address *of* the global rather than
		// the global itself, so one dereference is also accepted.
		if (!GMemory->IsAddressReadable(Address))
			return false;

		return IsNearAnchor(GMemory->Read<uintptr_t>(Address));
	};

	// Ten independent signals, matching the upstream reference implementation's `oks[10]`.
	// ParamSize and Children are each referenced twice in a genuine ProcessEvent (once
	// to size the parameter block, once to marshal it), so the second occurrence of
	// either is scored as its own signal rather than folded into the first.
	struct FSignals
	{
		bool bObjectsArray  = false;
		bool bUObjectIndex  = false;
		bool bItemSize      = false;
		bool bFunctionFlag1 = false;
		bool bStructSize    = false;
		bool bParamSize1    = false;
		bool bParamSize2    = false;
		bool bChildren1     = false;
		bool bFunctionFlag2 = false;
		bool bChildren2     = false;

		int32 Count() const
		{
			return static_cast<int32>(bObjectsArray) + static_cast<int32>(bUObjectIndex) + static_cast<int32>(bItemSize) +
			       static_cast<int32>(bFunctionFlag1) + static_cast<int32>(bStructSize) + static_cast<int32>(bParamSize1) +
			       static_cast<int32>(bParamSize2) + static_cast<int32>(bChildren1) + static_cast<int32>(bFunctionFlag2) +
			       static_cast<int32>(bChildren2);
		}

		std::string Describe() const
		{
			return fmt::format("Objs={} Idx={} Item={} Flags1={} Size={} Parms1={} Parms2={} Children1={} Flags2={} Children2={}",
			                   bObjectsArray,
			                   bUObjectIndex,
			                   bItemSize,
			                   bFunctionFlag1,
			                   bStructSize,
			                   bParamSize1,
			                   bParamSize2,
			                   bChildren1,
			                   bFunctionFlag2,
			                   bChildren2);
		}
	};

	/*
	 * Decoding goes through GArchDecoder, so ARM64 and ARM32 share one walk. On ARM32 an
	 * address can arrive as a literal-pool word instead of an immediate, and whether
	 * that word is the address or a PC-relative displacement is only settled by what
	 * follows, so both readings are tested.
	 */
	auto ScoreFunction = [&](uintptr_t FuncAddr) -> FSignals
	{
		FSignals Signals;

		std::vector<uint8_t> Code(kScanBytes, 0);
		if (!GMemory->IsAddressReadable(FuncAddr, kScanBytes) || !GMemory->ReadBytes(FuncAddr, Code.data(), Code.size()))
			return Signals;

		const int32 RegCount  = GArchDecoder->RegisterCount();
		const uint32_t Stride = GArchDecoder->InstructionStride();

		std::vector<uintptr_t> RegValue(static_cast<size_t>(RegCount), 0);
		std::vector<bool> bRegKnown(static_cast<size_t>(RegCount), false);

		bool bSeenReturn = false;

		size_t Cursor = 0;
		while (Cursor + 2 <= Code.size())
		{
			NormalizedInsn Insn;
			const bool bDecoded  = GArchDecoder->DecodeLocalBytes(Code.data() + Cursor, Code.size() - Cursor, FuncAddr + Cursor, Insn);
			const size_t Advance = Insn.Length ? Insn.Length : (Stride ? Stride : 4);

			if (!bDecoded)
			{
				Cursor += Advance;
				continue;
			}

			/*
			 * A fixed byte window can run past a short function's end into whatever
			 * follows it, and scoring that neighbor's instructions as this slot's own
			 * produces false signals. "A second Prologue" is not a safe boundary: a
			 * real ProcessEvent routinely emits several of them in its own entry
			 * sequence alone (an STP to save callee-saved register pairs, followed by
			 * a separate SUB SP,SP,#N for locals, are both classified as Prologue).
			 * What is reliable is that a fresh Prologue cannot belong to this function
			 * once it has already returned, so the boundary is "Prologue after a
			 * Return", not "the second Prologue".
			 */
			if (Insn.Kind == NormalizedInsn::EKind::Prologue && bSeenReturn)
				break;

			if (Insn.Kind == NormalizedInsn::EKind::Return)
				bSeenReturn = true;

			const bool bValidDest = Insn.Dest >= 0 && Insn.Dest < RegCount;
			const bool bValidBase = Insn.Base >= 0 && Insn.Base < RegCount;

			switch (Insn.Kind)
			{
			case NormalizedInsn::EKind::SetBase:
				if (bValidDest)
				{
					RegValue[Insn.Dest]  = static_cast<uintptr_t>(Insn.Value);
					bRegKnown[Insn.Dest] = true;
					if (Insn.bExactAddress && ReferencesObjectArray(RegValue[Insn.Dest]))
						Signals.bObjectsArray = true;
				}
				break;

			case NormalizedInsn::EKind::AddImm:
				if (bValidDest && bValidBase && bRegKnown[Insn.Base])
				{
					RegValue[Insn.Dest]  = RegValue[Insn.Base] + static_cast<uintptr_t>(Insn.Value);
					bRegKnown[Insn.Dest] = true;
					if (ReferencesObjectArray(RegValue[Insn.Dest]))
						Signals.bObjectsArray = true;
				}
				else if (bValidDest)
				{
					bRegKnown[Insn.Dest] = false;
				}
				break;

			case NormalizedInsn::EKind::LoadLiteral:
				if (bValidDest)
				{
					uintptr_t Word = 0;
					if (GMemory->IsAddressReadable(static_cast<uintptr_t>(Insn.Value)))
						Word = GMemory->Read<uintptr_t>(static_cast<uintptr_t>(Insn.Value));

					RegValue[Insn.Dest]  = Word;
					bRegKnown[Insn.Dest] = Word != 0;

					if (ReferencesObjectArray(Word) || ReferencesObjectArray(static_cast<uintptr_t>(Insn.Value) + Word))
						Signals.bObjectsArray = true;
				}
				break;

			case NormalizedInsn::EKind::MoveImm:
				if (bValidDest)
				{
					RegValue[Insn.Dest]  = Insn.bComposes ? (RegValue[Insn.Dest] | static_cast<uintptr_t>(Insn.Value)) : static_cast<uintptr_t>(Insn.Value);
					bRegKnown[Insn.Dest] = Insn.bAddressLike || Insn.bComposes;

					if (Insn.bAddressLike && ReferencesObjectArray(RegValue[Insn.Dest]))
						Signals.bObjectsArray = true;
				}

				if (ExpItemSize > 0 && static_cast<int64_t>(Insn.Value) == ExpItemSize)
					Signals.bItemSize = true;
				break;

			case NormalizedInsn::EKind::Load:
			case NormalizedInsn::EKind::Store:
				if (ExpUObjectIndex != -1 && Insn.Offset == ExpUObjectIndex)
					Signals.bUObjectIndex = true;

				// The flags word is read a byte at a time to test individual bits, so each
				// byte is its own signal rather than one range covering the whole word.
				if (ExpFunctionFlags != -1 && Insn.Offset == ExpFunctionFlags + 1)
					Signals.bFunctionFlag1 = true;

				if (ExpFunctionFlags != -1 && Insn.Offset == ExpFunctionFlags + 2)
					Signals.bFunctionFlag2 = true;

				if (ExpStructSize != -1 && Insn.Offset == ExpStructSize)
					Signals.bStructSize = true;

				// ParamSize is read twice - once for memset, once for memcpy - so the
				// second occurrence only counts once the first has already been seen.
				if (ExpParamSize != -1 && Insn.Offset == ExpParamSize)
				{
					if (Signals.bParamSize1)
						Signals.bParamSize2 = true;
					else
						Signals.bParamSize1 = true;
				}

				// Children is walked twice as well, so the same two-hit pattern applies.
				if (ExpChildren != -1 && Insn.Offset == ExpChildren)
				{
					if (Signals.bChildren1)
						Signals.bChildren2 = true;
					else
						Signals.bChildren1 = true;
				}

				/*
				 * A global is reached as ADRP + ADD or, just as often, ADRP + LDR with the
				 * displacement folded into the load. Testing only the ADD form makes the
				 * match depend on which encoding the compiler happened to pick, so the
				 * address this access forms is checked here too.
				 */
				if (bValidBase && bRegKnown[Insn.Base])
				{
					const uintptr_t Accessed = RegValue[Insn.Base] + static_cast<uintptr_t>(Insn.Offset);
					if (ReferencesObjectArray(Accessed))
						Signals.bObjectsArray = true;

					// Carry the loaded word forward so a following ADD that steps to the
					// embedded sub-struct still resolves.
					if (bValidDest && Insn.Kind == NormalizedInsn::EKind::Load && GMemory->IsAddressReadable(Accessed))
					{
						RegValue[Insn.Dest]  = GMemory->Read<uintptr_t>(Accessed);
						bRegKnown[Insn.Dest] = RegValue[Insn.Dest] != 0;

						if (ReferencesObjectArray(RegValue[Insn.Dest]))
							Signals.bObjectsArray = true;

						if (Insn.bWritesBackBase)
							bRegKnown[Insn.Base] = false;
						break;
					}
				}

				// A write-back form changes the base as a side effect, and a load retires
				// whatever the destination previously held.
				if (Insn.bWritesBackBase && bValidBase)
					bRegKnown[Insn.Base] = false;

				if (bValidDest && Insn.Kind == NormalizedInsn::EKind::Load)
					bRegKnown[Insn.Dest] = false;
				break;

			case NormalizedInsn::EKind::Call:
				for (int32 Reg = 0; Reg < RegCount; Reg++)
				{
					if (GArchDecoder->CallClobbers(Reg))
						bRegKnown[Reg] = false;
				}
				break;

			case NormalizedInsn::EKind::Return:
				std::fill(bRegKnown.begin(), bRegKnown.end(), false);
				break;

			default:
				if (bValidDest)
					bRegKnown[Insn.Dest] = false;
				break;
			}

			Cursor += Advance;
		}

		return Signals;
	};

	const auto UnrealModule = GMemory->GetUnrealModule();

	int32 BestIndex    = -1;
	int32 BestScore    = 0;
	uintptr_t BestAddr = 0;
	bool bBySymbol     = false;
	FSignals BestSignals;

	for (int32 Index = 0; Index < kMaxVTableEntries; Index++)
	{
		const uintptr_t SlotAddr = VTable + static_cast<uintptr_t>(Index) * sizeof(void*);

		// A stray non-function slot (RTTI-adjacent data, padding, a transiently
		// unreadable page) does not necessarily mean the vtable has ended, so a bad
		// slot is skipped rather than treated as a stopping point - every index up to
		// kMaxVTableEntries is tried.
		if (!GMemory->IsAddressReadable(SlotAddr))
			continue;

		const uintptr_t FuncAddr = GMemory->Read<uintptr_t>(SlotAddr);
		if (!GMemory->IsAddressReadable(FuncAddr) || !UnrealModule.Contains(FuncAddr))
			continue;

		if (SymbolAddr != 0 && FuncAddr == SymbolAddr)
		{
			BestIndex = Index;
			BestAddr  = FuncAddr;
			bBySymbol = true;
			break;
		}

		/* On ARM32 the low bit selects Thumb state and is not part of the address. */
		const uintptr_t CodeAddr = GArchDecoder->Arch() == EArch::Arm32 ? (FuncAddr & ~static_cast<uintptr_t>(1)) : FuncAddr;

		const FSignals Signals = ScoreFunction(CodeAddr);
		if (Signals.Count() > BestScore)
		{
			BestScore   = Signals.Count();
			BestIndex   = Index;
			BestAddr    = FuncAddr;
			BestSignals = Signals;
		}
	}

	if (BestIndex < 0)
	{
		GLogger.FmtWrite(ELogLevel::Error, "InitProcessEvent: No VTable slot matched ProcessEvent!\n");
		return;
	}

	if (bBySymbol)
		GLogger.FmtWrite(ELogLevel::Info, "InitProcessEvent: Resolved by exported symbol at VTable index {}.\n", BestIndex);
	else if (BestScore < kMinScore)
		GLogger.FmtWrite(ELogLevel::Warning, "InitProcessEvent: Best slot {} scored only {}/{} ({}) - result may be wrong.\n", BestIndex, BestScore, kMinScore, BestSignals.Describe());
	else
		GLogger.FmtWrite(ELogLevel::Info, "InitProcessEvent: VTable index {} scored {} ({})\n", BestIndex, BestScore, BestSignals.Describe());

	this->Statics.PEIndex = BestIndex;

	// Consumers emit this into the SDK as a module-relative offset, and the field is an
	// int32, so an absolute address would both mislead and truncate.
	this->Statics.PEOffset = GMemory->GetUnrealModule().AddressToOffset(BestAddr & ~static_cast<uintptr_t>(1));
}

void FInSDKOffsets::Init()
{
	GLogger.FmtWrite(ELogLevel::Info, "Finding ULevel.Actors...\n");
	this->InitLevelActorsOffset();
	GLogger.FmtWrite(ELogLevel::Info, "ULevel.Actors = 0x{:X}\n", (uint32_t)ULevel.Actors);

	GLogger.FmtWrite(ELogLevel::Info, "Finding UDataTable.RowMap...\n");
	this->InitDatatableRowMapOffset();
	GLogger.FmtWrite(ELogLevel::Info, "UDataTable.RowMap = 0x{:X}\n", (uint32_t)UDataTable.RowMap);

	GLogger.FmtWrite(ELogLevel::Info, "Finding GEngine & GWorld Offsets...\n");
	this->InitUEngineAndUWorld();
	if (Statics.GEngine && Statics.GWorld == 0)
	{
		GLogger.FmtWrite(ELogLevel::Info, "GWorld wasn't found, You may get GWorld via GEngine->GameViewport->World\n");
	}
	GLogger.FmtWrite(ELogLevel::Info, "GEngine Offset: 0x{:X}\n", Statics.GEngine);
	GLogger.FmtWrite(ELogLevel::Info, "GWorld Offset: 0x{:X}\n", Statics.GWorld);

	GLogger.FmtWrite(ELogLevel::Info, "Finding ProcessEvent...\n");
	this->InitProcessEvent();

	GLogger.FmtWrite(ELogLevel::Info, "ProcessEvent Index: {}\n", Statics.PEIndex);
	GLogger.FmtWrite(ELogLevel::Info, "ProcessEvent Offset: 0x{:X}\n", Statics.PEOffset);

	GLogger.FmtWrite(ELogLevel::Info, "Finding FText Offsets...\n");
	this->InitFText();
	GLogger.FmtWrite(ELogLevel::Info, "FText.Size: 0x{:X}\n", FText.Size);
	GLogger.FmtWrite(ELogLevel::Info, "FText.TextData: 0x{:X}\n", FText.TextData);
	GLogger.FmtWrite(ELogLevel::Info, "FText.InTextDataString: 0x{:X}\n", FText.InTextDataString);
	GLogger.FmtWrite(ELogLevel::Info, "InternalSettings::bUseChar16String: {}\n", InternalSettings::bUseChar16String);

	GLogger.FmtWrite(ELogLevel::Info, "Finding TDelegateSize...\n");
	this->InitTDelegateSize();
	GLogger.FmtWrite(ELogLevel::Info, "DelegateProperty.SizeOf = 0x{:X}\n", (uint32_t)DelegateProperty.SizeOf);

	GLogger.FmtWrite(ELogLevel::Info, "Finding FFieldPathSize...\n");
	this->InitFFieldPathSize();
	GLogger.FmtWrite(ELogLevel::Info, "FieldPathProperty.SizeOf = 0x{:X}\n", (uint32_t)FieldPathProperty.SizeOf);

	GLogger.FmtWrite(ELogLevel::Info, "Finding TMulticastInlineDelegateSize...\n");
	this->InitTMulticastInlineDelegateSize();
	GLogger.FmtWrite(ELogLevel::Info, "MulticastInlineDelegateProperty.SizeOf = 0x{:X}\n", (uint32_t)MulticastInlineDelegateProperty.SizeOf);
}