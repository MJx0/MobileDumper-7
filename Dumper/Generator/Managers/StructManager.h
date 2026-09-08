#pragma once

#include <unordered_map>
#include <unordered_set>

#include "../../Engine/Unreal/UnrealObjects.h"

#include "../HashStringTable.h"

// A derived struct's own explicit padding doesn't always replace the base struct's implicit
// trailing padding at the same offset - compilers aren't consistent about it, which is why
// StructInfo tracks whether trailing padding was reused rather than assuming it.

struct StructInfo
{
	HashStringTableIndex Name;

	/* End of the last member-variable of this struct, used to calculate implicit trailing padding */
	int32 LastMemberEnd = 0x0;

	/* Unaligned size of this struct */
	int32 Size = INT_MAX;

	/* Alignment of this struct for alignas(Alignment), might be implicit */
	int32 Alignment = 0x1;


	/* Whether alignment should be explicitly specified with 'alignas(Alignment)' */
	bool bUseExplicitAlignment;

	/* Whether this struct has child-structs for which the compiler places members in this structs' trailing padding, see example above (line 9) */
	bool bHasReusedTrailingPadding = false;

	/* Wheter this class is ever inherited from. Set to false when this struct is found to be another structs super */
	bool bIsFinal = true;

	/* Whether this struct is in a package that has cyclic dependencies. Actual index of cyclic package is stored in StructManager::CyclicStructsAndPackages */
	bool bIsPartOfCyclicPackage;
};

class StructManager;

class StructInfoHandle
{
private:
	const StructInfo* Info = nullptr;

public:
	StructInfoHandle() = default;
	StructInfoHandle(const StructInfo& InInfo);

	explicit operator bool() const { return Info != nullptr; }

public:
	int32 GetLastMemberEnd() const;
	int32 GetSize() const;
	int32 GetUnalignedSize() const;
	int32 GetAlignment() const;
	bool ShouldUseExplicitAlignment() const;
	const StringEntry& GetName() const;

	/* False when this handle came from the sentinel StructManager returns for a
	 * null or unregistered struct. GetName() would then index the table with
	 * InvalidIndex, which decodes to bucket 31 at a 64 MB offset. */
	bool HasValidName() const;
	bool IsFinal() const;
	bool HasReusedTrailingPadding() const;

	bool IsPartOfCyclicPackage() const;
};

class StructManager
{
private:
	friend StructInfoHandle;
	friend class StructManagerTest;

public:
	using OverrideMapType   = std::unordered_map<int32 /*StructIdx*/, StructInfo>;
	using CycleInfoListType = std::unordered_map<int32 /*StructIdx*/, std::unordered_set<int32 /* Packages cyclic with this structs' package */>>;

private:
	/* NameTable containing names of all structs/classes as well as information on name-collisions */
	static inline HashStringTable UniqueNameTable;

	/* Map containing infos on all structs/classes. Implemented due to bugs/inconsistencies in Unreal's reflection system */
	static inline OverrideMapType StructInfoOverrides;

	/* Map containing infos on all structs/classes that are within a packages that has cyclic dependencies */
	static inline CycleInfoListType CyclicStructsAndPackages;

	static inline bool bIsInitialized = false;

private:
	static void InitAlignmentsAndNames();
	static void InitSizesAndIsFinal();

public:
	static void Init();

private:
	static inline const StringEntry& GetName(const StructInfo& Info)
	{
		return UniqueNameTable[Info.Name];
	}

public:
	static inline const OverrideMapType& GetStructInfos()
	{
		return StructInfoOverrides;
	}

	static inline bool IsStructNameUnique(HashStringTableIndex NameIndex)
	{
		return UniqueNameTable[NameIndex].IsUnique();
	}

	// debug function
	static inline std::string GetName(HashStringTableIndex NameIndex)
	{
		return UniqueNameTable[NameIndex].GetName();
	}

	static StructInfoHandle GetInfo(const UEStruct Struct);

	static bool IsStructCyclicWithPackage(int32 StructIndex, int32 PackageIndex);

	/*
	 * Utility function for PackageManager::PostInit to handle the initialization of our list of cyclic structs and their respective packages
	 *
	 * Marks StructInfo as 'bIsPartOfCyclicPackage = true' and adds struct to 'CyclicStructsAndPackages'
	 */
	static void PackageManagerSetCycleForStruct(int32 StructIndex, int32 PackageIndex);
};
