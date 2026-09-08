#pragma once

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

		GLogger.FmtWrite(ELogLevel::Info, "Found {} GNames Matches via pattern.\n", Matches.size());

		for (const auto& Match : Matches)
		{
			std::vector<uint32> Insns(10, 0);
			GMemory->ReadBytes(Match, Insns.data(), Insns.size() * sizeof(uint32));

			GLogger.FmtWrite(ELogLevel::Info, "Testing GNames Match (0x{:X}) -> 0x{:X}\n", GMemory->GetUnrealModule().AddressToOffset(Match), Match);
			uintptr_t ADRP = Utils::Arm64::Find_ADRP_Final_Address(Insns, Match);
			if (ADRP)
			{
				GLogger.FmtWrite(ELogLevel::Info, "Found GNames ADRP 0x{:X}\n", ADRP);
				return ADRP;
			}
		}

		return 0;
	}

	void DecryptGNames(uintptr_t& NamesPtr) const override
	{
		GLogger.FmtWrite(ELogLevel::Info, "Decrypting GNames 0x{:X}...\n", NamesPtr);

		int64_t var_2;
		int64_t var_5[16];

		var_2                        = (GMemory->Read<int32_t>(NamesPtr) - 100) / 3u;
		var_5[(uint32_t)(var_2 - 1)] = GMemory->Read<int64_t>(NamesPtr + 8);

		while (var_2 - 2 >= 0)
		{
			var_5[(uint32_t)(var_2 - 2)] = GMemory->Read<int64_t>(var_5[var_2 - 1]);
			--var_2;
		}

		NamesPtr = var_5[0];
		GLogger.FmtWrite(ELogLevel::Info, "Decrypted GNames 0x{:X}.\n", NamesPtr);
	}
};