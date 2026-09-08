#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "../OffsetFinder/Layouts.h"

class FNameEntry
{
private:
	friend class NameArray;

	static inline std::function<std::wstring(uintptr_t)> GetStrFn = nullptr;

private:
	uint8* Address;

public:
	inline static void SetGetStrFn(const std::function<std::wstring(uintptr_t)>& Fn)
	{
		GetStrFn = Fn;
	}

	FNameEntry()
	    : Address(nullptr)
	{
	}

	FNameEntry(uint8* Ptr);

public:
	std::wstring GetWString();
	std::string GetString();
	void* GetAddress();
};

class NameArray
{
private:
	static inline std::function<uintptr_t(int32 Index)> ByIndexFn = nullptr;

public:
	static inline std::function<void(char*, int)> DecryptUTF8Fn      = nullptr;
	static inline std::function<void(char16_t*, int)> DecryptUTF16Fn = nullptr;
	static inline std::function<void(char32_t*, int)> DecryptUTF32Fn = nullptr;
	static inline std::function<void(uintptr_t&)> DecryptNameChunkFn = nullptr;
	static inline std::function<void(uintptr_t&)> DecryptNameEntryFn = nullptr;

	inline static void SetByIndexFn(const std::function<uintptr_t(int32)>& Fn) { ByIndexFn = Fn; }
	inline static void SetDecryptUTF8Fn(const std::function<void(char*, int)>& Fn) { DecryptUTF8Fn = Fn; }
	inline static void SetDecryptUTF16Fn(const std::function<void(char16_t*, int)>& Fn) { DecryptUTF16Fn = Fn; }
	inline static void SetDecryptUTF32Fn(const std::function<void(char32_t*, int)>& Fn) { DecryptUTF32Fn = Fn; }
	inline static void SetDecryptNameChunkFn(const std::function<void(uintptr_t&)>& Fn) { DecryptNameChunkFn = Fn; }
	inline static void SetDecryptNameEntryFn(const std::function<void(uintptr_t&)>& Fn) { DecryptNameEntryFn = Fn; }

	static int32 GetNumChunks();

	static int32 GetNumElements();
	static int32 GetByteCursor();

	static FNameEntry GetNameEntry(const void* Name);
	static FNameEntry GetNameEntry(int32 Idx);
};
