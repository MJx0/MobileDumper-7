#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>

#include "../../IProfile.h"

class FortniteProfile : public IProfile
{
public:
	FortniteProfile() = default;

	std::vector<std::string> GetSupportedGames() const override
	{
		return {"com.chairentertainment.Fortnite"};
	}

	uintptr_t GetGNames() const override
	{
		const auto& Mod            = GMemory->GetUnrealModule();
		const uintptr_t ModStart   = Mod.GetStart();
		const uintptr_t ModEnd     = Mod.GetStart() + Mod.GetSize();
		constexpr size_t ChunkSize = 0x100000;

		std::vector<uintptr_t> Buf(ChunkSize / sizeof(uintptr_t));
		size_t Checked = 0;

		for (const auto& Seg : Mod.GetSegments())
		{
			if (!Seg.IsValid() || !Seg.IsReadable() || Seg.IsExecutable())
				continue;

			const uintptr_t SegEnd = Seg.GetStart() + Seg.GetSize();
			for (uintptr_t Cur = Seg.GetStart(); Cur < SegEnd; Cur += ChunkSize)
			{
				const size_t N   = std::min<size_t>(ChunkSize, SegEnd - Cur);
				const size_t Got = GMemory->ReadBytes(Cur, Buf.data(), N);

				for (size_t I = 0; I < Got / sizeof(uintptr_t); ++I)
				{
					const uintptr_t P    = Buf[I];
					const uintptr_t Slot = Cur + I * sizeof(uintptr_t);

					if (P < 0x100000000ull || (P & 7) || (P >= ModStart && P < ModEnd))
						continue;

					++Checked;
					if (LooksLikeNameArray(P))
					{
						GLogger.FmtWrite(ELogLevel::Info, "FortniteProfile: GNames pointer slot 0x{:X} (offset 0x{:X}) -> 0x{:X} after {} candidates\n", Slot, Mod.AddressToOffset(Slot), P, Checked);
						return Slot;
					}
				}
			}
		}

		GLogger.FmtWrite(ELogLevel::Warning, "FortniteProfile: no TNameEntryArray found after {} candidates\n", Checked);
		return 0;
	}

	bool ResolveGNamesLayout(uintptr_t NamesPtr, std::unique_ptr<INamesLayout>& Out) const override
	{
		if (IProfile::ResolveGNamesLayout(NamesPtr, Out))
			return true;

		if (!LooksLikeNameArray(NamesPtr))
			return false;

		auto L                       = std::make_unique<FNameArrayLayout>();
		L->Chunks                    = 0x0;
		L->ElementsPerChunk          = 0x4000;
		L->NumElements               = 0x400;
		L->FNameEntry.String         = 0x10;
		L->FNameEntry.Index          = 0x0;
		L->FNameEntry.WideMask       = 0x1;
		L->FNameEntry.GetIsWide      = [](int32 Idx) { return (Idx & 1) != 0; };
		Out                          = std::move(L);

		GLogger.FmtWrite(ELogLevel::Info, "FortniteProfile: using built-in TNameEntryArray layout (16384 per chunk)\n");
		return true;
	}

private:
	static bool LooksLikeNameArray(uintptr_t Obj)
	{
		if (!GMemory->IsAddressReadable(Obj))
			return false;

		const uintptr_t C0 = GMemory->Read<uintptr_t>(Obj);
		if (C0 < 0x100000000ull || (C0 & 7) || !GMemory->IsAddressReadable(C0, 16))
			return false;

		const uintptr_t E0 = GMemory->Read<uintptr_t>(C0);
		const uintptr_t E1 = GMemory->Read<uintptr_t>(C0 + 8);
		if (E0 < 0x100000000ull || E1 < 0x100000000ull)
			return false;
		if (!GMemory->IsAddressReadable(E0, 0x20) || !GMemory->IsAddressReadable(E1, 0x20))
			return false;

		char S0[8]  = {};
		char S1[16] = {};
		GMemory->ReadBytes(E0 + 0x10, S0, 5);
		GMemory->ReadBytes(E1 + 0x10, S1, 12);

		if (std::memcmp(S0, "None", 5) != 0)
			return false;

		for (int I = 0; I < 3; ++I)
		{
			if (!std::isalpha(static_cast<unsigned char>(S1[I])))
				return false;
		}

		return true;
	}
};
