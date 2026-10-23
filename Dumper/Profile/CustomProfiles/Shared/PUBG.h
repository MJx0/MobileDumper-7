#pragma once

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "../../IProfile.h"

class PUBGProfile : public IProfile
{
public:
	std::vector<std::string> GetSupportedGames() const override
	{
		return {
		    "com.tencent.ig",
		    "com.rekoo.pubgm",
		    "com.pubg.imobile",
		    "com.pubg.krmobile",
		    "com.vng.pubgmobile",
		};
	}

	uintptr_t GetGNames() const override
	{
#if defined(__ANDROID__)
		return FindGNamesGlobal();
#else
		constexpr const char* Pattern = "ff c3 01 91 c0 03 5f d6 ? ? ? ? ? ? ? 91 ? ? ? 94 ? ? ? 34 ff ? ? ? ? ? ? ? ? ? ? 91";
		constexpr int Step            = 0x1C;

		std::vector<uintptr_t> Matches;

		for (const auto& Segment : GMemory->GetUnrealModule().GetSegments())
		{
			if (!Segment.IsValid() || !Segment.IsReadable() || !Segment.IsExecutable())
				continue;

			auto TempMatches = GMemory->FindAllPatternInRange(Segment.GetStart(), Segment.GetSize(), Pattern, Step);
			if (!TempMatches.empty())
			{
				Matches.insert(Matches.end(), TempMatches.begin(), TempMatches.end());
			}

			if (Matches.size() >= 3)
				break;
		}

		GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Found {} GNames Matches via pattern.\n", Matches.size());

		for (const auto& Match : Matches)
		{
			std::vector<uint32> Insns(10, 0);
			GMemory->ReadBytes(Match, Insns.data(), Insns.size() * sizeof(uint32));

			GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Testing GNames Match (0x{:X}) -> 0x{:X}\n", GMemory->GetUnrealModule().AddressToOffset(Match), Match);
			uintptr_t ADRP = Utils::Arm64::Find_ADRP_Final_Address(Insns, Match);
			if (ADRP)
			{
				GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Found GNames ADRP 0x{:X}\n", ADRP);
				return ADRP;
			}
		}

		return 0;
#endif
	}

	void OverrideSettings(FSettings& Settings) const override
	{
#if defined(__ANDROID__)
		// Match the manager chain used by ResolveNameArray on either pointer width.
		Settings.CppGenerator.GetGNamesFuncBody = R"({
	const uintptr_t Base = GetImageBase();

	if (!Base)
		return 0;

	const uintptr_t Manager = *reinterpret_cast<uintptr_t*>(Base + Offsets::GNames);
	if (!Manager)
		return 0;

	constexpr uintptr_t ArrayOffset = sizeof(uintptr_t) == 8 ? 0x110 : 0x88;
	return *reinterpret_cast<uintptr_t*>(Manager + ArrayOffset);
}
)";
#endif
	}

private:
	// The manager object sits between the global and the name array. The array
	// offset differs by pointer width.
	static constexpr uintptr_t kManagerArrayOffset = sizeof(uintptr_t) == 8 ? 0x110 : 0x88;

	static bool IsPointerLike(uintptr_t Value)
	{
		constexpr uint64_t Limit = sizeof(uintptr_t) == 8 ? 0x0000FFFFFFFFFFFFULL : 0xFFFF0000ULL;
		return Value >= 0x10000 && Value < Limit && (Value & (sizeof(uintptr_t) - 1)) == 0;
	}

	static bool IsNoneNameAt(uintptr_t EntryAddr)
	{
		static constexpr uint8_t AnsiNone[5] = {'N', 'o', 'n', 'e', 0};
		static constexpr uint8_t WideNone[10] = {'N', 0, 'o', 0, 'n', 0, 'e', 0, 0, 0};

		// Entries store (slot << 1) | bIsWide. Some builds put the index at +0 with
		// the text at +4; others keep a leading word, so index is at +4 (32-bit) or
		// +8 (64-bit) and the text follows it.
		for (uintptr_t IndexOffset : {uintptr_t(0), uintptr_t(4), uintptr_t(8)})
		{
			uint32_t Index = 0;
			if (GMemory->ReadBytes(EntryAddr + IndexOffset, &Index, sizeof(Index)) != sizeof(Index))
				continue;

			if ((Index >> 1) != 0)
				continue;

			uint8_t Text[10]{};
			if (GMemory->ReadBytes(EntryAddr + IndexOffset + 4, Text, sizeof(Text)) != sizeof(Text))
				continue;

			if (std::memcmp(Text, AnsiNone, sizeof(AnsiNone)) == 0 ||
			    std::memcmp(Text, WideNone, sizeof(WideNone)) == 0)
				return true;
		}

		return false;
	}

	static bool LooksLikeNameArray(uintptr_t ArrayAddr)
	{
		constexpr uintptr_t PointerSize = sizeof(uintptr_t);

		// TStaticIndirectArrayThreadSafeRead keeps its chunk table at the head of the
		// object; some builds pad it by a few words.
		for (uintptr_t Header = 0; Header <= 0x10; Header += PointerSize)
		{
			const uintptr_t Chunks = GMemory->Read<uintptr_t>(ArrayAddr + Header);
			if (!IsPointerLike(Chunks))
				continue;

			const uintptr_t Chunk0 = GMemory->Read<uintptr_t>(Chunks);
			if (!IsPointerLike(Chunk0))
				continue;

			// Chunks of pointers to individually allocated entries.
			const uintptr_t Entry0 = GMemory->Read<uintptr_t>(Chunk0);
			if (IsPointerLike(Entry0) && IsNoneNameAt(Entry0))
				return true;

			// Chunks holding the entries inline.
			if (IsNoneNameAt(Chunk0))
				return true;
		}

		return false;
	}

	static uintptr_t ResolveNameArray(uintptr_t Manager)
	{
		if (!IsPointerLike(Manager))
			return 0;

		const uintptr_t Array = GMemory->Read<uintptr_t>(Manager + kManagerArrayOffset);
		return IsPointerLike(Array) && LooksLikeNameArray(Array) ? Array : 0;
	}

	uintptr_t FindGNamesGlobal() const
	{
		constexpr size_t kBlockSize = 0x40000;
		constexpr size_t kPointerSize = sizeof(uintptr_t);

		std::vector<uint8_t> Block;
		std::unordered_set<uintptr_t> Seen;

		// Preserve map order: file-backed globals precede cached pointers in BSS.
		for (const auto& Segment : GMemory->GetUnrealModule().GetSegments())
		{
			if (!Segment.IsValid() || !Segment.IsReadable() || !Segment.IsWriteable() || Segment.IsExecutable())
				continue;

			const size_t Size = Segment.GetSize();
			for (size_t Offset = 0; Offset + kPointerSize <= Size; Offset += kBlockSize)
			{
				const size_t BlockSize = std::min(kBlockSize, Size - Offset);
				Block.resize(BlockSize);
				if (GMemory->ReadBytes(Segment.GetStart() + Offset, Block.data(), BlockSize) != BlockSize)
					continue;

				for (size_t InBlock = 0; InBlock + kPointerSize <= BlockSize; InBlock += kPointerSize)
				{
					uintptr_t Value = 0;
					std::memcpy(&Value, Block.data() + InBlock, sizeof(Value));
					if (!IsPointerLike(Value) || !Seen.insert(Value).second || !ResolveNameArray(Value))
						continue;

					const uintptr_t Global = Segment.GetStart() + Offset + InBlock;
					GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Structural scan found GNames global at 0x{:X} -> 0x{:X}\n",
					                 GMemory->GetUnrealModule().AddressToOffset(Global), Global);
					return Global;
				}
			}
		}

		GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Structural scan found no GNames global.\n");
		return 0;
	}

	void DecryptGNames(uintptr_t& NamesPtr) const override
	{
		if (const uintptr_t Array = ResolveNameArray(GMemory->Read<uintptr_t>(NamesPtr)))
		{
			NamesPtr = Array;
			return;
		}

		// The slot this profile understands holds a small int32 descriptor:
		// (Value - 100) / 3 is the number of pointer hops to follow (1..16).
		// Analyzer candidates are arbitrary addresses, so the value there can be
		// anything - using it as an index unguarded walks off the stack.
		const int32_t Code = GMemory->Read<int32_t>(NamesPtr);
		if (Code < 100)
			return;

		const int32_t Length = (Code - 100) / 3;
		if (Length < 1 || Length > 16)
			return;

		int64_t Chain[16]{};
		Chain[Length - 1] = GMemory->Read<int64_t>(NamesPtr + 8);

		for (int32_t i = Length - 1; i >= 1; --i)
		{
			if (!GMemory->IsAddressReadable(Chain[i]))
				return;

			Chain[i - 1] = GMemory->Read<int64_t>(Chain[i]);
		}

		if (!GMemory->IsAddressReadable(Chain[0]))
			return;

		NamesPtr = Chain[0];
	}
};
