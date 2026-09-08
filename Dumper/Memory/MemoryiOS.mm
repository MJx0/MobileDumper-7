#ifdef __APPLE__

#include "MemoryiOS.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#import <Foundation/Foundation.h>
#include <KittyMemory/KittyScanner.hpp>

#include "../Settings.h"
#include "../Utils/Logger.h"

bool FMemoryiOS::Initialize()
{
	return RefreshMemory();
}

std::string FMemoryiOS::GetProcessName() const
{
	NSBundle* MainBundle = [NSBundle mainBundle];
	if (MainBundle && MainBundle.bundleIdentifier.length > 0)
		return [MainBundle.bundleIdentifier UTF8String];

	return [[[NSProcessInfo processInfo] processName] UTF8String];
}

int FMemoryiOS::GetProcessID() const
{
	return TaskId;
}

bool FMemoryiOS::RefreshMemory()
{
	const std::lock_guard<std::mutex> Lock(RelocCacheMutex);
	RelocCacheBlocks.clear();
	RelocCacheLru.clear();
	RelocCacheBytes.clear();

	PtrValidator.setUseCache(true);

	auto AllRegions = KittyMemory::getAllRegions();
	MemoryRegionsCache.clear();
	MemoryRegionsCache.reserve(AllRegions.size());
	for (const auto& it : AllRegions)
	{
		MemRegionInfo Region{it.name, it.start, it.end, it.offset, it.readable, it.writeable, it.executable};
		MemoryRegionsCache.push_back(Region);
	}

	return PtrValidator.cachedRegions().size() > 0 && MemoryRegionsCache.size() > 0;
}

bool FMemoryiOS::IsMemoryAccessOk() const
{
	if (!PtrValidator.cachedRegions().empty() && !MemoryRegionsCache.empty())
	{
		uintptr_t Word = 0;
		return ReadBytes(reinterpret_cast<uintptr_t>(_dyld_get_image_header(0)), &Word, sizeof(Word)) > 0;
	}
	return false;
}

bool FMemoryiOS::IsValidAddress(uintptr_t Address) const
{
	return PtrValidator.isPtrReadable(Address, sizeof(void*));
}

bool FMemoryiOS::IsAddressReadable(uintptr_t Address, size_t Size) const
{
	return PtrValidator.isPtrReadable(Address, Size);
}

bool FMemoryiOS::IsAddressWriteable(uintptr_t Address, size_t Size) const
{
	return PtrValidator.isPtrWritable(Address, Size);
}

bool FMemoryiOS::IsAddressExecutable(uintptr_t Address, size_t Size) const
{
	return PtrValidator.isPtrExecutable(Address, Size);
}

MemRegionInfo FMemoryiOS::GetAddressRegionInfo(uintptr_t Address) const
{
	if (Address == 0)
		return {};

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

size_t FMemoryiOS::ReadBytes(uintptr_t Address, void* Buffer, size_t Size) const
{
	if (Address == 0 || Buffer == nullptr || Size == 0)
		return 0;

	const size_t PageSize = GetPageSize();

	uintptr_t Current = Address;
	size_t Remaining  = Size;
	size_t TotalRead  = 0;

	while (Remaining > 0)
	{
		// Bytes remaining in the current page.
		const size_t PageOffset  = Current & (PageSize - 1);
		const size_t BytesInPage = PageSize - PageOffset;
		const size_t ChunkSize   = std::min(Remaining, BytesInPage);

		// Validate only this page/chunk.
		if (!PtrValidator.isPtrReadable(Current, ChunkSize))
			break;

		memcpy(static_cast<uint8_t*>(Buffer) + TotalRead,
		       reinterpret_cast<const void*>(Current),
		       ChunkSize);

		Current   += ChunkSize;
		Remaining -= ChunkSize;
		TotalRead += ChunkSize;
	}

	return TotalRead;
}

bool FMemoryiOS::WriteBytes(uintptr_t Address, const void* Buffer, size_t Size) const
{
	return PtrValidator.isPtrWritable(Address, Size)
	           ? (memcpy(reinterpret_cast<void*>(Address),
	                     reinterpret_cast<const void*>(Buffer),
	                     Size) != nullptr)
	           : false;
}

bool FMemoryiOS::ReadRelocationPointer(uintptr_t Slot, uintptr_t& Out) const
{
	uintptr_t Stored = 0;
	if (!ReadRelocationSlot(Slot, Stored))
		return false;
	if (Stored == 0 || !IsAddressReadable(Stored))
		return false;

	Out = Stored;
	return true;
}

bool FMemoryiOS::ReadRelocationSlot(uintptr_t Slot, uintptr_t& Out) const
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

std::pair<KittyScanner::MachOImage, ModuleInfo>* FMemoryiOS::GetOrInsertModuleCache(const std::string& ModuleName)
{
	if (ModuleCache.contains(ModuleName))
		return &ModuleCache[ModuleName];

	auto ModInfoFromMachO = [](KittyScanner::MachOImage& Image) -> ModuleInfo
	{
		std::vector<MemRegionInfo> Segments;
		for (const auto& Seg : Image.segments())
		{
			MemRegionInfo S{Seg.first, Seg.second.start, Seg.second.end, Seg.second.offset, Seg.second.readable, Seg.second.writeable, Seg.second.executable};
			Segments.push_back(S);
		}

		ModuleInfo ModInfo{Image.name(), Image.start(), Image.end(), Image.slide(), std::move(Segments)};
		return ModInfo;
	};

	{
		KittyScanner::MachOImage Image = KittyScanner::MachOImage::findMachOImage(ModuleName.empty() ? nullptr : ModuleName.c_str());
		if (Image.isValid())
		{
			ModuleInfo ModInfo      = ModInfoFromMachO(Image);
			ModuleCache[ModuleName] = {Image, ModInfo};
			return &ModuleCache[ModuleName];
		}
	}

	return nullptr;
}

ModuleInfo FMemoryiOS::GetModuleInfo(const std::string& ModuleName)
{
	auto Module = GetOrInsertModuleCache(ModuleName);
	return Module ? Module->second : ModuleInfo{};
}

uintptr_t FMemoryiOS::FindModuleSymbol(const std::string& ModuleName, const std::string& SymbolName)
{
	if (!ModuleName.empty() || !SymbolName.empty())
		return 0;

	auto Module = GetOrInsertModuleCache(ModuleName);
	if (Module && Module->first.isValid())
	{
		uintptr_t Address = Module->first.findSymbol(SymbolName);

		if (Address == 0 && SymbolName[0] != '_')
		{
			std::string PrefixedSymbolName = "_" + SymbolName;
			return Module->first.findSymbol(PrefixedSymbolName);
		}
	}

	return 0;
}

ModuleInfo FMemoryiOS::GetUnrealModule()
{
	if (CachedUnrealModule.first.isValid() && CachedUnrealModule.second.IsValid())
		return CachedUnrealModule.second;

	std::string ModuleName = GSettings.General.UnrealModuleName;
	if (ModuleName.empty())
		ModuleName = KittyScanner::MachOImage::getMainImage().name();

	auto Module = GetOrInsertModuleCache(ModuleName);
	if (Module)
	{
		CachedUnrealModule = *Module;
		return CachedUnrealModule.second;
	}

	return ModuleInfo{};
}

uintptr_t FMemoryiOS::FindUnrealSymbol(const std::string& SymbolName)
{
	if (!CachedUnrealModule.first.isValid())
		((void)GetUnrealModule());

	if (CachedUnrealModule.first.isValid())
		return CachedUnrealModule.first.findSymbol(SymbolName);

	return 0;
}

uintptr_t FMemoryiOS::FindPatternInRange(uintptr_t Start,
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
			uintptr_t Result = KittyScanner::findIdaPatternFirst(Cur, SubEnd, Pattern);
			if (!Result)
				break;
			if (Skipped++ >= SkipCount)
				return Result + Step;
			Cur = Result + 4; // ARM64 instruction width
		}
	}

	return 0;
}

std::vector<uintptr_t> FMemoryiOS::FindAllPatternInRange(uintptr_t Start,
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

		for (uintptr_t R : KittyScanner::findIdaPatternAll(SubStart, SubEnd, Pattern))
		{
			Results.push_back(R + Step);
			if (MaxHits && Results.size() >= MaxHits)
				return Results;
		}
	}
	return Results;
}

uintptr_t FMemoryiOS::FindAlignedValueInRange(uintptr_t Value,
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

std::vector<uintptr_t> FMemoryiOS::FindAllAlignedValuesInRange(uintptr_t Value,
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

uintptr_t FMemoryiOS::FindRawDataInRange(const void* Data,
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
		if (uintptr_t R = KittyScanner::findDataFirst(SubStart, SubEnd, Data, DataSize))
			return R;
	}
	return 0;
}

std::vector<uintptr_t> FMemoryiOS::FindAllRawDataInRange(const void* Data,
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
		for (uintptr_t R : KittyScanner::findDataAll(SubStart, SubEnd, Data, DataSize))
		{
			Results.push_back(R);
			if (MaxHits && Results.size() >= MaxHits)
				return Results;
		}
	}
	return Results;
}

#endif // __APPLE__
