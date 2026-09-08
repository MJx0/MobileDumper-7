#include "Layouts.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <string_view>

#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"

#include "../Unreal/NameArray.h"
#include "../Unreal/ObjectArray.h"

namespace LayoutDetection
{
	namespace
	{

		bool IsReadable(uintptr_t Address, size_t Size = sizeof(void*))
		{
			return Address != 0 && GMemory && GMemory->IsAddressReadable(Address, Size);
		}

		template <typename T>
		bool SafeRead(uintptr_t Address, T& Out)
		{
			if (!IsReadable(Address, sizeof(T)))
				return false;

			Out = GMemory->Read<T>(Address);
			return true;
		}

		bool ReadBlock(uintptr_t Address, std::vector<uint8_t>& Out, size_t Len)
		{
			if (!IsReadable(Address, Len))
				return false;

			Out.assign(Len, 0);
			return GMemory->ReadBytes(Address, Out.data(), Len);
		}

		/*
		 * Reads as much as the mapping allows, shrinking on failure.
		 *
		 * A struct near the end of its region cannot satisfy a large fixed read even though
		 * its early bytes are fine, and a hard failure there silently skips whatever was
		 * being searched for. Returns the number of bytes actually read.
		 */
		size_t ReadBlockBestEffort(uintptr_t Address, std::vector<uint8_t>& Out, size_t MaxLen)
		{
			for (size_t Len = MaxLen; Len >= sizeof(uintptr_t); Len /= 2)
			{
				if (ReadBlock(Address, Out, Len))
					return Len;
			}

			Out.clear();
			return 0;
		}

		uintptr_t PeekPtr(const std::vector<uint8_t>& Block, size_t Offset)
		{
			if (Offset + sizeof(uintptr_t) > Block.size())
				return 0;

			uintptr_t Value = 0;
			memcpy(&Value, Block.data() + Offset, sizeof(uintptr_t));
			return Value;
		}

		int32 PeekInt32(const std::vector<uint8_t>& Block, size_t Offset)
		{
			if (Offset + sizeof(int32) > Block.size())
				return 0;

			int32 Value = 0;
			memcpy(&Value, Block.data() + Offset, sizeof(int32));
			return Value;
		}

		uint32 PeekUInt32(const std::vector<uint8_t>& Block, size_t Offset)
		{
			if (Offset + sizeof(uint32) > Block.size())
				return 0;

			uint32 Value = 0;
			memcpy(&Value, Block.data() + Offset, sizeof(uint32));
			return Value;
		}

		/*
		 * Gated on a walkable vtable rather than on the value merely looking like a pointer:
		 * an FUObjectItem's Flags/ClusterRootIndex pair reads as a large aligned value whenever
		 * ClusterRootIndex is non-zero, and treating that as an object slot collapses the
		 * detected stride to 8 and loses the real layout.
		 */
		bool IsLikelyUObject(uintptr_t Ptr)
		{
			uintptr_t VTable = 0;
			if (!SafeRead(Ptr, VTable) || !IsReadable(VTable))
				return false;

			uintptr_t FirstVFunc = 0;
			return SafeRead(VTable, FirstVFunc) && FirstVFunc != 0;
		}

		/*
		 * Strict ASCII. Used only as a heuristic when judging whether raw bytes look like a
		 * name at all; it is the wrong test for validating a real one, since asset names are
		 * routinely CJK and arrive as UTF-8 bytes above 0x7E.
		 */
		bool IsPrintableAscii(std::string_view Str)
		{
			if (Str.empty() || Str.size() > static_cast<size_t>(kMaxNameLen))
				return false;

			for (unsigned char Ch : Str)
			{
				if (Ch < 0x20 || Ch > 0x7E)
					return false;
			}

			return true;
		}

		/// @brief Accepts any name that carries no control characters, including non-ASCII.
		bool IsPlausibleNameText(std::string_view Str)
		{
			if (Str.empty() || Str.size() > static_cast<size_t>(kMaxNameLen))
				return false;

			for (unsigned char Ch : Str)
			{
				if (Ch < 0x20 && Ch != '\n' && Ch != '\r' && Ch != '\t')
					return false;
			}

			return true;
		}

		struct FPointerRun
		{
			int32 Offset = 0;
			int32 Count  = 0;
		};

		std::vector<int32> CollectPointerCandidates(const std::vector<uint8_t>& Block)
		{
			std::vector<int32> Result;
			for (size_t Offset = 0; Offset + sizeof(void*) <= Block.size(); Offset += sizeof(void*))
			{
				if (IsReadable(PeekPtr(Block, Offset)))
					Result.push_back(static_cast<int32>(Offset));
			}
			return Result;
		}

		std::vector<int32> CollectInt32Candidates(const std::vector<uint8_t>& Block, int32 Min, int32 Max)
		{
			std::vector<int32> Result;
			for (size_t Offset = 0; Offset + sizeof(int32) <= Block.size(); Offset += sizeof(int32))
			{
				const int32 Value = PeekInt32(Block, Offset);
				if (Value >= Min && Value <= Max)
					Result.push_back(static_cast<int32>(Offset));
			}
			return Result;
		}

		std::vector<FPointerRun> CollectPointerRuns(const std::vector<uint8_t>& Block)
		{
			std::vector<FPointerRun> Result;

			size_t Offset = 0;
			while (Offset + sizeof(void*) <= Block.size())
			{
				if (!IsReadable(PeekPtr(Block, Offset)))
				{
					Offset += sizeof(void*);
					continue;
				}

				FPointerRun Run;
				Run.Offset = static_cast<int32>(Offset);
				while (Offset + sizeof(void*) <= Block.size() && IsReadable(PeekPtr(Block, Offset)))
				{
					Run.Count++;
					Offset += sizeof(void*);
				}
				Result.push_back(Run);
			}

			return Result;
		}

		/*
		 * Bonus scoring only. These names are recognized wherever they happen to appear;
		 * their indices are never required, because a game may reorder or obfuscate every
		 * name after index 0.
		 */
		int32 CountKnownNameHits(const std::vector<std::string>& Names)
		{
			static constexpr const char* kKnownNames[] = {
			    "None", "ByteProperty", "IntProperty", "BoolProperty", "FloatProperty", "NameProperty", "StrProperty", "TextProperty", "StructProperty", "ObjectProperty", "ClassProperty", "ArrayProperty", "MapProperty", "Class", "Package", "Function", "Object", "Field", "Struct", "Enum", "Core", "CoreUObject", "/Script/CoreUObject"};

			int32 Hits = 0;
			for (const std::string& Name : Names)
			{
				for (const char* Known : kKnownNames)
				{
					if (Name == Known)
					{
						Hits++;
						break;
					}
				}
			}
			return Hits;
		}

		struct FItemLayout
		{
			int32 ObjectOffset = -1;
			int32 ItemSize     = -1;
			int32 IndexOffset  = -1;

			bool IsValid() const { return ObjectOffset != -1 && ItemSize > 0 && IndexOffset != -1; }
		};

		uintptr_t ReadObjectAt(uintptr_t ItemAddr, const FItemLayout& Item)
		{
			if (ItemAddr == 0 || Item.ObjectOffset < 0)
				return 0;

			uintptr_t Ptr = 0;
			if (!SafeRead(ItemAddr + Item.ObjectOffset, Ptr))
				return 0;

			if (ObjectArray::DecryptObjectItemFn)
				ObjectArray::DecryptObjectItemFn(Ptr);

			return Ptr;
		}

		std::string ReadDecryptedName(uintptr_t StringAddr, int32 NameLen)
		{
			if (NameLen <= 0 || NameLen > kMaxNameLen)
				return {};

			// Legacy array entries carry no length, so callers pass the cap. Shrink to what is
			// mapped rather than failing: a short name at the end of a region would otherwise
			// read as unreadable and reject an otherwise correct layout.
			int32 Readable = NameLen;
			while (Readable > 0 && !IsReadable(StringAddr, static_cast<size_t>(Readable)))
				Readable /= 2;

			if (Readable <= 0)
				return {};

			std::string Str = GMemory->ReadUTF8(StringAddr, Readable);
			if (Str.empty())
				return {};

			if (NameArray::DecryptUTF8Fn)
				NameArray::DecryptUTF8Fn(Str.data(), Str.length());

			return Str;
		}

		std::u16string ReadDecryptedNameWide(uintptr_t StringAddr, int32 NameLen)
		{
			if (NameLen <= 0 || NameLen > kMaxNameLen || !IsReadable(StringAddr, static_cast<size_t>(NameLen) * 2))
				return {};

			std::u16string Str = GMemory->ReadUTF16(StringAddr, NameLen);
			if (Str.empty())
				return {};

			if (NameArray::DecryptUTF16Fn)
				NameArray::DecryptUTF16Fn(Str.data(), Str.length());

			return Str;
		}

		/*
		 * Cheap "does this look like a name" used by BlocksBit voting.
		 * The single 64-bit read is only sound when no decryptor is installed; with one
		 * set, the bytes must be decrypted first or every candidate scores zero and
		 * detection silently falls back to the default BlocksBit.
		 */
		bool LooksLikePrintableNameAt(uintptr_t StringAddr, int32 NameLen)
		{
			const int32 CheckLen = std::min(NameLen, static_cast<int32>(sizeof(uint64)));

			if (NameArray::DecryptUTF8Fn)
			{
				const std::string Str = ReadDecryptedName(StringAddr, CheckLen);
				return static_cast<int32>(Str.size()) == CheckLen && IsPrintableAscii(Str);
			}

			uint64 NameWord = 0;
			if (!SafeRead(StringAddr, NameWord))
				return false;

			for (int32 i = 0; i < CheckLen; i++)
			{
				const uint8 Ch = static_cast<uint8>(NameWord >> (i * 8));
				if (Ch < 0x20 || Ch > 0x7E)
					return false;
			}

			return true;
		}

		// Resolves the address of item Index for a given stride. Fixed and chunked arrays
		// differ only in this step, so every validator below is shared between them.
		using FItemAddrFn = std::function<uintptr_t(int32 Index, int32 ItemSize)>;

		uintptr_t GetFixedItemAddr(uintptr_t ItemsBase, int32 ItemSize, int32 Index)
		{
			return ItemsBase + static_cast<uintptr_t>(Index) * static_cast<uintptr_t>(ItemSize);
		}

		uintptr_t GetChunkedItemAddr(uintptr_t ChunksBase, int32 ElementsPerChunk, int32 ItemSize, int32 Index)
		{
			if (ElementsPerChunk <= 0)
				return 0;

			const int32 ChunkIndex   = Index / ElementsPerChunk;
			const int32 IndexInChunk = Index % ElementsPerChunk;

			uintptr_t ChunkAddr = 0;
			if (!SafeRead(ChunksBase + static_cast<uintptr_t>(ChunkIndex) * kPtrSize, ChunkAddr) || !IsReadable(ChunkAddr))
				return 0;

			return ChunkAddr + static_cast<uintptr_t>(IndexInChunk) * static_cast<uintptr_t>(ItemSize);
		}

		/*
		 * Confirms a fully specified item layout by requiring every sampled slot to hold a
		 * real UObject that reports its own slot index. A wrong stride misaligns every
		 * sample, so this rejects almost everything that is not the genuine layout.
		 *
		 * Deliberately does not probe the slot past NumObjects: the game can populate that
		 * slot between the count read and the probe, which made the previous implementation
		 * reject correct layouts at random.
		 */
		bool VerifyItemLayout(const FItemAddrFn& AddrFn, int32 MaxIndex, FItemLayout* InOut)
		{
			uintptr_t SampleObjects[kItemSampleCount] = {};
			int32 SampleIndices[kItemSampleCount]     = {};
			int32 SampleCount                         = 0;

			for (int32 i = 0; i < kItemSampleCount; i++)
			{
				const int32 Index = kItemSampleIndices[i];
				if (MaxIndex > 0 && Index >= MaxIndex)
					break;

				// A wrong stride misaligns from the very first sample, so running out of
				// objects late is a small array rather than a bad candidate.
				const uintptr_t ObjAddr = ReadObjectAt(AddrFn(Index, InOut->ItemSize), *InOut);
				if (!IsLikelyUObject(ObjAddr))
					break;

				SampleObjects[SampleCount] = ObjAddr;
				SampleIndices[SampleCount] = Index;
				SampleCount++;
			}

			if (SampleCount < kIndexConsistencyCount)
				return false;

			std::vector<uint8_t> ObjectBlocks[kItemSampleCount];
			for (int32 i = 0; i < SampleCount; i++)
			{
				if (!ReadBlock(SampleObjects[i], ObjectBlocks[i], static_cast<size_t>(kMaxIndexScan)))
					return false;
			}

			for (int32 Offset = 0; Offset + static_cast<int32>(sizeof(int32)) <= kMaxIndexScan; Offset += sizeof(int32))
			{
				bool bAllMatch = true;
				for (int32 i = 0; i < SampleCount; i++)
				{
					if (PeekInt32(ObjectBlocks[i], static_cast<size_t>(Offset)) != SampleIndices[i])
					{
						bAllMatch = false;
						break;
					}
				}

				if (bAllMatch)
				{
					InOut->IndexOffset = Offset;
					return true;
				}
			}

			return false;
		}

		/*
		 * Recovers the FUObjectItem shape from the item block alone. Every
		 * (ObjectOffset, ItemSize) pair derivable from qualifying slots is tried, so a
		 * single bad guess cannot sink the probe.
		 */
		bool DiscoverItemLayout(uintptr_t FirstItemBase, const FItemAddrFn& AddrFn, int32 MaxIndex, FItemLayout* Out)
		{
			std::vector<uint8_t> Block;
			if (!Out || !ReadBlock(FirstItemBase, Block, static_cast<size_t>(kItemReadSize)))
				return false;

			std::vector<int32> ObjectOffsets;
			for (int32 Offset = 0; Offset + kPtrSize <= kMaxObjectSlotScan; Offset += sizeof(int32))
			{
				if (IsLikelyUObject(PeekPtr(Block, static_cast<size_t>(Offset))))
					ObjectOffsets.push_back(Offset);
			}

			if (ObjectOffsets.empty())
				return false;

			for (int32 ObjectOffset : ObjectOffsets)
			{
				for (int32 ItemSize = kPtrSize; ItemSize <= kMaxItemStride; ItemSize += sizeof(int32))
				{
					// Item 1's object pointer must sit exactly one stride along.
					const int32 NextSlot = ObjectOffset + ItemSize;
					if (NextSlot + kPtrSize > kItemReadSize)
						break;

					if (!IsLikelyUObject(PeekPtr(Block, static_cast<size_t>(NextSlot))))
						continue;

					FItemLayout Candidate;
					Candidate.ObjectOffset = ObjectOffset;
					Candidate.ItemSize     = ItemSize;

					if (VerifyItemLayout(AddrFn, MaxIndex, &Candidate))
					{
						*Out = Candidate;
						return true;
					}
				}
			}

			return false;
		}

		// Fraction of sampled slots that hold a UObject reporting its own index. Live
		// objects are destroyed over time, so a real array is dense but never perfect.
		double ScoreObjectSamples(const FItemAddrFn& AddrFn, const FItemLayout& Item, int32 Num, int32 MaxSamples, int32* OutTested, int32* OutValid)
		{
			const int32 Step = std::max(1, Num / std::max(1, MaxSamples));

			int32 Tested = 0;
			int32 Valid  = 0;

			for (int32 Index = 0; Index < Num && Tested < MaxSamples; Index += Step)
			{
				Tested++;

				const uintptr_t ObjAddr = ReadObjectAt(AddrFn(Index, Item.ItemSize), Item);
				if (!IsLikelyUObject(ObjAddr))
					continue;

				int32 Reported = 0;
				if (SafeRead(ObjAddr + Item.IndexOffset, Reported) && Reported == Index)
					Valid++;
			}

			if (OutTested)
				*OutTested = Tested;
			if (OutValid)
				*OutValid = Valid;

			return Tested > 0 ? static_cast<double>(Valid) / static_cast<double>(Tested) : 0.0;
		}

		/*
		 * Density below a candidate count proves nothing on its own: every count smaller than
		 * the real one is also fully populated, which is why an unfiltered scan reports many
		 * different offsets at 100%. The real count is the point where population stops, so
		 * this measures what lies past it.
		 *
		 * Probing starts kCountSlack entries beyond the candidate. The array only ever grows,
		 * so that margin removes the read-count-then-probe race that made the previous
		 * boundary check reject correct layouts at random.
		 */
		double MeasurePastCountPopulation(const FItemAddrFn& AddrFn, const FItemLayout& Item, int32 Num)
		{
			int32 Tested    = 0;
			int32 Populated = 0;

			for (int32 i = 0; i < kPastProbeCount; i++)
			{
				const int64 Index = static_cast<int64>(Num) + kCountSlack + static_cast<int64>(i) * kPastProbeStep;
				if (Index > kMaxObjectCount)
					break;

				Tested++;

				const uintptr_t ObjAddr = ReadObjectAt(AddrFn(static_cast<int32>(Index), Item.ItemSize), Item);
				if (!IsLikelyUObject(ObjAddr))
					continue;

				// Only a genuinely live slot reports the index it occupies; unmapped chunk
				// tables and stale pointers can satisfy the vtable check but never this.
				int32 Reported = 0;
				if (SafeRead(ObjAddr + Item.IndexOffset, Reported) && Reported == static_cast<int32>(Index))
					Populated++;
			}

			return Tested > 0 ? static_cast<double>(Populated) / static_cast<double>(Tested) : 0.0;
		}

		/*
		 * Preferred source: the first object of chunk 1 reports index ElementsPerChunk by
		 * definition, which reads the stride straight out of live data.
		 *
		 * With a single allocated chunk that observation is impossible, so the stride is
		 * derived from the capacity pair instead - MaxElements == MaxChunks * ElementsPerChunk
		 * - by looking for two header int32s that divide to a plausible power-of-two chunk
		 * size. Only if that also fails is the conventional default used.
		 */
		int32 DiscoverElementsPerChunk(uintptr_t ChunksBase, int32 ChunkCount, const FItemLayout& Item, const std::vector<uint8_t>& Header, int32 ObjectsOffset)
		{
			if (ChunkCount >= 2)
			{
				uintptr_t SecondChunk = 0;
				if (SafeRead(ChunksBase + kPtrSize, SecondChunk) && IsReadable(SecondChunk))
				{
					const uintptr_t FirstObject = ReadObjectAt(SecondChunk, Item);

					int32 Observed = 0;
					if (IsLikelyUObject(FirstObject) && SafeRead(FirstObject + Item.IndexOffset, Observed) &&
					    Observed >= kMinElementsPerChunk && Observed <= kMaxElementsPerChunk)
						return Observed;
				}
			}

			auto Overlaps = [ObjectsOffset](size_t Offset) -> bool
			{
				const int32 Value = static_cast<int32>(Offset);
				return Value >= ObjectsOffset && Value < ObjectsOffset + kPtrSize;
			};

			for (size_t CapOffset = 0; CapOffset + sizeof(int32) <= Header.size(); CapOffset += sizeof(int32))
			{
				if (Overlaps(CapOffset))
					continue;

				const int32 MaxElements = PeekInt32(Header, CapOffset);
				if (MaxElements < kMinElementsPerChunk || MaxElements > kMaxObjectCapacity)
					continue;

				for (size_t ChunkOffset = 0; ChunkOffset + sizeof(int32) <= Header.size(); ChunkOffset += sizeof(int32))
				{
					if (ChunkOffset == CapOffset || Overlaps(ChunkOffset))
						continue;

					const int32 MaxChunks = PeekInt32(Header, ChunkOffset);
					if (MaxChunks <= 0 || MaxChunks > kMaxChunkScan * kMaxChunkScan)
						continue;

					if (MaxElements % MaxChunks != 0)
						continue;

					const int32 Derived = MaxElements / MaxChunks;
					if (Derived < kMinElementsPerChunk || Derived > kMaxElementsPerChunk)
						continue;

					// Engine chunk sizes are powers of two; requiring it rejects the many
					// coincidental divisions a header full of counters would otherwise offer.
					if ((Derived & (Derived - 1)) != 0)
						continue;

					return Derived;
				}
			}

			return kDefaultElementsPerChunk;
		}

		// DecryptChunkFn is only ever supplied by the names call site: chunk pointers are
		// never encrypted for GObjects, and passing nullptr here (the objects call site)
		// keeps this identical to a plain readability probe.
		int32 CountReadableChunkPointers(uintptr_t ChunksBase, const std::function<void(uintptr_t&)>& DecryptChunkFn = nullptr)
		{
			int32 Count = 0;
			for (int32 i = 0; i < kMaxChunkScan; i++)
			{
				uintptr_t ChunkAddr = 0;
				if (!SafeRead(ChunksBase + static_cast<uintptr_t>(i) * kPtrSize, ChunkAddr))
					break;

				if (DecryptChunkFn)
					DecryptChunkFn(ChunkAddr);

				if (!IsReadable(ChunkAddr))
					break;

				Count++;
			}
			return Count;
		}

		// Records why a candidate was discarded. Detection walks a large cross product, so
		// the list is capped; without this every rejection is a silent `continue` and a
		// failed run reports nothing but "no candidate passed validation".
		void Reject(std::vector<std::string>& Failures, std::string Reason)
		{
			if (static_cast<int32>(Failures.size()) < kMaxRejectReasons)
				Failures.push_back(std::move(Reason));
			else if (static_cast<int32>(Failures.size()) == kMaxRejectReasons)
				Failures.push_back("  Reject: ... further rejections suppressed");
		}

		// ============================================================================
		//  Objects: hypotheses and scoring
		// ============================================================================

		struct FObjectsCandidate
		{
			std::unique_ptr<IObjectsLayout> Layout;
			double Confidence   = 0.0;
			EObjectsType Type   = EObjectsType::Array;
			int32 SamplesTested = 0;
			int32 SamplesValid  = 0;
			/// Offset-level, for Debug.
			std::string Summary;
			/// Plain language, for Info.
			std::string Description;
		};

		// The required relationships already hold by construction wherever this is reached,
		// so the optional capacity fields are a small bonus rather than a large penalty.
		double CombineObjectsScore(double SampleRatio, bool bHasOptionalCounts)
		{
			double Score = kWeightStructural + (kWeightSampleRatio + kWeightCrossValidate) * SampleRatio;

			if (bHasOptionalCounts)
				Score += kBonusKnownNames;

			return std::min(Score, 1.0);
		}

		/*
		 * Fixed hypothesis: an Objects pointer leading to contiguous FUObjectItem storage,
		 * plus an int32 pair satisfying 0 < NumObjects <= MaxObjects. Field order is never
		 * assumed - every readable pointer offset is crossed with every plausible int32.
		 */
		// Returns true when an item stride held across samples, meaning the candidate is
		// worth dumping even though it was ultimately rejected.
		bool EnumerateFixedCandidates(uintptr_t Root, const std::vector<uint8_t>& Header, const FOptions& Options, std::vector<FObjectsCandidate>& Out, std::vector<std::string>& Failures)
		{
			bool bFoundItems                       = false;
			const std::vector<int32> PtrCandidates = CollectPointerCandidates(Header);
			const std::vector<int32> IntCandidates = CollectInt32Candidates(Header, kMinObjectCount, kMaxObjectCount);

			if (PtrCandidates.empty() || IntCandidates.empty())
			{
				Failures.push_back(fmt::format("EnumerateFixedCandidates: No candidates at root 0x{:X} ({} pointer, {} int32)", Root, PtrCandidates.size(), IntCandidates.size()));
				return false;
			}

			for (int32 ObjectsOffset : PtrCandidates)
			{
				const uintptr_t ItemsBase = PeekPtr(Header, static_cast<size_t>(ObjectsOffset));

				FItemAddrFn AddrFn = [ItemsBase](int32 Index, int32 ItemSize) -> uintptr_t
				{
					return GetFixedItemAddr(ItemsBase, ItemSize, Index);
				};

				FItemLayout Item;
				if (!DiscoverItemLayout(ItemsBase, AddrFn, 0, &Item))
				{
					Reject(Failures, fmt::format("  EnumerateFixedCandidates: Objects@+0x{:X} -> 0x{:X} - no item stride held across samples", ObjectsOffset, ItemsBase));
					continue;
				}

				bFoundItems = true;

				for (int32 NumOffset : IntCandidates)
				{
					// The count cannot overlap the pointer it describes.
					if (NumOffset >= ObjectsOffset && NumOffset < ObjectsOffset + kPtrSize)
						continue;

					const int32 Num = PeekInt32(Header, static_cast<size_t>(NumOffset));
					if (Num < kMinObjectCount || Num > kMaxObjectCount)
						continue;

					int32 Tested             = 0;
					int32 Valid              = 0;
					const double SampleRatio = ScoreObjectSamples(AddrFn, Item, Num, Options.MaxSamples, &Tested, &Valid);
					if (SampleRatio < Options.MinimumConfidence)
					{
						Reject(Failures, fmt::format("  EnumerateFixedCandidates: Objects@+0x{:X} Num@+0x{:X}={} - only {}/{} samples valid", ObjectsOffset, NumOffset, Num, Valid, Tested));
						continue;
					}

					const double PastRatio = MeasurePastCountPopulation(AddrFn, Item, Num);

					auto Layout                 = std::make_unique<FFixedUObjectArrayLayout>();
					Layout->Objects             = ObjectsOffset;
					Layout->NumObjects          = NumOffset;
					Layout->FUObjectItem.Object = Item.ObjectOffset;
					Layout->FUObjectItem.Size   = Item.ItemSize;

					// MaxObjects is optional evidence. It is searched over the raw header rather
					// than the live-count candidates, whose ceiling excludes real capacities.
					for (size_t MaxOffset = 0; MaxOffset + sizeof(int32) <= Header.size(); MaxOffset += sizeof(int32))
					{
						if (static_cast<int32>(MaxOffset) == NumOffset)
							continue;
						if (static_cast<int32>(MaxOffset) >= ObjectsOffset && static_cast<int32>(MaxOffset) < ObjectsOffset + kPtrSize)
							continue;

						const int32 Value = PeekInt32(Header, MaxOffset);
						if (Value >= Num && Value <= kMaxObjectCapacity)
						{
							Layout->MaxObjects = static_cast<int32>(MaxOffset);
							break;
						}
					}

					FObjectsCandidate Candidate;
					Candidate.Type          = EObjectsType::Array;
					Candidate.Confidence    = CombineObjectsScore(SampleRatio, Layout->MaxObjects != -1) * (1.0 - kPastCountPenalty * PastRatio);
					Candidate.SamplesTested = Tested;
					Candidate.SamplesValid  = Valid;
					Candidate.Summary       = fmt::format("EnumerateFixedCandidates: Objects@+0x{:X} Num@+0x{:X}={} stride=0x{:X} objOff=0x{:X} items {}/{} tail {:.0f}%",
					                                      ObjectsOffset,
					                                      NumOffset,
					                                      Num,
					                                      Item.ItemSize,
					                                      Item.ObjectOffset,
					                                      Valid,
					                                      Tested,
					                                      PastRatio * 100.0);
					Candidate.Description   = fmt::format("EnumerateFixedCandidates: Fixed array candidate - {} objects, {} of {} sampled slots held a valid object",
					                                      Num,
					                                      Valid,
					                                      Tested);
					Candidate.Layout        = std::move(Layout);
					Out.push_back(std::move(Candidate));
				}
			}

			return bFoundItems;
		}

		/*
		 * Chunked hypothesis: an Objects pointer leading to a run of chunk pointers.
		 * ElementsPerChunk is discovered from chunk 1's first object, never assumed.
		 */
		/// @copydoc EnumerateFixedCandidates
		bool EnumerateChunkedCandidates(uintptr_t Root, const std::vector<uint8_t>& Header, const FOptions& Options, std::vector<FObjectsCandidate>& Out, std::vector<std::string>& Failures)
		{
			bool bFoundItems                       = false;
			const std::vector<int32> PtrCandidates = CollectPointerCandidates(Header);
			const std::vector<int32> IntCandidates = CollectInt32Candidates(Header, kMinObjectCount, kMaxObjectCount);

			if (PtrCandidates.empty() || IntCandidates.empty())
			{
				Failures.push_back(fmt::format("EnumerateChunkedCandidates: No candidates at root 0x{:X}", Root));
				return false;
			}

			for (int32 ObjectsOffset : PtrCandidates)
			{
				const uintptr_t ChunksBase = PeekPtr(Header, static_cast<size_t>(ObjectsOffset));

				const int32 ChunkCount = CountReadableChunkPointers(ChunksBase);
				if (ChunkCount < kMinChunkPtrRun)
				{
					Reject(Failures, fmt::format("  EnumerateChunkedCandidates: Objects@+0x{:X} -> 0x{:X} - only {} readable chunk pointers", ObjectsOffset, ChunksBase, ChunkCount));
					continue;
				}

				uintptr_t FirstChunk = 0;
				if (!SafeRead(ChunksBase, FirstChunk) || !IsReadable(FirstChunk))
					continue;

				// Stride is recovered from chunk 0, where indices are still chunk-local.
				FItemAddrFn ProbeFn = [FirstChunk](int32 Index, int32 ItemSize) -> uintptr_t
				{
					return GetFixedItemAddr(FirstChunk, ItemSize, Index);
				};

				FItemLayout Item;
				if (!DiscoverItemLayout(FirstChunk, ProbeFn, 0, &Item))
				{
					Reject(Failures, fmt::format("  EnumerateChunkedCandidates: Objects@+0x{:X} chunk0 0x{:X} - no item stride held across samples", ObjectsOffset, FirstChunk));
					continue;
				}

				bFoundItems = true;

				const int32 ElementsPerChunk = DiscoverElementsPerChunk(ChunksBase, ChunkCount, Item, Header, ObjectsOffset);
				if (ElementsPerChunk < kMinElementsPerChunk || ElementsPerChunk > kMaxElementsPerChunk)
				{
					Reject(Failures, fmt::format("  EnumerateChunkedCandidates: Objects@+0x{:X} - ElementsPerChunk resolved to 0x{:X}, outside [0x{:X}, 0x{:X}]", ObjectsOffset, ElementsPerChunk, kMinElementsPerChunk, kMaxElementsPerChunk));
					continue;
				}

				FItemAddrFn AddrFn = [ChunksBase, ElementsPerChunk](int32 Index, int32 ItemSize) -> uintptr_t
				{
					return GetChunkedItemAddr(ChunksBase, ElementsPerChunk, ItemSize, Index);
				};

				for (int32 NumOffset : IntCandidates)
				{
					if (NumOffset >= ObjectsOffset && NumOffset < ObjectsOffset + kPtrSize)
						continue;

					const int32 Num = PeekInt32(Header, static_cast<size_t>(NumOffset));
					if (Num < kMinObjectCount || Num > kMaxObjectCount)
						continue;

					// The observed chunk count must be consistent with the claimed element count.
					const int32 RequiredChunks = ((Num - 1) / ElementsPerChunk) + 1;
					if (RequiredChunks > ChunkCount)
					{
						Reject(Failures, fmt::format("  EnumerateChunkedCandidates: Objects@+0x{:X} Num@+0x{:X}={} - needs {} chunks but only {} are readable", ObjectsOffset, NumOffset, Num, RequiredChunks, ChunkCount));
						continue;
					}

					// A single-chunk fallback is only usable if it spans the whole live range.
					if (ChunkCount < 2 && Num > ElementsPerChunk)
						continue;

					int32 Tested             = 0;
					int32 Valid              = 0;
					const double SampleRatio = ScoreObjectSamples(AddrFn, Item, Num, Options.MaxSamples, &Tested, &Valid);
					if (SampleRatio < Options.MinimumConfidence)
					{
						Reject(Failures, fmt::format("  EnumerateChunkedCandidates: Objects@+0x{:X} Num@+0x{:X}={} - only {}/{} samples valid", ObjectsOffset, NumOffset, Num, Valid, Tested));
						continue;
					}

					const double PastRatio = MeasurePastCountPopulation(AddrFn, Item, Num);

					auto Layout                 = std::make_unique<FChunkedUObjectArrayLayout>();
					Layout->Objects             = ObjectsOffset;
					Layout->NumElements         = NumOffset;
					Layout->ElementsPerChunk    = ElementsPerChunk;
					Layout->FUObjectItem.Object = Item.ObjectOffset;
					Layout->FUObjectItem.Size   = Item.ItemSize;

					// MaxElements / NumChunks / MaxChunks are optional supporting evidence.
					// MaxElements is MaxChunks * ElementsPerChunk, so it routinely exceeds the
					// live-count ceiling and must be searched over the raw header.
					for (size_t Offset = 0; Offset + sizeof(int32) <= Header.size(); Offset += sizeof(int32))
					{
						if (static_cast<int32>(Offset) == NumOffset)
							continue;
						if (static_cast<int32>(Offset) >= ObjectsOffset && static_cast<int32>(Offset) < ObjectsOffset + kPtrSize)
							continue;

						const int32 Value = PeekInt32(Header, Offset);

						if (Layout->MaxElements == -1 && Value >= Num && Value <= kMaxObjectCapacity && Value % ElementsPerChunk == 0)
							Layout->MaxElements = static_cast<int32>(Offset);
						else if (Layout->NumChunks == -1 && Value == RequiredChunks)
							Layout->NumChunks = static_cast<int32>(Offset);
						else if (Layout->MaxChunks == -1 && Value > RequiredChunks && Value <= kMaxChunkScan * kMaxChunkScan)
							Layout->MaxChunks = static_cast<int32>(Offset);
					}

					FObjectsCandidate Candidate;
					Candidate.Type          = EObjectsType::Chunked;
					Candidate.Confidence    = CombineObjectsScore(SampleRatio, Layout->MaxElements != -1) * (1.0 - kPastCountPenalty * PastRatio);
					Candidate.SamplesTested = Tested;
					Candidate.SamplesValid  = Valid;
					Candidate.Summary       = fmt::format("EnumerateChunkedCandidates: Objects@+0x{:X} Num@+0x{:X}={} EPC=0x{:X} stride=0x{:X} objOff=0x{:X} items {}/{} tail {:.0f}%",
					                                      ObjectsOffset,
					                                      NumOffset,
					                                      Num,
					                                      ElementsPerChunk,
					                                      Item.ItemSize,
					                                      Item.ObjectOffset,
					                                      Valid,
					                                      Tested,
					                                      PastRatio * 100.0);
					Candidate.Description   = fmt::format("EnumerateChunkedCandidates: Chunked array candidate - {} objects in chunks of {}, {} of {} sampled slots held a valid object",
					                                      Num,
					                                      ElementsPerChunk,
					                                      Valid,
					                                      Tested);
					Candidate.Layout        = std::move(Layout);
					Out.push_back(std::move(Candidate));
				}
			}

			return bFoundItems;
		}

		/*
		 * Every returned offset is relative to the address the caller supplied, so the
		 * candidate is the only usable root.
		 *
		 * An outer FUObjectArray embeds ObjObjects by value, so "the candidate is the outer
		 * struct" needs no separate root: it is already covered by finding Objects at a
		 * larger offset within the header scan. Sweeping Candidate+N as extra roots instead
		 * produced duplicate layouts rebased onto a root the caller never learns about,
		 * which Generator then applied to GObjects.
		 *
		 * The pointer-to-array case is likewise the caller's to resolve - Generator already
		 * retries detection against the dereferenced candidate.
		 */
		std::vector<uintptr_t> BuildObjectRoots(uintptr_t CandidateAddress)
		{
			std::vector<uintptr_t> Roots;

			if (IsReadable(CandidateAddress))
				Roots.push_back(CandidateAddress);

			return Roots;
		}

		// Locates the "None" literal that always occupies name entry 0. The raw compare is
		// tried first because it is far cheaper; the decrypted path covers obfuscated games.
		bool FindNoneOffset(uintptr_t BaseAddr, int32* OutOffset)
		{
			if (!OutOffset)
				return false;

			std::vector<uint8_t> Block;
			if (!ReadBlock(BaseAddr, Block, static_cast<size_t>(kNoneSearchBytes)))
				return false;

			for (int32 Offset = 0; Offset + kNoneStrLen <= kNoneSearchBytes; Offset += 2)
			{
				if (PeekUInt32(Block, static_cast<size_t>(Offset)) == kNoneAsUInt32)
				{
					*OutOffset = Offset;
					return true;
				}
			}

			if (NameArray::DecryptUTF8Fn)
			{
				for (int32 Offset = 0; Offset + kNoneStrLen <= kNoneSearchBytes; Offset += 2)
				{
					if (ReadDecryptedName(BaseAddr + Offset, kNoneStrLen) == "None")
					{
						*OutOffset = Offset;
						return true;
					}
				}
			}

			return false;
		}

		bool StartsWithNoneEntry(uintptr_t Address)
		{
			int32 Unused = -1;
			return IsReadable(Address) && FindNoneOffset(Address, &Unused);
		}

		/*
		 * Walks packed entries from the start of a block and returns how many decoded
		 * coherently. Entry 0 must be "None"; nothing is assumed about any later name.
		 */
		int32 WalkPoolEntries(uintptr_t BlockStart, const FNamePoolLayout& Layout, std::vector<std::string>* OutNames)
		{
			if (Layout.FNameEntry.Stride <= 0)
				return 0;

			// EntryAddr is rebuilt from BlockStart and a plain byte offset every iteration,
			// rather than carried forward and incremented in place, so a decrypted entry
			// address (potentially not a simple linear transform of the raw one) is never
			// fed back in as if it were raw on the next step.
			uintptr_t RunningOffset = 0;
			int32 Decoded           = 0;

			for (int32 i = 0; i < kNameWalkCount; i++)
			{
				uintptr_t EntryAddr = BlockStart + RunningOffset;
				if (NameArray::DecryptNameEntryFn)
					NameArray::DecryptNameEntryFn(EntryAddr);

				uint16 Header = 0;
				if (!SafeRead(EntryAddr + Layout.FNameEntry.Header, Header))
					break;

				const int32 NameLen = Header >> Layout.FNameEntry.LengthShiftCount;
				const bool bIsWide  = (Header & Layout.FNameEntry.NameWideMask) != 0;

				if (NameLen < kMinNameLen || NameLen > kMaxNameLen)
					break;

				if (bIsWide)
				{
					// Wide entries carry no ASCII signal, so they are stepped over unscored.
					if (i == 0)
						return 0;

					if (ReadDecryptedNameWide(EntryAddr + Layout.FNameEntry.String, NameLen).empty())
						break;
				}
				else
				{
					const std::string Name = ReadDecryptedName(EntryAddr + Layout.FNameEntry.String, NameLen);
					if (static_cast<int32>(Name.size()) != NameLen || !IsPlausibleNameText(Name))
						break;

					if (i == 0 && Name != "None")
						return 0;

					if (OutNames)
						OutNames->push_back(Name);
				}

				Decoded++;

				const int32 EntryBytes  = Layout.FNameEntry.String + NameLen * (bIsWide ? 2 : 1);
				const int32 Stride      = Layout.FNameEntry.Stride;
				RunningOffset          += static_cast<uintptr_t>((EntryBytes + Stride - 1) / Stride) * static_cast<uintptr_t>(Stride);
			}

			return Decoded;
		}

		/*
		 * Recovers the pool entry header shape without depending on name ordering.
		 *
		 * Entry 0 is "None", whose length is exactly 4. That single universal fact pins the
		 * header: Header >> LengthShiftCount must equal 4. The surviving
		 * (Header, Shift, Stride) triples are then disambiguated by walking consecutive
		 * entries and keeping whichever walks furthest.
		 *
		 * The previous implementation instead required the literal "Byte" immediately after
		 * "None", which fails on any game that reorders or obfuscates names past index 0.
		 */
		bool DiscoverPoolEntryLayout(uintptr_t BlockStart, FNamePoolLayout* Out, int32* OutWalkScore)
		{
			if (!Out)
				return false;

			// BlockStart is chunk-decrypted only (by the caller); entry 0 of the block gets
			// its own, separate entry decryption pass here, exactly as GetNameEntryByIndex
			// decrypts a chunk-relative offset 0 as an entry in its own right. BlockStart
			// itself is left untouched so it can still be passed to WalkPoolEntries below,
			// which redoes this same per-entry decryption internally.
			uintptr_t Entry0 = BlockStart;
			if (NameArray::DecryptNameEntryFn)
				NameArray::DecryptNameEntryFn(Entry0);

			int32 StringOffset = -1;
			if (!FindNoneOffset(Entry0, &StringOffset))
				return false;

			const int32 HeaderCandidates[] = {0, StringOffset - 2, StringOffset - 4};

			FNamePoolLayout Best;
			int32 BestWalk = 0;
			bool bFound    = false;

			for (int32 HeaderOffset : HeaderCandidates)
			{
				if (HeaderOffset < 0 || HeaderOffset + static_cast<int32>(sizeof(uint16)) > StringOffset)
					continue;

				uint16 RawHeader = 0;
				if (!SafeRead(Entry0 + HeaderOffset, RawHeader))
					continue;

				for (int32 Shift = 0; Shift < kMaxLengthShiftCount; Shift++)
				{
					if ((RawHeader >> Shift) != kNoneStrLen)
						continue;

					for (int32 Stride : kPoolStrides)
					{
						FNamePoolLayout Candidate             = *Out;
						Candidate.FNameEntry.Header           = HeaderOffset;
						Candidate.FNameEntry.String           = StringOffset;
						Candidate.FNameEntry.Stride           = Stride;
						Candidate.FNameEntry.LengthShiftCount = Shift;
						Candidate.FNameEntry.NameWideMask     = kDefaultNameWideMask;

						const int32 Walk = WalkPoolEntries(BlockStart, Candidate, nullptr);
						if (Walk > BestWalk)
						{
							BestWalk = Walk;
							Best     = Candidate;
							bFound   = true;
						}
					}
				}
			}

			if (!bFound)
				return false;

			*Out = Best;
			if (OutWalkScore)
				*OutWalkScore = BestWalk;

			return true;
		}

		/*
		 * Two independent methods, tried in order.
		 *
		 * 1. Contiguity: a contiguous pool allocates blocks exactly (1 << BlocksBit) apart,
		 *    so consecutive block pointers differ by that amount.
		 * 2. Object voting: decode live UObject FName fields as (BlockIdx << B) | ByteOffset
		 *    for each candidate B and score the values that land on a readable, plausibly
		 *    named entry.
		 *
		 * Voting is deliberately not gated on block count. The previous implementation
		 * nested it inside the contiguity check's `BlockCount >= 3` guard, making it
		 * unreachable for small pools - exactly the case where contiguity has too few
		 * pairs to be conclusive.
		 */
		int32 GetBlocksBit(uintptr_t BlocksStart, int32 BlockCount, const FNamePoolLayout& Layout)
		{
			if (BlockCount >= kBlocksBitMinPairs)
			{
				const int32 MaxChunkIndex = BlockCount - 1;
				const int32 PairCount     = std::min(MaxChunkIndex, kBlocksBitMinPairs);

				// A block holds (1 << BlocksBit) entries of Stride bytes each, so consecutive
				// block pointers are Stride << BlocksBit apart. Matching the delta against
				// 1 << Bit instead reports Bit + log2(Stride) - two blocks 0x80000 apart with a
				// 2-byte stride are BlocksBit 18, not 19.
				const uintptr_t Stride = static_cast<uintptr_t>(std::max(1, Layout.FNameEntry.Stride));

				for (int32 Bit = kBlocksBitMin; Bit <= kBlocksBitMax; Bit++)
				{
					const uintptr_t Expected = Stride << Bit;

					int32 Matches = 0;
					for (int32 i = 0; i < PairCount; i++)
					{
						uintptr_t Current = 0;
						uintptr_t Next    = 0;
						if (!SafeRead(BlocksStart + static_cast<uintptr_t>(i) * kPtrSize, Current))
							break;
						if (!SafeRead(BlocksStart + static_cast<uintptr_t>(i + 1) * kPtrSize, Next))
							break;

						// Spacing is measured on decrypted addresses: an encrypted transform
						// need not preserve the difference between two raw pointers.
						if (NameArray::DecryptNameChunkFn)
						{
							NameArray::DecryptNameChunkFn(Current);
							NameArray::DecryptNameChunkFn(Next);
						}

						const uintptr_t Diff = Next >= Current ? Next - Current : Current - Next;
						if (Diff == Expected)
							Matches++;
					}

					if (PairCount > 0 && Matches == PairCount)
					{
						GLogger.FmtWrite(ELogLevel::Info, "GetBlocksBit: FNamePool BlocksBit ({}) resolved by block contiguity ({} pairs).\n", Bit, Matches);
						return Bit;
					}
				}
			}

			const int32 NumObjects = ObjectArray::Num();
			if (NumObjects > 0 && BlockCount > 1)
			{
				const int32 MaxChunkIndex       = BlockCount - 1;
				int32 Scores[kBlocksBitMax + 1] = {};

				const int32 Samples[] = {
				    0, NumObjects / 8, NumObjects / 4, 3 * NumObjects / 8, NumObjects / 2, 5 * NumObjects / 8, 3 * NumObjects / 4, 7 * NumObjects / 8, NumObjects - 1, 50, 100, 200, 500, 1000, 5000, 10000, 50000, 100000};

				for (int32 SampleIdx : Samples)
				{
					if (SampleIdx < 0 || SampleIdx >= NumObjects)
						continue;

					const uintptr_t ObjAddr = reinterpret_cast<uintptr_t>(ObjectArray::GetByIndex(SampleIdx).GetAddress());
					if (!ObjAddr)
						continue;

					for (int32 Offset = 0; Offset <= kFNameFieldScan; Offset += sizeof(int32))
					{
						int32 Value = 0;
						if (!SafeRead(ObjAddr + Offset, Value) || Value <= 0)
							continue;

						if (static_cast<int64>(Value) >= static_cast<int64>(BlockCount) << kBlocksBitMax)
							continue;

						for (int32 Bit = kBlocksBitMin; Bit <= kBlocksBitMax; Bit++)
						{
							const int32 BlockIdx = Value >> Bit;
							const int32 ByteOff  = (Value & ((1 << Bit) - 1)) * Layout.FNameEntry.Stride;

							// Block 0 decodes identically for every Bit, so it carries no signal.
							if (BlockIdx == 0 || BlockIdx > MaxChunkIndex)
								continue;

							uintptr_t BlockPtr = 0;
							if (!SafeRead(BlocksStart + static_cast<uintptr_t>(BlockIdx) * kPtrSize, BlockPtr))
								continue;

							if (NameArray::DecryptNameChunkFn)
								NameArray::DecryptNameChunkFn(BlockPtr);

							if (!IsReadable(BlockPtr))
								continue;

							uintptr_t EntryAddr = BlockPtr + static_cast<uintptr_t>(ByteOff);
							if (NameArray::DecryptNameEntryFn)
								NameArray::DecryptNameEntryFn(EntryAddr);

							uint16 Header = 0;
							if (!SafeRead(EntryAddr + Layout.FNameEntry.Header, Header))
								continue;

							const int32 NameLen = Header >> Layout.FNameEntry.LengthShiftCount;
							if (NameLen < 3 || NameLen > kMaxNameLen)
								continue;

							if (LooksLikePrintableNameAt(EntryAddr + Layout.FNameEntry.String, NameLen))
								Scores[Bit]++;
						}
					}
				}

				int32 BestBit   = -1;
				int32 BestScore = 0;
				for (int32 Bit = kBlocksBitMin; Bit <= kBlocksBitMax; Bit++)
				{
					if (Scores[Bit] > BestScore)
					{
						BestBit   = Bit;
						BestScore = Scores[Bit];
					}
				}

				if (BestBit != -1 && BestScore >= kBlocksBitMinVotes)
				{
					GLogger.FmtWrite(ELogLevel::Info, "GetBlocksBit: FNamePool BlocksBit ({}) resolved by object FName voting ({} votes).\n", BestBit, BestScore);
					return BestBit;
				}
			}

			GLogger.FmtWrite(ELogLevel::Warning, "GetBlocksBit: FNamePool BlocksBit could not be resolved, using default (0x{:X}).\n", kBlocksBitDefault);
			return kBlocksBitDefault;
		}

		// ============================================================================
		//  Names: hypotheses and scoring
		// ============================================================================

		struct FNamesCandidate
		{
			std::unique_ptr<INamesLayout> Layout;
			double Confidence   = 0.0;
			ENamesType Type     = ENamesType::Array;
			int32 SamplesTested = 0;
			int32 SamplesValid  = 0;
			/// Offset-level, for Debug.
			std::string Summary;
			/// Plain language, for Info.
			std::string Description;
		};

		/*
		 * Decodes sampled UObject FName fields through a names candidate and reports the
		 * fraction that yield a plausible name. Returns a negative value when objects are
		 * not yet available, so names detection stays independently callable.
		 */
		double CrossValidateWithObjects(const std::function<std::string(int32)>& DecodeName)
		{
			const int32 NumObjects = ObjectArray::Num();
			if (NumObjects <= 0)
				return -1.0;

			const int32 Samples[] = {1, 5, 17, 43, 91, 200, 500, 1200, 3000, 7000};

			int32 Tested = 0;
			int32 Valid  = 0;

			for (int32 SampleIdx : Samples)
			{
				if (SampleIdx >= NumObjects)
					continue;

				const uintptr_t ObjAddr = reinterpret_cast<uintptr_t>(ObjectArray::GetByIndex(SampleIdx).GetAddress());
				if (!ObjAddr)
					continue;

				// The UObject FName offset is not known yet, so every plausible slot is tried
				// and the object counts as validated if any of them decodes to a real name.
				bool bAnyValid = false;
				for (int32 Offset = 0; Offset <= kFNameFieldScan && !bAnyValid; Offset += sizeof(int32))
				{
					int32 NameIndex = 0;
					if (!SafeRead(ObjAddr + Offset, NameIndex) || NameIndex < 0 || NameIndex > kMaxNameElements)
						continue;

					bAnyValid = IsPlausibleNameText(DecodeName(NameIndex));
				}

				Tested++;
				if (bAnyValid)
					Valid++;
			}

			return Tested > 0 ? static_cast<double>(Valid) / static_cast<double>(Tested) : -1.0;
		}

		double CombineNamesScore(double WalkRatio, int32 KnownHits, double CrossRatio, bool bNoneAtZero)
		{
			// A negative CrossRatio means objects were not available. Awarding full marks for
			// a check that never ran let weak candidates score near-perfect, so the weight is
			// redistributed onto the evidence that was actually gathered instead.
			const bool bCrossRan    = CrossRatio >= 0.0;
			const double WalkWeight = bCrossRan ? kWeightSampleRatio : kWeightSampleRatio + kWeightCrossValidate;

			double Score = kWeightStructural + WalkWeight * WalkRatio;

			if (bCrossRan)
				Score += kWeightCrossValidate * CrossRatio;

			if (bNoneAtZero)
				Score += kBonusNoneAtZero;

			if (KnownHits > 0)
				Score += std::min(kBonusKnownNames * KnownHits, kBonusKnownNames * 4.0);

			return std::min(Score, 1.0);
		}

		// Returns true when a structural anchor was found (a block starting with "None"),
		// meaning the candidate is worth dumping even though it was ultimately rejected.
		bool EnumeratePoolCandidates(uintptr_t Root, const std::vector<uint8_t>& Header, const FOptions& Options, std::vector<FNamesCandidate>& Out, std::vector<std::string>& Failures)
		{
			bool bAnyBlocks = false;

			for (const FPointerRun& Run : CollectPointerRuns(Header))
			{
				uintptr_t FirstBlock = PeekPtr(Header, static_cast<size_t>(Run.Offset));
				if (NameArray::DecryptNameChunkFn)
					NameArray::DecryptNameChunkFn(FirstBlock);

				// Entry 0 of the block gets its own, separate entry decryption for this
				// probe; DiscoverPoolEntryLayout/WalkPoolEntries redo it internally when
				// given the chunk-only-decrypted FirstBlock below.
				uintptr_t FirstBlockEntry0 = FirstBlock;
				if (NameArray::DecryptNameEntryFn)
					NameArray::DecryptNameEntryFn(FirstBlockEntry0);

				if (!StartsWithNoneEntry(FirstBlockEntry0))
					continue;

				bAnyBlocks = true;

				FNamePoolLayout Layout;
				Layout.Blocks = Run.Offset;

				int32 WalkScore = 0;
				if (!DiscoverPoolEntryLayout(FirstBlock, &Layout, &WalkScore))
				{
					Failures.push_back(fmt::format("EnumeratePoolCandidates: Blocks@+0x{:X} block0 0x{:X} - no (Header, Shift, Stride) triple decoded \"None\"", Run.Offset, FirstBlock));
					continue;
				}

				int32 AllocatedBlocks = 0;
				for (int32 i = 0; i < kMaxPoolBlocks; i++)
				{
					uintptr_t BlockPtr = 0;
					if (!SafeRead(Root + Layout.Blocks + static_cast<uintptr_t>(i) * kPtrSize, BlockPtr))
						break;

					if (NameArray::DecryptNameChunkFn)
						NameArray::DecryptNameChunkFn(BlockPtr);

					if (!IsReadable(BlockPtr))
						break;

					AllocatedBlocks++;
				}

				const int32 BlockCount = std::max(std::max(1, Run.Count), AllocatedBlocks);
				Layout.BlocksBit       = GetBlocksBit(Root + Run.Offset, BlockCount, Layout);

				/*
				 * MaxChunkIndex and ByteCursor are optional, and both are counted from the real
				 * block array rather than the header window: Blocks commonly starts near 0xC8,
				 * so the window truncates the array to a few entries and any count derived from
				 * it is far below the live block index.
				 */
				const int64 BlockSizeBytes = static_cast<int64>(std::max(1, Layout.FNameEntry.Stride)) << Layout.BlocksBit;

				auto TryOptionalPair = [&](int32 IndexOffset) -> bool
				{
					const int32 CursorOffset = IndexOffset + static_cast<int32>(sizeof(int32));
					if (IndexOffset < 0)
						return false;

					int32 CurrentBlock = 0;
					int32 ByteCursor   = 0;
					if (!SafeRead(Root + IndexOffset, CurrentBlock) || !SafeRead(Root + CursorOffset, ByteCursor))
						return false;

					if (CurrentBlock != AllocatedBlocks - 1)
						return false;

					// The cursor is a byte position inside the current block.
					if (ByteCursor < 0 || ByteCursor > BlockSizeBytes || ByteCursor > kMaxByteCursor)
						return false;

					Layout.MaxChunkIndex = IndexOffset;
					Layout.ByteCursor    = CursorOffset;
					return true;
				};

				// FNameEntryAllocator declares CurrentBlock and CurrentByteCursor immediately
				// before the block array, so that pair is tried first.
				if (AllocatedBlocks > 0 && !TryOptionalPair(Layout.Blocks - 2 * static_cast<int32>(sizeof(int32))))
				{
					for (int32 Offset = 0; Offset + 2 * static_cast<int32>(sizeof(int32)) <= Layout.Blocks; Offset += sizeof(int32))
					{
						if (TryOptionalPair(Offset))
							break;
					}
				}

				std::vector<std::string> Names;
				const int32 Decoded    = WalkPoolEntries(FirstBlock, Layout, &Names);
				const double WalkRatio = static_cast<double>(Decoded) / static_cast<double>(kNameWalkCount);
				const bool bNoneAtZero = !Names.empty() && Names[0] == "None";
				const int32 KnownHits  = CountKnownNameHits(Names);

				if (!bNoneAtZero)
				{
					Failures.push_back(fmt::format("EnumeratePoolCandidates: Blocks@+0x{:X} - entry 0 did not decode to \"None\"", Run.Offset));
					continue;
				}

				double CrossRatio = -1.0;
				if (Options.bEnableCrossValidation)
				{
					const FNamePoolLayout Snapshot = Layout;
					const uintptr_t BlocksBase     = Root + Layout.Blocks;

					CrossRatio = CrossValidateWithObjects([&Snapshot, BlocksBase](int32 Index) -> std::string
					{
						const int32 ChunkIdx = Index >> Snapshot.BlocksBit;
						const int32 ByteOff  = (Index & ((1 << Snapshot.BlocksBit) - 1)) * Snapshot.FNameEntry.Stride;

						uintptr_t ChunkAddr = 0;
						if (!SafeRead(BlocksBase + static_cast<uintptr_t>(ChunkIdx) * kPtrSize, ChunkAddr))
							return {};

						if (NameArray::DecryptNameChunkFn)
							NameArray::DecryptNameChunkFn(ChunkAddr);

						if (!IsReadable(ChunkAddr))
							return {};

						uintptr_t EntryAddr = ChunkAddr + static_cast<uintptr_t>(ByteOff);
						if (NameArray::DecryptNameEntryFn)
							NameArray::DecryptNameEntryFn(EntryAddr);

						uint16 EntryHeader = 0;
						if (!SafeRead(EntryAddr + Snapshot.FNameEntry.Header, EntryHeader))
							return {};

						const int32 NameLen = EntryHeader >> Snapshot.FNameEntry.LengthShiftCount;
						if ((EntryHeader & Snapshot.FNameEntry.NameWideMask) != 0)
							return {};

						return ReadDecryptedName(EntryAddr + Snapshot.FNameEntry.String, NameLen);
					});
				}

				FNamesCandidate Candidate;
				Candidate.Type          = ENamesType::Pool;
				Candidate.Confidence    = CombineNamesScore(WalkRatio, KnownHits, CrossRatio, bNoneAtZero);
				Candidate.SamplesTested = kNameWalkCount;
				Candidate.SamplesValid  = Decoded;
				Candidate.Summary       = fmt::format("EnumeratePoolCandidates: Blocks@+0x{:X} blocksBit=0x{:X} hdr=0x{:X} str=0x{:X} stride={} shift={} entries {}/{} known={}",
				                                      Layout.Blocks,
				                                      Layout.BlocksBit,
				                                      Layout.FNameEntry.Header,
				                                      Layout.FNameEntry.String,
				                                      Layout.FNameEntry.Stride,
				                                      Layout.FNameEntry.LengthShiftCount,
				                                      Decoded,
				                                      kNameWalkCount,
				                                      KnownHits);
				Candidate.Description   = fmt::format("EnumeratePoolCandidates: Name pool candidate - decoded {} of {} sample names, {} matched known engine names",
				                                      Decoded,
				                                      kNameWalkCount,
				                                      KnownHits);
				Candidate.Layout        = std::make_unique<FNamePoolLayout>(Layout);
				Out.push_back(std::move(Candidate));
			}

			if (!bAnyBlocks)
				Failures.push_back(fmt::format("EnumeratePoolCandidates: No pointer run at root 0x{:X} led to a block starting with \"None\"", Root));

			return bAnyBlocks;
		}

		/*
		 * Legacy indirect array: Chunks -> chunk pointers -> FNameEntry pointers.
		 * The entry index field stores (Index << 1) | WideBit, which pins it exactly.
		 */
		bool DiscoverArrayEntryLayout(uintptr_t FirstChunk, FNameArrayLayout* Out)
		{
			if (!Out)
				return false;

			uintptr_t FirstEntry = 0;
			if (!SafeRead(FirstChunk, FirstEntry))
				return false;

			if (NameArray::DecryptNameEntryFn)
				NameArray::DecryptNameEntryFn(FirstEntry);

			if (!IsReadable(FirstEntry))
				return false;

			int32 StringOffset = -1;
			if (!FindNoneOffset(FirstEntry, &StringOffset))
				return false;

			Out->FNameEntry.String       = StringOffset;
			Out->FNameEntry.NameWideMask = kDefaultNameWideMask;

			const int32 MaxIndexScan = std::min(StringOffset + static_cast<int32>(sizeof(int32)), kNoneSearchBytes);
			for (int32 Offset = 0; Offset + static_cast<int32>(sizeof(uint32)) <= MaxIndexScan; Offset += sizeof(int32))
			{
				int32 Confirmed = 0;
				bool bMismatch  = false;

				for (int32 i = 0; i < kNameIndexSampleCount && !bMismatch; i++)
				{
					const int32 SampleIdx = kNameIndexSampleIdx[i];

					uintptr_t EntryAddr = 0;
					if (!SafeRead(FirstChunk + static_cast<uintptr_t>(SampleIdx) * kPtrSize, EntryAddr))
						continue; // An empty slot says nothing about this offset.

					if (NameArray::DecryptNameEntryFn)
						NameArray::DecryptNameEntryFn(EntryAddr);

					if (!IsReadable(EntryAddr))
						continue;

					uint32 IndexField = 0;
					if (!SafeRead(EntryAddr + Offset, IndexField))
						continue;

					// A live entry that disagrees does rule the offset out.
					if (static_cast<int32>(IndexField >> 1) != SampleIdx)
						bMismatch = true;
					else
						Confirmed++;
				}

				if (!bMismatch && Confirmed >= kMinIndexSampleHits)
				{
					Out->FNameEntry.Index = Offset;
					return true;
				}
			}

			return false;
		}

		int32 DiscoverNamesPerChunk(uintptr_t ChunksBase, int32 ChunkCount, const FNameArrayLayout& Layout)
		{
			if (ChunkCount < 2)
				return kDefaultNamesPerChunk;

			uintptr_t SecondChunk = 0;
			if (!SafeRead(ChunksBase + kPtrSize, SecondChunk))
				return 0;

			if (NameArray::DecryptNameChunkFn)
				NameArray::DecryptNameChunkFn(SecondChunk);

			if (!IsReadable(SecondChunk))
				return 0;

			uintptr_t FirstEntry = 0;
			if (!SafeRead(SecondChunk, FirstEntry))
				return 0;

			if (NameArray::DecryptNameEntryFn)
				NameArray::DecryptNameEntryFn(FirstEntry);

			if (!IsReadable(FirstEntry))
				return 0;

			uint32 IndexField = 0;
			if (!SafeRead(FirstEntry + Layout.FNameEntry.Index, IndexField))
				return 0;

			const int32 ElementsPerChunk = static_cast<int32>(IndexField >> 1);
			if (ElementsPerChunk < kMinNamesPerChunk || ElementsPerChunk > kMaxNamesPerChunk)
				return 0;

			return ElementsPerChunk;
		}

		int32 WalkArrayEntries(uintptr_t FirstChunk, const FNameArrayLayout& Layout, std::vector<std::string>* OutNames, std::string* OutStopReason)
		{
			int32 Decoded          = 0;
			int32 ConsecutiveHoles = 0;

			for (int32 i = 0; i < kMaxArrayWalkSlots && Decoded < kNameSampleCount; i++)
			{
				const uintptr_t SlotAddr = FirstChunk + static_cast<uintptr_t>(i) * kPtrSize;

				uintptr_t EntryAddr = 0;
				if (!SafeRead(SlotAddr, EntryAddr))
				{
					if (OutStopReason)
						*OutStopReason = fmt::format("WalkArrayEntries: Slot [{}] at 0x{:X} is not readable", i, SlotAddr);
					break;
				}

				if (NameArray::DecryptNameEntryFn)
					NameArray::DecryptNameEntryFn(EntryAddr);

				/*
				 * A null slot is a hole, not the end. The engine registers hardcoded EName
				 * values at fixed indices and that enum has gaps, so the low range of a name
				 * array is legitimately sparse. Only a long unbroken run of nulls means the
				 * populated range is genuinely over.
				 */
				if (!IsReadable(EntryAddr))
				{
					if (++ConsecutiveHoles >= kMaxConsecutiveHoles)
					{
						if (OutStopReason)
							*OutStopReason = fmt::format("WalkArrayEntries: {} consecutive empty slots ending at [{}]", ConsecutiveHoles, i);
						break;
					}

					continue;
				}

				uint32 IndexField = 0;
				if (!SafeRead(EntryAddr + Layout.FNameEntry.Index, IndexField))
				{
					if (OutStopReason)
						*OutStopReason = fmt::format("WalkArrayEntries: Entry [{}] at 0x{:X} has no readable index field at +0x{:X}", i, EntryAddr, Layout.FNameEntry.Index);
					break;
				}

				ConsecutiveHoles = 0;

				if (static_cast<int32>(IndexField >> 1) != i)
				{
					if (OutStopReason)
						*OutStopReason = fmt::format("WalkArrayEntries: Entry [{}] at 0x{:X} reports index {} instead", i, EntryAddr, IndexField >> 1);
					return 0;
				}

				// The index field agreeing with the slot is the structural proof, so the entry
				// counts here. Whether its text is readable is bonus evidence gathered below.
				Decoded++;

				if ((IndexField & Layout.FNameEntry.NameWideMask) != 0)
					continue;

				const std::string Name = ReadDecryptedName(EntryAddr + Layout.FNameEntry.String, kMaxNameLen);
				if (!IsPlausibleNameText(Name))
				{
					// Entry 0 is the one name whose value is known, so it must decode.
					if (i == 0)
						return 0;

					continue;
				}

				if (i == 0 && Name != "None")
					return 0;

				if (OutNames)
					OutNames->push_back(Name);
			}

			return Decoded;
		}

		/*
		 * The chunk pointer array occupies the head of the struct, so NumElements routinely
		 * sits past the normal header window and every value there looks like a pointer.
		 * A candidate is accepted only when it behaves like the count: the entry just below
		 * it resolves and entries past it do not. The margin keeps the probe race-free.
		 */
		int32 DiscoverArrayNumElements(uintptr_t Root, uintptr_t ChunksBase, const FNameArrayLayout& Layout, int32 AllocatedChunkCount)
		{
			if (Layout.ElementsPerChunk <= 0)
				return -1;

			std::vector<uint8_t> Wide;
			if (ReadBlockBestEffort(Root, Wide, static_cast<size_t>(kNamesHeaderScanSize)) == 0)
				return -1;

			auto EntryAt = [ChunksBase, &Layout](int32 Index) -> uintptr_t
			{
				const int32 ChunkIdx = Index / Layout.ElementsPerChunk;
				const int32 InChunk  = Index % Layout.ElementsPerChunk;

				uintptr_t ChunkAddr = 0;
				if (!SafeRead(ChunksBase + static_cast<uintptr_t>(ChunkIdx) * kPtrSize, ChunkAddr))
					return 0;

				if (NameArray::DecryptNameChunkFn)
					NameArray::DecryptNameChunkFn(ChunkAddr);

				if (!IsReadable(ChunkAddr))
					return 0;

				uintptr_t EntryAddr = 0;
				if (!SafeRead(ChunkAddr + static_cast<uintptr_t>(InChunk) * kPtrSize, EntryAddr))
					return 0;

				if (NameArray::DecryptNameEntryFn)
					NameArray::DecryptNameEntryFn(EntryAddr);

				return EntryAddr;
			};

			// The last live entry must both resolve and report its own index. Requiring the
			// stored index keeps this specific across a window wide enough to clear the table.
			auto IsLiveEntry = [&EntryAt, &Layout](int32 Index) -> bool
			{
				const uintptr_t EntryAddr = EntryAt(Index);
				if (!IsReadable(EntryAddr))
					return false;

				uint32 IndexField = 0;
				return SafeRead(EntryAddr + Layout.FNameEntry.Index, IndexField) && static_cast<int32>(IndexField >> 1) == Index;
			};

			// The already-allocated chunk pointers occupy
			// [Layout.Chunks, Layout.Chunks + AllocatedChunkCount * sizeof(uintptr_t)) and are
			// known to hold pointers, not a counter. Reinterpreting their raw bytes as an int32
			// (e.g. a pointer's high 32 bits) can coincidentally look exactly like a plausible,
			// small element count, so that range is excluded from the scan.
			const size_t ChunksFieldBegin = static_cast<size_t>(Layout.Chunks);
			const size_t ChunksFieldEnd   = ChunksFieldBegin + static_cast<size_t>(std::max(1, AllocatedChunkCount)) * sizeof(uintptr_t);

			for (size_t Offset = 0; Offset + sizeof(int32) <= Wide.size(); Offset += sizeof(int32))
			{
				if (Offset + sizeof(int32) > ChunksFieldBegin && Offset < ChunksFieldEnd)
					continue;

				const int32 Value = PeekInt32(Wide, Offset);
				if (Value < kMinNameCount || Value > kMaxNameElements)
					continue;

				// The final slots can be holes, so the last live entry is looked for just below
				// the count rather than demanded exactly at it.
				bool bFoundLast = false;
				for (int32 Back = 1; Back <= kMaxConsecutiveHoles && !bFoundLast; Back++)
				{
					if (Value - Back < 0)
						break;

					bFoundLast = IsLiveEntry(Value - Back);
				}

				if (!bFoundLast)
					continue;

				bool bTailEmpty = true;
				for (int32 i = 0; i < kNameTailProbe && bTailEmpty; i++)
				{
					const int64 PastIndex = static_cast<int64>(Value) + kNameTailMargin + i;
					if (PastIndex <= kMaxNameElements && IsLiveEntry(static_cast<int32>(PastIndex)))
						bTailEmpty = false;
				}

				if (bTailEmpty)
					return static_cast<int32>(Offset);
			}

			return -1;
		}

		/// @copydoc EnumeratePoolCandidates
		bool EnumerateArrayCandidates(uintptr_t Root, const std::vector<uint8_t>& Header, const FOptions& Options, std::vector<FNamesCandidate>& Out, std::vector<std::string>& Failures)
		{
			bool bAnyChunks = false;

			for (const FPointerRun& Run : CollectPointerRuns(Header))
			{
				const uintptr_t ChunksBase = Root + Run.Offset;

				uintptr_t FirstChunk = 0;
				if (!SafeRead(ChunksBase, FirstChunk))
					continue;

				if (NameArray::DecryptNameChunkFn)
					NameArray::DecryptNameChunkFn(FirstChunk);

				if (!IsReadable(FirstChunk))
					continue;

				uintptr_t FirstEntry = 0;
				if (!SafeRead(FirstChunk, FirstEntry))
					continue;

				if (NameArray::DecryptNameEntryFn)
					NameArray::DecryptNameEntryFn(FirstEntry);

				if (!StartsWithNoneEntry(FirstEntry))
					continue;

				bAnyChunks = true;

				FNameArrayLayout Layout;
				Layout.Chunks = Run.Offset;

				if (!DiscoverArrayEntryLayout(FirstChunk, &Layout))
				{
					Failures.push_back(fmt::format("EnumerateArrayCandidates: Chunks@+0x{:X} - no index field reported (Index << 1) consistently", Run.Offset));
					continue;
				}

				const int32 ChunkCount       = CountReadableChunkPointers(ChunksBase, NameArray::DecryptNameChunkFn);
				const int32 ElementsPerChunk = DiscoverNamesPerChunk(ChunksBase, ChunkCount, Layout);
				if (ElementsPerChunk <= 0)
				{
					Failures.push_back(fmt::format("EnumerateArrayCandidates: Chunks@+0x{:X} - ElementsPerChunk not resolvable", Run.Offset));
					continue;
				}
				Layout.ElementsPerChunk = ElementsPerChunk;

				Layout.NumElements = DiscoverArrayNumElements(Root, ChunksBase, Layout, ChunkCount);
				if (Layout.NumElements == -1)
				{
					Failures.push_back(fmt::format("EnumerateArrayCandidates: Chunks@+0x{:X} EPC=0x{:X} chunks={} idx=0x{:X} str=0x{:X} - no int32 within 0x{:X} bytes behaved like NumElements",
					                               Run.Offset,
					                               ElementsPerChunk,
					                               ChunkCount,
					                               Layout.FNameEntry.Index,
					                               Layout.FNameEntry.String,
					                               kNamesHeaderScanSize));
					continue;
				}

				std::vector<std::string> Names;
				const int32 Decoded    = WalkArrayEntries(FirstChunk, Layout, &Names, nullptr);
				const double WalkRatio = static_cast<double>(Decoded) / static_cast<double>(kNameSampleCount);
				const bool bNoneAtZero = !Names.empty() && Names[0] == "None";
				const int32 KnownHits  = CountKnownNameHits(Names);

				if (!bNoneAtZero)
				{
					Failures.push_back(fmt::format("EnumerateArrayCandidates: Chunks@+0x{:X} - entry 0 did not decode to \"None\"", Run.Offset));
					continue;
				}

				double CrossRatio = -1.0;
				if (Options.bEnableCrossValidation)
				{
					const FNameArrayLayout Snapshot = Layout;
					CrossRatio                      = CrossValidateWithObjects([&Snapshot, ChunksBase](int32 Index) -> std::string
					{
						const int32 ChunkIdx = Index / Snapshot.ElementsPerChunk;
						const int32 InChunk  = Index % Snapshot.ElementsPerChunk;

						uintptr_t ChunkAddr = 0;
						if (!SafeRead(ChunksBase + static_cast<uintptr_t>(ChunkIdx) * kPtrSize, ChunkAddr))
							return {};

						if (NameArray::DecryptNameChunkFn)
							NameArray::DecryptNameChunkFn(ChunkAddr);

						if (!IsReadable(ChunkAddr))
							return {};

						uintptr_t EntryAddr = 0;
						if (!SafeRead(ChunkAddr + static_cast<uintptr_t>(InChunk) * kPtrSize, EntryAddr))
							return {};

						if (NameArray::DecryptNameEntryFn)
							NameArray::DecryptNameEntryFn(EntryAddr);

						if (!IsReadable(EntryAddr))
							return {};

						return ReadDecryptedName(EntryAddr + Snapshot.FNameEntry.String, kMaxNameLen);
					});
				}

				FNamesCandidate Candidate;
				Candidate.Type          = ENamesType::Array;
				Candidate.Confidence    = CombineNamesScore(WalkRatio, KnownHits, CrossRatio, bNoneAtZero);
				Candidate.SamplesTested = kNameSampleCount;
				Candidate.SamplesValid  = Decoded;
				Candidate.Summary       = fmt::format("EnumerateArrayCandidates: Chunks@+0x{:X} EPC=0x{:X} Num@+0x{:X} idx=0x{:X} str=0x{:X} entries {}/{} known={}",
				                                      Layout.Chunks,
				                                      Layout.ElementsPerChunk,
				                                      Layout.NumElements,
				                                      Layout.FNameEntry.Index,
				                                      Layout.FNameEntry.String,
				                                      Decoded,
				                                      kNameSampleCount,
				                                      KnownHits);
				Candidate.Description   = fmt::format("EnumerateArrayCandidates: Name array candidate - decoded {} of {} sample names, {} matched known engine names",
				                                      Decoded,
				                                      kNameSampleCount,
				                                      KnownHits);
				Candidate.Layout        = std::make_unique<FNameArrayLayout>(Layout);
				Out.push_back(std::move(Candidate));
			}

			if (!bAnyChunks)
				Failures.push_back(fmt::format("EnumerateArrayCandidates: No pointer run at root 0x{:X} led to an entry starting with \"None\"", Root));

			return bAnyChunks;
		}

	} // namespace

	FObjectsDetectionResult DetectObjectsLayout(uintptr_t CandidateAddress, const FOptions& Options)
	{
		FObjectsDetectionResult Result;

		if (!IsReadable(CandidateAddress))
		{
			Result.Failures.push_back(fmt::format("DetectObjectsLayout: Candidate 0x{:X} is not readable", CandidateAddress));
			return Result;
		}

		Result.Details.push_back(fmt::format("DetectObjectsLayout: Candidate 0x{:X}", CandidateAddress));
		Result.Evidence.push_back(fmt::format("DetectObjectsLayout: Examining 0x{:X} for an object array.", CandidateAddress));

		const bool bRequireFixed   = Options.HintMode == EHintMode::Require && Options.ObjectsTypeHint == EObjectsType::Array;
		const bool bRequireChunked = Options.HintMode == EHintMode::Require && Options.ObjectsTypeHint == EObjectsType::Chunked;

		std::vector<FObjectsCandidate> Candidates;
		bool bFoundAnchor = false;

		for (uintptr_t Root : BuildObjectRoots(CandidateAddress))
		{
			std::vector<uint8_t> Header;
			if (!ReadBlock(Root, Header, static_cast<size_t>(kHeaderReadSize)))
				continue;

			if (!bRequireFixed)
				bFoundAnchor |= EnumerateChunkedCandidates(Root, Header, Options, Candidates, Result.Failures);

			if (!bRequireChunked)
				bFoundAnchor |= EnumerateFixedCandidates(Root, Header, Options, Candidates, Result.Failures);
		}

		if (Candidates.empty())
		{
			Result.Failures.push_back("DetectObjectsLayout: No candidate passed validation");

			// Only dump when an item stride actually held; the analyzer probes many
			// unrelated addresses and dumping each one buries the failure that matters.
			if (bFoundAnchor)
				DumpCandidateMemory(CandidateAddress, "GObjects");

			return Result;
		}

		// A Prefer hint nudges its type but cannot outvote materially stronger evidence.
		FObjectsCandidate* Best = nullptr;
		for (FObjectsCandidate& Candidate : Candidates)
		{
			double Effective = Candidate.Confidence;
			if (Options.HintMode == EHintMode::Prefer && Options.ObjectsTypeHint == Candidate.Type)
				Effective += kHintTieBreakBonus;

			if (!Best)
			{
				Best = &Candidate;
				continue;
			}

			double BestEffective = Best->Confidence;
			if (Options.HintMode == EHintMode::Prefer && Options.ObjectsTypeHint == Best->Type)
				BestEffective += kHintTieBreakBonus;

			if (Effective > BestEffective)
				Best = &Candidate;
		}

		for (const FObjectsCandidate& Candidate : Candidates)
		{
			Result.Details.push_back(fmt::format("DetectObjectsLayout: {} confidence {:.1f}%", Candidate.Summary, Candidate.Confidence * 100.0));
			Result.Evidence.push_back(fmt::format("DetectObjectsLayout: {} ({:.1f}% confidence)", Candidate.Description, Candidate.Confidence * 100.0));
		}

		if (Best->Confidence < Options.MinimumConfidence)
		{
			Result.Failures.push_back(fmt::format("DetectObjectsLayout: Best candidate scored {:.1f}%, below the {:.1f}% threshold.",
			                                      Best->Confidence * 100.0,
			                                      Options.MinimumConfidence * 100.0));
			return Result;
		}

		if (!Best->Layout || !Best->Layout->IsValid())
		{
			Result.Failures.push_back("DetectObjectsLayout: Best candidate is missing required offsets");
			return Result;
		}

		Result.bSuccess   = true;
		Result.Confidence = Best->Confidence;
		Result.Layout     = std::move(Best->Layout);
		Result.Details.push_back(fmt::format("  DetectObjectsLayout: Selected: {}, confidence {:.1f}%",
		                                     Best->Type == EObjectsType::Chunked ? "FChunkedUObjectArray" : "FFixedUObjectArray",
		                                     Best->Confidence * 100.0));
		Result.Evidence.push_back(fmt::format("DetectObjectsLayout: Selected the {} object array layout ({:.1f}% confidenc)",
		                                      Best->Type == EObjectsType::Chunked ? "FChunkedUObjectArray" : "FFixedUObjectArray",
		                                      Best->Confidence * 100.0));

		return Result;
	}

	FNamesDetectionResult DetectNamesLayout(uintptr_t CandidateAddress, const FOptions& Options)
	{
		FNamesDetectionResult Result;

		if (!IsReadable(CandidateAddress))
		{
			Result.Failures.push_back(fmt::format("DetectNamesLayout: Candidate 0x{:X} is not readable", CandidateAddress));
			return Result;
		}

		Result.Details.push_back(fmt::format("DetectNamesLayout: Candidate 0x{:X}", CandidateAddress));
		Result.Evidence.push_back(fmt::format("DetectNamesLayout: Examining 0x{:X} for a name table.", CandidateAddress));

		const bool bRequireArray = Options.HintMode == EHintMode::Require && Options.NamesTypeHint == ENamesType::Array;
		const bool bRequirePool  = Options.HintMode == EHintMode::Require && Options.NamesTypeHint == ENamesType::Pool;

		// Offsets are returned relative to the supplied address, so the candidate is the
		// only usable root; Generator already retries detection on the dereferenced form.
		std::vector<uintptr_t> Roots;
		Roots.push_back(CandidateAddress);

		std::vector<FNamesCandidate> Candidates;
		bool bFoundAnchor = false;

		for (uintptr_t Root : Roots)
		{
			std::vector<uint8_t> Header;
			if (!ReadBlock(Root, Header, static_cast<size_t>(kHeaderReadSize)))
				continue;

			if (!bRequireArray)
				bFoundAnchor |= EnumeratePoolCandidates(Root, Header, Options, Candidates, Result.Failures);

			if (!bRequirePool)
				bFoundAnchor |= EnumerateArrayCandidates(Root, Header, Options, Candidates, Result.Failures);
		}

		if (Candidates.empty())
		{
			Result.Failures.push_back("DetectNamesLayout: No candidate passed validation");

			// Only dump when the candidate actually looked like a names structure. The
			// analyzer probes many unrelated addresses and dumping each one buries the
			// failure that matters.
			if (bFoundAnchor)
				DumpCandidateMemory(CandidateAddress, "GNames");

			return Result;
		}

		FNamesCandidate* Best = nullptr;
		for (FNamesCandidate& Candidate : Candidates)
		{
			double Effective = Candidate.Confidence;
			if (Options.HintMode == EHintMode::Prefer && Options.NamesTypeHint == Candidate.Type)
				Effective += kHintTieBreakBonus;

			if (!Best)
			{
				Best = &Candidate;
				continue;
			}

			double BestEffective = Best->Confidence;
			if (Options.HintMode == EHintMode::Prefer && Options.NamesTypeHint == Best->Type)
				BestEffective += kHintTieBreakBonus;

			if (Effective > BestEffective)
				Best = &Candidate;
		}

		for (const FNamesCandidate& Candidate : Candidates)
		{
			Result.Details.push_back(fmt::format("DetectNamesLayout: {} confidence {:.1f}%", Candidate.Summary, Candidate.Confidence * 100.0));
			Result.Evidence.push_back(fmt::format("DetectNamesLayout: {} ({:.1f}% confidence)", Candidate.Description, Candidate.Confidence * 100.0));
		}

		if (Best->Confidence < Options.MinimumConfidence)
		{
			Result.Failures.push_back(fmt::format("DetectNamesLayout: Best candidate scored {:.1f}%, below the {:.1f}% threshold",
			                                      Best->Confidence * 100.0,
			                                      Options.MinimumConfidence * 100.0));
			return Result;
		}

		if (!Best->Layout || !Best->Layout->IsValid())
		{
			Result.Failures.push_back("DetectNamesLayout: Best candidate is missing required offsets");
			return Result;
		}

		Result.bSuccess   = true;
		Result.Confidence = Best->Confidence;
		Result.Layout     = std::move(Best->Layout);
		Result.Details.push_back(fmt::format("  DetectNamesLayout: Selected: {}, confidence {:.1f}%",
		                                     Best->Type == ENamesType::Pool ? "FNamePool" : "TNameArray",
		                                     Best->Confidence * 100.0));
		Result.Evidence.push_back(fmt::format("DetectNamesLayout: Selected the {} layout ({:.1f}% confidence)",
		                                      Best->Type == ENamesType::Pool ? "FNamePool" : "TNameArray",
		                                      Best->Confidence * 100.0));

		return Result;
	}

	FObjectsTestResult TestObjectsLayout(uintptr_t CandidateAddress, const IObjectsLayout* Layout, const FOptions& Options)
	{
		FObjectsTestResult Result;

		if (!Layout || !Layout->IsValid())
		{
			Result.Failures.push_back("TestObjectsLayout: Layout is null or structurally incomplete");
			return Result;
		}

		if (!IsReadable(CandidateAddress))
		{
			Result.Failures.push_back(fmt::format("TestObjectsLayout: Address 0x{:X} not readable", CandidateAddress));
			return Result;
		}

		FItemLayout Item;
		FItemAddrFn AddrFn;
		int32 Num = 0;

		if (Layout->GetType() == EObjectsType::Array)
		{
			const FFixedUObjectArrayLayout& L = *static_cast<const FFixedUObjectArrayLayout*>(Layout);

			uintptr_t ItemsBase = 0;
			if (!SafeRead(CandidateAddress + L.Objects, ItemsBase) || !IsReadable(ItemsBase))
			{
				Result.Failures.push_back(fmt::format("TestObjectsLayout: Objects@+0x{:X} did not yield a readable item array", L.Objects));
				return Result;
			}

			if (!SafeRead(CandidateAddress + L.NumObjects, Num) || Num < kMinObjectCount || Num > kMaxObjectCount)
			{
				Result.Failures.push_back(fmt::format("TestObjectsLayout: NumObjects@+0x{:X} = {} is out of range [{}, {}]", L.NumObjects, Num, kMinObjectCount, kMaxObjectCount));
				return Result;
			}

			Item.ObjectOffset = L.FUObjectItem.Object;
			Item.ItemSize     = L.FUObjectItem.Size;
			AddrFn            = [ItemsBase](int32 Index, int32 ItemSize) -> uintptr_t
			{ return GetFixedItemAddr(ItemsBase, ItemSize, Index); };

			Result.Details.push_back(fmt::format("TestObjectsLayout: Fixed, Objects=0x{:X} Num={} stride=0x{:X}", L.Objects, Num, L.FUObjectItem.Size));
			Result.Evidence.push_back(fmt::format("TestObjectsLayout: Verifying the fixed object array layout at 0x{:X} ({} objects)", CandidateAddress, Num));
		}
		else
		{
			const FChunkedUObjectArrayLayout& L = *static_cast<const FChunkedUObjectArrayLayout*>(Layout);

			uintptr_t ChunksBase = 0;
			if (!SafeRead(CandidateAddress + L.Objects, ChunksBase) || !IsReadable(ChunksBase))
			{
				Result.Failures.push_back(fmt::format("TestObjectsLayout: Objects@+0x{:X} did not yield a readable chunk table", L.Objects));
				return Result;
			}

			if (!SafeRead(CandidateAddress + L.NumElements, Num) || Num < kMinObjectCount || Num > kMaxObjectCount)
			{
				Result.Failures.push_back(fmt::format("TestObjectsLayout: NumElements@+0x{:X} = {} is out of range [{}, {}]", L.NumElements, Num, kMinObjectCount, kMaxObjectCount));
				return Result;
			}

			if (L.ElementsPerChunk <= 0)
			{
				Result.Failures.push_back(fmt::format("TestObjectsLayout: ElementsPerChunk = {} is not positive", L.ElementsPerChunk));
				return Result;
			}

			const int32 ElementsPerChunk = L.ElementsPerChunk;
			Item.ObjectOffset            = L.FUObjectItem.Object;
			Item.ItemSize                = L.FUObjectItem.Size;
			AddrFn                       = [ChunksBase, ElementsPerChunk](int32 Index, int32 ItemSize) -> uintptr_t
			{ return GetChunkedItemAddr(ChunksBase, ElementsPerChunk, ItemSize, Index); };

			Result.Details.push_back(fmt::format("TestObjectsLayout: Chunked, Objects=0x{:X} Num={} EPC=0x{:X} stride=0x{:X}", L.Objects, Num, ElementsPerChunk, L.FUObjectItem.Size));
			Result.Evidence.push_back(fmt::format("TestObjectsLayout: Verifying the chunked object array layout at 0x{:X} ({} objects in chunks of {})", CandidateAddress, Num, ElementsPerChunk));
		}

		// The supplied item Object offset must resolve to real UObjects, and the supplied
		// stride must keep them aligned. The InternalIndex offset is derived rather than
		// supplied, so it is located here purely to make the per-sample check meaningful.
		if (!VerifyItemLayout(AddrFn, Num, &Item))
		{
			Result.Failures.push_back(fmt::format("TestObjectsLayout: Item stride 0x{:X} / object offset 0x{:X} did not hold across samples", Item.ItemSize, Item.ObjectOffset));
			return Result;
		}

		const int32 SampleBudget = std::min(Options.MaxSamples, kTestSampleCount);
		const double Ratio       = ScoreObjectSamples(AddrFn, Item, Num, SampleBudget, &Result.SamplesTested, &Result.SamplesValid);

		Result.Confidence = Ratio;
		Result.bValid     = Ratio >= kTestMinValidRatio;

		if (Result.bValid)
			Result.Evidence.push_back(fmt::format("TestObjectsLayout: {} of {} sampled slots held a valid object.", Result.SamplesValid, Result.SamplesTested));
		else
			Result.Failures.push_back(fmt::format("TestObjectsLayout: Only {}/{} samples validated ({:.1f}%, need {:.1f}%)",
			                                      Result.SamplesValid,
			                                      Result.SamplesTested,
			                                      Ratio * 100.0,
			                                      kTestMinValidRatio * 100.0));

		return Result;
	}

	FNamesTestResult TestNamesLayout(uintptr_t CandidateAddress, const INamesLayout* Layout, const FOptions& Options)
	{
		((void)Options);

		FNamesTestResult Result;

		if (!Layout || !Layout->IsValid())
		{
			Result.Failures.push_back("TestNamesLayout: Layout is null or structurally incomplete");
			return Result;
		}

		if (!IsReadable(CandidateAddress))
		{
			Result.Failures.push_back(fmt::format("TestNamesLayout: Address 0x{:X} not readable", CandidateAddress));
			return Result;
		}

		std::vector<std::string> Names;

		if (Layout->GetType() == ENamesType::Pool)
		{
			const FNamePoolLayout& L = *static_cast<const FNamePoolLayout*>(Layout);

			uintptr_t FirstBlock = 0;
			if (!SafeRead(CandidateAddress + L.Blocks, FirstBlock))
			{
				Result.Failures.push_back(fmt::format("TestNamesLayout: Blocks@+0x{:X} did not yield a readable block", L.Blocks));
				return Result;
			}

			if (NameArray::DecryptNameChunkFn)
				NameArray::DecryptNameChunkFn(FirstBlock);

			if (!IsReadable(FirstBlock))
			{
				Result.Failures.push_back(fmt::format("TestNamesLayout: Blocks@+0x{:X} did not yield a readable block", L.Blocks));
				return Result;
			}

			if (L.BlocksBit < kBlocksBitMin || L.BlocksBit > kBlocksBitMax)
				Result.Failures.push_back(fmt::format("TestNamesLayout: BlocksBit 0x{:X} is outside the plausible range [0x{:X}, 0x{:X}]", L.BlocksBit, kBlocksBitMin, kBlocksBitMax));

			if (L.MaxChunkIndex != -1)
			{
				int32 MaxChunkIndex = 0;
				if (!SafeRead(CandidateAddress + L.MaxChunkIndex, MaxChunkIndex) || MaxChunkIndex < 0)
					Result.Failures.push_back(fmt::format("TestNamesLayout: MaxChunkIndex@+0x{:X} = {} is negative", L.MaxChunkIndex, MaxChunkIndex));
			}

			if (L.ByteCursor != -1)
			{
				int32 ByteCursor = 0;
				if (!SafeRead(CandidateAddress + L.ByteCursor, ByteCursor) || ByteCursor < 0 || ByteCursor > kMaxByteCursor)
					Result.Failures.push_back(fmt::format("TestNamesLayout: ByteCursor@+0x{:X} = {} is out of range [0, 0x{:X}]", L.ByteCursor, ByteCursor, kMaxByteCursor));
			}

			Result.SamplesTested = kTestSampleCount;
			Result.SamplesValid  = std::min(WalkPoolEntries(FirstBlock, L, &Names), kTestSampleCount);

			Result.Evidence.push_back(fmt::format("TestNamesLayout: Verifying the name pool layout at 0x{:X}.", CandidateAddress));
			Result.Details.push_back(fmt::format("TestNamesLayout: Pool, Blocks=0x{:X} blocksBit=0x{:X} hdr=0x{:X} str=0x{:X} stride={} shift={}",
			                                     L.Blocks,
			                                     L.BlocksBit,
			                                     L.FNameEntry.Header,
			                                     L.FNameEntry.String,
			                                     L.FNameEntry.Stride,
			                                     L.FNameEntry.LengthShiftCount));
		}
		else
		{
			const FNameArrayLayout& L = *static_cast<const FNameArrayLayout*>(Layout);

			uintptr_t FirstChunk = 0;
			if (!SafeRead(CandidateAddress + L.Chunks, FirstChunk))
			{
				Result.Failures.push_back(fmt::format("TestNamesLayout: Chunks@+0x{:X} did not yield a readable chunk", L.Chunks));
				return Result;
			}

			if (NameArray::DecryptNameChunkFn)
				NameArray::DecryptNameChunkFn(FirstChunk);

			if (!IsReadable(FirstChunk))
			{
				Result.Failures.push_back(fmt::format("TestNamesLayout: Chunks@+0x{:X} did not yield a readable chunk", L.Chunks));
				return Result;
			}

			if (L.ElementsPerChunk <= 0)
			{
				Result.Failures.push_back(fmt::format("TestNamesLayout: ElementsPerChunk = {} is not positive", L.ElementsPerChunk));
				return Result;
			}

			std::string StopReason;
			const int32 Walked = WalkArrayEntries(FirstChunk, L, &Names, &StopReason);

			Result.SamplesTested = kTestSampleCount;
			Result.SamplesValid  = std::min(Walked, kTestSampleCount);

			Result.Evidence.push_back(fmt::format("TestNamesLayout: Verifying the name array layout at 0x{:X} ({} entries per chunk)", CandidateAddress, L.ElementsPerChunk));
			Result.Details.push_back(fmt::format("TestNamesLayout: Array, Chunks=0x{:X} EPC=0x{:X} idx=0x{:X} str=0x{:X} chunk0=0x{:X}",
			                                     L.Chunks,
			                                     L.ElementsPerChunk,
			                                     L.FNameEntry.Index,
			                                     L.FNameEntry.String,
			                                     FirstChunk));

			if (!StopReason.empty())
			{
				Result.Evidence.push_back(fmt::format("TestNamesLayout: Name walk stopped after {} entries - {}", Walked, StopReason));
				Result.Details.push_back(fmt::format("TestNamesLayout: Walk stopped after {} entries: {}", Walked, StopReason));

				// The slots around the stop, plus the chunk table itself, distinguish a
				// genuinely short chunk from a wrong ElementsPerChunk or a wrong base.
				const int32 First = std::max(0, Walked - 2);
				for (int32 i = First; i < First + 8; i++)
				{
					uintptr_t EntryAddr      = 0;
					const uintptr_t SlotAddr = FirstChunk + static_cast<uintptr_t>(i) * kPtrSize;
					SafeRead(SlotAddr, EntryAddr);
					if (NameArray::DecryptNameEntryFn)
						NameArray::DecryptNameEntryFn(EntryAddr);

					uint32 IndexField    = 0;
					const bool bHasIndex = SafeRead(EntryAddr + L.FNameEntry.Index, IndexField);

					Result.Details.push_back(fmt::format("TestNamesLayout:   Slot[{}] 0x{:X} -> 0x{:X}{}", i, SlotAddr, EntryAddr, bHasIndex ? fmt::format(" index={}", IndexField >> 1) : std::string(" <unreadable>")));
				}

				for (int32 i = 0; i < 6; i++)
				{
					uintptr_t ChunkAddr       = 0;
					const uintptr_t ChunkSlot = CandidateAddress + L.Chunks + static_cast<uintptr_t>(i) * kPtrSize;
					SafeRead(ChunkSlot, ChunkAddr);
					if (NameArray::DecryptNameChunkFn)
						NameArray::DecryptNameChunkFn(ChunkAddr);
					Result.Details.push_back(fmt::format("TestNamesLayout:   Chunk[{}] 0x{:X} -> 0x{:X}", i, ChunkSlot, ChunkAddr));
				}
			}
		}

		if (Names.empty() || Names[0] != "None")
		{
			Result.Failures.push_back("TestNamesLayout: Entry 0 did not decode to \"None\"");
			return Result;
		}

		const double Ratio = Result.SamplesTested > 0 ? static_cast<double>(Result.SamplesValid) / static_cast<double>(Result.SamplesTested) : 0.0;

		Result.Confidence = Ratio;
		Result.bValid     = Result.Failures.empty() && Ratio >= kTestMinValidRatio;

		if (Result.bValid)
			Result.Evidence.push_back(fmt::format("TestNamesLayout: Entry 0 reads \"None\"; decoded {} of {} sampled names, {} matched known engine names.",
			                                      Result.SamplesValid,
			                                      Result.SamplesTested,
			                                      CountKnownNameHits(Names)));
		else if (Ratio < kTestMinValidRatio)
			Result.Failures.push_back(fmt::format("TestNamesLayout: Only {}/{} entries decoded ({:.1f}%, need {:.1f}%)",
			                                      Result.SamplesValid,
			                                      Result.SamplesTested,
			                                      Ratio * 100.0,
			                                      kTestMinValidRatio * 100.0));

		return Result;
	}

	// ============================================================================
	//  Diagnostics
	// ============================================================================

	void DumpCandidateMemory(uintptr_t CandidateAddress, const std::string& Tag)
	{
		if (!IsReadable(CandidateAddress))
		{
			GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}] dump: 0x{:X} is not readable\n", Tag, CandidateAddress);
			return;
		}

		std::vector<uint8_t> Header;
		const size_t HeaderSize = ReadBlockBestEffort(CandidateAddress, Header, static_cast<size_t>(kDumpHeaderSize));
		if (HeaderSize == 0)
		{
			GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}] dump: 0x{:X} could not be read\n", Tag, CandidateAddress);
			return;
		}

		GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}] dump of 0x{:X} (0x{:X} bytes readable)\n", Tag, CandidateAddress, HeaderSize);
		GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}]   off     qword             ptr  i32[lo]      i32[hi]      ascii\n", Tag);

		// One line per pointer-sized slot: the raw qword, whether it resolves, and both
		// halves as int32 - every field the detector reasons about, in one view.
		// An inline chunk table is mostly unused, so zero runs are collapsed rather than
		// printed; without that the counters after the table drown in blank lines.
		int32 ZeroRunStart    = -1;
		const int32 DumpLimit = static_cast<int32>(HeaderSize);
		for (int32 Offset = 0; Offset + kPtrSize <= DumpLimit; Offset += kPtrSize)
		{
			const uintptr_t Qword = PeekPtr(Header, static_cast<size_t>(Offset));

			if (Qword == 0)
			{
				if (ZeroRunStart == -1)
					ZeroRunStart = Offset;
				continue;
			}

			if (ZeroRunStart != -1)
			{
				GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}]   +0x{:03X}..+0x{:03X}  all zero\n", Tag, ZeroRunStart, Offset - kPtrSize);
				ZeroRunStart = -1;
			}

			const int32 Low  = PeekInt32(Header, static_cast<size_t>(Offset));
			const int32 High = PeekInt32(Header, static_cast<size_t>(Offset + sizeof(int32)));

			std::string Ascii;
			for (int32 i = 0; i < kPtrSize; i++)
			{
				const uint8 Ch = Header[static_cast<size_t>(Offset + i)];
				Ascii.push_back((Ch >= 0x20 && Ch <= 0x7E) ? static_cast<char>(Ch) : '.');
			}

			GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}]   +0x{:03X}  0x{:016X}  {}  {:<11} {:<11} {}\n", Tag, Offset, Qword, IsReadable(Qword) ? "R" : "-", Low, High, Ascii);
		}

		if (ZeroRunStart != -1)
			GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}]   +0x{:03X}..+0x{:03X}  all zero\n", Tag, ZeroRunStart, DumpLimit - kPtrSize);

		// Follow readable pointers: the item array, chunk table or name block that the
		// candidate leads to is where a failed hypothesis usually goes wrong.
		int32 Followed = 0;
		for (int32 Offset = 0; Offset + kPtrSize <= DumpLimit && Followed < kDumpFollowLimit; Offset += kPtrSize)
		{
			const uintptr_t Target = PeekPtr(Header, static_cast<size_t>(Offset));
			if (!IsReadable(Target))
				continue;

			std::vector<uint8_t> Block;
			if (!ReadBlock(Target, Block, static_cast<size_t>(kDumpFollowSize)))
				continue;

			Followed++;
			GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}]   follow +0x{:03X} -> 0x{:X}\n", Tag, Offset, Target);

			for (int32 Inner = 0; Inner + kPtrSize <= kDumpFollowSize; Inner += kPtrSize)
			{
				const uintptr_t InnerQword = PeekPtr(Block, static_cast<size_t>(Inner));

				std::string Ascii;
				for (int32 i = 0; i < kPtrSize; i++)
				{
					const uint8 Ch = Block[static_cast<size_t>(Inner + i)];
					Ascii.push_back((Ch >= 0x20 && Ch <= 0x7E) ? static_cast<char>(Ch) : '.');
				}

				GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}]     +0x{:03X}  0x{:016X}  {}  {:<11} {:<11} {}\n", Tag, Inner, InnerQword, IsReadable(InnerQword) ? "R" : "-", PeekInt32(Block, static_cast<size_t>(Inner)), PeekInt32(Block, static_cast<size_t>(Inner + sizeof(int32))), Ascii);
			}
		}

		GLogger.FmtWrite(ELogLevel::Info, "DumpCandidateMemory: [{}] end of dump\n", Tag);
	}

} // namespace LayoutDetection
