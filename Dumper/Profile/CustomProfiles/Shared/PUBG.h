#pragma once

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "../../IProfile.h"

class PUBGProfile : public IProfile
{
public:
	PUBGProfile() = default;

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
#if defined(__ANDROID__) && !defined(__LP64__)
		// 32-bit UE4 targets (PUBG <= 4.x) do not contain the AArch64 pattern below.
		// GNames is a pointer global, but a manager object sits between it and the
		// name array, which itself starts at manager + 0x88. Locate it structurally:
		// global -> manager (+0x88) -> name array -> chunk table -> chunk -> "None".
		if (const uintptr_t Scanned = FindGNamesGlobal())
			return Scanned;

		return 0;
#else
#ifdef __ANDROID__
		// android inlined
		constexpr const char* Pattern = "81 80 52 ? ? ? ? ? 81 80 52 ? 03 1F 2A";
		constexpr int Step            = 0x1F;
#else
		constexpr const char* Pattern = "ff c3 01 91 c0 03 5f d6 ? ? ? ? ? ? ? 91 ? ? ? 94 ? ? ? 34 ff ? ? ? ? ? ? ? ? ? ? 91";
		constexpr int Step            = 0x1C;
#endif

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
#if defined(__ANDROID__) && !defined(__LP64__)
		// The 32-bit global points at a manager object and the name array lives at
		// manager + 0x88. Emit that resolution so the generated SDK's name lookups
		// match the layout this dumper validated.
		Settings.CppGenerator.GetGNamesFuncBody = R"({
	const uintptr_t Base = GetImageBase();

	if (!Base)
		return 0;

	const uintptr_t Manager = *reinterpret_cast<uintptr_t*>(Base + Offsets::GNames);
	if (!Manager)
		return 0;

	return *reinterpret_cast<uintptr_t*>(Manager + 0x88);
}
)";
#endif
	}

private:
	static bool IsPointerLike(uint32_t Value)
	{
		return Value >= 0x10000 && Value < 0xFFFF0000 && (Value & (sizeof(uint32_t) - 1)) == 0;
	}

	static bool IsNoneNameAt(uintptr_t EntryAddr)
	{
		static constexpr uint8_t AnsiNone[5] = {'N', 'o', 'n', 'e', 0};
		static constexpr uint8_t WideNone[10] = {'N', 0, 'o', 0, 'n', 0, 'e', 0, 0, 0};

		// Entries store (slot << 1) | bIsWide. Some builds put the index at +0 with
		// the text at +4; others keep a leading word, so index is at +4 and the text
		// at +8.
		for (uintptr_t IndexOffset : {uintptr_t(0), uintptr_t(4)})
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
		// TStaticIndirectArrayThreadSafeRead keeps its chunk table at the head of the
		// object; some builds pad it by a few words.
		for (uintptr_t Header = 0; Header <= 0x10; Header += sizeof(uint32_t))
		{
			const uint32_t Chunks = GMemory->Read<uint32_t>(ArrayAddr + Header);
			if (!IsPointerLike(Chunks))
				continue;

			const uint32_t Chunk0 = GMemory->Read<uint32_t>(Chunks);
			if (!IsPointerLike(Chunk0))
				continue;

			// Chunks of pointers to individually allocated entries.
			const uint32_t Entry0 = GMemory->Read<uint32_t>(Chunk0);
			if (IsPointerLike(Entry0) && IsNoneNameAt(Entry0))
				return true;

			// Chunks holding the entries inline.
			if (IsNoneNameAt(Chunk0))
				return true;
		}

		return false;
	}

	uintptr_t FindGNamesGlobal() const
	{
		constexpr size_t kBlockSize = 0x40000;

		std::vector<uint8_t> Block;
		std::unordered_set<uint32_t> Seen;

		// Scan writable segments in map order. The first structurally valid chain
		// is the global the engine is exposing at dump time; later copies (for
		// example a cached manager pointer in BSS) can resolve through an extra
		// indirection and must not take precedence.
		for (const auto& Segment : GMemory->GetUnrealModule().GetSegments())
		{
			if (!Segment.IsValid() || !Segment.IsReadable() || !Segment.IsWriteable() || Segment.IsExecutable())
				continue;

			const size_t Size = Segment.GetSize();
			for (size_t Offset = 0; Offset + sizeof(uint32_t) <= Size; Offset += kBlockSize)
			{
				const size_t BlockSize = std::min(kBlockSize, Size - Offset);
				Block.resize(BlockSize);
				if (GMemory->ReadBytes(Segment.GetStart() + Offset, Block.data(), BlockSize) != BlockSize)
					continue;

				for (size_t InBlock = 0; InBlock + sizeof(uint32_t) <= BlockSize; InBlock += sizeof(uint32_t))
				{
					uint32_t Value = 0;
					std::memcpy(&Value, Block.data() + InBlock, sizeof(Value));
					if (!IsPointerLike(Value))
						continue;
					if (!Seen.insert(Value).second)
						continue;
					// Either the array itself, or a manager whose +0x88 holds the array pointer.
					if (!LooksLikeNameArray(Value))
					{
						const uint32_t Nested = GMemory->Read<uint32_t>(Value + 0x88);
						if (!IsPointerLike(Nested) || !LooksLikeNameArray(Nested))
							continue;
					}

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
		// 32-bit BGMI: the global points at a manager object whose +0x88 holds the
		// actual name array pointer. Resolve that chain when it validates so the
		// layout detector sees the array object itself.
		if (sizeof(uintptr_t) == sizeof(uint32_t) && GMemory->IsAddressReadable(NamesPtr))
		{
			const uint32_t Manager = GMemory->Read<uint32_t>(NamesPtr);
			if (Manager != 0 && GMemory->IsAddressReadable(Manager + 0x88))
			{
				const uint32_t NameArray = GMemory->Read<uint32_t>(Manager + 0x88);
				if (NameArray != 0 && LooksLikeNameArray(NameArray))
				{
					NamesPtr = NameArray;
					return;
				}
			}
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
