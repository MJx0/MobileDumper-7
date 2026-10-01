#include "EnumManager.h"

#include <iterator>

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

/*
 * Names a generated enum value may not use, because the toolchain has already claimed them.
 *
 * Most are object-like macros from the C library headers the SDK ends up including, so a value
 * with the same spelling is textually replaced before the compiler ever sees it. UE's own
 * generated "_MAX" sentinel makes that collision easy to hit, since so many system limits are
 * spelled the same way. The remainder are C++ keywords and the few identifiers GCC and Clang
 * predefine outside strict-conformance mode.
 */
void EnumManager::InitIllegalNames()
{
	static const char* const Illegal[] = {
	    // Windows-style names UE itself leaks through its platform headers.
	    "IN",
	    "OUT",
	    "TRUE",
	    "FALSE",
	    "DELETE",
	    "RELATIVE",
	    "TRANSPARENT",
	    "NO_ERROR",
	    "IGNORE",
	    "small",
	    "PF_MAX",
	    "SW_MAX",
	    "MM_MAX",
	    "EVENT_MAX",

	    // <limits.h>
	    "CHAR_MAX",
	    "CHAR_MIN",
	    "SCHAR_MAX",
	    "SCHAR_MIN",
	    "UCHAR_MAX",
	    "SHRT_MAX",
	    "SHRT_MIN",
	    "USHRT_MAX",
	    "INT_MAX",
	    "INT_MIN",
	    "UINT_MAX",
	    "LONG_MAX",
	    "LONG_MIN",
	    "ULONG_MAX",
	    "LLONG_MAX",
	    "LLONG_MIN",
	    "ULLONG_MAX",
	    "SIZE_MAX",
	    "SSIZE_MAX",
	    "MB_LEN_MAX",
	    "WORD_BIT",
	    "LONG_BIT",
	    "PATH_MAX",
	    "NAME_MAX",
	    "HOST_NAME_MAX",
	    "LOGIN_NAME_MAX",
	    "TTY_NAME_MAX",
	    "LINE_MAX",
	    "IOV_MAX",
	    "ARG_MAX",
	    "OPEN_MAX",
	    "LINK_MAX",
	    "NGROUPS_MAX",
	    "PIPE_BUF",

	    // <stdint.h>
	    "INT8_MAX",
	    "INT16_MAX",
	    "INT32_MAX",
	    "INT64_MAX",
	    "INT8_MIN",
	    "INT16_MIN",
	    "INT32_MIN",
	    "INT64_MIN",
	    "UINT8_MAX",
	    "UINT16_MAX",
	    "UINT32_MAX",
	    "UINT64_MAX",
	    "INTPTR_MAX",
	    "INTPTR_MIN",
	    "UINTPTR_MAX",
	    "INTMAX_MAX",
	    "INTMAX_MIN",
	    "UINTMAX_MAX",
	    "PTRDIFF_MAX",
	    "PTRDIFF_MIN",
	    "SIG_ATOMIC_MAX",
	    "SIG_ATOMIC_MIN",
	    "WCHAR_MAX",
	    "WCHAR_MIN",
	    "WINT_MAX",
	    "WINT_MIN",

	    // <float.h>
	    "FLT_MAX",
	    "FLT_MIN",
	    "DBL_MAX",
	    "DBL_MIN",
	    "LDBL_MAX",
	    "LDBL_MIN",
	    "FLT_EPSILON",
	    "DBL_EPSILON",

	    // <stdlib.h> / <stdio.h>
	    "RAND_MAX",
	    "EOF",
	    "BUFSIZ",
	    "stdin",
	    "stdout",
	    "stderr",

	    // <math.h> - DOMAIN/OVERFLOW/UNDERFLOW come from the matherr exception codes.
	    "INFINITY",
	    "NAN",
	    "HUGE_VAL",
	    "HUGE_VALF",
	    "DOMAIN",
	    "OVERFLOW",
	    "UNDERFLOW",
	    "FP_NAN",
	    "FP_INFINITE",
	    "FP_ZERO",
	    "FP_NORMAL",
	    "FP_SUBNORMAL",

	    // <signal.h>
	    "SIG_DFL",
	    "SIG_IGN",
	    "SIG_ERR",

	    // <sys/sysmacros.h> - lowercase, and function-like, so an unparenthesised value still breaks.
	    "major",
	    "minor",

	    // <endian.h>
	    "BYTE_ORDER",
	    "BIG_ENDIAN",
	    "LITTLE_ENDIAN",
	    "PDP_ENDIAN",

	    // <elf.h> machine-type table, which ends in a sentinel spelled like UE's own.
	    "EM_MAX",
	    "EM_NONE",
	    "EM_NUM",

	    // <errno.h> - bare names, so an error enum lands on them directly.
	    "EDOM",
	    "ERANGE",
	    "EPERM",
	    "ENOENT",
	    "EINTR",
	    "EIO",
	    "EAGAIN",
	    "ENOMEM",
	    "EACCES",
	    "EBUSY",
	    "EEXIST",
	    "ENODEV",
	    "EINVAL",
	    "ENOSPC",
	    "EPIPE",
	    "ENOSYS",
	    "ETIMEDOUT",
	    "EOVERFLOW",
	    "ENAMETOOLONG",

	    // Predefined by GCC and Clang unless -std=c++NN (no GNU extensions) is used.
	    "linux",
	    "unix",
	    "i386",

	    // C++ keywords and alternative operator tokens.
	    "short",
	    "long",
	    "int",
	    "signed",
	    "unsigned",
	    "char",
	    "float",
	    "double",
	    "void",
	    "bool",
	    "and",
	    "or",
	    "not",
	    "xor",
	    "compl",
	    "bitand",
	    "bitor",
	    "and_eq",
	    "or_eq",
	    "xor_eq",
	    "not_eq",
	};

	IllegalNames.reserve(std::size(Illegal));
	for (const char* Name : Illegal)
	{
		IllegalNames.push_back(UniqueEnumValueNames.FindOrAdd(Name).first);
	}
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
