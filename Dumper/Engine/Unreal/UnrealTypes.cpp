#include "UnrealTypes.h"

#include "../../Memory/IMemory.h"
#include "../../Utils/Encoding/UnicodeNames.h"
#include "../../Utils/Utils.h"

#include "../OffsetFinder/Offsets.h"

#include "NameArray.h"

std::string MakeNameValid(std::wstring&& Name)
{
	static constexpr const wchar_t* Numbers[10] =
	    {
	        L"Zero",
	        L"One",
	        L"Two",
	        L"Three",
	        L"Four",
	        L"Five",
	        L"Six",
	        L"Seven",
	        L"Eight",
	        L"Nine"};

	if (Name == L"bool")
		return "Bool";

	if (Name == L"NULL")
		return "NULLL";

	/* Replace 0 with Zero or 9 with Nine, if it is the first letter of the name. */
	if (Name[0] <= '9' && Name[0] >= '0')
	{
		Name.replace(0, 1, Numbers[Name[0] - '0']);
	}

	std::u32string Utf32Name;

	if constexpr (sizeof(wchar_t) == 4)
		Utf32Name.assign(reinterpret_cast<const char32_t*>(Name.data()), Name.size());
	else
	{
		const std::u16string u16(reinterpret_cast<const char16_t*>(Name.data()), Name.size());
		Utf32Name = UtfN::Utf16StringToUtf32String<std::u32string>(u16);
	}

	bool bIsFirstIteration = true;
	for (auto It = UtfN::utf32_iterator<std::u32string::iterator>(Utf32Name); It; ++It)
	{
		if (bIsFirstIteration && !IsUnicodeCharXIDStart(Name[0]))
		{
			/* Replace invalid starting character with 'm' character. 'm' for "member" */
			Name[0] = 'm';

			bIsFirstIteration = false;
		}

		if (!IsUnicodeCharXIDContinue((*It).Get()))
			It.Replace('_');
	}

	return Utils::String::UTF32ToString(Utf32Name);
}


FName::FName(const void* Ptr)
    : Address(static_cast<const uint8*>(Ptr))
{
}

std::wstring FName::ToRawWString() const
{
	if (!Address)
		return L"None";

	if (!InternalSettings::bUseOutlineNumberName)
	{
		const uint32 Number = FName(Address).GetNumber();

		if (Number > 0)
			return NameArray::GetNameEntry(Address).GetWString() + L'_' + std::to_wstring(Number - 1);
	}

	return NameArray::GetNameEntry(Address).GetWString();
}

std::wstring FName::ToWString() const
{
	std::wstring OutputString = ToRawWString();

	size_t pos = OutputString.rfind('/');

	if (pos == std::wstring::npos)
		return OutputString;

	return OutputString.substr(pos + 1);
}

std::string FName::ToRawString() const
{
	if (!Address)
		return "None";

	return UtfN::WStringToString(ToRawWString());
}

std::string FName::ToString() const
{
	if (!Address)
		return "None";

	return UtfN::WStringToString(ToWString());
}

std::string FName::ToValidString() const
{
	return MakeNameValid(ToWString());
}

int32 FName::GetCompIdx() const
{
	return GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Address) + GOffsets.FName.CompIdx);
}

uint32 FName::GetNumber() const
{
	if (InternalSettings::bUseOutlineNumberName)
		return 0x0;

	if (InternalSettings::bUseNamePool)
		return GMemory->Read<uint32>(reinterpret_cast<uintptr_t>(Address) + GOffsets.FName.Number); // The number is uint32 on versions <= UE4.23

	return static_cast<uint32_t>(GMemory->Read<int32>(reinterpret_cast<uintptr_t>(Address) + GOffsets.FName.Number));
}

bool FName::operator==(FName Other) const
{
	return GetCompIdx() == Other.GetCompIdx();
}

bool FName::operator!=(FName Other) const
{
	return GetCompIdx() != Other.GetCompIdx();
}

std::string FName::CompIdxToString(int CmpIdx)
{
	if (!InternalSettings::bUseCasePreservingName)
	{
		struct FakeFName
		{
			int CompIdx;
			uint8 Pad[0x4];
		} Name{CmpIdx};

		return FName(&Name).ToString();
	}
	else
	{
		struct FakeFName
		{
			int CompIdx;
			uint8 Pad[0xC];
		} Name{CmpIdx};

		return FName(&Name).ToString();
	}
}