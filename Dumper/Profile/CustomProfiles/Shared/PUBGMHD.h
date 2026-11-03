#pragma once

#include <cstring>

#include "PUBG.h"

class PUBGMHDProfile : public PUBGProfile
{
public:
	PUBGMHDProfile() = default;

	std::vector<std::string> GetSupportedGames() const override
	{
		return {"com.tencent.tmgp.pubgmhd"};
	}

	// The 32-bit works fine via the generic detector.
	// These overrides only needed for 64-bit version
#ifdef __LP64__
	uintptr_t GetGObjects() const override
	{
		const uintptr_t Found = FindGObjectsStructuralScan();
		return Found ? Found : IProfile::GetGObjects();
	}

	bool ResolveGObjectsLayout(uintptr_t ObjectsPtr, std::unique_ptr<IObjectsLayout>& Out, std::string* OutError = nullptr) const override
	{
		std::unique_ptr<FFixedUObjectArrayLayout> FixedLayout = MakeFixedObjectsLayout();
		if (LayoutDetection::TestObjectsLayout(ObjectsPtr, FixedLayout.get()).bValid)
		{
			GLogger.FmtWrite(ELogLevel::Info, "ResolveGObjectsLayout: Resolved by manual user layout offsets.\n");
			Out = std::move(FixedLayout);
			return true;
		}
		return IProfile::ResolveGObjectsLayout(ObjectsPtr, Out, OutError);
	}

private:
	static constexpr int32 kFixedObjectsOffset    = 0xC8;
	static constexpr int32 kFixedMaxObjectsOffset = 0xFC;
	static constexpr int32 kFixedNumObjectsOffset = 0x100;
	static constexpr int32 kFixedItemSize         = 0x18;

	static std::unique_ptr<FFixedUObjectArrayLayout> MakeFixedObjectsLayout()
	{
		auto Layout                 = std::make_unique<FFixedUObjectArrayLayout>();
		Layout->Objects             = kFixedObjectsOffset;
		Layout->NumObjects          = kFixedNumObjectsOffset;
		Layout->MaxObjects          = kFixedMaxObjectsOffset;
		Layout->FUObjectItem.Object = 0x0;
		Layout->FUObjectItem.Size   = kFixedItemSize;
		return Layout;
	}

	static uintptr_t FindGObjectsStructuralScan()
	{
		constexpr size_t kBlockSize   = 1024 * 1024; // 1MB
		constexpr size_t kHeaderBytes = kFixedNumObjectsOffset + sizeof(int32);
		constexpr uint64_t kMaxUserVA = 1ull << 48;

		std::vector<uint8_t> Block;

		for (const auto& Segment : GMemory->GetUnrealModule().GetSegments())
		{
			if (!Segment.IsValid() || !Segment.IsReadable() || !Segment.IsWriteable() || Segment.IsExecutable())
				continue;

			const size_t Size = Segment.GetSize();
			for (size_t Offset = 0; Offset + kHeaderBytes <= Size; Offset += kBlockSize)
			{
				const size_t BlockSize = std::min(kBlockSize + kHeaderBytes, Size - Offset);

				if (BlockSize != Block.size())
					Block.resize(BlockSize);

				if (GMemory->ReadBytes(Segment.GetStart() + Offset, Block.data(), BlockSize) != BlockSize)
					continue;

				for (size_t InBlock = 0; InBlock + kHeaderBytes <= BlockSize; InBlock += sizeof(uintptr_t))
				{
					uintptr_t ItemsPtr = 0;
					std::memcpy(&ItemsPtr, Block.data() + InBlock + kFixedObjectsOffset, sizeof(ItemsPtr));

					if (static_cast<uint64_t>(ItemsPtr) < 0x1000 || static_cast<uint64_t>(ItemsPtr) >= kMaxUserVA)
						continue;

					int32 NumObjects = 0;
					std::memcpy(&NumObjects, Block.data() + InBlock + kFixedNumObjectsOffset, sizeof(NumObjects));

					if (NumObjects < LayoutDetection::kMinObjectCount || NumObjects > LayoutDetection::kMaxObjectCount)
						continue;

					const uintptr_t Global                                 = Segment.GetStart() + Offset + InBlock;
					const std::unique_ptr<FFixedUObjectArrayLayout> Layout = MakeFixedObjectsLayout();

					if (!LayoutDetection::TestObjectsLayout(Global, Layout.get()).bValid)
						continue;

					GLogger.FmtWrite(ELogLevel::Info, "GetGObjects: Structural scan found GUObjectArray at 0x{:X} -> 0x{:X}\n", GMemory->GetUnrealModule().AddressToOffset(Global), Global);
					return Global;
				}
			}
		}

		return 0;
	}
#endif
};
