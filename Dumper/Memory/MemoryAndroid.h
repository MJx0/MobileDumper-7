#pragma once
#ifdef __ANDROID__

#include <unistd.h>

#include <KittyMemoryEx/KittyMemoryEx.hpp>
#include <KittyMemoryEx/KittyMemoryMgr.hpp>
#include <KittyMemoryEx/KittyPtrValidator.hpp>

#include "IMemory.h"

/**
 * @brief Android memory backend — external process reads via KittyMemoryEx.
 */
class FMemoryAndroid : public IMemory
{
	/// @brief Process ID of the target application.
	pid_t ProcessId;

	/// @brief Memory operation mode used by KittyMemoryEx.
	EKittyMemOP KittyMemOp;

	/// @brief Public so platform code can access elfScanner/memScanner directly.
	mutable KittyMemoryMgr KittyMemMgr;

	/// @brief Fast Kitty poniter validator.
	mutable KittyPtrValidator PtrValidator;

	/// @brief Cached memory regions information.
	mutable std::vector<MemRegionInfo> MemoryRegionsCache;

	/// @brief Cached ELF scanners and module information by module name.
	mutable std::unordered_map<std::string, std::pair<ElfScanner, ModuleInfo>> ModuleCache;

	/// @brief Cached Unreal Engine module information.
	mutable std::pair<ElfScanner, ModuleInfo> CachedUnrealModule;

	/**
	 * @brief Retrieves a cached module entry or inserts a new one.
	 * @param ModuleName Name of the module to retrieve or cache.
	 * @return Pointer to the cached module entry.
	 */
	std::pair<ElfScanner, ModuleInfo>* GetOrInsertModuleCache(const std::string& ModuleName);

	/// @brief Number of bytes loaded per relocation-slot cache miss.
	static constexpr size_t kRelocCacheBlockSize = 4096;

	/// @brief Maximum number of blocks retained by the relocation-slot cache.
	static constexpr size_t kRelocCacheBlockCount = 64;

	/// @brief Metadata for one cached relocation block.
	struct RelocCacheBlock
	{
		/// @brief Block-aligned target address represented by this entry.
		uintptr_t Base = 0;

		/// @brief Whether this entry currently represents a cached block.
		bool bValid = false;

		/// @brief Whether the block was successfully read from the target process.
		bool bReadable = false;
	};

	/// @brief Synchronizes access to the relocation-slot cache.
	mutable std::mutex RelocCacheMutex;

	/// @brief Storage for the bytes of all cached relocation blocks.
	mutable std::vector<uint8_t> RelocCacheBytes;

	/// @brief Metadata for the cached relocation blocks.
	mutable std::vector<RelocCacheBlock> RelocCacheBlocks;

	/// @brief Cache-entry indices ordered from most to least recently used.
	mutable std::vector<uint8_t> RelocCacheLru;

	/**
	 * @brief Reads a pointer-sized value from a relocation slot using the block cache.
	 *
	 * @param Slot Address of the relocation slot.
	 * @param Out Receives the stored pointer-sized value.
	 * @return true if the slot was read successfully.
	 */
	bool ReadRelocationSlot(uintptr_t Slot, uintptr_t& Out) const;

public:
	FMemoryAndroid()           = default;
	~FMemoryAndroid() override = default;

	FMemoryAndroid(const FMemoryAndroid&)            = delete;
	FMemoryAndroid& operator=(const FMemoryAndroid&) = delete;

	/// @brief Attaches to the process identified by @p InPid.
	/// @param InPid    Target PID; defaults to the calling process (getpid()).
	/// @param InMemOp  Memory operation mode; defaults to EK_MEM_OP_SYSCALL.
	explicit FMemoryAndroid(pid_t InPid = getpid(), EKittyMemOP InMemOp = EK_MEM_OP_SYSCALL)
	    : ProcessId(InPid),
	      KittyMemOp(InMemOp)
	{
	}

	/// @copydoc IMemory::Initialize
	bool Initialize() override;

	/// @copydoc IMemory::GetProcessName
	std::string GetProcessName() const override;

	/// @copydoc IMemory::GetProcessID
	int GetProcessID() const override;

	/// @copydoc IMemory::RefreshMemory
	bool RefreshMemory() override;

	/// @copydoc IMemory::IsMemoryAccessOk
	bool IsMemoryAccessOk() const override;

	/// @copydoc IMemory::IsValidAddress
	bool IsValidAddress(uintptr_t Address) const override;

	/// @copydoc IMemory::IsBadReadPtr
	bool IsAddressReadable(uintptr_t Address, size_t Size = sizeof(void*)) const override;

	/// @copydoc IMemory::IsAddressWriteable
	bool IsAddressWriteable(uintptr_t Address, size_t Size = sizeof(void*)) const override;

	/// @copydoc IMemory::IsAddressExecutable
	bool IsAddressExecutable(uintptr_t Address, size_t Size = sizeof(void*)) const override;

	/// @copydoc IMemory::GetAddressRegionInfo
	MemRegionInfo GetAddressRegionInfo(uintptr_t Address) const override;

	/// @copydoc IMemory::ReadBytes
	size_t ReadBytes(uintptr_t Address, void* Buffer, size_t Size) const override;

	/// @copydoc IMemory::WriteBytes
	bool WriteBytes(uintptr_t Address, const void* Buffer, size_t Size) const override;

	/// @copydoc IMemory::ReadRelocationPointer
	bool ReadRelocationPointer(uintptr_t Slot, uintptr_t& Out) const override;

	/// @copydoc IMemory::GetModuleInfo
	ModuleInfo GetModuleInfo(const std::string& ModuleName) override;

	/// @copydoc IMemory::FindModuleSymbol
	uintptr_t FindModuleSymbol(const std::string& ModuleName, const std::string& SymbolName) override;

	/// @copydoc IMemory::GetUnrealModule
	ModuleInfo GetUnrealModule() override;

	/// @copydoc IMemory::FindUnrealSymbol
	uintptr_t FindUnrealSymbol(const std::string& SymbolName) override;

	/// @copydoc IMemory::DumpUnrealModule
	bool DumpUnrealModule(const std::string& DestinationPath) override;

	/// @copydoc IMemory::FindPatternInRange
	uintptr_t FindPatternInRange(uintptr_t Start, size_t Range, const std::string& Pattern, int Step = 0, uint32_t SkipCount = 0) const override;

	/// @copydoc IMemory::FindAllPatternInRange
	std::vector<uintptr_t> FindAllPatternInRange(uintptr_t Start, size_t Range, const std::string& Pattern, int Step = 0, size_t MaxHits = 0) const override;

	/// @copydoc IMemory::FindAlignedValueInRange
	uintptr_t FindAlignedValueInRange(uintptr_t Value, int32_t Alignment, uintptr_t Start, size_t Range) const override;

	/// @copydoc IMemory::FindAllAlignedValuesInRange
	std::vector<uintptr_t> FindAllAlignedValuesInRange(uintptr_t Value, int32_t Alignment, uintptr_t Start, size_t Range, size_t MaxHits = 0) const override;

	/// @copydoc IMemory::FindRawDataInRange
	uintptr_t FindRawDataInRange(const void* Data, size_t DataSize, uintptr_t Start, size_t Range) const override;

	/// @copydoc IMemory::FindAllRawDataInRange
	std::vector<uintptr_t> FindAllRawDataInRange(const void* Data, size_t DataSize, uintptr_t Start, size_t Range, size_t MaxHits = 0) const override;
};

#endif // __ANDROID__
