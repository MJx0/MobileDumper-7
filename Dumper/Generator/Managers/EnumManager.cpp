#include "EnumManager.h"

#include <algorithm>
#include <cstdint>

#include "../../Engine/Unreal/ObjectArray.h"

namespace EnumInitHelper
{
	template <typename T>
	constexpr inline uint64 GetMaxOfType()
	{
		return (1ull << (sizeof(T) * 0x8ull)) - 1;
	}

	void SetEnumSizeForValue(uint8& Size, uint64 EnumValue)
	{
		if (EnumValue > GetMaxOfType<uint32>())
		{
			Size = 0x8;
		}
		else if (EnumValue > GetMaxOfType<uint16>())
		{
			Size = std::max<uint8>(Size, 0x4);
		}
		else if (EnumValue > GetMaxOfType<uint8>())
		{
			Size = std::max<uint8>(Size, 0x2);
		}
		else
		{
			Size = std::max<uint8>(Size, 0x1);
		}
	}

	/* Values read from UEnum::Names are always sign-extended to int64, so a negative Value here is a genuine signed enum member (eg. an explicit -1 sentinel), not a raw-byte misread. */
	void SetEnumSizeForSignedValue(uint8& Size, int64 EnumValue)
	{
		if (EnumValue > INT32_MAX || EnumValue < INT32_MIN)
		{
			Size = 0x8;
		}
		else if (EnumValue > INT16_MAX || EnumValue < INT16_MIN)
		{
			Size = std::max<uint8>(Size, 0x4);
		}
		else if (EnumValue > INT8_MAX || EnumValue < INT8_MIN)
		{
			Size = std::max<uint8>(Size, 0x2);
		}
		else
		{
			Size = std::max<uint8>(Size, 0x1);
		}
	}
}

std::string EnumCollisionInfo::GetUniqueName() const
{
	const std::string Name = EnumManager::GetValueName(*this).GetName();

	if (CollisionCount > 0)
		return Name + "_" + std::to_string(CollisionCount - 1);

	return Name;
}

std::string EnumCollisionInfo::GetRawName() const
{
	return EnumManager::GetValueName(*this).GetName();
}

uint64 EnumCollisionInfo::GetValue() const
{
	return MemberValue;
}

uint8 EnumCollisionInfo::GetCollisionCount() const
{
	return CollisionCount;
}

EnumInfoHandle::EnumInfoHandle(const EnumInfo& InInfo)
    : Info(&InInfo)
{
}

uint8 EnumInfoHandle::GetUnderlyingTypeSize() const
{
	return Info->UnderlyingTypeSize;
}

bool EnumInfoHandle::IsUnderlayingTypeSigned() const
{
	return Info->bIsSigned;
}

const StringEntry& EnumInfoHandle::GetName() const
{
	return EnumManager::GetEnumName(*Info);
}

bool EnumInfoHandle::HasValidName() const
{
	return Info != nullptr && static_cast<int32>(Info->Name) != HashStringTableIndex::InvalidIndex;
}

int32 EnumInfoHandle::GetNumMembers() const
{
	return Info->MemberInfos.size();
}

CollisionInfoIterator EnumInfoHandle::GetMemberCollisionInfoIterator() const
{
	return CollisionInfoIterator(Info->MemberInfos);
}

void EnumManager::InitInternal()
{
	for (auto Obj : ObjectArray())
	{
		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject))
			continue;

		if (!InternalSettings::bHasUnderlayingTypeInUEnum && Obj.IsA(EClassCastFlags::Struct))
		{
			UEStruct ObjAsStruct = Obj.Cast<UEStruct>();

			for (UEProperty Property : ObjAsStruct.GetProperties())
			{
				if (!Property.IsA(EClassCastFlags::EnumProperty) && !Property.IsA(EClassCastFlags::ByteProperty))
					continue;

				UEEnum Enum                    = nullptr;
				UEProperty UnderlayingProperty = nullptr;

				if (Property.IsA(EClassCastFlags::EnumProperty))
				{
					Enum                = Property.Cast<UEEnumProperty>().GetEnum();
					UnderlayingProperty = Property.Cast<UEEnumProperty>().GetUnderlayingProperty();

					if (!UnderlayingProperty)
						continue;
				}
				else /* ByteProperty */
				{
					Enum                = Property.Cast<UEByteProperty>().GetEnum();
					UnderlayingProperty = Property;
				}

				if (!Enum)
					continue;

				EnumInfo& Info = EnumInfoOverrides[Enum.GetIndex()];

				/*
				 * Only initialize on first discovery. This map is shared by every property that
				 * references this enum; resetting UnderlyingTypeSize on every hit would make the
				 * detected size reflect only the last-processed property instead of the max across
				 * all of them, understating it whenever an earlier property (e.g. a real EnumProperty
				 * elsewhere) revealed a wider underlying type than the one currently being examined.
				 */
				if (!Info.bWasInstanceFound)
				{
					Info.bWasInstanceFound  = true;
					Info.UnderlyingTypeSize = 0x1;
				}

				const int32 PropertySize = Property.GetSize();
				Info.UnderlyingTypeSize  = std::max(Info.UnderlyingTypeSize, static_cast<uint8>(PropertySize));
			}
		}
		else if (Obj.IsA(EClassCastFlags::Enum))
		{
			UEEnum ObjAsEnum = Obj.Cast<UEEnum>();

			/* Add name to override info */
			EnumInfo& NewOrExistingInfo = EnumInfoOverrides[Obj.GetIndex()];
			NewOrExistingInfo.Name      = UniqueEnumNameTable.FindOrAdd(ObjAsEnum.GetEnumPrefixedName()).first;

			uint64 EnumMaxValue    = 0x0;
			int64 EnumMinValue     = 0x0;
			bool bHasNegativeValue = false;

			/* Initialize enum-member names and their collision infos */
			std::vector<std::pair<FName, int64>> NameValuePairs = ObjAsEnum.GetNameValuePairs();
			for (size_t i = 0; i < NameValuePairs.size(); i++)
			{
				auto& [Name, Value] = NameValuePairs[i];

				std::wstring NameWitPrefix = Name.ToWString();

				if (!NameWitPrefix.ends_with(L"_MAX"))
				{
					if (Value < 0)
					{
						bHasNegativeValue = true;
						EnumMinValue      = std::min(EnumMinValue, Value);
					}
					else
					{
						EnumMaxValue = std::max<uint64>(EnumMaxValue, static_cast<uint64>(Value));
					}
				}

				auto [NameIndex, bWasInserted] = UniqueEnumValueNames.FindOrAdd(MakeNameValid(NameWitPrefix.substr(NameWitPrefix.find_last_of(L"::") + 1)));

				EnumCollisionInfo CurrentEnumValueInfo;
				CurrentEnumValueInfo.MemberName  = NameIndex;
				CurrentEnumValueInfo.MemberValue = Value;

				if (bWasInserted) [[likely]]
				{
					NewOrExistingInfo.MemberInfos.push_back(CurrentEnumValueInfo);
					continue;
				}

				/* A value with this name exists globally, now check if it also exists localy (aka. is duplicated) */
				for (size_t j = 0; j < i; j++)
				{
					EnumCollisionInfo& CrosscheckedInfo = NewOrExistingInfo.MemberInfos[j];

					if (CrosscheckedInfo.MemberName != NameIndex) [[likely]]
						continue;

					/* Duplicate was found */
					CurrentEnumValueInfo.CollisionCount = CrosscheckedInfo.CollisionCount + 1;
					break;
				}

				/* Check if this name is illegal */
				for (HashStringTableIndex IllegalIndex : IllegalNames)
				{
					if (NameIndex == IllegalIndex) [[unlikely]]
					{
						CurrentEnumValueInfo.CollisionCount++;
						break;
					}
				}

				NewOrExistingInfo.MemberInfos.push_back(CurrentEnumValueInfo);
			}

			if (InternalSettings::bHasUnderlayingTypeInUEnum)
			{
				auto [Size, bIsSigned]               = ObjAsEnum.GetSizeSignedPair();
				NewOrExistingInfo.UnderlyingTypeSize = Size;
				NewOrExistingInfo.bIsSigned          = bIsSigned;
			}

			/* Initialize the size based on the highest (and, if any member is negative, lowest) value contained by this enum */
			if (!NewOrExistingInfo.bWasEnumSizeInitialized && !NewOrExistingInfo.bWasInstanceFound)
			{
				if (bHasNegativeValue)
				{
					NewOrExistingInfo.bIsSigned = true;
					EnumInitHelper::SetEnumSizeForSignedValue(NewOrExistingInfo.UnderlyingTypeSize, EnumMinValue);
					EnumInitHelper::SetEnumSizeForSignedValue(NewOrExistingInfo.UnderlyingTypeSize, static_cast<int64>(EnumMaxValue));
				}
				else
				{
					EnumInitHelper::SetEnumSizeForValue(NewOrExistingInfo.UnderlyingTypeSize, EnumMaxValue);
				}

				NewOrExistingInfo.bWasEnumSizeInitialized = true;
			}
		}
	}
}

void EnumManager::InitIllegalNames()
{
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("IN").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("OUT").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("TRUE").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("FALSE").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("DELETE").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("PF_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("SW_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("MM_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("INT_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("UINT_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("LONG_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("ULONG_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("SIZE_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("PATH_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("RELATIVE").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("TRANSPARENT").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("NO_ERROR").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("EVENT_MAX").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("IGNORE").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("small").first);

	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("short").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("long").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("int").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("signed").first);
	IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd("unsigned").first);
}

void EnumManager::Init()
{
	if (bIsInitialized)
		return;

	bIsInitialized = true;

	EnumInfoOverrides.reserve(0x1000);

	InitIllegalNames(); // call this first
	InitInternal();
}
