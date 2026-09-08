#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../Memory/IMemory.h"
#include "../../Utils/Logger.h"
#include "../Unreal/NameArray.h"

namespace OffsetFinder
{
	constexpr int32_t OffsetNotFound       = -1;
	constexpr int32_t OffsetFinderMinValue = sizeof(void*) == 4 ? 0x18 : 0x28;
	constexpr int32_t OffsetFinderMaxValue = 0x1A0;

	template <int Alignement = 4, typename T>
	int32_t FindOffset(const std::vector<std::pair<void*, T>>& ObjectValuePair, int MinOffset = OffsetFinderMinValue, int MaxOffset = OffsetFinderMaxValue)
	{
		int HighestFoundOffset = MinOffset;
		bool bFoundOffset      = false;

		for (int i = 0; i < int(ObjectValuePair.size()); i++)
		{
			if (ObjectValuePair[i].first == nullptr)
			{
				GLogger.FmtWrite(ELogLevel::Error, "FindOffset is skipping ObjectValuePair[{}] because .first is nullptr.\n", i);
				continue;
			}

			for (int j = HighestFoundOffset; j < MaxOffset; j += Alignement)
			{
				const T TypedValueAtOffset = GMemory->Read<T>(reinterpret_cast<uintptr_t>(ObjectValuePair[i].first) + j);

				if (TypedValueAtOffset == ObjectValuePair[i].second && j >= HighestFoundOffset)
				{
					bFoundOffset = true;

					if (j > HighestFoundOffset)
					{
						// -1, not 0: earlier entries need re-verifying at the raised offset too,
						// and the loop's own i++ turns -1 into 0 for the next iteration.
						HighestFoundOffset = j;
						i                  = -1;
					}
					j = MaxOffset;
				}
			}
		}

		return bFoundOffset ? HighestFoundOffset : OffsetNotFound;
	}

	template <int Alignement = 4, typename T>
	int32_t FindOffsetByScore(const std::vector<std::pair<void*, T>>& ObjectValuePair, int32_t MinOffset, int32_t MaxOffset, int32_t* OutScore = nullptr, int32_t* OutTotalConsidered = nullptr, const std::vector<int32_t>& ExcludeOffsets = {})
	{
		int32_t TotalConsidered = 0;
		for (const auto& Entry : ObjectValuePair)
			if (Entry.first)
				TotalConsidered++;

		int32_t BestOffset = OffsetNotFound;
		int32_t BestScore  = 0;

		for (int32_t Off = MinOffset; Off < MaxOffset; Off += Alignement)
		{
			if (std::find(ExcludeOffsets.begin(), ExcludeOffsets.end(), Off) != ExcludeOffsets.end())
				continue;

			int32_t Score = 0;
			for (const auto& [Ptr, Expected] : ObjectValuePair)
			{
				if (Ptr && GMemory->Read<T>(reinterpret_cast<uintptr_t>(Ptr) + Off) == Expected)
					Score++;
			}

			if (Score > BestScore)
			{
				BestScore  = Score;
				BestOffset = Off;
			}
		}

		if (OutScore)
			*OutScore = BestScore;
		if (OutTotalConsidered)
			*OutTotalConsidered = TotalConsidered;

		return BestScore > 0 ? BestOffset : OffsetNotFound;
	}

	template <typename T>
	std::string HexOf(const T& Value)
	{
		static_assert(sizeof(T) <= sizeof(uint64_t), "HexOf only supports values up to 8 bytes.");
		uint64_t Raw = 0;
		memcpy(&Raw, &Value, sizeof(T));
		return fmt::format("0x{:0{}X}", Raw, sizeof(T) * 2);
	}

	template <typename T, int Alignement = alignof(T)>
	void DumpOffsetCandidates(const std::vector<std::pair<void*, T>>& ObjectValuePair, int32_t MinOffset, int32_t MaxOffset, const std::string& Tag)
	{
		GLogger.FmtWrite(ELogLevel::Info, "[OffsetDump:{}] Scanning [0x{:X}, 0x{:X}) step 0x{:X} across {} entrie(s)\n", Tag, MinOffset, MaxOffset, Alignement, ObjectValuePair.size());

		std::vector<std::vector<int32_t>> PerEntryMatches(ObjectValuePair.size());

		for (size_t i = 0; i < ObjectValuePair.size(); i++)
		{
			void* Ptr = ObjectValuePair[i].first;

			if (!Ptr)
			{
				GLogger.FmtWrite(ELogLevel::Warning, "[OffsetDump:{}] Entry {} has a null pointer, skipping\n", Tag, i);
				continue;
			}

			const T& Expected = ObjectValuePair[i].second;

			for (int32_t Off = MinOffset; Off < MaxOffset; Off += Alignement)
			{
				if (GMemory->Read<T>(reinterpret_cast<uintptr_t>(Ptr) + Off) == Expected)
					PerEntryMatches[i].push_back(Off);
			}

			std::string MatchList;
			for (int32_t Off : PerEntryMatches[i])
				MatchList += fmt::format("0x{:X} ", Off);

			GLogger.FmtWrite(ELogLevel::Info, "[OffsetDump:{}] Entry {} (obj=0x{:X}, expects {}) matches at: {}\n", Tag, i, reinterpret_cast<uintptr_t>(Ptr), HexOf(Expected), MatchList.empty() ? "(none)" : MatchList);
		}

		GLogger.FmtWrite(ELogLevel::Info, "[OffsetDump:{}] Per-offset table (value @ offset, * marks a match):\n", Tag);
		for (int32_t Off = MinOffset; Off < MaxOffset; Off += Alignement)
		{
			std::string Row = fmt::format("[OffsetDump:{}]   0x{:X}:", Tag, Off);
			bool bAllMatch  = true;

			for (size_t i = 0; i < ObjectValuePair.size(); i++)
			{
				void* Ptr = ObjectValuePair[i].first;
				if (!Ptr)
					continue;

				const bool bMatches = std::find(PerEntryMatches[i].begin(), PerEntryMatches[i].end(), Off) != PerEntryMatches[i].end();
				bAllMatch           = bAllMatch && bMatches;

				const T ValueAtOff  = GMemory->Read<T>(reinterpret_cast<uintptr_t>(Ptr) + Off);
				Row                += fmt::format("  [{}]={}{}", i, HexOf(ValueAtOff), bMatches ? "*" : " ");
			}

			GLogger.FmtWrite(ELogLevel::Info, "{}{}\n", Row, bAllMatch ? "  <-- ALL MATCH" : "");
		}

		std::vector<int32_t> Consensus;
		for (int32_t Off = MinOffset; Off < MaxOffset; Off += Alignement)
		{
			bool bAllMatch = true;
			for (size_t i = 0; i < ObjectValuePair.size(); i++)
			{
				if (!ObjectValuePair[i].first)
					continue;

				if (std::find(PerEntryMatches[i].begin(), PerEntryMatches[i].end(), Off) == PerEntryMatches[i].end())
				{
					bAllMatch = false;
					break;
				}
			}
			if (bAllMatch)
				Consensus.push_back(Off);
		}

		std::string ConsensusList;
		for (int32_t Off : Consensus)
			ConsensusList += fmt::format("0x{:X} ", Off);

		GLogger.FmtWrite(ELogLevel::Info, "[OffsetDump:{}] Consensus (every entry matches simultaneously): {}\n", Tag, ConsensusList.empty() ? "(none)" : ConsensusList);
	}

	template <bool bCheckForVft = true>
	int32_t GetValidPointerOffset(const void* PtrObjA, const void* PtrObjB, int32_t StartingOffset, int32_t MaxOffset, bool bNeedsToBeInModuleMemory = false)
	{
		const uint8_t* ObjA = static_cast<const uint8_t*>(PtrObjA);
		const uint8_t* ObjB = static_cast<const uint8_t*>(PtrObjB);

		if (!GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(ObjA)) || !GMemory->IsAddressReadable(reinterpret_cast<uintptr_t>(ObjB)))
			return OffsetNotFound;

		for (int j = StartingOffset; j <= MaxOffset; j += sizeof(void*))
		{
			const uintptr_t ValA = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(ObjA) + j);
			const uintptr_t ValB = GMemory->Read<uintptr_t>(reinterpret_cast<uintptr_t>(ObjB) + j);
			const bool bIsAValid = GMemory->IsAddressReadable(ValA) && (bCheckForVft ? GMemory->IsAddressReadable(GMemory->Read<uintptr_t>(ValA)) : true);
			const bool bIsBValid = GMemory->IsAddressReadable(ValB) && (bCheckForVft ? GMemory->IsAddressReadable(GMemory->Read<uintptr_t>(ValB)) : true);

			if (bNeedsToBeInModuleMemory)
			{
				ModuleInfo ModInfo = GMemory->GetUnrealModule();
				if (!ModInfo.Contains(ValA) || !ModInfo.Contains(ValB))
					continue;
			}

			if (bIsAValid && bIsBValid)
				return j;
		}

		return OffsetNotFound;
	}

	// Finds the exec-function registered with a UFunction whose name matches RefStr.
	// Locates the (string_ptr, func_ptr) registration pair written by FNativeFunctionRegistrar
	// by using platform bulk-scanners instead of a linear sweep, which is critical for performance
	// on mobile where each GMemory read is a syscall.
	template <typename Type = const char*>
	void* FindUnrealExecFunctionByString(Type RefStr)
	{
		if (!GMemory)
			return nullptr;

		ModuleInfo ModuleInfo = GMemory->GetUnrealModule();

		uintptr_t StringAddr = GMemory->FindByStringInRange(RefStr, ModuleInfo.GetBase(), ModuleInfo.GetSize());
		if (!StringAddr)
			return nullptr;

		// Step 2: search data sections for a pointer holding that string's address.
		uintptr_t PtrSlot = GMemory->FindAlignedValueInRange(StringAddr, sizeof(uintptr_t), ModuleInfo.GetBase(), ModuleInfo.GetSize());
		if (PtrSlot == 0)
			return nullptr;

		// Step 3: exec function pointer immediately follows the string pointer in the table.
		const uintptr_t ExecFuncAddr = GMemory->Read<uintptr_t>(PtrSlot + sizeof(void*));

		if (!GMemory->IsAddressReadable(ExecFuncAddr))
			return nullptr;

		return reinterpret_cast<void*>(ExecFuncAddr);
	}

	// IsPotentialValidOffset rules out offsets that structurally can't hold UObject::Name/FField::Name
	// (e.g. it can't be at the same offset as UObject::Class); remaining offsets are then narrowed
	// down by the comparison-index/average-value heuristics below, ideally to just one.
	template <typename IteratorType>
	int32_t FindNameOffsetForSomeClass(std::function<bool(int32_t Value)> IsPotentialValidOffset, IteratorType DataSetStartIterator, IteratorType DataSetEndIterator, int MaxIteration = 0)
	{
		struct ValueInfo
		{
			int32_t Offset;
			int32_t NumNamesWithLowCmpIdx = 0x0;
			uint64_t TotalValue           = 0x0;
			bool bIsValidCmpIdxRange      = true;
		};

		std::vector<ValueInfo> PossibleOffsets;

		constexpr auto MaxAllowedComparisonIndexValue        = 0x4000000;
		constexpr auto MaxAllowedAverageComparisonIndexValue = MaxAllowedComparisonIndexValue / 2;
		constexpr auto MinAllowedAverageComparisonIndexValue = 0x280;
		constexpr auto LowComparisonIndexUpperCap            = 0x10;
		constexpr auto MaxAllowedNamesWithLowCmpIdx          = 0x40;

		for (int i = sizeof(void*); i <= 0x40; i += 0x4)
		{
			if (!IsPotentialValidOffset(i))
				continue;

			PossibleOffsets.push_back(ValueInfo{i});
		}

		auto GetDataAtOffsetAsInt = [](const void* Ptr, int32_t Offset) -> uint32_t
		{ return GMemory->Read<uint32_t>(reinterpret_cast<uintptr_t>(Ptr) + Offset); };

		const IteratorType VerifyStartIt = DataSetStartIterator;

		int NumObjectsConsidered = 0;

		for (; DataSetStartIterator != DataSetEndIterator; ++DataSetStartIterator)
		{
			if (NumObjectsConsidered++ >= MaxIteration)
				break;

			constexpr auto SmallPageSize            = 0x1000;
			constexpr auto MaxAccessedSizeInUObject = 0x44;

			// Object allocations are page-aligned in both size and base, and UObject's real size
			// isn't known yet (that's what this offset search is for) - so an object within
			// MaxAccessedSizeInUObject of a page boundary might read past its actual allocation.
			const void* CurrentObjectOrField = (*DataSetStartIterator).GetAddress();
			if (!CurrentObjectOrField)
				break;

			const bool bIsGoingPastPageBounds = (reinterpret_cast<const uintptr_t>(CurrentObjectOrField) & (SmallPageSize - 1)) > (SmallPageSize - MaxAccessedSizeInUObject);
			if (bIsGoingPastPageBounds)
				continue;

			for (ValueInfo& Info : PossibleOffsets)
			{
				const uint32_t ValueAtOffset = GetDataAtOffsetAsInt(CurrentObjectOrField, Info.Offset);

				Info.TotalValue            += ValueAtOffset;
				Info.bIsValidCmpIdxRange    = Info.bIsValidCmpIdxRange && ValueAtOffset < MaxAllowedComparisonIndexValue;
				Info.NumNamesWithLowCmpIdx += (ValueAtOffset <= LowComparisonIndexUpperCap);
			}
		}

		std::vector<int32_t> ValidOffsets;
		for (const ValueInfo& Info : PossibleOffsets)
		{
			const auto AverageValue = (Info.TotalValue / NumObjectsConsidered);

			if (Info.bIsValidCmpIdxRange && Info.NumNamesWithLowCmpIdx <= MaxAllowedNamesWithLowCmpIdx && AverageValue >= MinAllowedAverageComparisonIndexValue && AverageValue <= MaxAllowedAverageComparisonIndexValue)
				ValidOffsets.push_back(Info.Offset);
		}

		if (ValidOffsets.empty())
			return -1;

		if (ValidOffsets.size() == 1)
			return ValidOffsets[0];

		auto ScoreOffset = [&](int32_t Off) -> int32_t
		{
			int32_t Score   = 0;
			int32_t Checked = 0;
			for (auto It = VerifyStartIt; It != DataSetEndIterator && Checked < 20; ++It, ++Checked)
			{
				const void* Addr     = (*It).GetAddress();
				const int32_t CmpIdx = GMemory->Read<int32_t>(reinterpret_cast<uintptr_t>(Addr) + Off);
				if (CmpIdx <= 0)
					continue;
				const std::string Name = NameArray::GetNameEntry(CmpIdx).GetString();
				const bool bValid      = !Name.empty() && Name.size() <= 256 && std::all_of(Name.begin(), Name.end(), [](char C)
				{ return C >= 0x20 && C <= 0x7E; });
				if (bValid)
					Score++;
			}
			return Score;
		};

		int32_t BestOffset = ValidOffsets[0];
		int32_t BestScore  = ScoreOffset(BestOffset);

		for (int32_t i = 1; i < static_cast<int32_t>(ValidOffsets.size()); i++)
		{
			const int32_t Off   = ValidOffsets[i];
			const int32_t Score = ScoreOffset(Off);

			if (Score > BestScore)
			{
				BestOffset = Off;
				BestScore  = Score;
			}
			else if (Off == BestOffset + 4 || Score < BestScore)
			{
				// Silent: adjacent +4 is the DisplayIndex of a case-preserving FName, or scored lower.
			}
			else
			{
				GLogger.FmtWrite(ELogLevel::Warning, "Another [UObject/FField]::Name offset (0x{:04X}) is also considered valid.\n", Off);
			}
		}

		return BestOffset;
	}

	void FindStaticOffsets(std::unordered_map<std::string, uintptr_t>& ClassNamesToOffsets);
}
