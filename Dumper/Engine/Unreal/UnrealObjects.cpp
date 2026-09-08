#include "UnrealObjects.h"

#include <unordered_set>

#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"

#include "../OffsetFinder/Offsets.h"

#include "ObjectArray.h"

UEFFieldClass::UEFFieldClass(void* NewFieldClass)
{
	Class = !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(NewFieldClass)) ? nullptr : reinterpret_cast<uint8*>(NewFieldClass);
}

UEFFieldClass::UEFFieldClass(const UEFFieldClass& OldFieldClass)
{
	Class = !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(OldFieldClass.Class)) ? nullptr : OldFieldClass.Class;
}

void* UEFFieldClass::GetAddress()
{
	return Class;
}

UEFFieldClass::operator bool() const
{
	return Class != nullptr;
}

EClassCastFlags UEFFieldClass::GetCastFlags() const
{
	return GMemory->Read<EClassCastFlags>(reinterpret_cast<uintptr_t>(Class) + GOffsets.FFieldClass.CastFlags);
}

FName UEFFieldClass::GetFName() const
{
	return FName(Class + GOffsets.FFieldClass.Name); // Not the real FName, but a wrapper which holds the address of a FName
}

bool UEFFieldClass::IsType(EClassCastFlags Flags) const
{
	return (Flags != EClassCastFlags::None ? (GetCastFlags() & Flags) : true);
}

std::string UEFFieldClass::GetName() const
{
	return Class ? GetFName().ToString() : "None";
}

std::string UEFFieldClass::GetValidName() const
{
	return Class ? GetFName().ToValidString() : "None";
}

std::string UEFFieldClass::GetCppName() const
{
	// This is evile dark magic code which shouldn't exist
	return "F" + GetValidName();
}

UEFField::UEFField(void* NewField)
{
	Field = !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(NewField)) ? nullptr : reinterpret_cast<uint8*>(NewField);
}

UEFField::UEFField(const UEFField& OldField)
{
	Field = !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(OldField.Field)) ? nullptr : OldField.Field;
}

void* UEFField::GetAddress()
{
	return Field;
}

const void* UEFField::GetAddress() const
{
	return Field;
}

UEObject::UEObject(void* NewObject)
{
	Object = !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(NewObject)) ? nullptr : reinterpret_cast<uint8*>(NewObject);
}

class UEObject UEFField::GetOwnerAsUObject() const
{
	if (IsOwnerUObject())
	{
		if (InternalSettings::bUseMaskForFieldOwner)
			return reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Owner) & ~0x1ull);

		return reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Owner));
	}

	return nullptr;
}

class UEFField UEFField::GetOwnerAsFField() const
{
	if (!IsOwnerUObject())
		return reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Owner));

	return nullptr;
}

class UEObject UEFField::GetOwnerUObject() const
{
	UEFField Field = *this;

	while (!Field.IsOwnerUObject() && Field.GetOwnerAsFField())
	{
		Field = Field.GetOwnerAsFField();
	}

	return Field.GetOwnerAsUObject();
}


UEFFieldClass UEFField::GetClass() const
{
	return UEFFieldClass(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Class)));
}

FName UEFField::GetFName() const
{
	return FName(Field + GOffsets.FField.Name); // Not the real FName, but a wrapper which holds the address of a FName
}

UEFField UEFField::GetNext() const
{
	return UEFField(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Next)));
}

std::vector<std::pair<std::string, std::string>> UEFField::GetMetaData() const
{
	// using ValueType = std::conditional_t<sizeof(void*) == 0x8, int64, int32>;

	struct alignas(0x4) Name04Byte
	{
		uint8 Pad[0x04];
	};
	struct alignas(0x4) Name08Byte
	{
		uint8 Pad[0x08];
	};
	struct alignas(0x4) Name12Byte
	{
		uint8 Pad[0x0C];
	};
	struct alignas(0x4) Name16Byte
	{
		uint8 Pad[0x10];
	};

	// static constexpr uintptr_t PointeFlagHasTag = 0x1;
	// static constexpr uintptr_t PointerMaskNoTag = ~0x1;


	static auto GetPairsAsStrings = []<typename NameType>(uintptr_t MapAddr)
	{
		std::vector<std::pair<std::string, std::string>> Result;

		TMap<NameType, FString> LocalMap = GMemory->Read<TMap<NameType, FString>>(MapAddr);
		if (!LocalMap.IsValid())
			return Result;

		for (int32 i = 0; i < LocalMap.NumAllocated(); i++)
		{
			if (!LocalMap.GetAllocationFlags()[i])
				continue;

			const uintptr_t ElemAddr = LocalMap.GetElementAddress(i);
			FString LocalStr         = GMemory->Read<FString>(ElemAddr + sizeof(NameType));
			Result.emplace_back(
			    FName(reinterpret_cast<const void*>(ElemAddr)).ToString(),
			    LocalStr.ToString());
		}

		return Result;
	};

	const uintptr_t mapAddr = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.EditorOnlyMetadata);

	if (!mapAddr || !GMemory->IsAddressReadable(mapAddr))
		return {};

	if (GOffsets.FName.SizeOf > 0x8)
		return GetPairsAsStrings.template operator()<Name16Byte>(mapAddr);

	return GetPairsAsStrings.template operator()<Name08Byte>(mapAddr);
}

template <typename UEType>
UEType UEFField::Cast() const
{
	return UEType(Field);
}

bool UEFField::IsOwnerUObject() const
{
	if (InternalSettings::bUseMaskForFieldOwner)
	{
		return GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Owner) & 0x1;
	}

	return GMemory->Read<bool>(reinterpret_cast<uintptr_t>(Field) + GOffsets.FField.Owner + 0x8);
}

bool UEFField::IsA(EClassCastFlags Flags) const
{
	return (Flags != EClassCastFlags::None ? GetClass().IsType(Flags) : true);
}

std::string UEFField::GetName() const
{
	return Field ? GetFName().ToString() : "None";
}

std::string UEFField::GetValidName() const
{
	return Field ? GetFName().ToValidString() : "None";
}

std::string UEFField::GetCppName() const
{
	static UEClass ActorClass     = ObjectArray::FindClassFast("Actor");
	static UEClass InterfaceClass = ObjectArray::FindClassFast("Interface");

	std::string Temp = GetValidName();

	if (IsA(EClassCastFlags::Class))
	{
		if (Cast<UEClass>().HasType(ActorClass))
		{
			return 'A' + Temp;
		}
		else if (Cast<UEClass>().HasType(InterfaceClass))
		{
			return 'I' + Temp;
		}

		return 'U' + Temp;
	}

	return 'F' + Temp;
}

UEFField::operator bool() const
{
	return Field != nullptr && reinterpret_cast<void*>(Field + GOffsets.FField.Class) != nullptr;
}

bool UEFField::operator==(const UEFField& Other) const
{
	return Field == Other.Field;
}

bool UEFField::operator!=(const UEFField& Other) const
{
	return Field != Other.Field;
}

void (*UEObject::PE)(void*, void*, void*) = nullptr;

void* UEObject::GetAddress()
{
	return Object;
}

const void* UEObject::GetAddress() const
{
	return Object;
}

void* UEObject::GetVft() const
{
	return reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object)));
}

EObjectFlags UEObject::GetFlags() const
{
	return GMemory->Read<EObjectFlags>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UObject.Flags);
}

int32 UEObject::GetIndex() const
{
	return GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UObject.Index);
}

UEClass UEObject::GetClass() const
{
	return UEClass(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UObject.Class)));
}

FName UEObject::GetFName() const
{
	return FName(Object + GOffsets.UObject.Name); // Not the real FName, but a wrapper which holds the address of a FName
}

UEObject UEObject::GetOuter() const
{
	return UEObject(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UObject.Outer)));
}

int32 UEObject::GetPackageIndex() const
{
	return GetOutermost().GetIndex();
}

bool UEObject::HasAnyFlags(EObjectFlags Flags) const
{
	return GetFlags() & Flags;
}

bool UEObject::IsA(EClassCastFlags TypeFlags) const
{
	return (TypeFlags != EClassCastFlags::None ? GetClass().IsType(TypeFlags) : true);
}

bool UEObject::IsA(UEClass Class) const
{
	if (!Class)
		return false;

	for (UEClass Clss = GetClass(); Clss; Clss = Clss.GetSuper().Cast<UEClass>())
	{
		if (Clss == Class)
			return true;
	}

	return false;
}

UEObject UEObject::GetOutermost() const
{
	UEObject Outermost = *this;

	for (UEObject Outer = *this; GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Outer.GetAddress())); Outer = Outer.GetOuter())
	{
		Outermost = Outer;
	}

	return Outermost;
}

std::string UEObject::StringifyObjFlags() const
{
	return *this ? StringifyObjectFlags(GetFlags()) : "NoFlags";
}

std::string UEObject::GetName() const
{
	return Object ? GetFName().ToString() : "None";
}

std::string UEObject::GetNameWithPath() const
{
	return Object ? GetFName().ToRawString() : "None";
}

std::string UEObject::GetValidName() const
{
	return Object ? GetFName().ToValidString() : "None";
}

std::string UEObject::GetCppName() const
{
	static UEClass ActorClass     = nullptr;
	static UEClass InterfaceClass = nullptr;

	if (ActorClass == nullptr)
		ActorClass = ObjectArray::FindClassFast("Actor");

	if (InterfaceClass == nullptr)
		InterfaceClass = ObjectArray::FindClassFast("Interface");

	std::string Temp = GetValidName();

	if (IsA(EClassCastFlags::Class))
	{
		if (Cast<UEClass>().HasType(ActorClass))
		{
			return 'A' + Temp;
		}
		else if (Cast<UEClass>().HasType(InterfaceClass))
		{
			return 'I' + Temp;
		}

		return 'U' + Temp;
	}

	return 'F' + Temp;
}

std::string UEObject::GetFullName(int32& OutNameLength) const
{
	if (*this)
	{
		std::string Temp;

		for (UEObject Outer = GetOuter(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Outer.GetAddress())); Outer = Outer.GetOuter())
		{
			Temp = Outer.GetName() + '.' + Temp;
		}

		std::string Name = GetName();
		OutNameLength    = Name.size() + 1;

		Name = GetClass().GetName() + ' ' + Temp + Name;

		return Name;
	}

	return "None";
}

std::string UEObject::GetFullName() const
{
	if (*this)
	{
		std::string Temp;

		for (UEObject Outer = GetOuter(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Outer.GetAddress())); Outer = Outer.GetOuter())
		{
			Temp = Outer.GetName() + "." + Temp;
		}

		std::string Name  = GetClass().GetName();
		Name             += " ";
		Name             += Temp;
		Name             += GetName();

		return Name;
	}

	return "None";
}

std::string UEObject::GetPathName() const
{
	if (*this)
	{
		std::string Temp;

		for (UEObject Outer = GetOuter(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Outer.GetAddress())); Outer = Outer.GetOuter())
		{
			Temp = Outer.GetNameWithPath() + "." + Temp;
		}

		std::string Name  = GetClass().GetNameWithPath();
		Name             += " ";
		Name             += Temp;
		Name             += GetNameWithPath();

		return Name;
	}

	return "None";
}


UEObject::operator bool() const
{
	// if an object is 0x10000F000 it passes the nullptr check
	return Object != nullptr && reinterpret_cast<void*>(Object + GOffsets.UObject.Class) != nullptr;
}

UEObject::operator uint8*()
{
	return Object;
}

bool UEObject::operator==(const UEObject& Other) const
{
	return Object == Other.Object;
}

bool UEObject::operator!=(const UEObject& Other) const
{
	return Object != Other.Object;
}

void UEObject::ProcessEvent(UEFunction Func, void* Params)
{
	uintptr_t VftAddr                = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(GetAddress()));
	uintptr_t PrdAddr                = GMemory->Read<uintptr_t>(VftAddr + static_cast<uintptr_t>(GInSDKOffsets.Statics.PEIndex) * sizeof(void*));
	void (*Prd)(void*, void*, void*) = reinterpret_cast<decltype(Prd)>(PrdAddr);

	Prd(Object, Func.GetAddress(), Params);
}

UEField UEField::GetNext() const
{
	return UEField(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UField.Next)));
}

bool UEField::IsNextValid() const
{
	return GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(GetNext().GetAddress()));
}

std::vector<std::pair<FName, int64>> UEEnum::GetNameValuePairs() const
{
	// using ValueType = std::conditional_t<sizeof(void*) == 0x8, int64, int32>;

	struct alignas(0x4) Name04Byte
	{
		uint8 Pad[0x04];
	};
	struct alignas(0x4) Name08Byte
	{
		uint8 Pad[0x08];
	};
	struct alignas(0x4) Name12Byte
	{
		uint8 Pad[0x0C];
	};
	struct alignas(0x4) Name16Byte
	{
		uint8 Pad[0x10];
	};
	struct alignas(0x4) UInt8As64
	{
		uint8 Bytes[sizeof(void*)];
		inline operator int64() const { return Bytes[0]; };
	};

	static constexpr uintptr_t PointeFlagHasTag = 0x1;
	static constexpr uintptr_t PointerMaskNoTag = ~0x1;

	/*
	 * For UEVersion >= UE5.6
	 *
	 * See: https://github.com/EpicGames/UnrealEngine/blob/ue5-main/Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h#L3411
	 */
	static auto GetNameValuePairsForFNameData = [](const uintptr_t Object, const uint32_t EnumNamesOffset, const uint32_t FNameSize)
	{
		std::vector<std::pair<FName, int64>> Ret;

		const uintptr_t TaggedNamesPtr = GMemory->Read<uintptr_t>(Object + EnumNamesOffset);
		const bool bIsNamesPtrTagged   = (TaggedNamesPtr & PointeFlagHasTag) != 0;
		const uint8* NamesPtr          = reinterpret_cast<uint8*>(TaggedNamesPtr & PointerMaskNoTag);

		if (!bIsNamesPtrTagged)
		{
			/* StaticNamesUTF8 is not supported yet. See: https://github.com/EpicGames/UnrealEngine/blob/ue5-main/Engine/Source/Runtime/CoreUObject/Public/UObject/Class.h#L3408*/
			GLogger.FmtWrite(ELogLevel::Error, "[UEEnum::GetNameValuePairs()]: UEnum::Names pointer is tagged! This is not supported yet!\n");
		}

		const uintptr_t ValuesAddr = GMemory->Read<uintptr_t>(Object + EnumNamesOffset + 0x8) & PointerMaskNoTag;
		const int32 NumValues      = GMemory->Read<int32>(Object + EnumNamesOffset + 0x10);

		for (int32 i = 0; i < NumValues; i++)
		{
			Ret.push_back({FName(NamesPtr + (i * FNameSize)), GMemory->Read<int64>(ValuesAddr + i * sizeof(int64))});
		}

		return Ret;
	};

	if (InternalSettings::bIsNewUE5EnumNamesContainer)
	{
		return GetNameValuePairsForFNameData(reinterpret_cast<const uintptr_t>(Object), GOffsets.UEnum.Names - 0x8, GOffsets.FName.SizeOf);
	}


	static auto GetNameValuePairsWithIndex = []<typename NameType, typename ValueType>(const TArray<TPair<NameType, ValueType>>& EnumNameValuePairs)
	{
		std::vector<std::pair<FName, int64>> Ret;

		for (int i = 0; i < EnumNameValuePairs.Num(); i++)
		{
			const uintptr_t ElemAddr = EnumNameValuePairs.GetElementAddress(i);
			Ret.push_back({FName(reinterpret_cast<const void*>(ElemAddr)),
			               GMemory->Read<ValueType>(ElemAddr + sizeof(NameType))});
		}

		return Ret;
	};

	static auto GetNameValuePairs = []<typename NameType>(const TArray<NameType>& EnumNameValuePairs)
	{
		std::vector<std::pair<FName, int64>> Ret;

		for (int i = 0; i < EnumNameValuePairs.Num(); i++)
		{
			Ret.push_back({FName(reinterpret_cast<const void*>(EnumNameValuePairs.GetElementAddress(i))),
			               i});
		}

		return Ret;
	};

	if (InternalSettings::bIsEnumNameOnly)
	{
		if (InternalSettings::bUseCasePreservingName)
			return GetNameValuePairs(GMemory->Read<TArray<Name16Byte>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.Names));

		return GetNameValuePairs(GMemory->Read<TArray<Name08Byte>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.Names));
	}
	else
	{
		/* This only applies very very rarely on weird UE4.13 or UE4.14 games where the devs didn't know what they were doing. */
		if (InternalSettings::bIsSmallEnumValue)
		{
			if (InternalSettings::bUseCasePreservingName)
				return GetNameValuePairsWithIndex(GMemory->Read<TArray<TPair<Name16Byte, UInt8As64>>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.Names));

			return GetNameValuePairsWithIndex(GMemory->Read<TArray<TPair<Name08Byte, UInt8As64>>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.Names));
		}

		if (InternalSettings::bUseCasePreservingName)
			return GetNameValuePairsWithIndex(GMemory->Read<TArray<TPair<Name16Byte, int64>>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.Names));

		return GetNameValuePairsWithIndex(GMemory->Read<TArray<TPair<Name08Byte, int64>>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.Names));
	}
}

std::string UEEnum::GetSingleName(int32 Index) const
{
	return GetNameValuePairs()[Index].first.ToString();
}

std::string UEEnum::GetEnumPrefixedName() const
{
	std::string Temp = GetValidName();

	return /*Temp[0] == 'E' ? Temp :*/ 'E' + Temp;
}

std::string UEEnum::GetEnumTypeAsStr() const
{
	return "enum class " + GetEnumPrefixedName();
}

std::pair<uint8_t, bool> UEEnum::GetSizeSignedPair() const
{
	if (!InternalSettings::bHasUnderlayingTypeInUEnum)
		return {1, false};

	const EUnderlyingType Type = GMemory->Read<EUnderlyingType>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UEnum.UnderlyingType);

	const bool bIsSigned = Type < EUnderlyingType::uint8;
	const uint8_t Size   = 1 << (static_cast<uint8_t>(Type) % 4);

	return {Size, bIsSigned};
}

UEStruct UEStruct::GetSuper() const
{
	return UEStruct(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UStruct.SuperStruct)));
}

UEField UEStruct::GetChild() const
{
	return UEField(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UStruct.Children)));
}

UEFField UEStruct::GetChildProperties() const
{
	return UEFField(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UStruct.ChildProperties)));
}

int16 UEStruct::GetMinAlignment() const
{
	return GMemory->Read<int16>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UStruct.MinAlignment);
}

int32 UEStruct::GetStructSize() const
{
	return GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UStruct.Size);
}

bool UEStruct::HasType(UEStruct Type) const
{
	if (Type == nullptr)
		return false;

	for (UEStruct S = *this; GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(S.GetAddress())); S = S.GetSuper())
	{
		if (S == Type)
			return true;
	}

	return false;
}

std::vector<UEProperty> UEStruct::GetProperties() const
{
	std::vector<UEProperty> Properties;

	// Guards against a corrupted/cyclic Next-chain
	std::unordered_set<uintptr_t> VisitedFields;

	// Drops properties that don't fit within the struct's own reported size (corrupted reflection data)
	const int32 StructSize = GetStructSize();

	auto IsWithinStructBounds = [StructSize](UEProperty Prop) -> bool
	{
		const int32 ArrayDim = Prop.GetArrayDim();
		if (ArrayDim <= 0x0)
			return false;

		return (Prop.GetOffset() + (Prop.GetSize() * ArrayDim)) <= StructSize;
	};

	if (InternalSettings::bUseFProperty)
	{
		for (UEFField Field = GetChildProperties(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
		{
			if (!VisitedFields.insert(reinterpret_cast<uintptr_t>(Field.GetAddress())).second)
				break;

			if (!Field.IsA(EClassCastFlags::Property))
				continue;

			UEProperty Prop = Field.Cast<UEProperty>();
			if (IsWithinStructBounds(Prop))
				Properties.push_back(Prop);
		}

		return Properties;
	}
	for (UEField Field = GetChild(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
	{
		if (!VisitedFields.insert(reinterpret_cast<uintptr_t>(Field.GetAddress())).second)
			break;

		if (!Field.IsA(EClassCastFlags::Property))
			continue;

		UEProperty Prop = Field.Cast<UEProperty>();
		if (IsWithinStructBounds(Prop))
			Properties.push_back(Prop);
	}

	return Properties;
}

std::vector<UEFunction> UEStruct::GetFunctions() const
{
	std::vector<UEFunction> Functions;

	for (UEField Field = GetChild(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
	{
		if (Field.IsA(EClassCastFlags::Function))
			Functions.push_back(Field.Cast<UEFunction>());
	}

	return Functions;
}

UEProperty UEStruct::FindMember(const std::string& MemberName, EClassCastFlags TypeFlags) const
{
	if (!Object)
		return nullptr;

	if (InternalSettings::bUseFProperty)
	{
		for (UEFField Field = GetChildProperties(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
		{
			if (Field.IsA(TypeFlags) && Field.GetName() == MemberName)
			{
				return Field.Cast<UEProperty>();
			}
		}
	}

	for (UEField Field = GetChild(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
	{
		if (Field.IsA(TypeFlags) && Field.GetName() == MemberName)
		{
			return Field.Cast<UEProperty>();
		}
	}

	return nullptr;
}

bool UEStruct::HasMembers() const
{
	if (!Object)
		return false;

	if (InternalSettings::bUseFProperty)
	{
		for (UEFField Field = GetChildProperties(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
		{
			if (Field.IsA(EClassCastFlags::Property))
				return true;
		}
	}
	else
	{
		for (UEField Field = GetChild(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
		{
			if (Field.IsA(EClassCastFlags::Property))
				return true;
		}
	}

	return false;
}

EClassCastFlags UEClass::GetCastFlags() const
{
	return GMemory->Read<EClassCastFlags>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UClass.CastFlags);
}

std::string UEClass::StringifyCastFlags() const
{
	return StringifyClassCastFlags(GetCastFlags());
}

bool UEClass::IsType(EClassCastFlags TypeFlag) const
{
	return (TypeFlag != EClassCastFlags::None ? (GetCastFlags() & TypeFlag) : true);
}

UEObject UEClass::GetDefaultObject() const
{
	return UEObject(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UClass.ClassDefaultObject)));
}

TArray<FImplementedInterface> UEClass::GetImplementedInterfaces() const
{
	return GMemory->Read<TArray<FImplementedInterface>>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UClass.ImplementedInterfaces);
}

UEFunction UEClass::GetFunction(const std::string& ClassName, const std::string& FuncName) const
{
	for (UEStruct Struct = *this; GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Struct.GetAddress())); Struct = Struct.GetSuper())
	{
		if (Struct.GetName() != ClassName)
			continue;

		for (UEField Field = Struct.GetChild(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Field.GetAddress())); Field = Field.GetNext())
		{
			if (Field.IsA(EClassCastFlags::Function) && Field.GetName() == FuncName)
			{
				return Field.Cast<UEFunction>();
			}
		}
	}

	return nullptr;
}

EFunctionFlags UEFunction::GetFunctionFlags() const
{
	return GMemory->Read<EFunctionFlags>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UFunction.FunctionFlags);
}

bool UEFunction::HasFlags(EFunctionFlags FuncFlags) const
{
	return GetFunctionFlags() & FuncFlags;
}

uint8 UEFunction::GetNumParams() const
{
	return GMemory->Read<uint8>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UFunction.NumParams);
}

uint16 UEFunction::GetParamSize() const
{
	return GMemory->Read<uint16>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UFunction.ParamSize);
}

void* UEFunction::GetExecFunction() const
{
	return reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Object) + GOffsets.UFunction.ExecFunction));
}

UEProperty UEFunction::GetReturnProperty() const
{
	for (auto Prop : GetProperties())
	{
		if (Prop.HasPropertyFlags(EPropertyFlags::ReturnParm))
			return Prop;
	}

	return nullptr;
}


std::string UEFunction::StringifyFlags(const char* Seperator) const
{
	return StringifyFunctionFlags(GetFunctionFlags(), Seperator);
}

std::string UEFunction::GetParamStructName() const
{
	return GetOuter().GetCppName() + "_" + GetValidName() + "_Params";
}

void* UEProperty::GetAddress()
{
	return Base;
}

const void* UEProperty::GetAddress() const
{
	return Base;
}

std::pair<UEClass, UEFFieldClass> UEProperty::GetClass() const
{
	if (InternalSettings::bUseFProperty)
		return {UEClass(0), UEFField(Base).GetClass()};

	return {UEObject(Base).GetClass(), UEFFieldClass(0)};
}

EClassCastFlags UEProperty::GetCastFlags() const
{
	auto [Class, FieldClass] = GetClass();

	return Class ? Class.GetCastFlags() : FieldClass.GetCastFlags();
}

UEProperty::operator bool() const
{
	return Base != nullptr && ((Base + GOffsets.UObject.Class) != nullptr || (Base + GOffsets.FField.Class) != nullptr);
}


bool UEProperty::IsA(EClassCastFlags TypeFlags) const
{
	if (GetClass().first)
		return GetClass().first.IsType(TypeFlags);

	return GetClass().second.IsType(TypeFlags);
}

FName UEProperty::GetFName() const
{
	if (InternalSettings::bUseFProperty)
	{
		return FName(Base + GOffsets.FField.Name); // Not the real FName, but a wrapper which holds the address of a FName
	}

	return FName(Base + GOffsets.UObject.Name); // Not the real FName, but a wrapper which holds the address of a FName
}

int32 UEProperty::GetArrayDim() const
{
	if (InternalSettings::bUseUint8ArrayDim)
		return GMemory->Read<uint8>(reinterpret_cast<uintptr_t>(Base) + GOffsets.Property.ArrayDim);

	return GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Base) + GOffsets.Property.ArrayDim);
}

int32 UEProperty::GetSize() const
{
	return GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Base) + GOffsets.Property.ElementSize);
}

int32 UEProperty::GetOffset() const
{
	return GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Base) + GOffsets.Property.Offset_Internal);
}

EPropertyFlags UEProperty::GetPropertyFlags() const
{
	return GMemory->Read<EPropertyFlags>(reinterpret_cast<uintptr_t>(Base) + GOffsets.Property.PropertyFlags);
}

bool UEProperty::HasPropertyFlags(EPropertyFlags PropertyFlag) const
{
	return GetPropertyFlags() & PropertyFlag;
}

bool UEProperty::IsType(EClassCastFlags PossibleTypes) const
{
	return (static_cast<uint64>(GetCastFlags()) & static_cast<uint64>(PossibleTypes)) != 0;
}

std::string UEProperty::GetName() const
{
	return Base ? GetFName().ToString() : "None";
}

std::string UEProperty::GetValidName() const
{
	return Base ? GetFName().ToValidString() : "None";
}

int32 UEProperty::GetAlignment() const
{
	EClassCastFlags TypeFlags = (GetClass().first ? GetClass().first.GetCastFlags() : GetClass().second.GetCastFlags());

	if (TypeFlags & EClassCastFlags::ByteProperty)
	{
		return alignof(uint8); // 0x1
	}
	else if (TypeFlags & EClassCastFlags::UInt16Property)
	{
		return alignof(uint16); // 0x2
	}
	else if (TypeFlags & EClassCastFlags::UInt32Property)
	{
		return alignof(uint32); // 0x4
	}
	else if (TypeFlags & EClassCastFlags::UInt64Property)
	{
		return sizeof(void*); // 0x4 on 32bit or 0x8 on 64bit
	}
	else if (TypeFlags & EClassCastFlags::Int8Property)
	{
		return alignof(int8); // 0x1
	}
	else if (TypeFlags & EClassCastFlags::Int16Property)
	{
		return alignof(int16); // 0x2
	}
	else if (TypeFlags & EClassCastFlags::IntProperty)
	{
		return alignof(int32); // 0x4
	}
	else if (TypeFlags & EClassCastFlags::Int64Property)
	{
		return sizeof(void*); // 0x4 on 32bit or 0x8 on 64bit
	}
	else if (TypeFlags & EClassCastFlags::FloatProperty)
	{
		return alignof(float); // 0x4
	}
	else if (TypeFlags & EClassCastFlags::DoubleProperty)
	{
		return sizeof(void*); // 0x4 on 32bit or 0x8 on 64bit
	}
	else if (TypeFlags & EClassCastFlags::ClassProperty)
	{
		return alignof(void*); // 0x4 / 0x8
	}
	else if (TypeFlags & EClassCastFlags::NameProperty)
	{
		return alignof(int32); // FName is a bunch of int32s
	}
	else if (TypeFlags & EClassCastFlags::StrProperty)
	{
		return alignof(FString); // 0x8
	}
	else if (TypeFlags & EClassCastFlags::TextProperty)
	{
		return alignof(FString); // alignof member FString
	}
	else if (TypeFlags & EClassCastFlags::BoolProperty)
	{
		return alignof(bool); // 0x1
	}
	else if (TypeFlags & EClassCastFlags::StructProperty)
	{
		return Cast<UEStructProperty>().GetUnderlayingStruct().GetMinAlignment();
	}
	else if (TypeFlags & EClassCastFlags::ArrayProperty)
	{
		return alignof(TArray<int>); // 0x8
	}
	else if (TypeFlags & EClassCastFlags::DelegateProperty)
	{
		return alignof(int32); // 0x4
	}
	else if (TypeFlags & EClassCastFlags::WeakObjectProperty)
	{
		return alignof(int32); // TWeakObjectPtr is a bunch of int32s
	}
	else if (TypeFlags & EClassCastFlags::LazyObjectProperty)
	{
		return alignof(int32); // TLazyObjectPtr is a bunch of int32s
	}
	else if (TypeFlags & EClassCastFlags::SoftClassProperty)
	{
		return alignof(FString); // alignof member FString
	}
	else if (TypeFlags & EClassCastFlags::SoftObjectProperty)
	{
		return alignof(FString); // alignof member FString
	}
	else if (TypeFlags & EClassCastFlags::ObjectProperty)
	{
		return alignof(void*); // 0x4 / 0x8
	}
	else if (TypeFlags & EClassCastFlags::MapProperty)
	{
		return alignof(TArray<int>); // 0x8, TMap contains a TArray
	}
	else if (TypeFlags & EClassCastFlags::SetProperty)
	{
		return alignof(TArray<int>); // 0x8, TSet contains a TArray
	}
	else if (TypeFlags & EClassCastFlags::EnumProperty)
	{
		UEProperty P = Cast<UEEnumProperty>().GetUnderlayingProperty();

		return P ? P.GetAlignment() : 0x1;
	}
	else if (TypeFlags & EClassCastFlags::InterfaceProperty)
	{
		return alignof(void*); // 0x4 / 0x8
	}
	else if (TypeFlags & EClassCastFlags::FieldPathProperty)
	{
		return alignof(TArray<int>); // alignof member TArray<FName> and ptr;
	}
	else if (TypeFlags & EClassCastFlags::MulticastSparseDelegateProperty)
	{
		return 0x1; // size in PropertyFixup (alignment isn't greater than size)
	}
	else if (TypeFlags & EClassCastFlags::MulticastInlineDelegateProperty)
	{
		return alignof(TArray<int>); // alignof member TArray<FName>
	}
	else if (TypeFlags & EClassCastFlags::OptionalProperty)
	{
		UEProperty ValueProperty = Cast<UEOptionalProperty>().GetValueProperty();

		/* If this check is true it means, that there is no bool in this TOptional to check if the value is set */
		if (ValueProperty.GetSize() == GetSize()) [[unlikely]]
			return ValueProperty.GetAlignment();

		return GetSize() - ValueProperty.GetSize();
	}
	else if (TypeFlags & EClassCastFlags::Utf8StrProperty)
	{
		return alignof(FUtf8String); // 0x8, same as StrProperty
	}
	else if (TypeFlags & EClassCastFlags::AnsiStrProperty)
	{
		return alignof(FAnsiString); // 0x8, same as StrProperty
	}
	else if (TypeFlags & EClassCastFlags::VCellProperty)
	{
		return sizeof(void*); // pointer-sized
	}

	if (InternalSettings::bUseFProperty)
	{
		static std::unordered_map<void*, int32> UnknownProperties;

		static auto TryFindPropertyRefInOptionalToGetAlignment = [](std::unordered_map<void*, int32>& OutProperties, void* PropertyClass) -> int32
		{
			/* Search for a TOptionalProperty that contains an instance of this property */

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
					if (!Prop.IsA(EClassCastFlags::OptionalProperty) || Prop.IsA(EClassCastFlags::ObjectPropertyBase))
						continue;

					UEOptionalProperty Optional = Prop.Cast<UEOptionalProperty>();

					/* Safe to use first member, as we're guaranteed to use FProperty */
					if (Optional.GetValueProperty().GetClass().second.GetAddress() == PropertyClass)
						return OutProperties.insert({PropertyClass, Optional.GetAlignment()}).first->second;
				}
			}

			return OutProperties.insert({PropertyClass, 0x1}).first->second;
		};

		auto It = UnknownProperties.find(GetClass().second.GetAddress());

		/* Safe to use first member, as we're guaranteed to use FProperty */
		if (It == UnknownProperties.end())
			return TryFindPropertyRefInOptionalToGetAlignment(UnknownProperties, GetClass().second.GetAddress());

		return It->second;
	}

	return 0x1;
}

std::string UEProperty::GetCppType() const
{
	EClassCastFlags TypeFlags = (GetClass().first ? GetClass().first.GetCastFlags() : GetClass().second.GetCastFlags());

	if (TypeFlags & EClassCastFlags::ByteProperty)
	{
		return Cast<UEByteProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::UInt16Property)
	{
		return "uint16";
	}
	else if (TypeFlags & EClassCastFlags::UInt32Property)
	{
		return "uint32";
	}
	else if (TypeFlags & EClassCastFlags::UInt64Property)
	{
		return "uint64";
	}
	else if (TypeFlags & EClassCastFlags::Int8Property)
	{
		return "int8";
	}
	else if (TypeFlags & EClassCastFlags::Int16Property)
	{
		return "int16";
	}
	else if (TypeFlags & EClassCastFlags::IntProperty)
	{
		return "int32";
	}
	else if (TypeFlags & EClassCastFlags::Int64Property)
	{
		return "int64";
	}
	else if (TypeFlags & EClassCastFlags::FloatProperty)
	{
		return "float";
	}
	else if (TypeFlags & EClassCastFlags::DoubleProperty)
	{
		return "double";
	}
	else if (TypeFlags & EClassCastFlags::ClassProperty)
	{
		return Cast<UEClassProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::NameProperty)
	{
		return "class FName";
	}
	else if (TypeFlags & EClassCastFlags::StrProperty)
	{
		return "class FString";
	}
	else if (TypeFlags & EClassCastFlags::Utf8StrProperty)
	{
		return "FUtf8String";
	}
	else if (TypeFlags & EClassCastFlags::AnsiStrProperty)
	{
		return "FAnsiString";
	}
	else if (TypeFlags & EClassCastFlags::TextProperty)
	{
		return "class FText";
	}
	else if (TypeFlags & EClassCastFlags::BoolProperty)
	{
		return Cast<UEBoolProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::StructProperty)
	{
		return Cast<UEStructProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::ArrayProperty)
	{
		return Cast<UEArrayProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::WeakObjectProperty)
	{
		return Cast<UEWeakObjectProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::LazyObjectProperty)
	{
		return Cast<UELazyObjectProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::SoftClassProperty)
	{
		return Cast<UESoftClassProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::SoftObjectProperty)
	{
		return Cast<UESoftObjectProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::ObjectProperty)
	{
		return Cast<UEObjectProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::MapProperty)
	{
		return Cast<UEMapProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::SetProperty)
	{
		return Cast<UESetProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::EnumProperty)
	{
		return Cast<UEEnumProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::InterfaceProperty)
	{
		return Cast<UEInterfaceProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::FieldPathProperty)
	{
		if (InternalSettings::bIsObjPtrInsteadOfFieldPathProperty)
			return Cast<UEObjectProperty>().GetCppType();

		return Cast<UEFieldPathProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::DelegateProperty)
	{
		return Cast<UEDelegateProperty>().GetCppType();
	}
	else if (TypeFlags & EClassCastFlags::OptionalProperty)
	{
		return Cast<UEOptionalProperty>().GetCppType();
	}
	else
	{
		return (GetClass().first ? GetClass().first.GetCppName() : GetClass().second.GetCppName()) + "_";
		;
	}
}

std::string UEProperty::GetPropClassName() const
{
	return GetClass().first ? GetClass().first.GetName() : GetClass().second.GetName();
}

std::string UEProperty::StringifyFlags() const
{
	return StringifyPropertyFlags(GetPropertyFlags());
}

UEEnum UEByteProperty::GetEnum() const
{
	return UEEnum(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.ByteProperty.Enum)));
}

std::string UEByteProperty::GetCppType() const
{
	if (UEEnum Enum = GetEnum())
	{
		return Enum.GetEnumTypeAsStr();
	}

	return "uint8";
}

uint8 UEBoolProperty::GetFieldMask() const
{
	return GMemory->Read<PropertyBaseTypes::UBoolPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.BoolProperty.Base).FieldMask;
}

uint8 UEBoolProperty::GetByteOffset() const
{
	return GMemory->Read<PropertyBaseTypes::UBoolPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.BoolProperty.Base).ByteOffset;
}

uint8 UEBoolProperty::GetBitIndex() const
{
	const uint8 FieldMask = GetFieldMask();

	const uint8_t InitialBitOffset = GetByteOffset() * 0x8; // Example: Offset 3 ==> This bitfield is in the 4th bit ==> 3 lower bytes have 3 * 8 = 24 bits

	if (FieldMask != 0xFF)
	{
		if (FieldMask == 0x01)
		{
			return InitialBitOffset + 0;
		}
		if (FieldMask == 0x02)
		{
			return InitialBitOffset + 1;
		}
		if (FieldMask == 0x04)
		{
			return InitialBitOffset + 2;
		}
		if (FieldMask == 0x08)
		{
			return InitialBitOffset + 3;
		}
		if (FieldMask == 0x10)
		{
			return InitialBitOffset + 4;
		}
		if (FieldMask == 0x20)
		{
			return InitialBitOffset + 5;
		}
		if (FieldMask == 0x40)
		{
			return InitialBitOffset + 6;
		}
		if (FieldMask == 0x80)
		{
			return InitialBitOffset + 7;
		}
	}

	return 0xFF;
}

bool UEBoolProperty::IsNativeBool() const
{
	return GMemory->Read<PropertyBaseTypes::UBoolPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.BoolProperty.Base).FieldMask == 0xFF;
}

std::string UEBoolProperty::GetCppType() const
{
	return IsNativeBool() ? "bool" : "uint8";
}

UEClass UEObjectProperty::GetPropertyClass() const
{
	return UEClass(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.ObjectProperty.PropertyClass)));
}

std::string UEObjectProperty::GetCppType() const
{
	return fmt::format("class {}*", GetPropertyClass() ? GetPropertyClass().GetCppName() : "UObject");
}

UEClass UEClassProperty::GetMetaClass() const
{
	return UEClass(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.ClassProperty.MetaClass)));
}

std::string UEClassProperty::GetCppType() const
{
	return HasPropertyFlags(EPropertyFlags::UObjectWrapper) ? fmt::format("TSubclassOf<class {}>", GetMetaClass().GetCppName()) : "class UClass*";
}

std::string UEWeakObjectProperty::GetCppType() const
{
	return fmt::format("TWeakObjectPtr<class {}>", GetPropertyClass() ? GetPropertyClass().GetCppName() : "UObject");
}

std::string UELazyObjectProperty::GetCppType() const
{
	return fmt::format("TLazyObjectPtr<class {}>", GetPropertyClass() ? GetPropertyClass().GetCppName() : "UObject");
}

std::string UESoftObjectProperty::GetCppType() const
{
	return fmt::format("TSoftObjectPtr<class {}>", GetPropertyClass() ? GetPropertyClass().GetCppName() : "UObject");
}

std::string UESoftClassProperty::GetCppType() const
{
	return fmt::format("TSoftClassPtr<class {}>", GetMetaClass() ? GetMetaClass().GetCppName() : GetPropertyClass().GetCppName());
}

std::string UEInterfaceProperty::GetCppType() const
{
	return fmt::format("TScriptInterface<class {}>", GetPropertyClass().GetCppName());
}

UEStruct UEStructProperty::GetUnderlayingStruct() const
{
	return UEStruct(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.StructProperty.Struct)));
}

std::string UEStructProperty::GetCppType() const
{
	return fmt::format("struct {}", GetUnderlayingStruct().GetCppName());
}

UEProperty UEArrayProperty::GetInnerProperty() const
{
	return UEProperty(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.ArrayProperty.Inner)));
}

std::string UEArrayProperty::GetCppType() const
{
	return fmt::format("TArray<{}>", GetInnerProperty().GetCppType());
}

UEFunction UEDelegateProperty::GetSignatureFunction() const
{
	return UEFunction(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.DelegateProperty.SignatureFunction)));
}

std::string UEDelegateProperty::GetCppType() const
{
	return "TDeleage<GetCppTypeIsNotImplementedForDelegates>";
}

UEFunction UEMulticastInlineDelegateProperty::GetSignatureFunction() const
{
	// Uses "GOffsets.DelegateProperty.SignatureFunction" on purpose
	return UEFunction(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.DelegateProperty.SignatureFunction)));
}

std::string UEMulticastInlineDelegateProperty::GetCppType() const
{
	return "TMulticastInlineDelegate<GetCppTypeIsNotImplementedForDelegates>";
}

UEProperty UEMapProperty::GetKeyProperty() const
{
	return UEProperty(GMemory->Read<PropertyBaseTypes::UMapPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.MapProperty.Base).KeyProperty);
}

UEProperty UEMapProperty::GetValueProperty() const
{
	return UEProperty(GMemory->Read<PropertyBaseTypes::UMapPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.MapProperty.Base).ValueProperty);
}

std::string UEMapProperty::GetCppType() const
{
	return fmt::format("TMap<{}, {}>", GetKeyProperty().GetCppType(), GetValueProperty().GetCppType());
}

UEProperty UESetProperty::GetElementProperty() const
{
	return UEProperty(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.SetProperty.ElementProp)));
}

std::string UESetProperty::GetCppType() const
{
	return fmt::format("TSet<{}>", GetElementProperty().GetCppType());
}

UEProperty UEEnumProperty::GetUnderlayingProperty() const
{
	return UEProperty(GMemory->Read<PropertyBaseTypes::UEnumPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.EnumProperty.Base).UnderlayingProperty);
}

UEEnum UEEnumProperty::GetEnum() const
{
	return UEEnum(GMemory->Read<PropertyBaseTypes::UEnumPropertyBase>(reinterpret_cast<uintptr_t>(Base) + GOffsets.EnumProperty.Base).Enum);
}

std::string UEEnumProperty::GetCppType() const
{
	if (GetEnum())
		return GetEnum().GetEnumTypeAsStr();

	return GetUnderlayingProperty().GetCppType();
}

UEFFieldClass UEFieldPathProperty::GetFieldClass() const
{
	return UEFFieldClass(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.FieldPathProperty.FieldClass)));
}

std::string UEFieldPathProperty::GetCppType() const
{
	return fmt::format("TFieldPath<struct {}>", GetFieldClass().GetCppName());
}

UEProperty UEOptionalProperty::GetValueProperty() const
{
	return UEProperty(reinterpret_cast<void*>(GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(Base) + GOffsets.OptionalProperty.ValueProperty)));
}

std::string UEOptionalProperty::GetCppType() const
{
	return fmt::format("TOptional<{}>", GetValueProperty().GetCppType());
}
