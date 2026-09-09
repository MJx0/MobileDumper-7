#include "Generator.h"

#include <algorithm>
#include <ctime>
#include <fstream>

#include "../../Architecture/IArchDecoder.h"
#include "../../Engine/OffsetFinder/Layouts.h"
#include "../../Engine/OffsetFinder/Offsets.h"
#include "../../Engine/Unreal/NameArray.h"
#include "../../Engine/Unreal/ObjectArray.h"
#include "../../Memory/IMemory.h"
#include "../../Profile/IProfile.h"
#include "../../Utils/Json/json.hpp"
#include "../../Utils/Logger.h"
#include "../../Utils/Utils.h"

#include "../HashStringTable.h"
#include "../Managers/EnumManager.h"
#include "../Managers/MemberManager.h"
#include "../Managers/PackageManager.h"
#include "../Managers/StructManager.h"

bool Generator::InitUnrealModule(std::string& OutErrorString)
{
	int Retries       = 5;
	int RetryAfterSec = 5;
	ModuleInfo ModInfo{};
	do
	{
		ModInfo = GMemory->GetUnrealModule();
		if (ModInfo.IsValid())
			break;

		GLogger.FmtWrite(ELogLevel::Warning, "Failed to find Unreal Engine module, retrying in {} seconds...", RetryAfterSec);
		sleep(RetryAfterSec);
	} while (--Retries > 0);

	if (!ModInfo.IsValid())
	{
		OutErrorString = fmt::format("Failed to find Unreal Engine module after {} attempts!", Retries);
		GLogger.FmtWrite(ELogLevel::Error, "{}\n", OutErrorString);
		return false;
	}

	GLogger.FmtWrite(ELogLevel::Info, "Path: {}\n", ModInfo.GetPathName());
	GLogger.FmtWrite(ELogLevel::Info, "Start: 0x{:X}\n", ModInfo.GetStart());
	GLogger.FmtWrite(ELogLevel::Info, "End: 0x{:X}\n", ModInfo.GetEnd());
	GLogger.FmtWrite(ELogLevel::Info, "Base: 0x{:X}\n", ModInfo.GetBase());

	if (ModInfo.GetSegments().empty())
	{
		OutErrorString = "Failed to aquire UnrealEngine segments!";
		GLogger.FmtWrite(ELogLevel::Error, "{}\n", OutErrorString);
		return false;
	}

	const auto Segments = ModInfo.GetSegments();
	GLogger.FmtWrite(ELogLevel::Info, "Segments ({}):\n", Segments.size());
	for (size_t i = 0; i < ModInfo.GetSegments().size(); i++)
	{
		GLogger.FmtWrite(ELogLevel::Info, "[{:02}]: {}\n", i, Segments[i].ToString());
	}

	return true;
}

bool Generator::InitUEAnalyzerKitty(std::string& OutErrorString)
{
	UEAnalyzerKitty::AnalyzerOptions Options;
	Options.ThreadMode = UEAnalyzerKitty::EThreadMode::Two;
	Options.Targets    = {UEAnalyzerKitty::Targets::Names, UEAnalyzerKitty::Targets::GUObjectArray, UEAnalyzerKitty::Targets::ObjObjects};

	Analyzer = UEAnalyzerKitty::UEAnalyzer::Analyze(GMemory.get(), GArchDecoder.get(), Options);
	if (!Analyzer.IsValid())
	{
		OutErrorString = Analyzer.GetError();
		GLogger.FmtWrite(ELogLevel::Error, "UEAnalyzerKitty failed: \"{}\".\n", OutErrorString);
		return false;
	}

	InternalSettings::bUseChar16String = Analyzer.GetTCharKind() != UEAnalyzerKitty::ETCharKind::Char32;

	GLogger.FmtWrite(ELogLevel::Info, "Game build is likely using UTF{} FName strings.\n", InternalSettings::bUseChar16String ? "16" : "32");

	return true;
}

bool Generator::InitObjects(std::string& OutErrorString)
{
	ObjectArray::SetDecryptObjectItemFn([](uintptr_t& Item)
	{
		GProfile->DecryptObjectItem(Item);
	});

	// Resolves and then independently validates one interpretation of a candidate.
	// Every stage reports its outcome, so a failing run shows exactly how far it got.
	auto TryGObjectsAt = [](uintptr_t ArrayAddress, const char* Interpretation) -> bool
	{
		if (!GProfile->ResolveGObjectsLayout(ArrayAddress, GLayouts.ObjectsLayout))
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [{}] Resolve failed - no layout could be derived at 0x{:X}\n", Interpretation, ArrayAddress);
			return false;
		}

		if (!GLayouts.ObjectsLayout)
		{
			GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: [{}] Resolve succeeded but left the layout null!\n", Interpretation);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [{}] Resolve succeeded, layout is {}\n", Interpretation, GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked ? "Chunked" : "Fixed");

		const LayoutDetection::FObjectsTestResult Test = LayoutDetection::TestObjectsLayout(ArrayAddress, GLayouts.ObjectsLayout.get());

		for (const std::string& Line : Test.Details)
			GLogger.FmtWrite(ELogLevel::Debug, "{}\n", Line);

		for (const std::string& Line : Test.Evidence)
			GLogger.FmtWrite(ELogLevel::Info, "{}\n", Line);

		if (!Test.bValid)
		{
			for (const std::string& Line : Test.Failures)
				GLogger.FmtWrite(ELogLevel::Info, "{}\n", Line);

			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [{}] Validation failed ({}/{} samples, {:.1f}%)\n", Interpretation, Test.SamplesValid, Test.SamplesTested, Test.Confidence * 100.0);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [{}] Validation passed ({}/{} samples, {:.1f}%)\n", Interpretation, Test.SamplesValid, Test.SamplesTested, Test.Confidence * 100.0);
		return true;
	};

	auto InitGObjectsVars = [&TryGObjectsAt](uintptr_t Address, const std::string& FindMethod, float Confidence) -> bool
	{
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Testing GObjects candidate 0x{:X} ({} confidence {:.1f}%)...\n", Address, FindMethod, Confidence * 100.0f);

		uintptr_t ObjectsOffset = GMemory->GetUnrealModule().AddressToOffset(Address);
		uintptr_t DecAddress    = Address;
		GProfile->DecryptGObjects(DecAddress);
		if (DecAddress != Address)
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Decrypted GObjects pointer to 0x{:X}.\n", DecAddress);
			Address = DecAddress;
		}

		if (TryGObjectsAt(Address, "Direct"))
		{
			GObjects                       = Address;
			GInSDKOffsets.Statics.GObjects = ObjectsOffset;
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: GObjects accepted at 0x{:X} (Direct)\n", GObjects);
			return true;
		}

		const uintptr_t CandidateDeref = GMemory->Read<uintptr_t>(Address);
		if (!GMemory->IsAddressReadable(CandidateDeref))
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [Dereference] Skipped - 0x{:X} does not hold a readable pointer.\n", Address);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Dereferencing 0x{:X} -> 0x{:X}\n", Address, CandidateDeref);

		if (TryGObjectsAt(CandidateDeref, "Dereference"))
		{
			GObjects                       = CandidateDeref;
			GInSDKOffsets.Statics.GObjects = ObjectsOffset;
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: GObjects accepted at 0x{:X} (Dereference of 0x{:X})\n", GObjects, Address);
			return true;
		}

		return false;
	};

	GObjects = GProfile->GetGObjects();

	bool bSuccess = false;

	if (GMemory->IsAddressReadable(GObjects))
	{
		bSuccess = InitGObjectsVars(GObjects, "User Profile", 1.00f);
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: IProfile::GetGObjects() returned 0, falling back to UEAnalyzerKitty...\n");

		auto Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::GUObjectArray);
		if (Result.Candidates.empty())
		{
			GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: No GUObjectArray candidates found, retrying with ObjObjects...\n");
			Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::ObjObjects);
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: UEAnalyzerKitty returned {} GObjects candidate(s).\n", Result.Candidates.size());

		for (const auto& Candidate : Result.Candidates)
		{
			if (InitGObjectsVars(Candidate.Address, "KittyUEAnalyzer", Candidate.Confidence))
			{
				bSuccess = true;
				break;
			}
		}
	}

	if (!bSuccess)
	{
		OutErrorString = "Failed to find a valid GObjects!";
		GLogger.FmtWrite(ELogLevel::Error, "InitObjects: {}\n", OutErrorString);
		return false;
	}

	GLogger.FmtWrite(ELogLevel::Info, "GObjects pointer: 0x{:X}\n", GObjects);
	GLogger.FmtWrite(ELogLevel::Info, "GObjects offset: 0x{:X}\n", GInSDKOffsets.Statics.GObjects);

	if (GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked)
	{
		const FChunkedUObjectArrayLayout* L = static_cast<const FChunkedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "GObjects layout type: FChunkedFixedUObjectArray\n");
		GLogger.FmtWrite(ELogLevel::Info, "FChunkedFixedUObjectArray::Objects: 0x{:X} -> 0x{:X}\n", (uint32_t)L->Objects, GMemory->Read<uintptr_t>(GObjects + L->Objects));
		if (L->MaxElements != -1)
			GLogger.FmtWrite(ELogLevel::Info, "FChunkedFixedUObjectArray::MaxElements: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->MaxElements, GMemory->Read<int32>(GObjects + L->MaxElements));
		GLogger.FmtWrite(ELogLevel::Info, "FChunkedFixedUObjectArray::NumElements: 0x{:X} -> 0x{:X}\n", (uint32_t)L->NumElements, GMemory->Read<int32>(GObjects + L->NumElements));
		if (L->MaxChunks != -1)
			GLogger.FmtWrite(ELogLevel::Info, "FChunkedFixedUObjectArray::MaxChunks: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->MaxChunks, GMemory->Read<int32>(GObjects + L->MaxChunks));
		if (L->NumChunks != -1)
			GLogger.FmtWrite(ELogLevel::Info, "FChunkedFixedUObjectArray::NumChunks: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->NumChunks, GMemory->Read<int32>(GObjects + L->NumChunks));
		GLogger.FmtWrite(ELogLevel::Info, "FChunkedFixedUObjectArray::ElementsPerChunk: 0x{:X}\n", (uint32_t)L->ElementsPerChunk);
		GLogger.FmtWrite(ELogLevel::Info, "FUObjectItem::Object: 0x{:X}\n", (uint32_t)L->FUObjectItem.Object);
		GLogger.FmtWrite(ELogLevel::Info, "FUObjectItem::Size: 0x{:X}\n", (uint32_t)L->FUObjectItem.Size);
	}
	else
	{
		const FFixedUObjectArrayLayout* L = static_cast<const FFixedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "GObjects layout type: FFixedUObjectArray\n");
		GLogger.FmtWrite(ELogLevel::Info, "FFixedUObjectArray::Objects: 0x{:X} -> 0x{:X}\n", (uint32_t)L->Objects, GMemory->Read<uintptr_t>(GObjects + L->Objects));
		if (L->MaxObjects != -1)
			GLogger.FmtWrite(ELogLevel::Info, "FFixedUObjectArray::MaxObjects: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->MaxObjects, GMemory->Read<int32>(GObjects + L->MaxObjects));
		GLogger.FmtWrite(ELogLevel::Info, "FFixedUObjectArray::NumObjects: 0x{:X} -> 0x{:X}\n", (uint32_t)L->NumObjects, GMemory->Read<int32>(GObjects + L->NumObjects));
		GLogger.FmtWrite(ELogLevel::Info, "FUObjectItem::Object: 0x{:X}\n", (uint32_t)L->FUObjectItem.Object);
		GLogger.FmtWrite(ELogLevel::Info, "FUObjectItem::Size: 0x{:X}\n", (uint32_t)L->FUObjectItem.Size);
	}

	ObjectArray::SetByIndexFn([](int32 Index) -> uintptr_t
	{
		return GProfile->GetObjectByIndex(GObjects, GLayouts.ObjectsLayout, Index);
	});

	return true;
}

bool Generator::InitNames(std::string& OutErrorString)
{
	NameArray::SetDecryptUTF8Fn([](char* Data, int Len)
	{
		GProfile->DecryptUTF8(Data, Len);
	});

	NameArray::SetDecryptUTF16Fn([](char16_t* Data, int Len)
	{
		GProfile->DecryptUTF16(Data, Len);
	});

	NameArray::SetDecryptUTF32Fn([](char32_t* Data, int Len)
	{
		GProfile->DecryptUTF32(Data, Len);
	});

	NameArray::SetDecryptNameChunkFn([](uintptr_t& ChunkAddr)
	{
		GProfile->DecryptNameChunk(GNames, GLayouts.NamesLayout, ChunkAddr);
	});

	NameArray::SetDecryptNameEntryFn([](uintptr_t& NameEntry)
	{
		GProfile->DecryptNameEntry(GNames, GLayouts.NamesLayout, NameEntry);
	});

	// Resolves and then independently validates one interpretation of a candidate.
	// Every stage reports its outcome, so a failing run shows exactly how far it got.
	auto TryGNamesAt = [](uintptr_t NamesAddress, const char* Interpretation) -> bool
	{
		if (!GProfile->ResolveGNamesLayout(NamesAddress, GLayouts.NamesLayout))
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitNames: [{}] Resolve failed - no layout could be derived at 0x{:X}\n", Interpretation, NamesAddress);
			return false;
		}

		if (!GLayouts.NamesLayout)
		{
			GLogger.FmtWrite(ELogLevel::Warning, "InitNames: [{}] Resolve succeeded but left the layout null!\n", Interpretation);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitNames: [{}] Resolve succeeded, layout is {}\n", Interpretation, GLayouts.NamesLayout->GetType() == ENamesType::Pool ? "Pool" : "Array");

		const LayoutDetection::FNamesTestResult Test = LayoutDetection::TestNamesLayout(NamesAddress, GLayouts.NamesLayout.get());

		for (const std::string& Line : Test.Details)
			GLogger.FmtWrite(ELogLevel::Debug, "{}\n", Line);

		for (const std::string& Line : Test.Evidence)
			GLogger.FmtWrite(ELogLevel::Info, "{}\n", Line);

		if (!Test.bValid)
		{
			for (const std::string& Line : Test.Failures)
				GLogger.FmtWrite(ELogLevel::Info, "{}\n", Line);

			GLogger.FmtWrite(ELogLevel::Info, "InitNames: [{}] Validation failed ({}/{} entries, {:.1f}%)\n", Interpretation, Test.SamplesValid, Test.SamplesTested, Test.Confidence * 100.0);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitNames: [{}] Validation passed ({}/{} entries, {:.1f}%)\n", Interpretation, Test.SamplesValid, Test.SamplesTested, Test.Confidence * 100.0);
		return true;
	};

	auto InitGNamesVars = [&TryGNamesAt](uintptr_t Address, const std::string& FindMethod, float Confidence) -> bool
	{
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Testing GNames candidate 0x{:X} ({} confidence {:.1f}%)...\n", Address, FindMethod, Confidence * 100.0f);

		uintptr_t NamesOffset = GMemory->GetUnrealModule().AddressToOffset(Address);
		uintptr_t DecAddress  = Address;
		GProfile->DecryptGNames(DecAddress);
		if (DecAddress != Address)
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitNames: Decrypted GNames pointer to 0x{:X}.\n", DecAddress);
			Address = DecAddress;
		}

		if (TryGNamesAt(Address, "Direct"))
		{
			GNames                       = Address;
			GInSDKOffsets.Statics.GNames = NamesOffset;
			GLogger.FmtWrite(ELogLevel::Info, "InitNames: GNames accepted at 0x{:X} (Direct)\n", GNames);
			return true;
		}

		const uintptr_t CandidateDeref = GMemory->Read<uintptr_t>(Address);
		if (!GMemory->IsAddressReadable(CandidateDeref))
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitNames: [Dereference] Skipped - 0x{:X} does not hold a readable pointer.\n", Address);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Dereferencing 0x{:X} -> 0x{:X}\n", Address, CandidateDeref);

		if (TryGNamesAt(CandidateDeref, "Dereference"))
		{
			GNames                       = CandidateDeref;
			GInSDKOffsets.Statics.GNames = NamesOffset;
			GLogger.FmtWrite(ELogLevel::Info, "InitNames: GNames accepted at 0x{:X} (Dereference of 0x{:X})\n", GNames, Address);
			return true;
		}

		return false;
	};

	GNames = GProfile->GetGNames();

	bool bSuccess = false;

	if (GMemory->IsAddressReadable(GNames))
	{
		bSuccess = InitGNamesVars(GNames, "User Profile", 1.00f);
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitNames: IProfile::GetGNames() returned 0, falling back to UEAnalyzerKitty...\n");

		auto Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::Names);

		GLogger.FmtWrite(ELogLevel::Info, "InitNames: UEAnalyzerKitty returned {} GNames candidate(s).\n", Result.Candidates.size());

		for (const auto& Candidate : Result.Candidates)
		{
			if (InitGNamesVars(Candidate.Address, "KittyUEAnalyzer", Candidate.Confidence))
			{
				bSuccess = true;
				break;
			}
		}
	}

	if (!bSuccess)
	{
		OutErrorString = "Failed to find a valid GNames!";
		GLogger.FmtWrite(ELogLevel::Error, "InitNames: {}\n", OutErrorString);
		return false;
	}

	GLogger.FmtWrite(ELogLevel::Info, "GNames pointer: 0x{:X}\n", GNames);
	GLogger.FmtWrite(ELogLevel::Info, "GNames offset: 0x{:X}\n", GInSDKOffsets.Statics.GNames);

	if (GLayouts.NamesLayout->GetType() == ENamesType::Pool)
	{
		InternalSettings::bUseNamePool = true;

		const FNamePoolLayout* L = static_cast<const FNamePoolLayout*>(GLayouts.NamesLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "GNames layout type: FNamePool\n");
		GLogger.FmtWrite(ELogLevel::Info, "FNamePool::BlocksBit: 0x{:X}\n", (uint32_t)L->BlocksBit);
		{
			if (L->MaxChunkIndex != -1)
				GLogger.FmtWrite(ELogLevel::Info, "FNamePool::MaxChunkIndex: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->MaxChunkIndex, GMemory->Read<int32>(GNames + L->MaxChunkIndex));
			if (L->ByteCursor != -1)
				GLogger.FmtWrite(ELogLevel::Info, "FNamePool::ByteCursor: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->ByteCursor, GMemory->Read<int32>(GNames + L->ByteCursor));
		}
		GLogger.FmtWrite(ELogLevel::Info, "FNamePool::Blocks: 0x{:X} -> 0x{:X}\n", (uint32_t)L->Blocks, GMemory->Read<uintptr_t>(GNames + L->Blocks));
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::Stride: 0x{:X}\n", (uint32_t)L->FNameEntry.Stride);
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::Header: 0x{:X}\n", (uint32_t)L->FNameEntry.Header);
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::String: 0x{:X}\n", (uint32_t)L->FNameEntry.String);
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::NameWideMask: 0x{:X}\n", (uint32_t)L->FNameEntry.NameWideMask);
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::LengthShiftCount: 0x{:X}\n", (uint32_t)L->FNameEntry.LengthShiftCount);
	}
	else
	{
		const FNameArrayLayout* L = static_cast<const FNameArrayLayout*>(GLayouts.NamesLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "GNames layout type: TNameArray\n");
		GLogger.FmtWrite(ELogLevel::Info, "TNameArray::ElementsPerChunk: 0x{:X}\n", (uint32_t)L->ElementsPerChunk);
		GLogger.FmtWrite(ELogLevel::Info, "TNameArray::Chunks: 0x{:X} -> 0x{:X}\n", (uint32_t)L->Chunks, GMemory->Read<uintptr_t>(GNames + L->Chunks));
		{
			if (L->NumElements != -1)
				GLogger.FmtWrite(ELogLevel::Info, "TNameArray::NumElements: 0x{:X} -> 0x{:X} (Optional)\n", (uint32_t)L->NumElements, GMemory->Read<int32>(GNames + L->NumElements));
		}
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::String: 0x{:X}\n", (uint32_t)L->FNameEntry.String);
		GLogger.FmtWrite(ELogLevel::Info, "FNameEntry::Index: 0x{:X}\n", (uint32_t)L->FNameEntry.Index);
	}

	NameArray::SetByIndexFn([](int32 Index) -> uintptr_t
	{
		return GProfile->GetNameEntryByIndex(GNames, GLayouts.NamesLayout, Index);
	});

	FNameEntry::SetGetStrFn([](uintptr_t NameEntry) -> std::wstring
	{
		return GProfile->GetNameEntryString(GNames, GLayouts.NamesLayout, NameEntry);
	});

	{
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Testing IProfile::GetNameEntryByIndex and GetNameEntryString...\n");
		std::string FirstEntryStr = NameArray::GetNameEntry(0).GetString();
		if (FirstEntryStr != "None")
		{
			OutErrorString = "First name entry does not decode to \"None\". Check the GNames pointer, GNames layout, and IProfile::GetNameEntryByIndex/GetNameEntryString implementations.";
			GLogger.FmtWrite(ELogLevel::Error, "InitNames: {}\n", OutErrorString);
			return false;
		}
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Printing the first 10 name entries...\n");
		for (int i = 0; i <= 10; i++)
		{
			std::string EntryStr = NameArray::GetNameEntry(i).GetString();
			if (EntryStr.length() <= 0)
				continue;

			GLogger.FmtWrite(ELogLevel::Info, "[{:02}] {}\n", i, EntryStr);
		}
	}

	return true;
}

void Generator::ReleaseAnalyzer()
{
	Analyzer = UEAnalyzerKitty::UEAnalyzer{};
}

bool Generator::InitSettings(std::string& OutErrorString)
{
	((void)OutErrorString);

	GProfile->OverrideSettings(GSettings);

	GLogger.FmtWrite(ELogLevel::Info, "General::MaxFNameLen: {}\n", GSettings.General.MaxFNameLen);
	GLogger.FmtWrite(ELogLevel::Info, "Generator::ZipCompressionLevel: {}\n", GSettings.Generator.ZipCompressionLevel);
	GLogger.FmtWrite(ELogLevel::Info, "EngineCore::bEnableEncryptedObjectPropertySupport: {}\n", GSettings.EngineCore.bEnableEncryptedObjectPropertySupport);
	GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::UseMemberCustomSpacing: {}\n", GSettings.CppGenerator.UseMemberCustomSpacing);
	if (GSettings.CppGenerator.UseMemberCustomSpacing)
	{
		GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::MemberTypeColumnWidth: {}\n", GSettings.CppGenerator.MemberTypeColumnWidth);
		GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::MemberNameColumnWidth: {}\n", GSettings.CppGenerator.MemberNameColumnWidth);
	}
	GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::SDKNamespaceName: {}\n", GSettings.CppGenerator.SDKNamespaceName.empty() ? "(global)" : GSettings.CppGenerator.SDKNamespaceName);
	GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::XORString: {}\n", GSettings.CppGenerator.XORString.empty() ? "(none)" : GSettings.CppGenerator.XORString);
	GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::bForceNoGWorldInSDK: {}\n", GSettings.CppGenerator.bForceNoGWorldInSDK);
	GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::bAddManualOverrideOptions: {}\n", GSettings.CppGenerator.bAddManualOverrideOptions);
	GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::bAddFinalSpecifier: {}\n", GSettings.CppGenerator.bAddFinalSpecifier);
	GLogger.FmtWrite(ELogLevel::Info, "MappingGenerator::bShouldCheckForDuplicatedNames: {}\n", GSettings.MappingGenerator.bShouldCheckForDuplicatedNames);
	GLogger.FmtWrite(ELogLevel::Info, "MappingGenerator::bExcludeEditorOnlyProperties: {}\n", GSettings.MappingGenerator.bExcludeEditorOnlyProperties);
	GLogger.FmtWrite(ELogLevel::Info, "Debug::bGenerateAssertionFile: {}\n", GSettings.Debug.bGenerateAssertionFile);

	return true;
}

bool Generator::InitOffsets(std::string& OutErrorString)
{
	GProfile->OverrideInGenOffsets(GOffsets);

	if (!GOffsets.Init(OutErrorString))
		return false;

	GLogger.FmtWrite(ELogLevel::Info, "=== FInGenOffsets ===\n");

	if (InternalSettings::bUseFProperty)
	{
		GLogger.FmtWrite(ELogLevel::Info, "FField::Vft: 0x{:X}\n", (uint32_t)GOffsets.FField.Vft);
		GLogger.FmtWrite(ELogLevel::Info, "FField::Class: 0x{:X}\n", (uint32_t)GOffsets.FField.Class);
		GLogger.FmtWrite(ELogLevel::Info, "FField::Owner: 0x{:X}\n", (uint32_t)GOffsets.FField.Owner);
		GLogger.FmtWrite(ELogLevel::Info, "FField::Next: 0x{:X}\n", (uint32_t)GOffsets.FField.Next);
		GLogger.FmtWrite(ELogLevel::Info, "FField::Name: 0x{:X}\n", (uint32_t)GOffsets.FField.Name);
		if (GOffsets.FField.EditorOnlyMetadata != -1)
			GLogger.FmtWrite(ELogLevel::Info, "FField::EditorOnlyMetadata: 0x{:X}\n", (uint32_t)GOffsets.FField.EditorOnlyMetadata);

		GLogger.FmtWrite(ELogLevel::Info, "FFieldClass::Name: 0x{:X}\n", (uint32_t)GOffsets.FFieldClass.Name);
		GLogger.FmtWrite(ELogLevel::Info, "FFieldClass::CastFlags: 0x{:X}\n", (uint32_t)GOffsets.FFieldClass.CastFlags);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "FName::CompIdx: 0x{:X}\n", (uint32_t)GOffsets.FName.CompIdx);
		GLogger.FmtWrite(ELogLevel::Info, "FName::Number: 0x{:X}\n", (uint32_t)GOffsets.FName.Number);
		GLogger.FmtWrite(ELogLevel::Info, "FName::SizeOf: 0x{:X}\n", (uint32_t)GOffsets.FName.SizeOf);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "UObject::Vft: 0x{:X}\n", (uint32_t)GOffsets.UObject.Vft);
		GLogger.FmtWrite(ELogLevel::Info, "UObject::Flags: 0x{:X}\n", (uint32_t)GOffsets.UObject.Flags);
		GLogger.FmtWrite(ELogLevel::Info, "UObject::Index: 0x{:X}\n", (uint32_t)GOffsets.UObject.Index);
		GLogger.FmtWrite(ELogLevel::Info, "UObject::Class: 0x{:X}\n", (uint32_t)GOffsets.UObject.Class);
		GLogger.FmtWrite(ELogLevel::Info, "UObject::Name: 0x{:X}\n", (uint32_t)GOffsets.UObject.Name);
		GLogger.FmtWrite(ELogLevel::Info, "UObject::Outer: 0x{:X}\n", (uint32_t)GOffsets.UObject.Outer);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "UField::Next: 0x{:X}\n", (uint32_t)GOffsets.UField.Next);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "UEnum::Names: 0x{:X}\n", (uint32_t)GOffsets.UEnum.Names);
		if (InternalSettings::bHasUnderlayingTypeInUEnum)
			GLogger.FmtWrite(ELogLevel::Info, "UEnum::UnderlyingType: 0x{:X}\n", (uint32_t)GOffsets.UEnum.UnderlyingType);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "UStruct::StructBaseChain: 0x{:X}\n", (uint32_t)GOffsets.UStruct.StructBaseChain);
		GLogger.FmtWrite(ELogLevel::Info, "UStruct::SuperStruct: 0x{:X}\n", (uint32_t)GOffsets.UStruct.SuperStruct);
		GLogger.FmtWrite(ELogLevel::Info, "UStruct::Children: 0x{:X}\n", (uint32_t)GOffsets.UStruct.Children);

		if (InternalSettings::bUseFProperty)
			GLogger.FmtWrite(ELogLevel::Info, "UStruct::ChildProperties: 0x{:X}\n", (uint32_t)GOffsets.UStruct.ChildProperties);

		GLogger.FmtWrite(ELogLevel::Info, "UStruct::Size: 0x{:X}\n", (uint32_t)GOffsets.UStruct.Size);
		GLogger.FmtWrite(ELogLevel::Info, "UStruct::MinAlignment: 0x{:X}\n", (uint32_t)GOffsets.UStruct.MinAlignment);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "UFunction::FunctionFlags: 0x{:X}\n", (uint32_t)GOffsets.UFunction.FunctionFlags);
		GLogger.FmtWrite(ELogLevel::Info, "UFunction::NumParams: 0x{:X}\n", (uint32_t)GOffsets.UFunction.NumParams);
		GLogger.FmtWrite(ELogLevel::Info, "UFunction::ParamSize: 0x{:X}\n", (uint32_t)GOffsets.UFunction.ParamSize);
		GLogger.FmtWrite(ELogLevel::Info, "UFunction::ExecFunction: 0x{:X}\n", (uint32_t)GOffsets.UFunction.ExecFunction);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "UClass::CastFlags: 0x{:X}\n", (uint32_t)GOffsets.UClass.CastFlags);
		GLogger.FmtWrite(ELogLevel::Info, "UClass::ClassDefaultObject: 0x{:X}\n", (uint32_t)GOffsets.UClass.ClassDefaultObject);
		GLogger.FmtWrite(ELogLevel::Info, "UClass::ImplementedInterfaces: 0x{:X}\n", (uint32_t)GOffsets.UClass.ImplementedInterfaces);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "Property::ArrayDim: 0x{:X}\n", (uint32_t)GOffsets.Property.ArrayDim);
		GLogger.FmtWrite(ELogLevel::Info, "Property::ElementSize: 0x{:X}\n", (uint32_t)GOffsets.Property.ElementSize);
		GLogger.FmtWrite(ELogLevel::Info, "Property::PropertyFlags: 0x{:X}\n", (uint32_t)GOffsets.Property.PropertyFlags);
		GLogger.FmtWrite(ELogLevel::Info, "Property::Offset_Internal: 0x{:X}\n", (uint32_t)GOffsets.Property.Offset_Internal);
	}

	{
		GLogger.FmtWrite(ELogLevel::Info, "ByteProperty::Enum: 0x{:X}\n", (uint32_t)GOffsets.ByteProperty.Enum);
		GLogger.FmtWrite(ELogLevel::Info, "BoolProperty::Base: 0x{:X}\n", (uint32_t)GOffsets.BoolProperty.Base);
		GLogger.FmtWrite(ELogLevel::Info, "ObjectProperty::PropertyClass: 0x{:X}\n", (uint32_t)GOffsets.ObjectProperty.PropertyClass);
		GLogger.FmtWrite(ELogLevel::Info, "ClassProperty::MetaClass: 0x{:X}\n", (uint32_t)GOffsets.ClassProperty.MetaClass);
		GLogger.FmtWrite(ELogLevel::Info, "StructProperty::Struct: 0x{:X}\n", (uint32_t)GOffsets.StructProperty.Struct);
		GLogger.FmtWrite(ELogLevel::Info, "ArrayProperty::Inner: 0x{:X}\n", (uint32_t)GOffsets.ArrayProperty.Inner);
		GLogger.FmtWrite(ELogLevel::Info, "DelegateProperty::SignatureFunction: 0x{:X}\n", (uint32_t)GOffsets.DelegateProperty.SignatureFunction);
		GLogger.FmtWrite(ELogLevel::Info, "MapProperty::Base: 0x{:X}\n", (uint32_t)GOffsets.MapProperty.Base);
		GLogger.FmtWrite(ELogLevel::Info, "SetProperty::ElementProp: 0x{:X}\n", (uint32_t)GOffsets.SetProperty.ElementProp);
		GLogger.FmtWrite(ELogLevel::Info, "EnumProperty::Base: 0x{:X}\n", (uint32_t)GOffsets.EnumProperty.Base);
		if (InternalSettings::bUseFProperty)
			GLogger.FmtWrite(ELogLevel::Info, "FieldPathProperty::FieldClass: 0x{:X}\n", (uint32_t)GOffsets.FieldPathProperty.FieldClass);
		GLogger.FmtWrite(ELogLevel::Info, "OptionalProperty::ValueProperty: 0x{:X}\n", (uint32_t)GOffsets.OptionalProperty.ValueProperty);
		GLogger.FmtWrite(ELogLevel::Info, "FInstancedStruct::ScriptStruct: 0x{:X}\n", (uint32_t)GOffsets.FInstancedStruct.ScriptStruct);
		GLogger.FmtWrite(ELogLevel::Info, "FInstancedStruct::StructMemory: 0x{:X}\n", (uint32_t)GOffsets.FInstancedStruct.StructMemory);

		GLogger.FmtWrite(ELogLevel::Info, "Property::SizeOf: 0x{:X}\n", (uint32_t)GOffsets.Property.SizeOf);
	}

	GInSDKOffsets.Init();

	GProfile->OverrideInSKOffsets(GInSDKOffsets);

	GLogger.FmtWrite(ELogLevel::Info, "=== FInSKOffsets ===\n");
	GLogger.FmtWrite(ELogLevel::Info, "GEngine: 0x{:X}\n", GInSDKOffsets.Statics.GEngine);
	GLogger.FmtWrite(ELogLevel::Info, "GWorld: 0x{:X}\n", GInSDKOffsets.Statics.GWorld);
	GLogger.FmtWrite(ELogLevel::Info, "GObjects: 0x{:X}\n", GInSDKOffsets.Statics.GObjects);
	GLogger.FmtWrite(ELogLevel::Info, "GNames: 0x{:X}\n", GInSDKOffsets.Statics.GNames);
	GLogger.FmtWrite(ELogLevel::Info, "PEIndex: {}\n", GInSDKOffsets.Statics.PEIndex);
	GLogger.FmtWrite(ELogLevel::Info, "PEOffset: 0x{:X}\n", GInSDKOffsets.Statics.PEOffset);
	GLogger.FmtWrite(ELogLevel::Info, "FText::TextData: 0x{:X}\n", (uint32_t)GInSDKOffsets.FText.TextData);
	GLogger.FmtWrite(ELogLevel::Info, "FText::Size: 0x{:X}\n", (uint32_t)GInSDKOffsets.FText.Size);
	GLogger.FmtWrite(ELogLevel::Info, "FText::InTextDataString: 0x{:X}\n", (uint32_t)GInSDKOffsets.FText.InTextDataString);
	GLogger.FmtWrite(ELogLevel::Info, "ULevel::Actors: 0x{:X}\n", (uint32_t)GInSDKOffsets.ULevel.Actors);
	GLogger.FmtWrite(ELogLevel::Info, "UDataTable::RowMap: 0x{:X}\n", (uint32_t)GInSDKOffsets.UDataTable.RowMap);
	GLogger.FmtWrite(ELogLevel::Info, "DelegateProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.DelegateProperty.SizeOf);
	if (InternalSettings::bUseFProperty)
		GLogger.FmtWrite(ELogLevel::Info, "FieldPathProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.FieldPathProperty.SizeOf);
	GLogger.FmtWrite(ELogLevel::Info, "MulticastInlineDelegateProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.MulticastInlineDelegateProperty.SizeOf);

	GLogger.FmtWrite(ELogLevel::Info, "Printing first 10 objects...\n");
	{
		for (int i = 0; i < 10; i++)
		{
			GLogger.FmtWrite(ELogLevel::Info, "[{:02}] {}\n", i, ObjectArray::GetByIndex(i).GetName());
		}
	}

	return true;
}

bool Generator::InitInternalSettings(std::string& OutErrorString)
{
	((void)OutErrorString);

	InternalSettings::InitWeakObjectPtrSettings();
	InternalSettings::InitLargeWorldCoordinateSettings();

	InternalSettings::InitObjectPtrPropertySettings();
	InternalSettings::InitArrayDimSizeSettings();

	GLogger.FmtWrite(ELogLevel::Info, "GameName: {}\n", InternalSettings::GameName);
	GLogger.FmtWrite(ELogLevel::Info, "GameVersion: {}\n", InternalSettings::GameVersion);
	GLogger.FmtWrite(ELogLevel::Info, "bIsEnumNameOnly: {}\n", InternalSettings::bIsEnumNameOnly);
	GLogger.FmtWrite(ELogLevel::Info, "bIsSmallEnumValue: {}\n", InternalSettings::bIsSmallEnumValue);
	GLogger.FmtWrite(ELogLevel::Info, "bHasUnderlayingTypeInUEnum: {}\n", InternalSettings::bHasUnderlayingTypeInUEnum);
	GLogger.FmtWrite(ELogLevel::Info, "bIsNewUE5EnumNamesContainer: {}\n", InternalSettings::bIsNewUE5EnumNamesContainer);
	GLogger.FmtWrite(ELogLevel::Info, "bIsWeakObjectPtrWithoutTag: {}\n", InternalSettings::bIsWeakObjectPtrWithoutTag);
	GLogger.FmtWrite(ELogLevel::Info, "bUseFProperty: {}\n", InternalSettings::bUseFProperty);
	GLogger.FmtWrite(ELogLevel::Info, "bUseNamePool: {}\n", InternalSettings::bUseNamePool);
	GLogger.FmtWrite(ELogLevel::Info, "bIsObjectNameBeforeClass: {}\n", InternalSettings::bIsObjectNameBeforeClass);
	GLogger.FmtWrite(ELogLevel::Info, "bUseCasePreservingName: {}\n", InternalSettings::bUseCasePreservingName);
	GLogger.FmtWrite(ELogLevel::Info, "bUseOutlineNumberName: {}\n", InternalSettings::bUseOutlineNumberName);
	GLogger.FmtWrite(ELogLevel::Info, "bIsObjPtrInsteadOfFieldPathProperty: {}\n", InternalSettings::bIsObjPtrInsteadOfFieldPathProperty);
	GLogger.FmtWrite(ELogLevel::Info, "bUseMaskForFieldOwner: {}\n", InternalSettings::bUseMaskForFieldOwner);
	GLogger.FmtWrite(ELogLevel::Info, "bUseLargeWorldCoordinates: {}\n", InternalSettings::bUseLargeWorldCoordinates);
	GLogger.FmtWrite(ELogLevel::Info, "bUseUint8ArrayDim: {}\n", InternalSettings::bUseUint8ArrayDim);
	GLogger.FmtWrite(ELogLevel::Info, "bUseChar16String: {}\n", InternalSettings::bUseChar16String);

	return true;
}

void Generator::InitManagers()
{
	GLogger.FmtWrite(ELogLevel::Info, "(Early) Initializing PackageManager...\n");
	// Initialize PackageManager with all packages, their names, structs, classes enums, functions and dependencies
	PackageManager::Init();

	GLogger.FmtWrite(ELogLevel::Info, "Initializing StructManager...\n");
	// Initialize StructManager with all structs and their names
	StructManager::Init();

	GLogger.FmtWrite(ELogLevel::Info, "Initializing EnumManager...\n");
	// Initialize EnumManager with all enums and their names
	EnumManager::Init();

	GLogger.FmtWrite(ELogLevel::Info, "Initializing MemberManager...\n");
	// Initialized all Member-Name collisions
	MemberManager::Init();

	GLogger.FmtWrite(ELogLevel::Info, "(Late) Initializing PackageManager...\n");
	// Post-Initialize PackageManager after StructManager has been initialized. 'PostInit()' handles Cyclic-Dependencies detection
	PackageManager::PostInit();

	GLogger.FmtWrite(ELogLevel::Info, "Managers Initialized.\n");
}

bool Generator::SetupDumperFolder()
{
	try
	{
		std::string FolderGameName    = InternalSettings::GameName;
		std::string FolderGameVersion = InternalSettings::GameVersion;

		Utils::FileNameHelper::MakeValidFileName(FolderGameName);
		Utils::FileNameHelper::MakeValidFileName(FolderGameVersion);

		std::time_t Now = std::time(nullptr);
		std::tm TmBuf{};
		localtime_r(&Now, &TmBuf);
		char TimestampBuf[64];
		memset(TimestampBuf, 0, sizeof(TimestampBuf));
		std::strftime(TimestampBuf, sizeof(TimestampBuf), "%Y%m%d-%H%M%S", &TmBuf);

		DumperFolder = fs::path(GSettings.Generator.SDKGenerationPath) / (FolderGameName + "_v" + FolderGameVersion + "_" + TimestampBuf);

		if (fs::exists(DumperFolder))
			fs::remove_all(DumperFolder);

		fs::create_directories(DumperFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		GLogger.FmtWrite(ELogLevel::Info, "Could not create required folders! Info: \n{}\n", fe.what());
		return false;
	}

	return true;
}

bool Generator::ZipDumperFolder(fs::path& OutZipPath)
{
	OutZipPath.clear();

	if (DumperFolder.empty() || !fs::exists(DumperFolder))
	{
		GLogger.FmtWrite(ELogLevel::Error, "ZipDumperFolder: DumperFolder is not set or does not exist.\n");
		return false;
	}

	const fs::path ZipPath = DumperFolder.parent_path() / (DumperFolder.filename().string() + ".zip");

	std::error_code Ec;
	fs::remove(ZipPath, Ec);

	const int32 Level = std::clamp(GSettings.Generator.ZipCompressionLevel, 0, 9);
	const bool bZipOk = Utils::Zip::CreateZipWithDirectory(DumperFolder.string(), Level, ZipPath.string());

	if (bZipOk)
	{
		GLogger.FmtWrite(ELogLevel::Info, "ZipDumperFolder: Created {}\n", ZipPath.string());
		OutZipPath = ZipPath;
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Error, "ZipDumperFolder: Failed to create zip.\n");
	}

	return bZipOk;
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder)
{
	fs::path Dummy;
	std::string EmptyName = "";
	return SetupFolders(FolderName, OutFolder, EmptyName, Dummy);
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder, std::string& SubfolderName, fs::path& OutSubFolder)
{
	Utils::FileNameHelper::MakeValidFileName(FolderName);
	Utils::FileNameHelper::MakeValidFileName(SubfolderName);

	try
	{
		OutFolder    = DumperFolder / FolderName;
		OutSubFolder = OutFolder / SubfolderName;

		if (fs::exists(OutFolder))
		{
			fs::path Old = OutFolder.generic_string() + "_OLD";

			fs::remove_all(Old);

			fs::rename(OutFolder, Old);
		}

		fs::create_directories(OutFolder);

		if (!SubfolderName.empty())
			fs::create_directories(OutSubFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Could not create required folders! Info: \n{}\n", fe.what());
		return false;
	}

	return true;
}

void Generator::GenerateObjectsDump(bool bWithPathname)
{
	std::ofstream DumpStream(DumperFolder / "GObjects-Dump.txt");

	DumpStream << "Object dump by MobileDumper-7\n\n";
	DumpStream << (!InternalSettings::GameVersion.empty() && !InternalSettings::GameName.empty() ? (InternalSettings::GameVersion + '-' + InternalSettings::GameName) + "\n\n" : "");
	DumpStream << "Count: " << ObjectArray::Num() << "\n\n\n";

	for (auto Object : ObjectArray())
	{
		if (!Object.GetAddress())
			continue;

		if (!bWithPathname)
		{
			DumpStream << fmt::format("[{:08X}] {{{}}} {}\n", Object.GetIndex(), Object.GetAddress(), Object.GetFullName());
		}
		else
		{
			DumpStream << fmt::format("[{:08X}] {{{}}} {}\n", Object.GetIndex(), Object.GetAddress(), Object.GetPathName());
		}
	}

	DumpStream.close();
}

void Generator::GenerateObjectsWithPropertiesDump(bool bWithPathname)
{
	std::ofstream DumpStream(DumperFolder / "GObjects-Dump-WithProperties.txt");

	DumpStream << "Object dump by MobileDumper-7\n\n";
	DumpStream << (!InternalSettings::GameVersion.empty() && !InternalSettings::GameName.empty() ? (InternalSettings::GameVersion + '-' + InternalSettings::GameName) + "\n\n" : "");
	DumpStream << "Count: " << ObjectArray::Num() << "\n\n\n";

	for (auto Object : ObjectArray())
	{
		if (!Object.GetAddress())
			continue;

		if (!bWithPathname)
		{
			DumpStream << fmt::format("[{:08X}] {{{}}} {}\n", Object.GetIndex(), Object.GetAddress(), Object.GetFullName());
		}
		else
		{
			DumpStream << fmt::format("[{:08X}] {{{}}} {}\n", Object.GetIndex(), Object.GetAddress(), Object.GetPathName());
		}

		if (Object.IsA(EClassCastFlags::Struct))
		{
			for (UEProperty Prop : Object.Cast<UEStruct>().GetProperties())
			{
				DumpStream << fmt::format("[{:08X}] {{{}}} {} {}\n", Prop.GetOffset(), Prop.GetAddress(), Prop.GetPropClassName(), Prop.GetName());
			}
		}
	}

	DumpStream.close();
}


void Generator::GenerateEditorOnlyMetadataDump()
{
	if (GOffsets.FField.EditorOnlyMetadata == -1)
		return;

	nlohmann::json MetadataJson;
	MetadataJson["GameName"]    = InternalSettings::GameName;
	MetadataJson["GameVersion"] = InternalSettings::GameVersion;

	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (!Obj.IsA(EClassCastFlags::Struct))
			continue;

		UEStruct Struct = Obj.Cast<UEStruct>();

		std::vector<UEProperty> ChildProperties = Struct.GetProperties();
		if (ChildProperties.empty()) // Avoids allocating string for GetCppName() and prevents json from auto-creating empty objects for property-less structs
			continue;

		auto& StructMembers = MetadataJson[Struct.GetCppName()];

		for (UEProperty Prop : ChildProperties)
		{
			auto& Entries = StructMembers[Prop.GetValidName()];

			for (const auto& [Key, Value] : Prop.Cast<UEFField>().GetMetaData())
			{
				if (Key.empty() && Value.empty())
					continue;

				Entries[Key] = Value;
			}
		}
	}

	std::ofstream MetadataFile(DumperFolder / "Metadata.json");
	MetadataFile << MetadataJson.dump(4);
}