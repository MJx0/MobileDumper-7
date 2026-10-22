#include "Generator.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "../../Architecture/IArchDecoder.h"
#include "../../Engine/OffsetFinder/DecryptCallbacks.h"
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

		GLogger.FmtWrite(ELogLevel::Warning, "Failed to find Unreal Engine module, retrying in {} seconds...\n", RetryAfterSec);
		sleep(RetryAfterSec);
	} while (--Retries > 0);

	if (!ModInfo.IsValid())
	{
		OutErrorString = fmt::format("Failed to find Unreal Engine module after {} attempts!\n", Retries);
		GLogger.FmtWrite(ELogLevel::Error, "{}\n", OutErrorString);
		return false;
	}

	GLogger.FmtWrite(ELogLevel::Info, "Path: {}\n", ModInfo.GetPathName());
	GLogger.FmtWrite(ELogLevel::Info, "Start: 0x{:X}\n", ModInfo.GetStart());
	GLogger.FmtWrite(ELogLevel::Info, "End: 0x{:X}\n", ModInfo.GetEnd());
	GLogger.FmtWrite(ELogLevel::Info, "Base: 0x{:X}\n", ModInfo.GetBase());

	InternalSettings::UnrealModuleFileName = std::filesystem::path(ModInfo.GetPathName()).filename().string();

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
	Options.ThreadMode    = UEAnalyzerKitty::EThreadMode::Two;
	Options.Targets       = {UEAnalyzerKitty::Targets::Names, UEAnalyzerKitty::Targets::GUObjectArray, UEAnalyzerKitty::Targets::ObjObjects};
	Options.MaxCandidates = 50;

	Analyzer = UEAnalyzerKitty::UEAnalyzer::Analyze(GMemory.get(), GArchDecoder.get(), Options);
	if (!Analyzer.IsValid())
	{
		OutErrorString = Analyzer.GetError();
		GLogger.FmtWrite(ELogLevel::Error, "UEAnalyzerKitty failed: \"{}\".\n", OutErrorString);
		return false;
	}

	InternalSettings::bUseChar16String = Analyzer.GetTCharKind() != UEAnalyzerKitty::ETCharKind::Char32;
	GLogger.FmtWrite(ELogLevel::Info, "UE Engine TChar: UTF{}\n", InternalSettings::bUseChar16String ? "16" : "32");

	const UEAnalyzerKitty::FEngineVersionInfo& EngineVersion = Analyzer.GetEngineVersion();
	GLogger.FmtWrite(ELogLevel::Info, "UE Engine Version: {}\n", EngineVersion.ToString());

	if (EngineVersion.IsValid())
	{
		InternalSettings::EngineMajor         = EngineVersion.Major;
		InternalSettings::EngineMinor         = EngineVersion.Minor;
		InternalSettings::EnginePatch         = EngineVersion.Patch;
		InternalSettings::EngineVersionString = EngineVersion.ToString();
	}

	return true;
}

bool Generator::InitObjects(std::string& OutErrorString)
{
	auto TryGObjectsAt = [](uintptr_t ArrayAddress, const char* Interpretation, ELogLevel DiagLevel) -> bool
	{
		const auto Report = [Interpretation, DiagLevel](const std::string& Reason)
		{
			std::istringstream Lines(Reason);
			std::string Line;
			while (std::getline(Lines, Line))
				GLogger.FmtWrite(DiagLevel, "InitObjects: [{}] {}\n", Interpretation, Line);
		};

		std::string ResolveError;
		if (!GProfile->ResolveGObjectsLayout(ArrayAddress, GLayouts.ObjectsLayout, &ResolveError))
		{
			// An overriding profile is free to ignore OutError, so cover an empty reason.
			Report(ResolveError.empty() ? fmt::format("No layout could be derived at 0x{:X}", ArrayAddress) : ResolveError);
			return false;
		}

		// An overriding profile can return true and still leave the layout unusable.
		if (!GLayouts.ObjectsLayout || !GLayouts.ObjectsLayout->IsValid())
		{
			GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: [{}] ResolveGObjectsLayout reported success for 0x{:X} but left the layout incomplete\n", Interpretation, ArrayAddress);
			return false;
		}

		const LayoutDetection::FObjectsTestResult Test = LayoutDetection::TestObjectsLayout(ArrayAddress, GLayouts.ObjectsLayout.get());

		if (!Test.bValid)
		{
			Report(fmt::format("Layout resolved at 0x{:X} but failed validation: {}", ArrayAddress, Test.Summary));
			for (const std::string& Line : Test.Details)
				Report(Line);

			return false;
		}

		for (const std::string& Line : Test.Details)
			GLogger.FmtWrite(ELogLevel::Debug, "InitObjects: [{}] {}\n", Interpretation, Line);

		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [{}] Verified: {}\n", Interpretation, Test.Summary);
		return true;
	};

	auto InitGObjectsVars = [&TryGObjectsAt](uintptr_t Address, const std::string& FindMethod, float Confidence, ELogLevel DiagLevel) -> bool
	{
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Trying candidate 0x{:X} from {} ({:.1f}% confidence)\n", Address, FindMethod, Confidence * 100.0f);

		uintptr_t ObjectsOffset = GMemory->GetUnrealModule().AddressToOffset(Address);
		uintptr_t DecAddress    = Address;
		GProfile->DecryptGObjects(DecAddress);
		if (DecAddress != Address)
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Decrypted 0x{:X} -> 0x{:X}\n", Address, DecAddress);
			Address = DecAddress;
		}

		GObjects = Address;
		if (TryGObjectsAt(GObjects, "Direct", DiagLevel))
		{
			GInSDKOffsets.Statics.GObjects       = ObjectsOffset;
			InternalSettings::bIsGObjectsPointer = false;
			return true;
		}

		GObjects = GMemory->Read<uintptr_t>(GObjects);
		if (!GMemory->IsAddressReadable(GObjects))
		{
			GLogger.FmtWrite(DiagLevel, "InitObjects: [Dereference] Skipped, 0x{:X} does not hold a readable pointer\n", Address);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Debug, "InitObjects: [Dereference] Following 0x{:X} -> 0x{:X}\n", Address, GObjects);

		if (TryGObjectsAt(GObjects, "Dereference", DiagLevel))
		{
			GInSDKOffsets.Statics.GObjects       = ObjectsOffset;
			InternalSettings::bIsGObjectsPointer = true;
			return true;
		}

		return false;
	};

	GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Resolving GObjects...\n");

	GObjects = GProfile->GetGObjects();

	// InitGObjectsVars reassigns GObjects as it tries each interpretation, so the address the
	// profile actually returned has to be kept for the messages below.
	const uintptr_t ProfileObjects = GObjects;

	bool bSuccess = false;

	if (GMemory->IsAddressReadable(ProfileObjects))
	{
		bSuccess = InitGObjectsVars(ProfileObjects, "IProfile::GetGObjects", 1.00f, ELogLevel::Warning);

		if (!bSuccess)
			GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: IProfile::GetGObjects() returned 0x{:X}, but neither it nor its dereference held an object array - falling back to UEAnalyzerKitty\n", ProfileObjects);
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: IProfile::GetGObjects() returned unreadable address 0x{:X} - falling back to UEAnalyzerKitty\n", ProfileObjects);
	}

	if (!bSuccess)
	{
		auto Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::GUObjectArray);
		if (Result.Candidates.empty())
		{
			GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: No GUObjectArray candidate found, retrying as ObjObjects\n");
			Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::ObjObjects);
		}

		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: UEAnalyzerKitty offered {} candidate(s)\n", Result.Candidates.size());

		for (const auto& Candidate : Result.Candidates)
		{
			if (InitGObjectsVars(Candidate.Address, "UEAnalyzerKitty", Candidate.Confidence, ELogLevel::Debug))
			{
				bSuccess = true;
				break;
			}
		}

		if (!bSuccess)
			GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: All UEAnalyzerKitty candidates were rejected\n");
	}

	if (!bSuccess)
	{
		OutErrorString = "Failed to find a valid GObjects!";
		GLogger.FmtWrite(ELogLevel::Error, "InitObjects: {}\n", OutErrorString);
		return false;
	}

	GLogger.FmtWrite(ELogLevel::Info, "InitObjects: GObjects at 0x{:X}, module offset 0x{:X}{}\n", GObjects, GInSDKOffsets.Statics.GObjects, InternalSettings::bIsGObjectsPointer ? ", reached by dereference" : "");

	if (GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked)
	{
		const FChunkedUObjectArrayLayout* L = static_cast<const FChunkedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Layout FChunkedFixedUObjectArray\n");
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   Objects +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Objects, GDecryptCallbacks.ChunkedObjects.Objects({.Address = GObjects + L->Objects, .RawValue = GMemory->Read<uintptr_t>(GObjects + L->Objects)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   NumElements +0x{:03X} -> {}\n", (uint32_t)L->NumElements, GDecryptCallbacks.ChunkedObjects.NumElements({.Address = GObjects + L->NumElements, .RawValue = GMemory->Read<int32>(GObjects + L->NumElements)}));
		if (L->MaxElements != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   MaxElements +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxElements, GDecryptCallbacks.ChunkedObjects.MaxElements({.Address = GObjects + L->MaxElements, .RawValue = GMemory->Read<int32>(GObjects + L->MaxElements)}));
		if (L->NumChunks != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   NumChunks +0x{:03X} -> {} (optional)\n", (uint32_t)L->NumChunks, GDecryptCallbacks.ChunkedObjects.NumChunks({.Address = GObjects + L->NumChunks, .RawValue = GMemory->Read<int32>(GObjects + L->NumChunks)}));
		if (L->MaxChunks != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   MaxChunks +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxChunks, GDecryptCallbacks.ChunkedObjects.MaxChunks({.Address = GObjects + L->MaxChunks, .RawValue = GMemory->Read<int32>(GObjects + L->MaxChunks)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   ElementsPerChunk = 0x{:X}\n", (uint32_t)L->ElementsPerChunk);
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Object +0x{:03X}\n", (uint32_t)L->FUObjectItem.Object);
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Size = 0x{:X}\n", (uint32_t)L->FUObjectItem.Size);
	}
	else
	{
		const FFixedUObjectArrayLayout* L = static_cast<const FFixedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Layout FFixedUObjectArray\n");
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   Objects +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Objects, GDecryptCallbacks.FixedObjects.Objects({.Address = GObjects + L->Objects, .RawValue = GMemory->Read<uintptr_t>(GObjects + L->Objects)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   NumObjects +0x{:03X} -> {}\n", (uint32_t)L->NumObjects, GDecryptCallbacks.FixedObjects.NumObjects({.Address = GObjects + L->NumObjects, .RawValue = GMemory->Read<int32>(GObjects + L->NumObjects)}));
		if (L->MaxObjects != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   MaxObjects +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxObjects, GDecryptCallbacks.FixedObjects.MaxObjects({.Address = GObjects + L->MaxObjects, .RawValue = GMemory->Read<int32>(GObjects + L->MaxObjects)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Object +0x{:03X}\n", (uint32_t)L->FUObjectItem.Object);
		GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Size = 0x{:X}\n", (uint32_t)L->FUObjectItem.Size);
	}

	ObjectArray::SetByIndexFn([](int32 Index) -> uintptr_t
	{
		return GProfile->GetObjectByIndex(GObjects, GLayouts.ObjectsLayout, Index);
	});

	// LayoutDetection::DumpCandidateMemory(GObjects, "GObjects");

	/*
	{
	    for (int i = 0; i < 0x18; i += 8)
	    {
	        if (GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked)
	            reinterpret_cast<FChunkedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get())->FUObjectItem.Object = i;
	        else
	            reinterpret_cast<FFixedUObjectArrayLayout*>(GLayouts.ObjectsLayout.get())->FUObjectItem.Object = i;

	        for (size_t j = sizeof(void*); j < 0x28; j += 4)
	        {
	            uintptr_t Addr = reinterpret_cast<uintptr_t>(ObjectArray::GetByIndex(1).GetAddress());
	            if (!Addr)
	                continue;

	            int Index = GMemory->Read<int32>(Addr + j);
	            GLogger.FmtWrite(ELogLevel::Info, "GetByIndex[0x{:X} + 0x{:X}] = 0x{:X}\n", Addr, j, Index);
	        }
	        GLogger.FmtWrite(ELogLevel::Info, "==============\n");
	    }
	}
	*/

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

	NameArray::SetDecryptNameChunkFn([](int32 ChunkIdx, uintptr_t& ChunkAddr)
	{
		GProfile->DecryptNameChunk(GNames, ChunkIdx, ChunkAddr);
	});

	NameArray::SetDecryptNameEntryFn([](uintptr_t& NameEntry)
	{
		GProfile->DecryptNameEntry(GNames, NameEntry);
	});

	auto TryGNamesAt = [](uintptr_t NamesAddress, const char* Interpretation, ELogLevel DiagLevel) -> bool
	{
		const auto Report = [Interpretation, DiagLevel](const std::string& Reason)
		{
			std::istringstream Lines(Reason);
			std::string Line;
			while (std::getline(Lines, Line))
				GLogger.FmtWrite(DiagLevel, "InitNames: [{}] {}\n", Interpretation, Line);
		};

		std::string ResolveError;
		if (!GProfile->ResolveGNamesLayout(NamesAddress, GLayouts.NamesLayout, &ResolveError))
		{
			// An overriding profile is free to ignore OutError, so cover an empty reason.
			Report(ResolveError.empty() ? fmt::format("No layout could be derived at 0x{:X}", NamesAddress) : ResolveError);
			return false;
		}

		// An overriding profile can return true and still leave the layout unusable.
		if (!GLayouts.NamesLayout || !GLayouts.NamesLayout->IsValid())
		{
			GLogger.FmtWrite(ELogLevel::Warning, "InitNames: [{}] ResolveGNamesLayout reported success for 0x{:X} but left the layout incomplete\n", Interpretation, NamesAddress);
			return false;
		}

		const LayoutDetection::FNamesTestResult Test = LayoutDetection::TestNamesLayout(NamesAddress, GLayouts.NamesLayout.get());

		if (!Test.bValid)
		{
			Report(fmt::format("Layout resolved at 0x{:X} but failed validation: {}", NamesAddress, Test.Summary));
			for (const std::string& Line : Test.Details)
				Report(Line);

			return false;
		}

		for (const std::string& Line : Test.Details)
			GLogger.FmtWrite(ELogLevel::Debug, "InitNames: [{}] {}\n", Interpretation, Line);

		GLogger.FmtWrite(ELogLevel::Info, "InitNames: [{}] Verified: {}\n", Interpretation, Test.Summary);
		return true;
	};

	auto InitGNamesVars = [&TryGNamesAt](uintptr_t Address, const std::string& FindMethod, float Confidence, ELogLevel DiagLevel) -> bool
	{
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Trying candidate 0x{:X} from {} ({:.1f}% confidence)\n", Address, FindMethod, Confidence * 100.0f);

		uintptr_t NamesOffset = GMemory->GetUnrealModule().AddressToOffset(Address);
		uintptr_t DecAddress  = Address;
		GProfile->DecryptGNames(DecAddress);
		if (DecAddress != Address)
		{
			GLogger.FmtWrite(ELogLevel::Info, "InitNames: Decrypted 0x{:X} -> 0x{:X}\n", Address, DecAddress);
			Address = DecAddress;
		}

		GNames = Address;
		if (TryGNamesAt(GNames, "Direct", DiagLevel))
		{
			GInSDKOffsets.Statics.GNames       = NamesOffset;
			InternalSettings::bIsGNamesPointer = false;
			return true;
		}

		GNames = GMemory->Read<uintptr_t>(GNames);
		if (!GMemory->IsAddressReadable(GNames))
		{
			GLogger.FmtWrite(DiagLevel, "InitNames: [Dereference] Skipped, 0x{:X} does not hold a readable pointer\n", Address);
			return false;
		}

		GLogger.FmtWrite(ELogLevel::Debug, "InitNames: [Dereference] Following 0x{:X} -> 0x{:X}\n", Address, GNames);

		if (TryGNamesAt(GNames, "Dereference", DiagLevel))
		{
			GInSDKOffsets.Statics.GNames       = NamesOffset;
			InternalSettings::bIsGNamesPointer = true;
			return true;
		}

		return false;
	};

	GLogger.FmtWrite(ELogLevel::Info, "InitNames: Resolving GNames...\n");

	GNames = GProfile->GetGNames();

	// InitGNamesVars reassigns GNames as it tries each interpretation, so the address the
	// profile actually returned has to be kept for the messages below.
	const uintptr_t ProfileNames = GNames;

	bool bSuccess = false;

	if (GMemory->IsAddressReadable(ProfileNames))
	{
		bSuccess = InitGNamesVars(ProfileNames, "IProfile::GetGNames", 1.00f, ELogLevel::Warning);

		if (!bSuccess)
			GLogger.FmtWrite(ELogLevel::Warning, "InitNames: IProfile::GetGNames() returned 0x{:X}, but neither it nor its dereference held a name table - falling back to UEAnalyzerKitty\n", ProfileNames);
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Warning, "InitNames: IProfile::GetGNames() returned unreadable address 0x{:X} - falling back to UEAnalyzerKitty\n", ProfileNames);
	}

	if (!bSuccess)
	{
		auto Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::Names);

		GLogger.FmtWrite(ELogLevel::Info, "InitNames: UEAnalyzerKitty offered {} candidate(s)\n", Result.Candidates.size());

		for (const auto& Candidate : Result.Candidates)
		{
			if (InitGNamesVars(Candidate.Address, "UEAnalyzerKitty", Candidate.Confidence, ELogLevel::Debug))
			{
				bSuccess = true;
				break;
			}
		}

		if (!bSuccess)
			GLogger.FmtWrite(ELogLevel::Warning, "InitNames: All UEAnalyzerKitty candidates were rejected\n", Result.Candidates.size());
	}

	if (!bSuccess)
	{
		OutErrorString = "Failed to find a valid GNames!";
		GLogger.FmtWrite(ELogLevel::Error, "InitNames: {}\n", OutErrorString);
		return false;
	}

	GLogger.FmtWrite(ELogLevel::Info, "InitNames: GNames at 0x{:X}, module offset 0x{:X}{}\n", GNames, GInSDKOffsets.Statics.GNames, InternalSettings::bIsGNamesPointer ? ", reached by dereference" : "");

	if (GLayouts.NamesLayout->GetType() == ENamesType::Pool)
	{
		InternalSettings::bUseNamePool = true;

		const FNamePoolLayout* L = static_cast<const FNamePoolLayout*>(GLayouts.NamesLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Layout FNamePool\n");
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   Blocks +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Blocks, GDecryptCallbacks.NamePool.Blocks({.Address = GNames + L->Blocks, .RawValue = GMemory->Read<uintptr_t>(GNames + L->Blocks)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   BlocksBit = 0x{:X}\n", (uint32_t)L->BlocksBit);
		if (L->MaxChunkIndex != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitNames:   MaxChunkIndex +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxChunkIndex, GDecryptCallbacks.NamePool.MaxChunkIndex({.Address = GNames + L->MaxChunkIndex, .RawValue = GMemory->Read<int32>(GNames + L->MaxChunkIndex)}));
		if (L->ByteCursor != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitNames:   ByteCursor +0x{:03X} -> {} (optional)\n", (uint32_t)L->ByteCursor, GDecryptCallbacks.NamePool.ByteCursor({.Address = GNames + L->ByteCursor, .RawValue = GMemory->Read<int32>(GNames + L->ByteCursor)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::Header +0x{:03X}\n", (uint32_t)L->FNameEntry.Header);
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::String +0x{:03X}\n", (uint32_t)L->FNameEntry.String);
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::Stride = 0x{:X}\n", (uint32_t)L->FNameEntry.Stride);
		if (L->FNameEntry.LengthShift != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::LengthShift = 0x{:X} (optional)\n", (uint32_t)L->FNameEntry.LengthShift);
		if (L->FNameEntry.WideMask != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::WideMask = 0x{:X} (optional)\n", (uint32_t)L->FNameEntry.WideMask);
	}
	else
	{
		const FNameArrayLayout* L = static_cast<const FNameArrayLayout*>(GLayouts.NamesLayout.get());
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Layout TNameEntryArray\n");
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   Chunks +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Chunks, GDecryptCallbacks.NameArray.Chunks({.Address = GNames + L->Chunks, .RawValue = GMemory->Read<uintptr_t>(GNames + L->Chunks)}));
		if (L->NumElements != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitNames:   NumElements +0x{:03X} -> {} (optional)\n", (uint32_t)L->NumElements, GDecryptCallbacks.NameArray.NumElements({.Address = GNames + L->NumElements, .RawValue = GMemory->Read<int32>(GNames + L->NumElements)}));
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   ElementsPerChunk = 0x{:X}\n", (uint32_t)L->ElementsPerChunk);
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::Index +0x{:03X}\n", (uint32_t)L->FNameEntry.Index);
		GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::String +0x{:03X}\n", (uint32_t)L->FNameEntry.String);
		if (L->FNameEntry.WideMask != -1)
			GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::WideMask = 0x{:X} (optional)\n", (uint32_t)L->FNameEntry.WideMask);
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
		GLogger.FmtWrite(ELogLevel::Info, "InitNames: Printing first 10 name entries...\n");

		const FNamePoolLayout* PoolLayout = GLayouts.NamesLayout->GetType() == ENamesType::Pool
		                                        ? static_cast<const FNamePoolLayout*>(GLayouts.NamesLayout.get())
		                                        : nullptr;

		constexpr int32 MaxEntriesToPrint = 10;
		constexpr int32 MaxSlotsToScan    = 64;

		int32 Idx        = 0x0;
		int32 NumPrinted = 0x0;

		for (int32 NumScanned = 0x0; NumPrinted < MaxEntriesToPrint && NumScanned < MaxSlotsToScan; NumScanned++)
		{
			FNameEntry Entry           = NameArray::GetNameEntry(Idx);
			const uintptr_t EntryAddr  = reinterpret_cast<uintptr_t>(Entry.GetAddress());
			const std::string EntryStr = Entry.GetString();

			if (!EntryStr.empty())
			{
				GLogger.FmtWrite(ELogLevel::Info, "[{:02}] {}\n", Idx, EntryStr);
				NumPrinted++;
			}

			if (!PoolLayout)
			{
				Idx++;
				continue;
			}

			if (!EntryAddr)
				break;

			const uintptr_t HeaderAddr = EntryAddr + PoolLayout->FNameEntry.Header;
			const uint16 Header        = GDecryptCallbacks.NamePool.FNameEntry.Header({.Address = HeaderAddr, .RawValue = GMemory->Read<uint16>(HeaderAddr)});
			const int32 NameLen        = PoolLayout->FNameEntry.GetLength(Header);

			/* Length 0 marks an entry whose payload is an id/number pair rather than characters;
			   its size is not derivable from the header, so stop rather than guess a stride. */
			if (NameLen <= 0)
				break;

			const int32 CharSize   = PoolLayout->FNameEntry.GetIsWide(Header) ? (InternalSettings::bUseChar16String ? 2 : 4) : 1;
			const int32 EntryBytes = PoolLayout->FNameEntry.String + (NameLen * CharSize);

			Idx += Utils::Memory::AlignUp(EntryBytes, PoolLayout->FNameEntry.Stride) / PoolLayout->FNameEntry.Stride;
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

	GLogger.FmtWrite(ELogLevel::Info, "InitOffsets: Initializing FInGenOffsets...\n");

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

	GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	GLogger.FmtWrite(ELogLevel::Info, "InitOffsets: Initializing FInSKOffsets...\n");

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
	GLogger.FmtWrite(ELogLevel::Info, "FText::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.FText.SizeOf);
	GLogger.FmtWrite(ELogLevel::Info, "FText::InTextDataString: 0x{:X}\n", (uint32_t)GInSDKOffsets.FText.InTextDataString);
	GLogger.FmtWrite(ELogLevel::Info, "ULevel::Actors: 0x{:X}\n", (uint32_t)GInSDKOffsets.ULevel.Actors);
	GLogger.FmtWrite(ELogLevel::Info, "UDataTable::RowMap: 0x{:X}\n", (uint32_t)GInSDKOffsets.UDataTable.RowMap);
	GLogger.FmtWrite(ELogLevel::Info, "WeakObjectProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.WeakObjectProperty.SizeOf);
	GLogger.FmtWrite(ELogLevel::Info, "DelegateProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.DelegateProperty.SizeOf);
	if (InternalSettings::bUseFProperty)
		GLogger.FmtWrite(ELogLevel::Info, "FieldPathProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.FieldPathProperty.SizeOf);
	GLogger.FmtWrite(ELogLevel::Info, "MulticastInlineDelegateProperty::SizeOf: 0x{:X}\n", (uint32_t)GInSDKOffsets.MulticastInlineDelegateProperty.SizeOf);

	GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	GLogger.FmtWrite(ELogLevel::Info, "InitOffsets: Printing first 10 objects entries...\n");
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
	GLogger.FmtWrite(ELogLevel::Info, "EngineVersion: {}.{}.{}\n", InternalSettings::EngineMajor, InternalSettings::EngineMinor, InternalSettings::EnginePatch);
	GLogger.FmtWrite(ELogLevel::Info, "EngineVersionString: {}\n", InternalSettings::EngineVersionString);
	GLogger.FmtWrite(ELogLevel::Info, "UnrealModuleFileName: {}\n", InternalSettings::UnrealModuleFileName);
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
	GLogger.FmtWrite(ELogLevel::Info, "bIsGObjectsPointer: {}\n", InternalSettings::bIsGObjectsPointer);
	GLogger.FmtWrite(ELogLevel::Info, "bIsGNamesPointer: {}\n", InternalSettings::bIsGNamesPointer);

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