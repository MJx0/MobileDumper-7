#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Encoding/UtfN.hpp"

class IMemory;

/// @brief Utils shared across the dumper.
namespace Utils
{
	/// @brief Formats a duration as a value with the best-fitting unit (ns..hr).
	template <typename Rep, typename Period>
	std::string ChronoDurationToString(std::chrono::duration<Rep, Period> duration)
	{
		const auto ns = duration_cast<std::chrono::nanoseconds>(duration).count();

		std::ostringstream out;
		out << std::fixed << std::setprecision(2);

		if (ns < 1'000)
			out << ns << " ns";
		else if (ns < 1'000'000)
			out << ns / 1'000.0 << " us";
		else if (ns < 1'000'000'000)
			out << ns / 1'000'000.0 << " ms";
		else if (ns < 60'000'000'000LL)
			out << ns / 1'000'000'000.0 << " sec";
		else if (ns < 3'600'000'000'000LL)
			out << ns / 60'000'000'000.0 << " min";
		else
			out << ns / 3'600'000'000'000.0 << " hr";

		return out.str();
	}

	/// @brief Rounds Size up to the next multiple of Alignment.
	template <typename T>
	constexpr T Align(T Size, T Alignment)
	{
		static_assert(std::is_integral_v<T>, "Align can only hanlde integral types!");
		assert(Alignment != 0 && "Alignment was 0, division by zero exception.");

		const T RequiredAlign = Alignment - (Size % Alignment);

		return Size + (RequiredAlign != Alignment ? RequiredAlign : 0x0);
	}

	/// @brief String encoding conversions and small text helpers.
	namespace String
	{
		/// @brief Assumes UTF-8 input; identical to UTF8ToWString below.
		inline std::wstring StringToWString(const std::string& Str)
		{
			if (Str.empty())
				return {};

			return UtfN::StringToWString(Str);
		}

		/// @brief Encodes back to UTF-8.
		inline std::string WStringToString(const std::wstring& Str)
		{
			if (Str.empty())
				return {};

			return UtfN::WStringToString(Str);
		}

		/// @brief Explicit-name counterpart to StringToWString above.
		inline std::wstring UTF8ToWString(const std::string& Str)
		{
			if (Str.empty())
				return {};

			return UtfN::StringToWString(Str);
		}

		/// @brief Passes through on platforms where wchar_t is 16-bit, else re-encodes via UTF-32.
		inline std::wstring UTF16ToWString(const std::u16string& Str)
		{
			if (Str.empty())
				return {};

			if constexpr (sizeof(wchar_t) == 2)
				return std::wstring(reinterpret_cast<const wchar_t*>(Str.data()), Str.size());

			const std::u32string U32 = UtfN::Utf16StringToUtf32String<std::u32string>(Str);
			return std::wstring(reinterpret_cast<const wchar_t*>(U32.data()), U32.size());
		}

		/// @brief Passes through on platforms where wchar_t is 32-bit, else re-encodes via UTF-16.
		inline std::wstring UTF32ToWString(const std::u32string& Str)
		{
			if (Str.empty())
				return {};

			if constexpr (sizeof(wchar_t) == 4)
				return std::wstring(reinterpret_cast<const wchar_t*>(Str.data()), Str.size());

			return UtfN::Utf32StringToUtf16String<std::wstring>(Str);
		}

		/// @brief UTF-16 to UTF-8.
		inline std::string UTF16ToString(const std::u16string& Str)
		{
			if (Str.empty())
				return {};

			return WStringToString(UTF16ToWString(Str));
		}

		/// @brief UTF-32 to UTF-8.
		inline std::string UTF32ToString(const std::u32string& Str)
		{
			if (Str.empty())
				return {};

			return WStringToString(UTF32ToWString(Str));
		}

		/// @brief Lowercases ASCII letters; cast to unsigned char avoids UB on negative char values.
		inline std::string StrToLower(std::string Str)
		{
			if (Str.empty())
				return {};

			std::transform(Str.begin(), Str.end(), Str.begin(), [](unsigned char C)
			{ return std::tolower(C); });

			return Str;
		}

		/// @brief strlen/wcslen, chosen by CharType.
		template <typename CharType>
		inline int32_t StrlenHelper(const CharType* Str)
		{
			if constexpr (std::is_same<CharType, char>())
			{
				return static_cast<int32_t>(strlen(Str));
			}
			else
			{
				return static_cast<int32_t>(wcslen(Str));
			}
		}

		/// @brief strncmp/wcsncmp, chosen by CharType.
		template <typename CharType>
		inline bool StrnCmpHelper(const CharType* Left, const CharType* Right, size_t NumCharsToCompare)
		{
			if constexpr (std::is_same<CharType, char>())
			{
				return strncmp(Left, Right, NumCharsToCompare) == 0;
			}
			else
			{
				return wcsncmp(Left, Right, NumCharsToCompare) == 0;
			}
		}

		/// @brief Trims a single repeated character from both ends.
		inline std::string Trim(const std::string& Str, char TrimChar)
		{
			size_t Start = Str.find_first_not_of(TrimChar);
			if (Start == std::string::npos)
				return "";
			size_t End = Str.find_last_not_of(TrimChar);
			return Str.substr(Start, End - Start + 1);
		}

		/// @brief Everything after the first Delimiter, or Value unchanged if absent.
		inline std::string SubstrAfterFirst(const std::string& Value, const std::string& Delimiter)
		{
			const auto Pos = Value.find(Delimiter);
			return Pos == std::string::npos ? Value : Value.substr(Pos + Delimiter.length());
		}

		/// @brief Everything after the last Delimiter, or Value unchanged if absent.
		inline std::string SubstrAfterLast(const std::string& Value, const std::string& Delimiter)
		{
			const auto Pos = Value.rfind(Delimiter);
			return Pos == std::string::npos ? Value : Value.substr(Pos + Delimiter.length());
		}

	}

	/// @brief Filesystem-safe filename sanitization.
	namespace FileNameHelper
	{
		/// @brief Replaces characters illegal in a filename with '_'.
		inline void MakeValidFileName(std::string& InOutName)
		{
			for (char& c : InOutName)
			{
				if (c == '<' || c == '>' || c == ':' || c == '\"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
					c = '_';
			}
		}
	}

	/// @brief ARM64 Utils.
	namespace Arm64
	{
		/// @brief Resolves the target of `ADRP page ; ADD/LDR Rd, Rd, #off` at Insns[0..N).
		uintptr_t Find_ADRP_Final_Address(const std::vector<uint32_t>& Insns, uintptr_t Address);
	}

	/// @brief ARM32 Utils.
	namespace Arm32
	{
		/// @brief Resolves the target of `LDR Rd, [PC, #off] ; ADD Rd, PC, Rd` at Insns[0..N).
		/// Returns 0 if the pattern is not found.
		uintptr_t Find_LDR_ADD_PC_Address(const std::vector<uint32_t>& Insns, uintptr_t Address, IMemory* Memory);
	}

	/// @brief Zip Utils.
	namespace Zip
	{
		/// @brief Zips every file under InDir, recursively.
		bool CreateZipWithDirectory(const std::string& InDir, int CompressionLevel, const std::string& OutZip);
		/// @brief Zips a single file as one entry named after its filename.
		bool CreateZipWithFile(const std::string& InFile, int CompressionLevel, const std::string& OutZip);
		/// @brief Extracts every entry of InZip into OutFolder.
		bool ExtractZipToFolder(const std::string& InZip, const std::string& OutFolder);
		/// @brief Extracts one entry (EntryPath) of InZip into OutFolder.
		bool ExtractZipEntryToFolder(const std::string& InZip, const std::string& EntryPath, const std::string& OutFolder);
		/// @brief OutData is allocated by the zip library — caller must free() it.
		bool ExtractZipEntryToMemory(const std::string& InZip, const std::string& EntryPath, void** OutData, size_t* OutDataSize);
	}

	/// @brief Android binary AndroidManifest.xml parsing.
	namespace Apk
	{
		/// @brief One decoded attribute from a manifest element.
		struct ManifestAttribute
		{
			/// @brief Value tags for Type below.
			static constexpr uint8_t kTypeNull      = 0x00;
			static constexpr uint8_t kTypeReference = 0x01;
			static constexpr uint8_t kTypeString    = 0x03;
			static constexpr uint8_t kTypeIntDec    = 0x10;
			static constexpr uint8_t kTypeIntHex    = 0x11;
			static constexpr uint8_t kTypeBoolean   = 0x12;

			/// @brief Owning XML element, e.g. "manifest".
			std::string ElementName;
			/// @brief Attribute name, e.g. "versionName".
			std::string Name;
			/// @brief Attribute's XML namespace URI, if any.
			std::string Namespace;
			/// @brief Decoded value as text, regardless of Type.
			std::string StringValue;
			/// @brief One of the kType* tags above.
			uint32_t Type = 0;
			/// @brief Raw value backing StringValue: string-pool index, int, or bool, per Type.
			uint32_t Data = 0;
		};

		/// @brief Decodes every attribute of every element in the manifest.
		bool ParseAndroidManifest(
		    const uint8_t* ManifestData,
		    size_t ManifestDataSize,
		    std::vector<ManifestAttribute>& Attributes);

		/// @brief First attribute matching both element and attribute name, or empty.
		inline ManifestAttribute GetManifestAttribute(
		    const std::vector<ManifestAttribute>& Attributes,
		    const std::string& ElementName,
		    const std::string& AttributeName)
		{
			for (const auto& Attribute : Attributes)
			{
				if (Attribute.ElementName == ElementName &&
				    Attribute.Name == AttributeName)
					return Attribute;
			}

			return {};
		}

		/// @brief The manifest's android:versionName, or empty if not found.
		std::string GetApkVersion(const uint8_t* ManifestData, size_t ManifestDataSize);
	}
}