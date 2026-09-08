#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../Unreal/Enums.h"

/**
 * @brief Which global name container the target uses.
 */
enum class ENamesType
{
	Array, ///< Legacy indirect array of FNameEntry pointers.
	Pool   ///< FNamePool, whose blocks hold packed FNameEntry records.
};

/**
 * @brief Offsets needed to walk the target's global name container.
 *
 * Every offset is discovered from target memory; stock engine values are never assumed.
 */
struct INamesLayout
{
	virtual ~INamesLayout() = default;

	/// @brief True when every required offset has been resolved.
	virtual bool IsValid() const = 0;
	/// @brief Which container this layout describes.
	virtual ENamesType GetType() const = 0;

protected:
	INamesLayout()                                   = default;
	INamesLayout(const INamesLayout&)                = default;
	INamesLayout(INamesLayout&&) noexcept            = default;
	INamesLayout& operator=(const INamesLayout&)     = default;
	INamesLayout& operator=(INamesLayout&&) noexcept = default;
};

/**
 * @brief Layout of a legacy indirect name array.
 *
 * Indexing is `Chunks[Idx / ElementsPerChunk][Idx % ElementsPerChunk]`, which yields
 * an FNameEntry pointer rather than an inline record.
 */
struct FNameArrayLayout : public INamesLayout
{
	/// @brief Offset of the chunk pointer array within the container.
	int32 Chunks;
	/// @brief Entries per chunk. A count, not an offset.
	int32 ElementsPerChunk;
	/// @brief Offset of the live entry count.
	int32 NumElements;

	/// @brief Offsets within a single FNameEntry.
	struct
	{
		/// @brief Bit of Index that marks a wide (UTF-16) name.
		int32 NameWideMask;
		/// @brief Offset of the character data.
		int32 String;
		/// @brief Offset of the packed `(Index << 1) | bIsWide` field.
		int32 Index;

		/// @brief True when every required entry offset has been resolved.
		inline bool IsValid() const { return Index != -1 && String != -1; }
	} FNameEntry;

	FNameArrayLayout()
	    : Chunks(0x0),
	      ElementsPerChunk(0x4000),
	      NumElements(-1),
	      FNameEntry({0x1, -1, -1})
	{
	}

	inline bool IsValid() const override { return Chunks != -1 && ElementsPerChunk > 0 && NumElements != -1 && FNameEntry.IsValid(); }

	inline ENamesType GetType() const override { return ENamesType::Array; }
};

/**
 * @brief Layout of an FNamePool.
 *
 * An index splits into a block and a byte offset:
 * `Blocks[Idx >> BlocksBit] + (Idx & ((1 << BlocksBit) - 1)) * FNameEntry.Stride`,
 * which lands on a packed record rather than a pointer.
 */
struct FNamePoolLayout : public INamesLayout
{
	/// @brief Bits of an index that address within a block.
	int32 BlocksBit;
	/// @brief Offset of the highest allocated block index. Optional.
	int32 MaxChunkIndex;
	/// @brief Offset of the write cursor within the current block. Optional.
	int32 ByteCursor;
	/// @brief Offset of the block pointer array within the container.
	int32 Blocks;

	/// @brief Offsets within a single packed FNameEntry.
	struct
	{
		/// @brief Bit of Header that marks a wide (UTF-16) name.
		int32 NameWideMask;
		/// @brief Right shift applied to Header to recover the name length.
		int32 LengthShiftCount;
		/// @brief Alignment that entry advances are rounded up to.
		int32 Stride;
		/// @brief Offset of the uint16 header holding length and flags.
		int32 Header;
		/// @brief Offset of the character data.
		int32 String;

		/// @brief True when every required entry offset has been resolved.
		inline bool IsValid() const { return NameWideMask != -1 && LengthShiftCount != -1 && Stride != -1 && Header != -1 && String != -1; }
	} FNameEntry;

	FNamePoolLayout()
	    : BlocksBit(0x10),
	      MaxChunkIndex(-1),
	      ByteCursor(-1),
	      Blocks(-1),
	      FNameEntry({0x1, -1, -1, -1, -1})
	{
	}

	inline bool IsValid() const override { return BlocksBit != -1 && Blocks != -1 && FNameEntry.IsValid(); }

	inline ENamesType GetType() const override { return ENamesType::Pool; }
};

/**
 * @brief Which global object container the target uses.
 */
enum class EObjectsType
{
	Array,  ///< Single contiguous FUObjectItem allocation.
	Chunked ///< Table of chunk pointers, each chunk holding FUObjectItems.
};

/**
 * @brief Offsets needed to walk the target's global object array.
 *
 * Every offset is discovered from target memory; stock engine values are never assumed.
 */
struct IObjectsLayout
{
	virtual ~IObjectsLayout() = default;

	/// @brief Which container this layout describes.
	virtual EObjectsType GetType() const = 0;
	/// @brief True when every required offset has been resolved.
	virtual bool IsValid() const = 0;

protected:
	IObjectsLayout()                                     = default;
	IObjectsLayout(const IObjectsLayout&)                = default;
	IObjectsLayout(IObjectsLayout&&) noexcept            = default;
	IObjectsLayout& operator=(const IObjectsLayout&)     = default;
	IObjectsLayout& operator=(IObjectsLayout&&) noexcept = default;
};

/**
 * @brief Layout of a contiguous object array.
 *
 * Indexing is `*(Objects) + Idx * FUObjectItem.Size`, then the object pointer is read
 * at `FUObjectItem.Object` within that slot.
 */
struct FFixedUObjectArrayLayout : public IObjectsLayout
{
	/// @brief Offset of the pointer to the item allocation.
	int32 Objects;
	/// @brief Offset of the allocation capacity. Optional.
	int32 MaxObjects;
	/// @brief Offset of the live object count.
	int32 NumObjects;

	/// @brief Offsets within a single FUObjectItem.
	struct
	{
		/// @brief Offset of the UObject pointer within the item.
		int32 Object;
		/// @brief Byte stride between consecutive items.
		int32 Size;

		/// @brief True when every required item offset has been resolved.
		inline bool IsValid() const
		{
			return Object != -1 && Size != -1;
		}
	} FUObjectItem;

	FFixedUObjectArrayLayout()
	    : Objects(-1),
	      MaxObjects(-1),
	      NumObjects(-1),
	      FUObjectItem({-1, -1})
	{
	}

	inline EObjectsType GetType() const override
	{
		return EObjectsType::Array;
	}

	inline bool IsValid() const override
	{
		return Objects != -1 && NumObjects != -1 && FUObjectItem.IsValid();
	}
};

/**
 * @brief Layout of a chunked object array.
 *
 * Indexing is `(*Objects)[Idx / ElementsPerChunk] + (Idx % ElementsPerChunk) *
 * FUObjectItem.Size`, then the object pointer is read at `FUObjectItem.Object`.
 */
struct FChunkedUObjectArrayLayout : public IObjectsLayout
{
	/// @brief Offset of the pointer to the chunk pointer table.
	int32 Objects;
	/// @brief Offset of the live object count.
	int32 NumElements;
	/// @brief Offset of the total capacity across all chunks. Optional.
	int32 MaxElements;
	/// @brief Offset of the chunk table capacity. Optional.
	int32 MaxChunks;
	/// @brief Offset of the allocated chunk count. Optional.
	int32 NumChunks;
	/// @brief Items per chunk. A count, not an offset.
	int32 ElementsPerChunk;

	/// @brief Offsets within a single FUObjectItem.
	struct
	{
		/// @brief Offset of the UObject pointer within the item.
		int32 Object;
		/// @brief Byte stride between consecutive items.
		int32 Size;

		/// @brief True when every required item offset has been resolved.
		inline bool IsValid() const
		{
			return Object != -1 && Size != -1;
		}
	} FUObjectItem;

	FChunkedUObjectArrayLayout()
	    : Objects(-1),
	      NumElements(-1),
	      MaxElements(-1),
	      MaxChunks(-1),
	      NumChunks(-1),
	      ElementsPerChunk(-1),
	      FUObjectItem({-1, -1})
	{
	}

	inline EObjectsType GetType() const override
	{
		return EObjectsType::Chunked;
	}

	inline bool IsValid() const override
	{
		return Objects != -1 && NumElements != -1 && ElementsPerChunk != -1 && FUObjectItem.IsValid();
	}
};

/**
 * @brief The layouts in use for the current target.
 *
 * Populated by the caller once detection or a profile override has produced a layout
 * that passes validation.
 */
struct FLayouts
{
	/// @brief Layout of the global name container.
	std::unique_ptr<INamesLayout> NamesLayout = nullptr;
	/// @brief Layout of the global object array.
	std::unique_ptr<IObjectsLayout> ObjectsLayout = nullptr;
};

/**
 * @brief Discovery and validation of the target's object and name structure layouts.
 *
 * In stock Unreal Engine the structures relate as follows. This is context only; the
 * target's memory is authoritative and every offset is discovered, never assumed:
 *
 *     FUObjectArray -> ObjObjects -> FFixedUObjectArray         -> FUObjectItem
 *                                 -> FChunkedFixedUObjectArray  -> chunks -> FUObjectItem
 *     TStaticIndirectArrayThreadSafeRead -> chunks -> FNameEntry*
 *     FNamePool -> FNameEntryAllocator   -> Blocks -> packed FNameEntry records
 *
 * Games reorder members, insert padding, add fields and obfuscate data, so the
 * detector reasons only from observed memory. The one universal semantic assumption
 * is that FName index 0 decodes to "None"; nothing is assumed about later names.
 *
 * Discovery and validation are separate: detection searches for a layout and scores
 * it, while the testers apply a supplied layout exactly. Neither assigns @c GLayouts.
 */
namespace LayoutDetection
{
	// ---- Shared scan configuration ----

	/// @brief Bytes read from a candidate header when enumerating offsets.
	inline constexpr int32 kHeaderReadSize = 0x100;
	/// @brief Longest plausible FName string.
	inline constexpr int32 kMaxNameLen = 0xFF;
	/// @brief Shortest plausible FName string.
	inline constexpr int32 kMinNameLen = 1;
	/// @brief Pointer width of the target.
	inline constexpr int32 kPtrSize = static_cast<int32>(sizeof(void*));

	// ---- Objects: structure bounds ----

	/// @brief Bytes read from an item array base when detecting the item stride.
	inline constexpr int32 kItemReadSize = 0x80;
	/// @brief Largest plausible FUObjectItem size.
	inline constexpr int32 kMaxItemStride = 0x40;
	/// @brief How far into an FUObjectItem to look for the object pointer.
	inline constexpr int32 kMaxObjectSlotScan = 0x20;
	/// @brief How far into a UObject to look for its InternalIndex field.
	inline constexpr int32 kMaxIndexScan = 0x80;
	/// @brief Smallest plausible live object count.
	inline constexpr int32 kMinObjectCount = 64;
	/// @brief Largest plausible live object count.
	inline constexpr int32 kMaxObjectCount = 0x800000;
	/// @brief Capacity fields exceed the live-count ceiling, so they need a wider bound.
	inline constexpr int32 kMaxObjectCapacity = 0x8000000;
	/// @brief Smallest valid ElementsPerChunk.
	inline constexpr int32 kMinElementsPerChunk = 0x400;
	/// @brief Largest valid ElementsPerChunk.
	inline constexpr int32 kMaxElementsPerChunk = 0x100000;
	/// @brief Used only when a single chunk exists and the stride cannot be derived.
	inline constexpr int32 kDefaultElementsPerChunk = 0x10000;
	/// @brief Chunk pointers read when probing a chunk table.
	inline constexpr int32 kMaxChunkScan = 0x40;
	/// @brief Readable chunk pointers required. One allocated chunk is normal, since
	///        a chunked array grows its table only as objects are created.
	inline constexpr int32 kMinChunkPtrRun = 1;
	/// @brief Number of item slots sampled when confirming a stride.
	inline constexpr int32 kItemSampleCount = 8;
	/// @brief Item indices sampled when confirming a stride.
	inline constexpr int32 kItemSampleIndices[8] = {1, 4, 8, 20, 50, 100, 250, 500};
	/// @brief Samples that must agree on InternalIndex before a stride is accepted.
	inline constexpr int32 kIndexConsistencyCount = 5;

	// ---- Objects: count discrimination ----
	// Density below a candidate count proves nothing, since every count smaller than
	// the real one is also fully populated. The discriminator is what lies past it.

	/// @brief Entries skipped past a candidate count before probing, so a growing
	///        array cannot cause a false rejection.
	inline constexpr int32 kCountSlack = 64;
	/// @brief Slots probed past a candidate count.
	inline constexpr int32 kPastProbeCount = 8;
	/// @brief Spacing between past-count probes.
	inline constexpr int32 kPastProbeStep = 64;
	/// @brief Confidence penalty applied in proportion to a populated tail.
	inline constexpr double kPastCountPenalty = 0.55;

	// ---- Names: structure bounds ----

	/// @brief How far into an entry to search for the "None" literal.
	inline constexpr int32 kNoneSearchBytes = 0x20;
	/// @brief "None" encoded as a little-endian uint32.
	inline constexpr uint32 kNoneAsUInt32 = 0x656E6F4E;
	/// @brief Character length of "None".
	inline constexpr int32 kNoneStrLen = 4;
	/// @brief Packed pool entries walked when scoring a candidate.
	inline constexpr int32 kNameWalkCount = 0x40;
	/// @brief Array entries sampled when scoring a candidate.
	inline constexpr int32 kNameSampleCount = 0x40;
	/// @brief Slots examined while gathering that sample. The engine registers hardcoded
	///        EName values at fixed indices and the enum has gaps, so a name array
	///        contains null slots and more must be visited than are collected.
	inline constexpr int32 kMaxArrayWalkSlots = 0x200;
	/// @brief Consecutive null slots taken to mean the populated range has ended.
	inline constexpr int32 kMaxConsecutiveHoles = 0x20;
	/// @brief Confirmed index samples required to accept the entry index offset. Sampled
	///        slots may land on holes, so agreement is counted rather than demanded of all.
	inline constexpr int32 kMinIndexSampleHits = 3;
	/// @brief Smallest valid ElementsPerChunk for a name array.
	inline constexpr int32 kMinNamesPerChunk = 0x400;
	/// @brief Largest valid ElementsPerChunk for a name array.
	inline constexpr int32 kMaxNamesPerChunk = 0x100000;
	/// @brief Used only when a single chunk exists.
	inline constexpr int32 kDefaultNamesPerChunk = 0x4000;
	/// @brief Largest plausible name count.
	inline constexpr int32 kMaxNameElements = 0x800000;
	/// @brief Smallest plausible name count.
	inline constexpr int32 kMinNameCount = 64;
	/// @brief Largest plausible FNamePool byte cursor.
	inline constexpr int32 kMaxByteCursor = 0x80000;
	/// @brief Block pointers walked when counting how many blocks are allocated.
	inline constexpr int32 kMaxPoolBlocks = 0x2000;
	/// @brief Entry indices sampled when locating the array index field.
	inline constexpr int32 kNameIndexSampleCount = 5;
	/// @brief Entry indices sampled when locating the array index field.
	inline constexpr int32 kNameIndexSampleIdx[5] = {1, 4, 8, 20, 50};
	/// @brief Bytes searched for the element counter. A legacy names struct declares
	///        its chunk table inline and places the counter after it.
	inline constexpr int32 kNamesHeaderScanSize = 0x1000;
	/// @brief Entries skipped past a candidate count before probing.
	inline constexpr int32 kNameTailMargin = 64;
	/// @brief Entries probed past a candidate count.
	inline constexpr int32 kNameTailProbe = 4;

	// ---- Names: pool entry header ----

	/// @brief A uint16 header cannot be shifted further than its own width.
	inline constexpr int32 kMaxLengthShiftCount = 0x10;
	/// @brief Number of candidate pool entry strides.
	inline constexpr int32 kPoolStrideCount = 3;
	/// @brief Candidate pool entry strides.
	inline constexpr int32 kPoolStrides[3] = {1, 2, 4};
	/// @brief Conventional wide-name mask; validated before use.
	inline constexpr int32 kDefaultNameWideMask = 0x1;

	// ---- Names: FNamePool BlocksBit ----

	/// @brief Smallest BlocksBit to try, giving a 16 KB block.
	inline constexpr int32 kBlocksBitMin = 0xE;
	/// @brief Largest BlocksBit to try, giving a 1 MB block.
	inline constexpr int32 kBlocksBitMax = 0x14;
	/// @brief Fallback BlocksBit when detection is inconclusive, giving a 64 KB block.
	inline constexpr int32 kBlocksBitDefault = 0x10;
	/// @brief Votes required before the object-sampling method is trusted.
	inline constexpr int32 kBlocksBitMinVotes = 2;
	/// @brief Blocks required before the contiguity method is meaningful.
	inline constexpr int32 kBlocksBitMinPairs = 3;
	/// @brief Bytes of a UObject scanned for its FName index field.
	inline constexpr int32 kFNameFieldScan = kPtrSize * 6;

	// ---- Scoring ----

	/// @brief Weight for coherent counts, pointers and relationships.
	inline constexpr double kWeightStructural = 0.30;
	/// @brief Weight for the fraction of sampled entries that validated.
	inline constexpr double kWeightSampleRatio = 0.50;
	/// @brief Weight for objects decoded through a names candidate.
	inline constexpr double kWeightCrossValidate = 0.20;
	/// @brief Per-hit bonus for a recognized engine name, capped when applied.
	inline constexpr double kBonusKnownNames = 0.05;
	/// @brief Bonus for index 0 decoding to "None".
	inline constexpr double kBonusNoneAtZero = 0.10;
	/// @brief Tie-break applied to a preferred type; never decisive on its own.
	inline constexpr double kHintTieBreakBonus = 0.03;

	// ---- Acceptance ----

	/// @brief Confidence a candidate must reach to be accepted.
	inline constexpr double kDefaultMinConfidence = 0.60;
	/// @brief Entries sampled during deep validation.
	inline constexpr int32 kDefaultMaxSamples = 200;
	/// @brief Entries sampled by the lightweight testers.
	inline constexpr int32 kTestSampleCount = 50;
	/// @brief Fraction of samples a supplied layout must satisfy to be valid.
	inline constexpr double kTestMinValidRatio = 0.80;

	// ---- Diagnostics ----

	/// @brief Bytes of a candidate struct to dump.
	inline constexpr int32 kDumpHeaderSize = 0x900;
	/// @brief Bytes of each pointed-to target to dump.
	inline constexpr int32 kDumpFollowSize = 0x80;
	/// @brief Pointers followed per dump.
	inline constexpr int32 kDumpFollowLimit = 8;
	/// @brief Rejection reasons retained per detection run.
	inline constexpr int32 kMaxRejectReasons = 24;

	/**
	 * @brief Whether a type hint orders the search or restricts it.
	 *
	 * A hint is guidance, never proof; a hinted type must still pass full validation.
	 */
	enum class EHintMode
	{
		Prefer, ///< Try the hinted type first; another type may win on stronger evidence.
		Require ///< Accept only the hinted type.
	};

	/**
	 * @brief Hints and tuning shared by detection and testing.
	 */
	struct FOptions
	{
		/// @brief Preferred objects structure kind, if known.
		std::optional<EObjectsType> ObjectsTypeHint;
		/// @brief Preferred names structure kind, if known.
		std::optional<ENamesType> NamesTypeHint;
		/// @brief How the hints above are applied.
		EHintMode HintMode = EHintMode::Prefer;

		/// @brief Upper bound on entries sampled during deep validation.
		int32 MaxSamples = kDefaultMaxSamples;
		/// @brief Confidence a candidate must reach to be accepted.
		double MinimumConfidence = kDefaultMinConfidence;
		/// @brief Run the deeper validation stages.
		bool bEnableDeepValidation = true;

		/// @brief Decode sampled UObject FNames through a names candidate. Skipped when
		///        ObjectArray is not yet initialized, keeping names detection standalone.
		bool bEnableCrossValidation = true;
	};

	/**
	 * @brief Result of discovering an objects layout.
	 */
	struct FObjectsDetectionResult
	{
		/// @brief True when Layout holds a validated layout.
		bool bSuccess = false;
		/// @brief Confidence in the selected layout, in the range [0, 1].
		double Confidence = 0.0;
		/// @brief The detected layout, or null on failure.
		std::unique_ptr<IObjectsLayout> Layout;
		/// @brief Per-stage observations supporting the outcome.
		std::vector<std::string> Evidence;
		/// @brief Reasons candidates were rejected.
		std::vector<std::string> Failures;
		/// @brief Offset-level detail behind Evidence. Intended for Debug logging;
		///        Evidence stays readable without it.
		std::vector<std::string> Details;
	};

	/**
	 * @brief Result of discovering a names layout.
	 */
	struct FNamesDetectionResult
	{
		/// @brief True when Layout holds a validated layout.
		bool bSuccess = false;
		/// @brief Confidence in the selected layout, in the range [0, 1].
		double Confidence = 0.0;
		/// @brief The detected layout, or null on failure.
		std::unique_ptr<INamesLayout> Layout;
		/// @brief Per-stage observations supporting the outcome.
		std::vector<std::string> Evidence;
		/// @brief Reasons candidates were rejected.
		std::vector<std::string> Failures;
		/// @brief Offset-level detail behind Evidence. Intended for Debug logging;
		///        Evidence stays readable without it.
		std::vector<std::string> Details;
	};

	/**
	 * @brief Result of validating a supplied objects layout.
	 */
	struct FObjectsTestResult
	{
		/// @brief True when the supplied layout satisfied every check.
		bool bValid = false;
		/// @brief Fraction of samples that validated, in the range [0, 1].
		double Confidence = 0.0;
		/// @brief Number of item slots examined.
		int32 SamplesTested = 0;
		/// @brief Number of item slots that held a self-consistent object.
		int32 SamplesValid = 0;
		/// @brief Per-stage observations supporting the outcome.
		std::vector<std::string> Evidence;
		/// @brief Reasons the layout was rejected.
		std::vector<std::string> Failures;
		/// @brief Offset-level detail behind Evidence. Intended for Debug logging;
		///        Evidence stays readable without it.
		std::vector<std::string> Details;
	};

	/**
	 * @brief Result of validating a supplied names layout.
	 */
	struct FNamesTestResult
	{
		/// @brief True when the supplied layout satisfied every check.
		bool bValid = false;
		/// @brief Fraction of samples that validated, in the range [0, 1].
		double Confidence = 0.0;
		/// @brief Number of name entries examined.
		int32 SamplesTested = 0;
		/// @brief Number of entries that decoded coherently.
		int32 SamplesValid = 0;
		/// @brief Per-stage observations supporting the outcome.
		std::vector<std::string> Evidence;
		/// @brief Reasons the layout was rejected.
		std::vector<std::string> Failures;
		/// @brief Offset-level detail behind Evidence. Intended for Debug logging;
		///        Evidence stays readable without it.
		std::vector<std::string> Details;
	};

	/**
	 * @brief Discovers the objects array layout at a candidate address.
	 *
	 * The candidate may be an outer FUObjectArray or ObjObjects itself; both are
	 * covered, since the embedded array is found by offset within the header scan.
	 * Returned offsets are relative to @p CandidateAddress.
	 *
	 * @param CandidateAddress Address to interpret. Must already be decrypted.
	 * @param Options Hints and tuning.
	 * @return The detected layout with its confidence and diagnostics. Never assigns @c GLayouts.
	 */
	FObjectsDetectionResult DetectObjectsLayout(uintptr_t CandidateAddress, const FOptions& Options = {});

	/**
	 * @brief Discovers the names layout at a candidate address.
	 *
	 * Both the legacy indirect array and FNamePool are tried and scored. Returned
	 * offsets are relative to @p CandidateAddress.
	 *
	 * @param CandidateAddress Address to interpret. Must already be decrypted.
	 * @param Options Hints and tuning.
	 * @return The detected layout with its confidence and diagnostics. Never assigns @c GLayouts.
	 */
	FNamesDetectionResult DetectNamesLayout(uintptr_t CandidateAddress, const FOptions& Options = {});

	/**
	 * @brief Validates an already-known objects layout against target memory.
	 *
	 * Applies the supplied offsets exactly, performing no discovery and leaving
	 * @p Layout unmodified.
	 *
	 * @param CandidateAddress Address the layout's offsets are relative to.
	 * @param Layout Layout to validate.
	 * @param Options Tuning; hints are ignored.
	 * @return Validity, confidence and diagnostics.
	 */
	FObjectsTestResult TestObjectsLayout(uintptr_t CandidateAddress, const IObjectsLayout* Layout, const FOptions& Options = {});

	/**
	 * @brief Validates an already-known names layout against target memory.
	 *
	 * Applies the supplied offsets exactly, performing no discovery and leaving
	 * @p Layout unmodified.
	 *
	 * @param CandidateAddress Address the layout's offsets are relative to.
	 * @param Layout Layout to validate.
	 * @param Options Tuning; hints are ignored.
	 * @return Validity, confidence and diagnostics.
	 */
	FNamesTestResult TestNamesLayout(uintptr_t CandidateAddress, const INamesLayout* Layout, const FOptions& Options = {});

	/**
	 * @brief Logs the raw bytes around a candidate for offline diagnosis.
	 *
	 * Emits the candidate header as pointer and int32 slots with an ASCII view,
	 * collapsing runs of zeros, then follows each readable pointer and dumps the
	 * start of its target. Safe to call on any address.
	 *
	 * @param CandidateAddress Address to dump.
	 * @param Tag Short label prefixed to every emitted line, such as "GObjects".
	 */
	void DumpCandidateMemory(uintptr_t CandidateAddress, const std::string& Tag);
} // namespace LayoutDetection
