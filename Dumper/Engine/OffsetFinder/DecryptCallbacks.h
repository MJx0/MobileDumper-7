#pragma once

#include <cstdint>
#include <functional>

#include "../Unreal/Enums.h"

/**
 * @brief What a decryption hook is handed: the stored bytes, and where they came from.
 *
 * A cipher is not always confined to the field it protects - one shipped scheme combines three
 * consecutive words starting at @ref Address - so a hook receives the address as well and may
 * read from it rather than using @ref RawValue at all.
 *
 * @ref RawValue is still supplied so an ordinary hook needs no read of its own, and so a value
 * already decoded from a bulk-read block during layout detection can be passed straight in.
 */
template <typename T>
struct FDecryptContext
{
	/// @brief Address the value was read from.
	uintptr_t Address = 0;
	/// @brief Value exactly as stored in the target, before decryption.
	T RawValue{};
};

/// @brief A decryption hook for a field of type @p T.
template <typename T>
using FDecryptFn = std::function<T(const FDecryptContext<T>&)>;

/// @brief Default hook, for a target that stores the field in the clear.
template <typename T>
inline T DecryptIdentity(const FDecryptContext<T>& Context)
{
	return Context.RawValue;
}

/**
 * @brief Per-offset value decryption hooks.
 */
struct FDecryptCallbacks
{
	/// @brief Hooks for FName fields.
	struct FNameCallbacks
	{
		/// @brief Decrypts a comparison index.
		FDecryptFn<int32> CompIdx = DecryptIdentity<int32>;
		/// @brief Decrypts a name number.
		FDecryptFn<uint32> Number = DecryptIdentity<uint32>;
	} FName;

	/// @brief Hooks for UObject fields.
	struct FUObjectCallbacks
	{
		/// @brief Decrypts an object flags field.
		FDecryptFn<EObjectFlags> Flags = DecryptIdentity<EObjectFlags>;
		/// @brief Decrypts an internal object index.
		FDecryptFn<int32> Index = DecryptIdentity<int32>;
		/// @brief Decrypts a UClass pointer.
		FDecryptFn<uintptr_t> Class = DecryptIdentity<uintptr_t>;
		/// @brief Decrypts an outer UObject pointer.
		FDecryptFn<uintptr_t> Outer = DecryptIdentity<uintptr_t>;
	} UObject;

	/// @brief Hooks for a legacy indirect name array.
	struct FNameArrayCallbacks
	{
		/// @brief Decrypts the chunk pointer array base.
		FDecryptFn<uintptr_t> Chunks = DecryptIdentity<uintptr_t>;
		/// @brief Decrypts the live name count.
		FDecryptFn<int32> NumElements = DecryptIdentity<int32>;

		/// @brief Hooks for fields within a single FNameEntry.
		struct FNameEntryCallbacks
		{
			/// @brief Decrypts the packed `(Index << 1) | bIsWide` field.
			FDecryptFn<uint32> Index = DecryptIdentity<uint32>;
		} FNameEntry;
	} NameArray;

	/// @brief Hooks for an FNamePool.
	struct FNamePoolCallbacks
	{
		/// @brief Decrypts the block pointer array base.
		FDecryptFn<uintptr_t> Blocks = DecryptIdentity<uintptr_t>;
		/// @brief Decrypts the highest allocated block index.
		FDecryptFn<int32> MaxChunkIndex = DecryptIdentity<int32>;
		/// @brief Decrypts the write cursor within the current block.
		FDecryptFn<int32> ByteCursor = DecryptIdentity<int32>;

		/// @brief Hooks for fields within a single packed FNameEntry.
		struct FNameEntryCallbacks
		{
			/// @brief Decrypts the header holding length and flags.
			FDecryptFn<uint16> Header = DecryptIdentity<uint16>;
		} FNameEntry;
	} NamePool;

	/// @brief Hooks for a contiguous object array.
	struct FFixedObjectsCallbacks
	{
		/// @brief Decrypts the pointer to the item allocation.
		FDecryptFn<uintptr_t> Objects = DecryptIdentity<uintptr_t>;
		/// @brief Decrypts the live object count.
		FDecryptFn<int32> NumObjects = DecryptIdentity<int32>;
		/// @brief Decrypts the allocation capacity.
		FDecryptFn<int32> MaxObjects = DecryptIdentity<int32>;

		/// @brief Hooks for fields within a single FUObjectItem.
		struct FUObjectItemCallbacks
		{
			/// @brief Decrypts the UObject pointer within an item.
			FDecryptFn<uintptr_t> Object = DecryptIdentity<uintptr_t>;
		} FUObjectItem;
	} FixedObjects;

	/// @brief Hooks for a chunked object array.
	struct FChunkedObjectsCallbacks
	{
		/// @brief Decrypts the pointer to the chunk pointer table.
		FDecryptFn<uintptr_t> Objects = DecryptIdentity<uintptr_t>;
		/// @brief Decrypts the live object count.
		FDecryptFn<int32> NumElements = DecryptIdentity<int32>;
		/// @brief Decrypts the total capacity across all chunks.
		FDecryptFn<int32> MaxElements = DecryptIdentity<int32>;
		/// @brief Decrypts the chunk table capacity.
		FDecryptFn<int32> MaxChunks = DecryptIdentity<int32>;
		/// @brief Decrypts the allocated chunk count.
		FDecryptFn<int32> NumChunks = DecryptIdentity<int32>;

		/// @brief Hooks for fields within a single FUObjectItem.
		struct FUObjectItemCallbacks
		{
			/// @brief Decrypts the UObject pointer within an item.
			FDecryptFn<uintptr_t> Object = DecryptIdentity<uintptr_t>;
		} FUObjectItem;
	} ChunkedObjects;
};

/// @brief Active value decryption hooks.
extern FDecryptCallbacks GDecryptCallbacks;
