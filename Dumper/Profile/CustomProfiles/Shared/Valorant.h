#pragma once

#include "../../IProfile.h"

class ValorantProfile : public IProfile
{
public:
	ValorantProfile() = default;

	std::vector<std::string> GetSupportedGames() const override
	{
		return {"com.tencent.tmgp.codev"};
	}

	void OverrideDecryptCallbacks(FDecryptCallbacks& Callbacks) const override
	{
		Callbacks.Statics.GNames = [](const FDecryptContext<uintptr_t>& Context)
		{
			return DecryptGNames(Context.RawValue);
		};

		Callbacks.NameStorage.Chunk = [](const FDecryptIndexedContext<uintptr_t>& Context)
		{
			return DecryptNameChunk(Context.RawValue, Context.Index);
		};
	}

	void OverrideSettings(FSettings& Settings) const override
	{
		Settings.CppGenerator.DecryptionUtils.DecryptGNamesFuncBody = R"({
	uint32_t Offsets[8];
	std::memcpy(Offsets, reinterpret_cast<void*>(NamesPtr + 128), sizeof(Offsets));

	uint64_t DecNames = 0;
	for (int i = 0; i < 8; ++i)
		DecNames |= uint64_t(*reinterpret_cast<uint8_t*>(NamesPtr + Offsets[i])) << (i * 8);

	return DecNames;
})";

		Settings.CppGenerator.DecryptionUtils.DecryptFNameChunkFuncBody = R"({
#ifdef __APPLE__
	constexpr int Blocks = 0xD0;
#else
	constexpr int Blocks = 0x40;
#endif
	const uintptr_t EncBase = InSDKUtils::GetGNames() + 0x10000 + Blocks;

	if (ChunkIndex == 0)
		ChunkAddr ^= *reinterpret_cast<uint64_t*>(EncBase);

	return ChunkAddr;
})";
	}

private:
	static uintptr_t DecryptGNames(uintptr_t NamesPtr)
	{
		uint32_t Offsets[8];
		GMemory->ReadBytes(NamesPtr + 128, Offsets, sizeof(Offsets));

		uint64_t DecNames = 0;

		for (int i = 0; i < 8; ++i)
			DecNames |= uint64_t(GMemory->Read<uint8_t>(NamesPtr + Offsets[i])) << (i * 8);

		return DecNames;
	}

	static uintptr_t DecryptNameChunk(uintptr_t ChunkAddr, int32 ChunkIdx)
	{
#ifdef __APPLE__
		constexpr static int Blocks = 0xD0;
#else
		constexpr static int Blocks = 0x40;
#endif

		static uintptr_t EncFlagAddr = 0;
		static bool bOnce            = false;
		if (!bOnce)
		{
			bOnce = true;

			for (const auto& Segment : GMemory->GetUnrealModule().GetSegments())
			{
				if (!Segment.IsValid() || !Segment.IsReadable() || !Segment.IsExecutable())
					continue;

				const uintptr_t Hit = GMemory->FindPatternInRange(
				    Segment.GetStart(), Segment.GetSize(), "?? ?? 00 34 ?? 0C ?? 8B ?? ?? 90 52 ?? 00 A0 72", 4);

				if (!Hit)
					continue;

				// Walk backwards to find the ADRP immediately preceding the pattern hit.
				uintptr_t AdrpAddr = 0;
				for (uintptr_t Addr = Hit - 4; Addr >= Segment.GetStart() && Addr > Hit - 128; Addr -= 4)
				{
					const KittyInsnArm64 Insn = Utils::Arm64::DecodeInsn(GMemory->Read<uint32_t>(Addr), Addr);
					if (Insn.isValid() && Insn.type == EKittyInsnTypeArm64::ADRP)
					{
						AdrpAddr = Addr;
						break;
					}
				}

				if (!AdrpAddr)
					break;

				// Read forward from the ADRP so Find_ADRP_Final_Address can pick up ADRP+ADD/LDR.
				std::vector<uint32_t> Insns(10, 0);
				GMemory->ReadBytes(AdrpAddr, Insns.data(), Insns.size() * sizeof(uint32_t));

				const uintptr_t Target = Utils::Arm64::Find_ADRP_Final_Address(Insns, AdrpAddr);
				if (!Target)
					break;

				const uintptr_t PtrVal = GMemory->Read<uintptr_t>(Target);
				EncFlagAddr            = GMemory->IsAddressReadable(PtrVal) ? PtrVal : Target;
				break;
			}
		}

		// GNames is read here rather than taken as a parameter so the hook follows whichever
		// candidate detection is currently probing.
		const uintptr_t EncBase = GNames + 0x10000 + Blocks;

		if (EncFlagAddr && GMemory->Read<uint32_t>(EncFlagAddr) &&
		    GMemory->Read<uint32_t>(EncBase + 0x18 + ChunkIdx * 4))
		{
			ChunkAddr = GMemory->Read<uint64_t>(EncBase + 0x8018 + ChunkIdx * 8);
		}
		else if (ChunkIdx == 0)
		{
			ChunkAddr = (GMemory->Read<uint64_t>(EncBase) ^ ChunkAddr);
		}

		return ChunkAddr;
	}
};
