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
#include <unordered_set>
#include <vector>

#include "Encoding/UtfN.hpp"

#ifdef __ANDROID__
#include "KittyMemoryEx/KittyAsm.hpp"
#else
#include "KittyMemory/KittyAsm.hpp"
#endif

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

	/// @brief Formats a byte count as a value with the best-fitting unit (B..TB).
	inline std::string SizeToString(uint64_t Bytes)
	{
		std::ostringstream out;
		out << std::fixed << std::setprecision(2);

		if (Bytes < 1024ull)
			out << Bytes << " B";
		else if (Bytes < 1024ull * 1024)
			out << Bytes / 1024.0 << " KB";
		else if (Bytes < 1024ull * 1024 * 1024)
			out << Bytes / (1024.0 * 1024) << " MB";
		else if (Bytes < 1024ull * 1024 * 1024 * 1024)
			out << Bytes / (1024.0 * 1024 * 1024) << " GB";
		else
			out << Bytes / (1024.0 * 1024 * 1024 * 1024) << " TB";

		return out.str();
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

	/// @brief ARM64 Utils.
	namespace Arm64
	{
		/// @brief Decodes a single ARM64 instruction at Address.
		KittyInsnArm64 DecodeInsn(uint32_t Insn, uintptr_t Address);

		/// @brief Resolves the target of `ADRP page ; ADD/LDR Rd, Rd, #off` at Insns[0..N).
		uintptr_t Find_ADRP_Final_Address(const std::vector<uint32_t>& Insns, uintptr_t Address);
	}

	/// @brief ARM32 Utils.
	namespace Arm32
	{
		/// @brief Decodes a single ARM32 instruction at Address.
		KittyInsnArm32 DecodeInsn(uint32_t Insn, uint32_t Address);

		/// @brief Resolves the target of `LDR Rd, [PC, #off] ; ADD Rd, PC, Rd` at Insns[0..N).
		/// Returns 0 if the pattern is not found.
		uintptr_t Find_LDR_ADD_PC_Address(const std::vector<uint32_t>& Insns, uintptr_t Address, IMemory* Memory);
	}

	/// @brief Pointer/address-width helpers, alignment, and low-level integer ops.
	namespace Memory
	{
		/// @brief True when this build itself is 32-bit.
		consteval bool Is32Bit() { return sizeof(void*) == 4; }

		/// @brief Truncates a computed address to the analysed image's pointer width.
		inline uint64_t WrapAddress(uint64_t Address)
		{
			return static_cast<uint64_t>(static_cast<uintptr_t>(Address));
		}

		/// @brief Aligns a value down to the specified alignment.
		template <typename T, typename U>
		constexpr T AlignDown(T Value, U Alignment)
		{
			assert(Alignment != 0 && "Alignment was 0, division by zero exception.");

			return Value / Alignment * Alignment;
		}

		/// @brief Aligns a value up to the specified alignment.
		template <typename T, typename U>
		constexpr T AlignUp(T Value, U Alignment)
		{
			assert(Alignment != 0 && "Alignment was 0, division by zero exception.");

			return ((Value + Alignment - 1) / Alignment) * Alignment;
		}

		/// @brief Checks whether a value is aligned to the specified alignment.
		template <typename T, typename U>
		constexpr bool IsAligned(T Value, U Alignment)
		{
			assert(Alignment != 0 && "Alignment was 0, division by zero exception.");

			return (Value % Alignment) == 0;
		}

		/// @brief Removes top-byte pointer tags from a pointer.
		inline uintptr_t UntagPointer(uintptr_t ptr)
		{
#if defined(__LP64__)
			return ptr & ((static_cast<uintptr_t>(1) << 56) - 1);
#else
			return ptr;
#endif
		}

		/// @brief Removes top-byte pointer tags from a pointer.
		template <typename T>
		inline T* UntagPointer(T* ptr)
		{
			return reinterpret_cast<T*>(UntagPointer(reinterpret_cast<uintptr_t>(ptr)));
		}

		/// @brief Byte-swaps a 2/4/8-byte integral value, picking the width off sizeof(T).
		template <typename T>
		inline T Swap(T Value)
		{
			static_assert(std::is_integral_v<T>, "Swap can only handle integral types!");
			static_assert(sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "Swap only supports 2/4/8-byte types!");

			if constexpr (sizeof(T) == 2)
				return static_cast<T>(KittyAsm::swap16(static_cast<uint16_t>(Value)));
			else if constexpr (sizeof(T) == 4)
				return static_cast<T>(KittyAsm::swap32(static_cast<uint32_t>(Value)));
			else
				return static_cast<T>(KittyAsm::swap64(static_cast<uint64_t>(Value)));
		}

		/// @brief Rotates a 1/2/4/8-byte unsigned value right, picking the width off sizeof(T).
		template <typename T>
		inline T Ror(T Value, unsigned int Shift)
		{
			static_assert(std::is_unsigned_v<T>, "Ror can only handle unsigned integral types!");

			if constexpr (sizeof(T) == 1)
				return static_cast<T>(KittyAsm::ror8(static_cast<uint8_t>(Value), Shift));
			else if constexpr (sizeof(T) == 2)
				return static_cast<T>(KittyAsm::ror16(static_cast<uint16_t>(Value), Shift));
			else if constexpr (sizeof(T) == 4)
				return static_cast<T>(KittyAsm::ror32(static_cast<uint32_t>(Value), Shift));
			else
				return static_cast<T>(KittyAsm::ror64(static_cast<uint64_t>(Value), Shift));
		}

		/// @brief Rotates a 1/2/4/8-byte unsigned value left, picking the width off sizeof(T).
		template <typename T>
		inline T Rol(T Value, unsigned int Shift)
		{
			static_assert(std::is_unsigned_v<T>, "Rol can only handle unsigned integral types!");

			if constexpr (sizeof(T) == 1)
				return static_cast<T>(KittyAsm::rol8(static_cast<uint8_t>(Value), Shift));
			else if constexpr (sizeof(T) == 2)
				return static_cast<T>(KittyAsm::rol16(static_cast<uint16_t>(Value), Shift));
			else if constexpr (sizeof(T) == 4)
				return static_cast<T>(KittyAsm::rol32(static_cast<uint32_t>(Value), Shift));
			else
				return static_cast<T>(KittyAsm::rol64(static_cast<uint64_t>(Value), Shift));
		}
	}

	/// @brief IDA compatible byte/word/dword accessors.
	namespace IDA
	{
		/// @brief Byte n of x, by index. Read-only (const uint8_t&) if x is const.
		template <typename T>
		inline auto& BYTEn(T& x, int n)
		{
			if constexpr (std::is_const_v<T>)
				return *(reinterpret_cast<const uint8_t*>(&x) + n);
			else
				return *(reinterpret_cast<uint8_t*>(&x) + n);
		}

		/// @brief Signed byte n of x, by index. Read-only if x is const.
		template <typename T>
		inline auto& SBYTEn(T& x, int n)
		{
			if constexpr (std::is_const_v<T>)
				return *(reinterpret_cast<const int8_t*>(&x) + n);
			else
				return *(reinterpret_cast<int8_t*>(&x) + n);
		}

		/// @brief 16-bit word n of x, by index. Read-only if x is const.
		template <typename T>
		inline auto& WORDn(T& x, int n)
		{
			if constexpr (std::is_const_v<T>)
				return *(reinterpret_cast<const uint16_t*>(&x) + n);
			else
				return *(reinterpret_cast<uint16_t*>(&x) + n);
		}

		/// @brief Signed 16-bit word n of x, by index. Read-only if x is const.
		template <typename T>
		inline auto& SWORDn(T& x, int n)
		{
			if constexpr (std::is_const_v<T>)
				return *(reinterpret_cast<const int16_t*>(&x) + n);
			else
				return *(reinterpret_cast<int16_t*>(&x) + n);
		}

		/// @brief 32-bit dword n of x, by index. Read-only if x is const.
		template <typename T>
		inline auto& DWORDn(T& x, int n)
		{
			if constexpr (std::is_const_v<T>)
				return *(reinterpret_cast<const uint32_t*>(&x) + n);
			else
				return *(reinterpret_cast<uint32_t*>(&x) + n);
		}

		/// @brief Signed 32-bit dword n of x, by index. Read-only if x is const.
		template <typename T>
		inline auto& SDWORDn(T& x, int n)
		{
			if constexpr (std::is_const_v<T>)
				return *(reinterpret_cast<const int32_t*>(&x) + n);
			else
				return *(reinterpret_cast<int32_t*>(&x) + n);
		}

		/// @brief Lowest byte of x.
		template <typename T>
		inline auto& LOBYTE(T& x)
		{
			return BYTEn(x, 0);
		}

		/// @brief Second-lowest byte of x.
		template <typename T>
		inline auto& HIBYTE(T& x)
		{
			return BYTEn(x, 1);
		}

		/// @brief Byte 1 of x.
		template <typename T>
		inline auto& BYTE1(T& x)
		{
			return BYTEn(x, 1);
		}

		/// @brief Byte 2 of x.
		template <typename T>
		inline auto& BYTE2(T& x)
		{
			return BYTEn(x, 2);
		}

		/// @brief Byte 3 of x.
		template <typename T>
		inline auto& BYTE3(T& x)
		{
			return BYTEn(x, 3);
		}

		/// @brief Byte 4 of x.
		template <typename T>
		inline auto& BYTE4(T& x)
		{
			return BYTEn(x, 4);
		}

		/// @brief Byte 5 of x.
		template <typename T>
		inline auto& BYTE5(T& x)
		{
			return BYTEn(x, 5);
		}

		/// @brief Byte 6 of x.
		template <typename T>
		inline auto& BYTE6(T& x)
		{
			return BYTEn(x, 6);
		}

		/// @brief Byte 7 of x.
		template <typename T>
		inline auto& BYTE7(T& x)
		{
			return BYTEn(x, 7);
		}

		/// @brief Signed LOBYTE.
		template <typename T>
		inline auto& SLOBYTE(T& x)
		{
			return SBYTEn(x, 0);
		}

		/// @brief Signed HIBYTE.
		template <typename T>
		inline auto& SHIBYTE(T& x)
		{
			return SBYTEn(x, 1);
		}

		/// @brief Lowest 16-bit word of x.
		template <typename T>
		inline auto& LOWORD(T& x)
		{
			return WORDn(x, 0);
		}

		/// @brief Second-lowest 16-bit word of x.
		template <typename T>
		inline auto& HIWORD(T& x)
		{
			return WORDn(x, 1);
		}

		/// @brief Word 1 of x.
		template <typename T>
		inline auto& WORD1(T& x)
		{
			return WORDn(x, 1);
		}

		/// @brief Word 2 of x.
		template <typename T>
		inline auto& WORD2(T& x)
		{
			return WORDn(x, 2);
		}

		/// @brief Word 3 of x.
		template <typename T>
		inline auto& WORD3(T& x)
		{
			return WORDn(x, 3);
		}

		/// @brief Signed LOWORD.
		template <typename T>
		inline auto& SLOWORD(T& x)
		{
			return SWORDn(x, 0);
		}

		/// @brief Signed HIWORD.
		template <typename T>
		inline auto& SHIWORD(T& x)
		{
			return SWORDn(x, 1);
		}

		/// @brief Low 32 bits of a 64-bit x.
		template <typename T>
		inline auto& LODWORD(T& x)
		{
			return DWORDn(x, 0);
		}

		/// @brief High 32 bits of a 64-bit x.
		template <typename T>
		inline auto& HIDWORD(T& x)
		{
			return DWORDn(x, 1);
		}

		/// @brief Signed LODWORD.
		template <typename T>
		inline auto& SLODWORD(T& x)
		{
			return SDWORDn(x, 0);
		}

		/// @brief Signed HIDWORD.
		template <typename T>
		inline auto& SHIDWORD(T& x)
		{
			return SDWORDn(x, 1);
		}
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

#ifdef __ANDROID__
	/// @brief Android utils.
	namespace Android
	{
		/// @brief Resolves a package's UID.
		bool GetPackageUid(const std::string& PackageName, uid_t& OutUid);

		/// @brief Reverse of GetPackageUid: the package name owning @p Uid.
		bool GetPackageNameByUid(uid_t Uid, std::string& OutPackageName);

		/// @brief Every running PID whose real UID is @p Uid.
		std::vector<pid_t> GetProcessIDsByUid(uid_t Uid);

		/// @brief Third-party packages, i.e. everything a user actually installed.
		/// Empty when neither lookup worked, which callers
		/// must treat as "unknown" rather than "none installed".
		const std::unordered_set<std::string>& GetUserInstalledPackages();

		/// @brief "main", a secondary process's suffix, or "unregistered" if ActivityManager has
		/// no record of @p Pid - immune to cmdline spoofing either way.
		std::string GetProcessRole(pid_t Pid, const std::string& PackageName);
	}
#endif
}