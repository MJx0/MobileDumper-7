#ifdef __ANDROID__

#include "MemoryAndroid.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

#include <KittyMemoryEx/KittyScanner.hpp>

#include "../Settings.h"

bool FMemoryAndroid::Initialize()
{
	return RefreshMemory();
}

int FMemoryAndroid::GetProcessID() const
{
	return ProcessId;
}

std::string FMemoryAndroid::GetProcessName() const
{
	return KittyMemMgr.processName();
}

bool FMemoryAndroid::RefreshMemory()
{
	const std::lock_guard<std::mutex> Lock(RelocCacheMutex);
	RelocCacheBlocks.clear();
	RelocCacheLru.clear();
	RelocCacheBytes.clear();
	RelocCacheBytes.shrink_to_fit();

	KittyMemMgr.initialize(ProcessId, KittyMemOp, false);

	PtrValidator.setPID(ProcessId);
	PtrValidator.setUseCache(true);

	auto AllMaps = KittyMemoryEx::getAllMaps(KittyMemMgr.processID());
	MemoryRegionsCache.clear();
	MemoryRegionsCache.reserve(AllMaps.size());
	for (const auto& it : AllMaps)
	{
		MemRegionInfo Region{it.pathname, it.startAddress, it.endAddress, it.offset, it.readable, it.writeable, it.executable};
		MemoryRegionsCache.push_back(Region);
	}

	return PtrValidator.cachedRegions().size() > 0 && MemoryRegionsCache.size() > 0;
}

bool FMemoryAndroid::IsMemoryAccessOk() const
{
	if (!PtrValidator.cachedRegions().empty() && !MemoryRegionsCache.empty())
	{
		uintptr_t ProgramBase = KittyMemMgr.elfScanner.getProgramElf().base();
		if (!ProgramBase)
			return false;

		uint32_t Word = 0;
		return ReadBytes(ProgramBase, &Word, sizeof(Word)) > 0;
	}

	return false;
}

bool FMemoryAndroid::IsValidAddress(uintptr_t Address) const
{
	return PtrValidator.isPtrReadable(Address);
}

bool FMemoryAndroid::IsAddressReadable(uintptr_t Address, size_t Size) const
{
	return PtrValidator.isPtrReadable(Address, Size);
}

bool FMemoryAndroid::IsAddressWriteable(uintptr_t Address, size_t Size) const
{
	return PtrValidator.isPtrWritable(Address, Size);
}

bool FMemoryAndroid::IsAddressExecutable(uintptr_t Address, size_t Size) const
{
	return PtrValidator.isPtrExecutable(Address, Size);
}

MemRegionInfo FMemoryAndroid::GetAddressRegionInfo(uintptr_t Address) const
{
	if (Address == 0)
		return {};

	Address = KittyUtils::untagPointer(Address);

	auto it = std::lower_bound(MemoryRegionsCache.begin(),
	                           MemoryRegionsCache.end(),
	                           Address,
	                           [](const MemRegionInfo& M, uintptr_t Val)
	{ return M.GetEnd() <= Val; });

	if (it != MemoryRegionsCache.end() && Address >= it->GetStart() && Address < it->GetEnd())
	{
		return *it;
	}

	return {};
}

size_t FMemoryAndroid::ReadBytes(uintptr_t Address, void* Buffer, size_t Size) const
{
	if (Address == 0 || Buffer == nullptr || Size == 0)
		return 0;

	const size_t PageSize = GetPageSize();

	uintptr_t Current = Address;
	size_t Remaining  = Size;
	size_t TotalRead  = 0;

	while (Remaining > 0)
	{
		const size_t PageOffset = Current & (PageSize - 1);
		const size_t ChunkSize  = std::min(Remaining, PageSize - PageOffset);

		const size_t BytesRead = KittyMemMgr.readMem(Current,
		                                             static_cast<uint8_t*>(Buffer) + TotalRead,
		                                             ChunkSize);

		TotalRead += BytesRead;

		// Stop if this chunk wasn't completely readable.
		if (BytesRead != ChunkSize)
			break;

		Current   += ChunkSize;
		Remaining -= ChunkSize;
	}

	return TotalRead;
}

bool FMemoryAndroid::WriteBytes(uintptr_t Address, const void* Buffer, size_t Size) const
{
	return KittyMemMgr.writeMem(Address, const_cast<void*>(Buffer), Size) == Size;
}

bool FMemoryAndroid::ReadRelocationPointer(uintptr_t Slot, uintptr_t& Out) const
{
	uintptr_t Stored = 0;
	if (!ReadRelocationSlot(Slot, Stored))
		return false;
	if (Stored == 0 || !IsAddressReadable(Stored))
		return false;

	Out = Stored;
	return true;
}

bool FMemoryAndroid::ReadRelocationSlot(uintptr_t Slot, uintptr_t& Out) const
{
	const uintptr_t Base = Slot & ~static_cast<uintptr_t>(kRelocCacheBlockSize - 1);
	const size_t Offset  = static_cast<size_t>(Slot - Base);

	// A word straddling two blocks takes the uncached path. Serving it would
	// mean splicing a second block in, and the halves could come from reads
	// taken at different moments - the one way this cache could answer with
	// bytes that never existed together.
	if (Offset + sizeof(uintptr_t) > kRelocCacheBlockSize)
		return ReadBytes(Slot, &Out, sizeof(Out));

	const std::lock_guard<std::mutex> Lock(RelocCacheMutex);

	if (RelocCacheBlocks.empty())
	{
		RelocCacheBlocks.resize(kRelocCacheBlockCount);
		RelocCacheBytes.resize(kRelocCacheBlockCount * kRelocCacheBlockSize);
		RelocCacheLru.clear();
		RelocCacheLru.reserve(kRelocCacheBlockCount);
	}

	// Most recent first, and the harvester walks slots in address order, so the
	// block wanted is nearly always the one at the front.
	for (size_t i = 0; i < RelocCacheLru.size(); ++i)
	{
		const size_t Index = RelocCacheLru[i];
		if (!RelocCacheBlocks[Index].bValid || RelocCacheBlocks[Index].Base != Base)
			continue;

		std::rotate(RelocCacheLru.begin(), RelocCacheLru.begin() + static_cast<ptrdiff_t>(i), RelocCacheLru.begin() + static_cast<ptrdiff_t>(i) + 1);
		if (!RelocCacheBlocks[Index].bReadable)
			return false;
		std::memcpy(&Out, RelocCacheBytes.data() + Index * kRelocCacheBlockSize + Offset, sizeof(Out));
		return true;
	}

	// Miss. Reuse the least recently used entry once the cache is full, so the
	// footprint is fixed at kRelocCacheBlockCount blocks however many slots are asked
	// about.
	size_t Index = 0;
	if (RelocCacheLru.size() < kRelocCacheBlockCount)
	{
		Index = RelocCacheLru.size();
		RelocCacheLru.insert(RelocCacheLru.begin(), static_cast<uint8_t>(Index));
	}
	else
	{
		Index = RelocCacheLru.back();
		RelocCacheLru.pop_back();
		RelocCacheLru.insert(RelocCacheLru.begin(), static_cast<uint8_t>(Index));
	}

	uint8_t* Block = RelocCacheBytes.data() + Index * kRelocCacheBlockSize;
	const bool bOk = ReadBytes(Base, Block, kRelocCacheBlockSize);

	RelocCacheBlocks[Index].Base      = Base;
	RelocCacheBlocks[Index].bValid    = true;
	RelocCacheBlocks[Index].bReadable = bOk;

	// A failed block is remembered rather than retried. kRelocCacheBlockSize is at or
	// below the target's page size and the block is aligned to it, so the block
	// lies wholly inside one mapping or wholly outside: "this block cannot be
	// read" is therefore exactly "this slot cannot be read", not an approximation
	// of it. Over half of all slot resolutions fail, so retrying them would leave
	// most of the syscalls in place.
	if (!bOk)
		return false;

	std::memcpy(&Out, Block + Offset, sizeof(Out));
	return true;
}

// ── Module operations ──────────────────────────────────────────────────────────

std::pair<ElfScanner, ModuleInfo>* FMemoryAndroid::GetOrInsertModuleCache(const std::string& ModuleName)
{
	if (ModuleCache.contains(ModuleName))
		return &ModuleCache[ModuleName];

	auto ModInfoFromElf = [](ElfScanner& Elf) -> ModuleInfo
	{
		std::vector<MemRegionInfo> Segments;
		for (const auto& Seg : Elf.segments())
		{
			MemRegionInfo S{Seg.pathname, Seg.startAddress, Seg.endAddress, Seg.offset, Seg.readable, Seg.writeable, Seg.executable};
			Segments.push_back(S);
		}

		ModuleInfo ModInfo{Elf.realPath(), Elf.base(), Elf.end(), Elf.base(), std::move(Segments)};
		return ModInfo;
	};

	{
		ElfScanner Elf = KittyMemMgr.elfScanner.createWithSoInfo(KittyMemMgr.linkerScanner.findSoInfo(ModuleName));
		if (Elf.isValid())
		{
			ModuleInfo ModInfo      = ModInfoFromElf(Elf);
			ModuleCache[ModuleName] = {Elf, ModInfo};
			return &ModuleCache[ModuleName];
		}
	}

	{
		ElfScanner Elf = KittyMemMgr.elfScanner.createWithSoInfo(KittyMemMgr.nbScanner.findSoInfo(ModuleName));
		if (Elf.isValid())
		{
			ModuleInfo ModInfo      = ModInfoFromElf(Elf);
			ModuleCache[ModuleName] = {Elf, ModInfo};
			return &ModuleCache[ModuleName];
		}
	}

	{
		ElfScanner Elf = KittyMemMgr.elfScanner.findElf(ModuleName);
		if (Elf.isValid())
		{
			ModuleInfo ModInfo      = ModInfoFromElf(Elf);
			ModuleCache[ModuleName] = {Elf, ModInfo};
			return &ModuleCache[ModuleName];
		}
	}

	return nullptr;
}

ModuleInfo FMemoryAndroid::GetModuleInfo(const std::string& ModuleName)
{
	auto Module = GetOrInsertModuleCache(ModuleName);
	return Module ? Module->second : ModuleInfo{};
}

uintptr_t FMemoryAndroid::FindModuleSymbol(const std::string& ModuleName, const std::string& SymbolName)
{
	if (!ModuleName.empty() || !SymbolName.empty())
		return 0;

	auto Module = GetOrInsertModuleCache(ModuleName);
	if (Module)
	{
		if (Module->first.isValid())
		{
			uintptr_t Ret = Module->first.findSymbol(SymbolName);
			if (Ret == 0)
				Ret = Module->first.findDebugSymbol(SymbolName);

			return Ret;
		}
	}

	return 0;
}

ModuleInfo FMemoryAndroid::GetUnrealModule()
{
	if (CachedUnrealModule.first.isValid() && CachedUnrealModule.second.IsValid())
		return CachedUnrealModule.second;

	if (!GSettings.General.UnrealModuleName.empty())
	{
		auto Module = GetOrInsertModuleCache(GSettings.General.UnrealModuleName);
		if (Module)
		{
			CachedUnrealModule = *Module;
			return CachedUnrealModule.second;
		}
	}

	for (const std::string& Name : GSettings.General.AndroidUnrealDefaultNames)
	{
		auto Module = GetOrInsertModuleCache(Name);
		if (Module)
		{
			CachedUnrealModule = *Module;
			return CachedUnrealModule.second;
		}
	}

	return ModuleInfo{};
}

uintptr_t FMemoryAndroid::FindUnrealSymbol(const std::string& SymbolName)
{
	if (!CachedUnrealModule.first.isValid())
		((void)GetUnrealModule());

	if (CachedUnrealModule.first.isValid())
	{
		uintptr_t Ret = CachedUnrealModule.first.findSymbol(SymbolName);
		if (Ret == 0)
			Ret = CachedUnrealModule.first.findDebugSymbol(SymbolName);

		return Ret;
	}

	return 0;
}

bool FMemoryAndroid::DumpUnrealModule(const std::string& DestinationPath)
{
	if (!CachedUnrealModule.first.isValid())
		((void)GetUnrealModule());

	if (!CachedUnrealModule.first.isValid())
		return false;

	return KittyMemMgr.dumpMemELF(CachedUnrealModule.first, DestinationPath);
}

uintptr_t FMemoryAndroid::FindPatternInRange(uintptr_t Start,
                                             size_t Range,
                                             const std::string& Pattern,
                                             int Step,
                                             uint32_t SkipCount) const
{
	uint32_t Skipped = 0;
	for (const auto& Segment : BuildSegmentsRanges(Start, Range))
	{
		uintptr_t SubStart = Segment.GetStart();
		uintptr_t SubEnd   = Segment.GetEnd();

		uintptr_t Cur = SubStart;
		while (Cur < SubEnd)
		{
			uintptr_t Result = KittyMemMgr.memScanner.findIdaPatternFirst(Cur, SubEnd, Pattern);
			if (!Result)
				break;
			if (Skipped++ >= SkipCount)
				return Result + Step;
			Cur = Result + 4; // ARM64 instruction width
		}
	}
	return 0;
}

std::vector<uintptr_t> FMemoryAndroid::FindAllPatternInRange(uintptr_t Start,
                                                             size_t Range,
                                                             const std::string& Pattern,
                                                             int Step,
                                                             size_t MaxHits) const
{
	std::vector<uintptr_t> Results;
	for (const auto& Segment : BuildSegmentsRanges(Start, Range))
	{
		uintptr_t SubStart = Segment.GetStart();
		uintptr_t SubEnd   = Segment.GetEnd();

		for (uintptr_t R : KittyMemMgr.memScanner.findIdaPatternAll(SubStart, SubEnd, Pattern))
		{
			Results.push_back(R + Step);
			if (MaxHits && Results.size() >= MaxHits)
				return Results;
		}
	}
	return Results;
}

uintptr_t FMemoryAndroid::FindAlignedValueInRange(uintptr_t Value,
                                                  int32_t Alignment,
                                                  uintptr_t Start,
                                                  size_t Range) const
{
	for (const auto& Segment : BuildSegmentsRanges(Start, Range))
	{
		uintptr_t SubStart = Segment.GetStart();
		uintptr_t SubEnd   = Segment.GetEnd();

		if (Alignment > 1)
			SubStart = (SubStart + Alignment - 1) & ~static_cast<uintptr_t>(Alignment - 1);
		if (SubStart >= SubEnd)
			continue;
		if (uintptr_t R = BulkFindFirst(Value, Alignment, SubStart, SubEnd))
			return R;
	}
	return 0;
}

std::vector<uintptr_t> FMemoryAndroid::FindAllAlignedValuesInRange(uintptr_t Value,
                                                                   int32_t Alignment,
                                                                   uintptr_t Start,
                                                                   size_t Range,
                                                                   size_t MaxHits) const
{
	std::vector<uintptr_t> Results;
	for (const auto& Segment : BuildSegmentsRanges(Start, Range))
	{
		uintptr_t SubStart = Segment.GetStart();
		uintptr_t SubEnd   = Segment.GetEnd();

		if (Alignment > 1)
			SubStart = (SubStart + Alignment - 1) & ~static_cast<uintptr_t>(Alignment - 1);
		if (SubStart >= SubEnd)
			continue;
		BulkFindAll(Value, Alignment, SubStart, SubEnd, Results, MaxHits);
		if (MaxHits && Results.size() >= MaxHits)
			return Results;
	}
	return Results;
}

uintptr_t FMemoryAndroid::FindRawDataInRange(const void* Data,
                                             size_t DataSize,
                                             uintptr_t Start,
                                             size_t Range) const
{
	for (const auto& Segment : BuildSegmentsRanges(Start, Range))
	{
		uintptr_t SubStart = Segment.GetStart();
		uintptr_t SubEnd   = Segment.GetEnd();

		if (SubEnd - SubStart < DataSize)
			continue;
		if (uintptr_t R = KittyMemMgr.memScanner.findDataFirst(SubStart, SubEnd, Data, DataSize))
			return R;
	}
	return 0;
}

std::vector<uintptr_t> FMemoryAndroid::FindAllRawDataInRange(const void* Data,
                                                             size_t DataSize,
                                                             uintptr_t Start,
                                                             size_t Range,
                                                             size_t MaxHits) const
{
	std::vector<uintptr_t> Results;
	for (const auto& Segment : BuildSegmentsRanges(Start, Range))
	{
		uintptr_t SubStart = Segment.GetStart();
		uintptr_t SubEnd   = Segment.GetEnd();

		if (SubEnd - SubStart < DataSize)
			continue;
		for (uintptr_t R : KittyMemMgr.memScanner.findDataAll(SubStart, SubEnd, Data, DataSize))
		{
			Results.push_back(R);
			if (MaxHits && Results.size() >= MaxHits)
				return Results;
		}
	}
	return Results;
}

#endif // __ANDROID__
