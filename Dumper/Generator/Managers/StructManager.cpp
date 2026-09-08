#include "StructManager.h"

#include <algorithm>

#include "../../Engine/Unreal/ObjectArray.h"
#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"
#include "../../Utils/Utils.h"

StructInfoHandle::StructInfoHandle(const StructInfo& InInfo)
    : Info(&InInfo)
{
}

int32 StructInfoHandle::GetLastMemberEnd() const
{
	return Info->LastMemberEnd;
}

int32 StructInfoHandle::GetSize() const
{
	/*
	 * Utils::Align(0, Alignment) rounds back down to 0 (0 is already a multiple of any alignment),
	 * but no complete C++ type can actually have sizeof == 0 -- an empty type's size is always at
	 * least its own alignment (the compiler pads a zero-member alignas(N) type up to N bytes).
	 * Falling through the plain 0 here would under-report the real compiled size of a memberless
	 * struct with a non-trivial alignment, corrupting both its own size assertion and the offset
	 * math of anything that embeds it as a member.
	 */
	const int32 AlignedSize = Utils::Align(Info->Size, Info->Alignment);
	return AlignedSize > 0x0 ? AlignedSize : Info->Alignment;
}

int32 StructInfoHandle::GetUnalignedSize() const
{
	return Info->Size;
}

int32 StructInfoHandle::GetAlignment() const
{
	return Info->Alignment;
}

bool StructInfoHandle::ShouldUseExplicitAlignment() const
{
	return Info->bUseExplicitAlignment;
}

const StringEntry& StructInfoHandle::GetName() const
{
	return StructManager::GetName(*Info);
}

bool StructInfoHandle::HasValidName() const
{
	return Info != nullptr && static_cast<int32>(Info->Name) != HashStringTableIndex::InvalidIndex;
}

bool StructInfoHandle::IsFinal() const
{
	return Info->bIsFinal;
}

bool StructInfoHandle::HasReusedTrailingPadding() const
{
	return Info->bHasReusedTrailingPadding;
}

bool StructInfoHandle::IsPartOfCyclicPackage() const
{
	return Info->bIsPartOfCyclicPackage;
}

/*
 * A struct-typed member's claimed offset/size is only trustworthy if it agrees with where a real
 * instance of it is actually placed elsewhere in the object graph - two sibling members occasionally
 * overlap even though each side's own reflection data is internally consistent on its own (confirmed
 * live: Engine.Material's PlanarReflectionOffsetScale, a Vector4MaterialInput, overlaps the very next
 * member by 12 bytes). This proves the referenced struct's real footprint there is smaller than its
 * own reported size - maps each affected struct to the proven-safe gap.
 */
static std::unordered_map<int32, int32> ComputeProvenSafeSizes(const std::vector<UEStruct>& AllStructs)
{
	std::unordered_map<int32, int32> ProvenSafeSize;

	for (UEStruct ObjAsStruct : AllStructs)
	{
		/*
		 * Function parameter lists are rebuilt per-call and, for Blueprint/K2Node-generated
		 * functions, can still be getting compiled at runtime - their layout isn't the stable,
		 * compiled-once struct layout this correction is meant to catch, so skip them.
		 */
		if (ObjAsStruct.IsA(EClassCastFlags::Function))
			continue;

		std::vector<UEProperty> SortedProps = ObjAsStruct.GetProperties();
		std::sort(SortedProps.begin(), SortedProps.end(), [](UEProperty A, UEProperty B) { return A.GetOffset() < B.GetOffset(); });

		for (size_t i = 0; i + 1 < SortedProps.size(); i++)
		{
			UEProperty Current = SortedProps[i];

			if (!Current.IsA(EClassCastFlags::StructProperty))
				continue;

			const int32 ArrayDim = Current.GetArrayDim();
			if (ArrayDim <= 0x0)
				continue;

			const int32 CurrentEnd = Current.GetOffset() + (Current.GetSize() * ArrayDim);
			const int32 NextOffset = SortedProps[i + 1].GetOffset();

			if (CurrentEnd <= NextOffset)
				continue;

			/*
			 * Confirm by re-reading before trusting a single observation: a struct's compiled-once
			 * reflection data reads identically every time, so a genuine inconsistency reproduces
			 * immediately. One that doesn't is more likely a process caught mid-mutation.
			 */
			if (Current.GetOffset() + (Current.GetSize() * Current.GetArrayDim()) != CurrentEnd || SortedProps[i + 1].GetOffset() != NextOffset)
				continue;

			const int32 UnderlyingIndex = Current.Cast<UEStructProperty>().GetUnderlayingStruct().GetIndex();
			const int32 SafeSize        = NextOffset - Current.GetOffset();

			auto It = ProvenSafeSize.find(UnderlyingIndex);
			if (It == ProvenSafeSize.end() || SafeSize < It->second)
				ProvenSafeSize[UnderlyingIndex] = SafeSize;
		}
	}

	return ProvenSafeSize;
}

void StructManager::InitAlignmentsAndNames()
{
	constexpr int32 DefaultClassAlignment = sizeof(void*);

	// sizeof(T) is always a multiple of alignof(T) in C++, so a corrected size also caps the alignment
	auto AlignmentCapFromProvenSize = [](int32 ProvenSize) -> int32
	{
		return ProvenSize > 0x0 ? (ProvenSize & -ProvenSize) : 0x1;
	};

	const UEClass InterfaceClass = ObjectArray::FindClassFast("Interface");

	const UEClass OnlineEngineInterfaceImplClass = ObjectArray::FindClassFast("OnlineEngineInterfaceImpl");

	/*
	 *  Cache all struct objects to avoid multiple full ObjectArray iterations
	 */
	std::vector<UEStruct> AllStructs;
	AllStructs.reserve(10000);

	for (auto Obj : ObjectArray())
	{
		if (Obj.IsA(EClassCastFlags::Struct))
			AllStructs.push_back(Obj.Cast<UEStruct>());
	}

	const std::unordered_map<int32, int32> ProvenSafeSize = ComputeProvenSafeSizes(AllStructs);

	for (UEStruct ObjAsStruct : AllStructs)
	{
		// Add name to override info
		StructInfo& NewOrExistingInfo = StructInfoOverrides[ObjAsStruct.GetIndex()];

		std::string CppName = ObjAsStruct.GetCppName();

		// Hardcoded fix for two 'UOnlineEngineInterfaceImpl' classes in the same package. Check will only match one of them.
		if (ObjAsStruct == OnlineEngineInterfaceImplClass) [[unlikely]]
			CppName += '2';

		NewOrExistingInfo.Name = UniqueNameTable.FindOrAdd(CppName, !ObjAsStruct.IsA(EClassCastFlags::Function)).first;

		// Interfaces inherit from UObject by default, but as a workaround to no virtual-inheritance we make them empty
		if (ObjAsStruct.HasType(InterfaceClass))
		{
			NewOrExistingInfo.Alignment                 = 0x1;
			NewOrExistingInfo.bHasReusedTrailingPadding = false;
			NewOrExistingInfo.bIsFinal                  = true;
			NewOrExistingInfo.Size                      = 0x0;

			continue;
		}

		const auto SafeSizeIt = ProvenSafeSize.find(ObjAsStruct.GetIndex());
		const bool bHasProvenSafeSize = SafeSizeIt != ProvenSafeSize.end();

		const int32 RawStructSize = ObjAsStruct.GetStructSize();
		const int32 CappedSize    = bHasProvenSafeSize ? std::min(RawStructSize, SafeSizeIt->second) : RawStructSize;

		if (CappedSize < RawStructSize)
			GLogger.FmtWrite(ELogLevel::Warning, "StructManager: Corrected size of '{}' from 0x{:X} to 0x{:X} (proven unreliable by a real sibling-member overlap)\n", ObjAsStruct.GetCppName(), RawStructSize, CappedSize);

		int32 MinAlignment = ObjAsStruct.GetMinAlignment();
		if (bHasProvenSafeSize)
		{
			const int32 CappedAlignment = AlignmentCapFromProvenSize(CappedSize);

			if (CappedAlignment < MinAlignment)
			{
				GLogger.FmtWrite(ELogLevel::Warning, "StructManager: Corrected alignment of '{}' from 0x{:X} to 0x{:X} (implied by its corrected size)\n", ObjAsStruct.GetCppName(), MinAlignment, CappedAlignment);
				MinAlignment = CappedAlignment;
			}
		}

		int32 HighestMemberAlignment = 0x1; // starting at 0x1 when checking **all**, not just struct-properties

		// Find member with the highest alignment. Members that no longer fit within a capped size are
		// skipped here too - they'll be dropped at generation time, so they shouldn't be allowed to
		// inflate this struct's own alignment either
		for (UEProperty Property : ObjAsStruct.GetProperties())
		{
			const int32 PropArrayDim = Property.GetArrayDim();
			if (PropArrayDim <= 0x0 || (Property.GetOffset() + (Property.GetSize() * PropArrayDim)) > CappedSize)
				continue;

			int32 CurrentPropertyAlignment = Property.GetAlignment();

			if (Property.IsA(EClassCastFlags::StructProperty))
			{
				UEStruct Underlying  = Property.Cast<UEStructProperty>().GetUnderlayingStruct();
				const auto NestedIt = ProvenSafeSize.find(Underlying.GetIndex());

				if (NestedIt != ProvenSafeSize.end())
				{
					const int32 NestedCappedSize = std::min(Underlying.GetStructSize(), NestedIt->second);
					CurrentPropertyAlignment     = std::min(CurrentPropertyAlignment, AlignmentCapFromProvenSize(NestedCappedSize));
				}
			}

			if (CurrentPropertyAlignment > HighestMemberAlignment)
				HighestMemberAlignment = CurrentPropertyAlignment;
		}

		/* On some strange games there are BlueprintGeneratedClass UClasses which don't inherit from UObject. */
		const bool bHasSuperClass = static_cast<bool>(ObjAsStruct.GetSuper());

		// if Class alignment is below pointer-alignment (0x8), use pointer-alignment instead, else use whichever, MinAlignment or HighestAlignment, is bigger
		if (ObjAsStruct.IsA(EClassCastFlags::Class) && bHasSuperClass && HighestMemberAlignment < DefaultClassAlignment)
		{
			NewOrExistingInfo.bUseExplicitAlignment = false;
			NewOrExistingInfo.Alignment             = DefaultClassAlignment;
		}
		else
		{
			NewOrExistingInfo.bUseExplicitAlignment = MinAlignment > HighestMemberAlignment;
			NewOrExistingInfo.Alignment             = std::max(MinAlignment, HighestMemberAlignment);
		}

		if (bHasProvenSafeSize)
			NewOrExistingInfo.Size = CappedSize;
	}

	// Second pass: Fix alignments based on super classes (reuse cached list)
	for (auto ObjAsStruct : AllStructs)
	{
		if (ObjAsStruct.IsA(EClassCastFlags::Function) || ObjAsStruct.HasType(InterfaceClass))
			continue;

		constexpr int MaxNumSuperClasses = 0x30;

		std::array<UEStruct, MaxNumSuperClasses> StructStack;
		int32 NumElementsInStructStack = 0x0;

		// Get a top to bottom list of a struct and all of its supers. Bounded by StructStack's
		// capacity: a cyclic/corrupted SuperStruct-chain would otherwise write past the array
		// and never terminate.
		for (UEStruct S = ObjAsStruct; GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(S.GetAddress())) && NumElementsInStructStack < MaxNumSuperClasses; S = S.GetSuper())
		{
			StructStack[NumElementsInStructStack] = S;
			NumElementsInStructStack++;
		}

		int32 CurrentHighestAlignment = 0x0;

		for (int i = NumElementsInStructStack - 1; i >= 0; i--)
		{
			StructInfo& Info = StructInfoOverrides[StructStack[i].GetIndex()];

			if (CurrentHighestAlignment < Info.Alignment)
			{
				CurrentHighestAlignment = Info.Alignment;
			}
			else
			{
				// We use the super classes' alignment, no need to explicitely set it
				Info.bUseExplicitAlignment = false;
				Info.Alignment             = CurrentHighestAlignment;
			}
		}
	}
}

void StructManager::InitSizesAndIsFinal()
{
	const UEClass InterfaceClass = ObjectArray::FindClassFast("Interface");

	// Reuse cached struct list from InitAlignmentsAndNames
	for (auto& [Index, Info] : StructInfoOverrides)
	{
		UEStruct ObjAsStruct = ObjectArray::GetByIndex<UEStruct>(Index);

		if (ObjAsStruct.HasType(InterfaceClass))
			continue;

		// Initialize struct-size if it wasn't set already
		if (Info.Size > ObjAsStruct.GetStructSize())
			Info.Size = ObjAsStruct.GetStructSize();

		UEStruct Super = ObjAsStruct.GetSuper();

		if (Info.Size == 0x0 && Super != nullptr)
			Info.Size = Super.GetStructSize();

		int32 LastMemberEnd = 0x0;
		int32 LowestOffset  = INT_MAX;

		// Find member with the lowest offset
		for (UEProperty Property : ObjAsStruct.GetProperties())
		{
			const int32 PropertyOffset = Property.GetOffset();
			const int32 PropertySize   = Property.GetSize();

			if (PropertyOffset < LowestOffset)
				LowestOffset = PropertyOffset;

			if ((PropertyOffset + PropertySize) > LastMemberEnd)
				LastMemberEnd = PropertyOffset + PropertySize;
		}

		/* No need to check any other structs, as finding the LastMemberEnd only involves this struct */
		Info.LastMemberEnd = LastMemberEnd;

		if (!Super || ObjAsStruct.IsA(EClassCastFlags::Function))
			continue;

		/*
		 * Loop all super-structs and set their struct-size to the lowest offset we found. Sets this size on the direct Super and all higher *empty* supers
		 *
		 * breaks out of the loop after encountering a super-struct which is not empty (aka. has member-variables)
		 */
		for (UEStruct S = Super; GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(S.GetAddress())); S = S.GetSuper())
		{
			auto It = StructInfoOverrides.find(S.GetIndex());

			if (It == StructInfoOverrides.end())
			{
				GLogger.FmtWrite(ELogLevel::Error, "StructManager::InitSizesAndIsFinal: Error, struct wasn't found in 'StructInfoOverrides'! Exiting...\n\n");
				exit(1);
			}

			StructInfo& Info = It->second;

			// Struct is not final, as it is another structs' super
			Info.bIsFinal = false;

			const int32 SizeToCheck = Info.Size == INT_MAX ? S.GetStructSize() : Info.Size;

			const bool bHasMembers = S.HasMembers();

			// Only change lowest offset if it's lower than the already found lowest offset (by default: struct-size)
			if (Utils::Align(SizeToCheck, Info.Alignment) > LowestOffset /*&& (bHasMembers || Info.Size != 0x1)*/)
			{
				if (Info.Size > LowestOffset)
					Info.Size = LowestOffset;

				Info.bHasReusedTrailingPadding = true;
			}

			if (bHasMembers)
				break;
		}
	}

	/*
	 * Propagate bHasReusedTrailingPadding through memberless (empty) structs.
	 *
	 * If struct A has bHasReusedTrailingPadding and struct B (memberless) inherits from A,
	 * B's unaligned size should be equal to A's — otherwise children of B would use B's aligned
	 * size as the start offset instead of A's unaligned end, producing wrong member positions
	 * and failing static_assert checks when the generated SDK is compiled with GCC/Clang.
	 */
	for (auto& [Index, Info] : StructInfoOverrides)
	{
		if (Info.bHasReusedTrailingPadding || Info.LastMemberEnd != 0)
			continue;

		UEStruct S = ObjectArray::GetByIndex<UEStruct>(Index);

		if (S.HasType(InterfaceClass) || S.IsA(EClassCastFlags::Function))
			continue;

		for (UEStruct Super = S.GetSuper(); GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(Super.GetAddress())); Super = Super.GetSuper())
		{
			auto It = StructInfoOverrides.find(Super.GetIndex());
			if (It == StructInfoOverrides.end())
				break;

			const StructInfo& SuperInfo = It->second;

			/*
			 * Stop at the *closest* ancestor that already reused trailing padding, even if that
			 * ancestor is itself memberless (LastMemberEnd == 0 -- e.g. an intermediate empty
			 * class whose own trailing-padding reuse was set from one of ITS siblings). Checking
			 * LastMemberEnd first would skip straight past such an ancestor to the next one with
			 * real members further up, copying that further ancestor's (larger, wrong) Size
			 * instead of the closer ancestor's already-correct, smaller one.
			 */
			if (SuperInfo.bHasReusedTrailingPadding)
			{
				Info.bHasReusedTrailingPadding = true;
				Info.Size                      = SuperInfo.Size;
				break;
			}

			if (SuperInfo.LastMemberEnd != 0)
				break;
		}
	}
}

void StructManager::Init()
{
	if (bIsInitialized)
		return;

	bIsInitialized = true;

	StructInfoOverrides.reserve(0x4000);

	InitAlignmentsAndNames();
	InitSizesAndIsFinal();

	/*
	 * The default class-alignment of 0x8 is only set for classes with a valid Super-class, because they inherit from UObject.
	 * UObject however doesn't have a super, so this needs to be set manually.
	 */
	const UEObject UObjectClass                                         = ObjectArray::FindClassFast("Object");
	StructInfoOverrides.find(UObjectClass.GetIndex())->second.Alignment = sizeof(void*);

	/* I still hate whoever decided to call "UStruct" "Ustruct" on some UE versions. */
	if (const UEObject UStructClass = ObjectArray::FindClassFast("struct"))
		StructInfoOverrides.find(UStructClass.GetIndex())->second.Name = UniqueNameTable.FindOrAdd(std::string("UStruct"), false).first;
}

StructInfoHandle StructManager::GetInfo(const UEStruct Struct)
{
	static StructInfo Invalid{.Name = HashStringTableIndex::FromInt(HashStringTableIndex::InvalidIndex)};

	// A null struct is routine here: StructWrapper::GetSuper() wraps the super of
	// every root struct, which is null. The wrapper reports IsValid() == false and
	// callers are expected to check. Anything that ignores that is caught in
	// StructWrapper::GetUniqueName() instead, where it actually matters.
	if (!Struct)
		return Invalid;

	auto It = StructInfoOverrides.find(Struct.GetIndex());
	if (It == StructInfoOverrides.end())
	{
		GLogger.FmtWrite(ELogLevel::Warning, "StructManager::GetInfo: struct not in map [ Index={}, Name='{}' ]\n", Struct.GetIndex(), Struct.GetFullName());
		return Invalid;
	}

	return It->second;
}

bool StructManager::IsStructCyclicWithPackage(int32 StructIndex, int32 PackageIndex)
{
	auto It = CyclicStructsAndPackages.find(StructIndex);
	if (It != CyclicStructsAndPackages.end())
		return It->second.contains(PackageIndex);

	return false;
}

/*
 * Utility function for PackageManager::PostInit to handle the initialization of our list of cyclic structs and their respective packages
 *
 * Marks StructInfo as 'bIsPartOfCyclicPackage = true' and adds struct to 'CyclicStructsAndPackages'
 */
void StructManager::PackageManagerSetCycleForStruct(int32 StructIndex, int32 PackageIndex)
{
	auto It = StructInfoOverrides.find(StructIndex);
	if (It == StructInfoOverrides.end())
	{
		GLogger.FmtWrite(ELogLevel::Warning, "StructManager::PackageManagerSetCycleForStruct: Struct not in map [ Index={} ]\n", StructIndex);
		return;
	}

	It->second.bIsPartOfCyclicPackage = true;

	CyclicStructsAndPackages[StructIndex].insert(PackageIndex);
}
