#include "UnrealContainers.h"

#include "../../Utils/Utils.h"

namespace UC
{
	std::string FString::ToString() const
	{
		if (!*this)
			return "";

		const uintptr_t Addr  = reinterpret_cast<uintptr_t>(Data);
		const int32 CharCount = NumElements - 1;

		if (InternalSettings::bUseChar16String || sizeof(wchar_t) == 2)
			return Utils::String::UTF16ToString(GMemory->ReadUTF16(Addr, CharCount));

		return Utils::String::UTF32ToString(GMemory->ReadUTF32(Addr, CharCount));
	}

	std::wstring FString::ToWString() const
	{
		if (!*this)
			return L"";

		const uintptr_t Addr  = reinterpret_cast<uintptr_t>(Data);
		const int32 CharCount = NumElements - 1;

		if (InternalSettings::bUseChar16String)
			return Utils::String::UTF16ToWString(GMemory->ReadUTF16(Addr, CharCount));

		return Utils::String::UTF32ToWString(GMemory->ReadUTF32(Addr, CharCount));
	}

	std::string FUtf8String::ToString() const
	{
		if (!*this)
			return "";

		return GMemory->ReadUTF8(reinterpret_cast<uintptr_t>(Data), NumElements - 1);
	}

	std::wstring FUtf8String::ToWString() const
	{
		return *this ? Utils::String::UTF8ToWString(ToString()) : L"";
	}

	std::string FAnsiString::ToString() const
	{
		if (!*this)
			return "";

		return GMemory->ReadUTF8(reinterpret_cast<uintptr_t>(Data), NumElements - 1);
	}

	std::wstring FAnsiString::ToWString() const
	{
		return *this ? Utils::String::UTF8ToWString(ToString()) : L"";
	}
}
