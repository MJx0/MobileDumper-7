#pragma once

#include <algorithm>
#include <cstring>
#include <unordered_set>

#include "../../IProfile.h"

// Thanks sunnysharma4002 https://github.com/MJx0/MobileDumper-7/pull/14

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
		return FindGNamesStructuralScan();
	}

	void OverrideSettings(FSettings& Settings) const override
	{
		if (bFoundViaNestedManager)
		{
			Settings.CppGenerator.InSDKUtils.GetGNamesFuncBody = fmt::format(R"({{
    const uintptr_t Base = GetImageBase();

    if (!Base)
        return 0;

    const uintptr_t Manager =
        *reinterpret_cast<uintptr_t*>(Base + Offsets::GNames);

    if (!Manager)
        return 0;

    const uintptr_t NameArray =
        *reinterpret_cast<uintptr_t*>(Manager + {});

    if (!NameArray)
        return 0;

    return *reinterpret_cast<uintptr_t*>(NameArray);
}})",
			                                                                 kNestedGNamesOffset);
		}
	}

	void OverrideDecryptCallbacks(FDecryptCallbacks& Callbacks) const override
	{
		Callbacks.Statics.GNames = [](const FDecryptContext<uintptr_t>& Context)
		{
			return DecryptGNames(Context.RawValue);
		};
	}

private:
#ifdef __LP64__
	static constexpr size_t kNestedGNamesOffset = 0x110;
#else
	static constexpr size_t kNestedGNamesOffset = 0x88;
#endif

	// Set by FindGNamesStructuralScan, read back by OverrideSettings to pick the matching body.
	mutable bool bFoundViaNestedManager = false;

	static uintptr_t DecryptGNames(uintptr_t NamesPtr)
	{
		const uintptr_t Manager = MD7::Globals::GMemory->Read<uintptr_t>(NamesPtr);
		if (MD7::Globals::GMemory->IsAddressReadable(Manager + kNestedGNamesOffset))
		{
			const uintptr_t NameArray = MD7::Globals::GMemory->Read<uintptr_t>(Manager + kNestedGNamesOffset);
			if (MD7::Globals::GMemory->IsAddressReadable(NameArray))
				return NameArray;
		}

		return NamesPtr;
	}

	uintptr_t FindGNamesStructuralScan() const
	{
		constexpr size_t kBlockSize = 1024 * 1024; // 1MB

		std::vector<uint8_t> Block;
		std::unordered_set<uintptr_t> Seen;

		auto IsReadWritePointer = [](uintptr_t Ptr) -> bool
		{
			return Utils::Memory::IsAligned(Ptr, sizeof(uintptr_t)) && MD7::Globals::GMemory->IsAddressReadable(Ptr) && MD7::Globals::GMemory->IsAddressWriteable(Ptr);
		};

		auto LooksLikeEntry0 = [](uintptr_t Entry0) -> bool
		{
			static constexpr uint8_t AnsiNone[5] = {'N', 'o', 'n', 'e', 0};

			for (uintptr_t IndexOffset : {uintptr_t(0), uintptr_t(4), uintptr_t(8)})
			{
				uint32_t Index = 0;
				if (MD7::Globals::GMemory->ReadBytes(Entry0 + IndexOffset, &Index, sizeof(Index)) != sizeof(Index))
					continue;

				if ((Index >> 1) != 0)
					continue;

				uint8_t Text[10]{};
				if (MD7::Globals::GMemory->ReadBytes(Entry0 + IndexOffset + sizeof(uint32_t), Text, sizeof(Text)) != sizeof(Text))
					continue;

				if (std::memcmp(Text, AnsiNone, sizeof(AnsiNone)) == 0)
					return true;
			}

			return false;
		};

		auto LooksLikeNameArrayDirect = [&IsReadWritePointer, &LooksLikeEntry0](uintptr_t Ptr) -> bool
		{
			const uintptr_t Chunk0 = MD7::Globals::GMemory->Read<uintptr_t>(Ptr);
			if (!IsReadWritePointer(Chunk0))
				return false;

			const uintptr_t Entry0 = MD7::Globals::GMemory->Read<uintptr_t>(Chunk0);
			if (!IsReadWritePointer(Entry0))
				return false;

			return LooksLikeEntry0(Entry0);
		};

		auto LooksLikeNameArrayNested = [&IsReadWritePointer, &LooksLikeEntry0](uintptr_t Ptr) -> bool
		{
			const uintptr_t Chunks = MD7::Globals::GMemory->Read<uintptr_t>(Ptr);
			if (!IsReadWritePointer(Chunks))
				return false;

			const uintptr_t Chunk0 = MD7::Globals::GMemory->Read<uintptr_t>(Chunks);
			if (!IsReadWritePointer(Chunk0))
				return false;

			const uintptr_t Entry0 = MD7::Globals::GMemory->Read<uintptr_t>(Chunk0);
			if (!IsReadWritePointer(Entry0))
				return false;

			return LooksLikeEntry0(Entry0);
		};

		for (const auto& Segment : MD7::Globals::GMemory->GetUnrealModule().GetSegments())
		{
			if (!Segment.IsValid() || !Segment.IsReadable() || !Segment.IsWriteable() || Segment.IsExecutable())
				continue;

			const size_t Size = Segment.GetSize();
			for (size_t Offset = 0; Offset + sizeof(uintptr_t) <= Size; Offset += kBlockSize)
			{
				const size_t BlockSize = std::min(kBlockSize, Size - Offset);

				if (BlockSize != Block.size())
					Block.resize(BlockSize);

				if (MD7::Globals::GMemory->ReadBytes(Segment.GetStart() + Offset, Block.data(), BlockSize) != BlockSize)
					continue;

				for (size_t InBlock = 0; InBlock + sizeof(uintptr_t) <= BlockSize; InBlock += sizeof(uintptr_t))
				{
					uintptr_t Value = 0;
					std::memcpy(&Value, Block.data() + InBlock, sizeof(Value));

					if (!IsReadWritePointer(Value))
						continue;

					if (!Seen.insert(Value).second)
						continue;

					const uintptr_t Global = Segment.GetStart() + Offset + InBlock;

					if (LooksLikeNameArrayDirect(Value))
					{
						bFoundViaNestedManager = false;
						MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Structural scan found GNames global at 0x{:X} -> 0x{:X}\n", MD7::Globals::GMemory->GetUnrealModule().AddressToOffset(Global), Global);
						return Global;
					}

					const uintptr_t Nested = MD7::Globals::GMemory->Read<uintptr_t>(Value + kNestedGNamesOffset);
					if (!IsReadWritePointer(Nested))
						continue;

					if (!LooksLikeNameArrayNested(Nested))
						continue;

					bFoundViaNestedManager = true;
					MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GetGNames: Structural scan found nested GNames global at 0x{:X} -> 0x{:X}\n", MD7::Globals::GMemory->GetUnrealModule().AddressToOffset(Global), Global);
					return Global;
				}
			}
		}

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "GetGNames: Structural scan couldn't find GNames global!\n");
		return 0;
	}
};
