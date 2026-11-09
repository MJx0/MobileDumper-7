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
		ModInfo = MD7::Globals::GMemory->GetUnrealModule();
		if (ModInfo.IsValid())
			break;

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "Failed to find Unreal Engine module, retrying in {} seconds...\n", RetryAfterSec);
		sleep(RetryAfterSec);
	} while (--Retries > 0);

	if (!ModInfo.IsValid())
	{
		OutErrorString = fmt::format("Failed to find Unreal Engine module after {} attempts!\n", Retries);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "{}\n", OutErrorString);
		return false;
	}

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Path: {}\n", ModInfo.GetPathName());
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Start: 0x{:X}\n", ModInfo.GetStart());
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "End: 0x{:X}\n", ModInfo.GetEnd());
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Base: 0x{:X}\n", ModInfo.GetBase());

	InternalSettings::UnrealModuleFileName = std::filesystem::path(ModInfo.GetPathName()).filename().string();

	if (ModInfo.GetSegments().empty())
	{
		OutErrorString = "Failed to aquire UnrealEngine segments!";
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "{}\n", OutErrorString);
		return false;
	}

	const auto Segments = ModInfo.GetSegments();
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Segments ({}):\n", Segments.size());
	for (size_t i = 0; i < ModInfo.GetSegments().size(); i++)
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "[{:02}]: {}\n", i, Segments[i].ToString());
	}

	return true;
}

bool Generator::InitUEAnalyzerKitty(std::string& OutErrorString)
{
	UEAnalyzerKitty::AnalyzerOptions Options;
	Options.ThreadMode    = UEAnalyzerKitty::EThreadMode::Two;
	Options.Targets       = {UEAnalyzerKitty::Targets::Names, UEAnalyzerKitty::Targets::GUObjectArray, UEAnalyzerKitty::Targets::ObjObjects};
	Options.MaxCandidates = 50;

	Analyzer = UEAnalyzerKitty::UEAnalyzer::Analyze(MD7::Globals::GMemory.get(), MD7::Globals::GArchDecoder.get(), Options);
	if (!Analyzer.IsValid())
	{
		OutErrorString = Analyzer.GetError();
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "UEAnalyzerKitty failed: \"{}\".\n", OutErrorString);
		return false;
	}

	InternalSettings::bUseChar16String = Analyzer.GetTCharKind() != UEAnalyzerKitty::ETCharKind::Char32;
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UE Engine TChar: UTF{}\n", InternalSettings::bUseChar16String ? "16" : "32");

	const UEAnalyzerKitty::FEngineVersionInfo& EngineVersion = Analyzer.GetEngineVersion();
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UE Engine Version: {}\n", EngineVersion.ToString());

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
				MD7::Globals::GLogger.FmtWrite(DiagLevel, "InitObjects: [{}] {}\n", Interpretation, Line);
		};

		std::string ResolveError;
		if (!MD7::Globals::GProfile->ResolveGObjectsLayout(ArrayAddress, MD7::Globals::GLayouts.ObjectsLayout, &ResolveError))
		{
			// An overriding profile is free to ignore OutError, so cover an empty reason.
			Report(ResolveError.empty() ? fmt::format("No layout could be derived at 0x{:X}", ArrayAddress) : ResolveError);
			return false;
		}

		// An overriding profile can return true and still leave the layout unusable.
		if (!MD7::Globals::GLayouts.ObjectsLayout || !MD7::Globals::GLayouts.ObjectsLayout->IsValid())
		{
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: [{}] ResolveGObjectsLayout reported success for 0x{:X} but left the layout incomplete\n", Interpretation, ArrayAddress);
			return false;
		}

		const LayoutDetection::FObjectsTestResult Test = LayoutDetection::TestObjectsLayout(ArrayAddress, MD7::Globals::GLayouts.ObjectsLayout.get());

		if (!Test.bValid)
		{
			Report(fmt::format("Layout resolved at 0x{:X} but failed validation: {}", ArrayAddress, Test.Summary));
			for (const std::string& Line : Test.Details)
				Report(Line);

			return false;
		}

		for (const std::string& Line : Test.Details)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Debug, "InitObjects: [{}] {}\n", Interpretation, Line);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: [{}] Verified: {}\n", Interpretation, Test.Summary);
		return true;
	};

	auto InitGObjectsVars = [&TryGObjectsAt](uintptr_t Address, const std::string& FindMethod, float Confidence, ELogLevel DiagLevel) -> bool
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Trying candidate 0x{:X} from {} ({:.1f}% confidence)\n", Address, FindMethod, Confidence * 100.0f);

		uintptr_t ObjectsOffset    = MD7::Globals::GMemory->GetUnrealModule().AddressToOffset(Address);
		const uintptr_t DecAddress = MD7::Globals::GDecryptCallbacks.Statics.GObjects({.Address = Address, .RawValue = Address});
		if (DecAddress != Address)
		{
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Decrypted 0x{:X} -> 0x{:X}\n", Address, DecAddress);
			Address = DecAddress;
		}

		MD7::Globals::GObjects = Address;
		if (TryGObjectsAt(MD7::Globals::GObjects, "Direct", DiagLevel))
		{
			MD7::Globals::GInSDKOffsets.Statics.GObjects       = ObjectsOffset;
			InternalSettings::bIsGObjectsPointer = false;
			return true;
		}

		MD7::Globals::GObjects = MD7::Globals::GMemory->Read<uintptr_t>(MD7::Globals::GObjects);
		if (!MD7::Globals::GMemory->IsAddressReadable(MD7::Globals::GObjects))
		{
			MD7::Globals::GLogger.FmtWrite(DiagLevel, "InitObjects: [Dereference] Skipped, 0x{:X} does not hold a readable pointer\n", Address);
			return false;
		}

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Debug, "InitObjects: [Dereference] Following 0x{:X} -> 0x{:X}\n", Address, MD7::Globals::GObjects);

		if (TryGObjectsAt(MD7::Globals::GObjects, "Dereference", DiagLevel))
		{
			MD7::Globals::GInSDKOffsets.Statics.GObjects       = ObjectsOffset;
			InternalSettings::bIsGObjectsPointer = true;
			return true;
		}

		return false;
	};

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Resolving GObjects...\n");

	MD7::Globals::GObjects = MD7::Globals::GProfile->GetGObjects();

	// InitGObjectsVars reassigns MD7::Globals::GObjects as it tries each interpretation, so the address the
	// profile actually returned has to be kept for the messages below.
	const uintptr_t ProfileObjects = MD7::Globals::GObjects;

	bool bSuccess = false;

	if (MD7::Globals::GMemory->IsAddressReadable(ProfileObjects))
	{
		bSuccess = InitGObjectsVars(ProfileObjects, "IProfile::GetGObjects", 1.00f, ELogLevel::Warning);

		if (!bSuccess)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: IProfile::GetGObjects() returned 0x{:X}, but neither it nor its dereference held an object array - falling back to UEAnalyzerKitty\n", ProfileObjects);
	}
	else
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: IProfile::GetGObjects() returned unreadable address 0x{:X} - falling back to UEAnalyzerKitty\n", ProfileObjects);
	}

	if (!bSuccess)
	{
		auto Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::GUObjectArray);
		if (Result.Candidates.empty())
		{
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: No GUObjectArray candidate found, retrying as ObjObjects\n");
			Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::ObjObjects);
		}

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: UEAnalyzerKitty offered {} candidate(s)\n", Result.Candidates.size());

		for (const auto& Candidate : Result.Candidates)
		{
			if (InitGObjectsVars(Candidate.Address, "UEAnalyzerKitty", Candidate.Confidence, ELogLevel::Debug))
			{
				bSuccess = true;
				break;
			}
		}

		if (!bSuccess)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitObjects: All UEAnalyzerKitty candidates were rejected\n");
	}

	if (!bSuccess)
	{
		OutErrorString = "Failed to find a valid GObjects!";
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "InitObjects: {}\n", OutErrorString);
		return false;
	}

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: GObjects at 0x{:X}, module offset 0x{:X}{}\n", MD7::Globals::GObjects, MD7::Globals::GInSDKOffsets.Statics.GObjects, InternalSettings::bIsGObjectsPointer ? ", reached by dereference" : "");

	if (MD7::Globals::GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked)
	{
		const FChunkedUObjectArrayLayout* L = static_cast<const FChunkedUObjectArrayLayout*>(MD7::Globals::GLayouts.ObjectsLayout.get());
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Layout FChunkedFixedUObjectArray\n");
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   Objects +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Objects, MD7::Globals::GDecryptCallbacks.ChunkedObjects.Objects({.Address = MD7::Globals::GObjects + L->Objects, .RawValue = MD7::Globals::GMemory->Read<uintptr_t>(MD7::Globals::GObjects + L->Objects)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   NumElements +0x{:03X} -> {}\n", (uint32_t)L->NumElements, MD7::Globals::GDecryptCallbacks.ChunkedObjects.NumElements({.Address = MD7::Globals::GObjects + L->NumElements, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GObjects + L->NumElements)}));
		if (L->MaxElements != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   MaxElements +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxElements, MD7::Globals::GDecryptCallbacks.ChunkedObjects.MaxElements({.Address = MD7::Globals::GObjects + L->MaxElements, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GObjects + L->MaxElements)}));
		if (L->NumChunks != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   NumChunks +0x{:03X} -> {} (optional)\n", (uint32_t)L->NumChunks, MD7::Globals::GDecryptCallbacks.ChunkedObjects.NumChunks({.Address = MD7::Globals::GObjects + L->NumChunks, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GObjects + L->NumChunks)}));
		if (L->MaxChunks != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   MaxChunks +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxChunks, MD7::Globals::GDecryptCallbacks.ChunkedObjects.MaxChunks({.Address = MD7::Globals::GObjects + L->MaxChunks, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GObjects + L->MaxChunks)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   ElementsPerChunk = 0x{:X}\n", (uint32_t)L->ElementsPerChunk);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Object +0x{:03X}\n", (uint32_t)L->FUObjectItem.Object);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Size = 0x{:X}\n", (uint32_t)L->FUObjectItem.Size);
	}
	else
	{
		const FFixedUObjectArrayLayout* L = static_cast<const FFixedUObjectArrayLayout*>(MD7::Globals::GLayouts.ObjectsLayout.get());
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects: Layout FFixedUObjectArray\n");
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   Objects +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Objects, MD7::Globals::GDecryptCallbacks.FixedObjects.Objects({.Address = MD7::Globals::GObjects + L->Objects, .RawValue = MD7::Globals::GMemory->Read<uintptr_t>(MD7::Globals::GObjects + L->Objects)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   NumObjects +0x{:03X} -> {}\n", (uint32_t)L->NumObjects, MD7::Globals::GDecryptCallbacks.FixedObjects.NumObjects({.Address = MD7::Globals::GObjects + L->NumObjects, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GObjects + L->NumObjects)}));
		if (L->MaxObjects != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   MaxObjects +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxObjects, MD7::Globals::GDecryptCallbacks.FixedObjects.MaxObjects({.Address = MD7::Globals::GObjects + L->MaxObjects, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GObjects + L->MaxObjects)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Object +0x{:03X}\n", (uint32_t)L->FUObjectItem.Object);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitObjects:   FUObjectItem::Size = 0x{:X}\n", (uint32_t)L->FUObjectItem.Size);
	}

	ObjectArray::SetByIndexFn([](int32 Index) -> uintptr_t
	{
		return MD7::Globals::GProfile->GetObjectByIndex(MD7::Globals::GObjects, MD7::Globals::GLayouts.ObjectsLayout, Index);
	});

	// LayoutDetection::DumpCandidateMemory(MD7::Globals::GObjects, "GObjects");

	/*
	{
	    for (int i = 0; i < 0x18; i += 8)
	    {
	        if (MD7::Globals::GLayouts.ObjectsLayout->GetType() == EObjectsType::Chunked)
	            reinterpret_cast<FChunkedUObjectArrayLayout*>(MD7::Globals::GLayouts.ObjectsLayout.get())->FUObjectItem.Object = i;
	        else
	            reinterpret_cast<FFixedUObjectArrayLayout*>(MD7::Globals::GLayouts.ObjectsLayout.get())->FUObjectItem.Object = i;

	        for (size_t j = sizeof(void*); j < 0x28; j += 4)
	        {
	            uintptr_t Addr = reinterpret_cast<uintptr_t>(ObjectArray::GetByIndex(1).GetAddress());
	            if (!Addr)
	                continue;

	            int Index = MD7::Globals::GMemory->Read<int32>(Addr + j);
	            MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GetByIndex[0x{:X} + 0x{:X}] = 0x{:X}\n", Addr, j, Index);
	        }
	        MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==============\n");
	    }
	}
	*/

	return true;
}

bool Generator::InitNames(std::string& OutErrorString)
{
	auto TryGNamesAt = [](uintptr_t NamesAddress, const char* Interpretation, ELogLevel DiagLevel) -> bool
	{
		const auto Report = [Interpretation, DiagLevel](const std::string& Reason)
		{
			std::istringstream Lines(Reason);
			std::string Line;
			while (std::getline(Lines, Line))
				MD7::Globals::GLogger.FmtWrite(DiagLevel, "InitNames: [{}] {}\n", Interpretation, Line);
		};

		std::string ResolveError;
		if (!MD7::Globals::GProfile->ResolveGNamesLayout(NamesAddress, MD7::Globals::GLayouts.NamesLayout, &ResolveError))
		{
			// An overriding profile is free to ignore OutError, so cover an empty reason.
			Report(ResolveError.empty() ? fmt::format("No layout could be derived at 0x{:X}", NamesAddress) : ResolveError);
			return false;
		}

		// An overriding profile can return true and still leave the layout unusable.
		if (!MD7::Globals::GLayouts.NamesLayout || !MD7::Globals::GLayouts.NamesLayout->IsValid())
		{
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitNames: [{}] ResolveGNamesLayout reported success for 0x{:X} but left the layout incomplete\n", Interpretation, NamesAddress);
			return false;
		}

		const LayoutDetection::FNamesTestResult Test = LayoutDetection::TestNamesLayout(NamesAddress, MD7::Globals::GLayouts.NamesLayout.get());

		if (!Test.bValid)
		{
			Report(fmt::format("Layout resolved at 0x{:X} but failed validation: {}", NamesAddress, Test.Summary));
			for (const std::string& Line : Test.Details)
				Report(Line);

			return false;
		}

		for (const std::string& Line : Test.Details)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Debug, "InitNames: [{}] {}\n", Interpretation, Line);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: [{}] Verified: {}\n", Interpretation, Test.Summary);
		return true;
	};

	auto InitGNamesVars = [&TryGNamesAt](uintptr_t Address, const std::string& FindMethod, float Confidence, ELogLevel DiagLevel) -> bool
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Trying candidate 0x{:X} from {} ({:.1f}% confidence)\n", Address, FindMethod, Confidence * 100.0f);

		uintptr_t NamesOffset      = MD7::Globals::GMemory->GetUnrealModule().AddressToOffset(Address);
		const uintptr_t DecAddress = MD7::Globals::GDecryptCallbacks.Statics.GNames({.Address = Address, .RawValue = Address});
		if (DecAddress != Address)
		{
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Decrypted 0x{:X} -> 0x{:X}\n", Address, DecAddress);
			Address = DecAddress;
		}

		MD7::Globals::GNames = Address;
		if (TryGNamesAt(MD7::Globals::GNames, "Direct", DiagLevel))
		{
			MD7::Globals::GInSDKOffsets.Statics.GNames       = NamesOffset;
			InternalSettings::bIsGNamesPointer = false;
			return true;
		}

		MD7::Globals::GNames = MD7::Globals::GMemory->Read<uintptr_t>(MD7::Globals::GNames);
		if (!MD7::Globals::GMemory->IsAddressReadable(MD7::Globals::GNames))
		{
			MD7::Globals::GLogger.FmtWrite(DiagLevel, "InitNames: [Dereference] Skipped, 0x{:X} does not hold a readable pointer\n", Address);
			return false;
		}

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Debug, "InitNames: [Dereference] Following 0x{:X} -> 0x{:X}\n", Address, MD7::Globals::GNames);

		if (TryGNamesAt(MD7::Globals::GNames, "Dereference", DiagLevel))
		{
			MD7::Globals::GInSDKOffsets.Statics.GNames       = NamesOffset;
			InternalSettings::bIsGNamesPointer = true;
			return true;
		}

		return false;
	};

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Resolving GNames...\n");

	MD7::Globals::GNames = MD7::Globals::GProfile->GetGNames();

	// InitGNamesVars reassigns MD7::Globals::GNames as it tries each interpretation, so the address the
	// profile actually returned has to be kept for the messages below.
	const uintptr_t ProfileNames = MD7::Globals::GNames;

	bool bSuccess = false;

	if (MD7::Globals::GMemory->IsAddressReadable(ProfileNames))
	{
		bSuccess = InitGNamesVars(ProfileNames, "IProfile::GetGNames", 1.00f, ELogLevel::Warning);

		if (!bSuccess)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitNames: IProfile::GetGNames() returned 0x{:X}, but neither it nor its dereference held a name table - falling back to UEAnalyzerKitty\n", ProfileNames);
	}
	else
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitNames: IProfile::GetGNames() returned unreadable address 0x{:X} - falling back to UEAnalyzerKitty\n", ProfileNames);
	}

	if (!bSuccess)
	{
		auto Result = Generator::Analyzer.Find(UEAnalyzerKitty::Targets::Names);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: UEAnalyzerKitty offered {} candidate(s)\n", Result.Candidates.size());

		for (const auto& Candidate : Result.Candidates)
		{
			if (InitGNamesVars(Candidate.Address, "UEAnalyzerKitty", Candidate.Confidence, ELogLevel::Debug))
			{
				bSuccess = true;
				break;
			}
		}

		if (!bSuccess)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "InitNames: All UEAnalyzerKitty candidates were rejected\n", Result.Candidates.size());
	}

	if (!bSuccess)
	{
		OutErrorString = "Failed to find a valid GNames!";
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "InitNames: {}\n", OutErrorString);
		return false;
	}

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: GNames at 0x{:X}, module offset 0x{:X}{}\n", MD7::Globals::GNames, MD7::Globals::GInSDKOffsets.Statics.GNames, InternalSettings::bIsGNamesPointer ? ", reached by dereference" : "");

	if (MD7::Globals::GLayouts.NamesLayout->GetType() == ENamesType::Pool)
	{
		InternalSettings::bUseNamePool = true;

		const FNamePoolLayout* L = static_cast<const FNamePoolLayout*>(MD7::Globals::GLayouts.NamesLayout.get());
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Layout FNamePool\n");
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   Blocks +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Blocks, MD7::Globals::GDecryptCallbacks.NamePool.Blocks({.Address = MD7::Globals::GNames + L->Blocks, .RawValue = MD7::Globals::GMemory->Read<uintptr_t>(MD7::Globals::GNames + L->Blocks)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   BlocksBit = 0x{:X}\n", (uint32_t)L->BlocksBit);
		if (L->MaxChunkIndex != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   MaxChunkIndex +0x{:03X} -> {} (optional)\n", (uint32_t)L->MaxChunkIndex, MD7::Globals::GDecryptCallbacks.NamePool.MaxChunkIndex({.Address = MD7::Globals::GNames + L->MaxChunkIndex, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GNames + L->MaxChunkIndex)}));
		if (L->ByteCursor != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   ByteCursor +0x{:03X} -> {} (optional)\n", (uint32_t)L->ByteCursor, MD7::Globals::GDecryptCallbacks.NamePool.ByteCursor({.Address = MD7::Globals::GNames + L->ByteCursor, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GNames + L->ByteCursor)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::Header +0x{:03X}\n", (uint32_t)L->FNameEntry.Header);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::String +0x{:03X}\n", (uint32_t)L->FNameEntry.String);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::Stride = 0x{:X}\n", (uint32_t)L->FNameEntry.Stride);
		if (L->FNameEntry.LengthShift != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::LengthShift = 0x{:X} (optional)\n", (uint32_t)L->FNameEntry.LengthShift);
		if (L->FNameEntry.WideMask != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::WideMask = 0x{:X} (optional)\n", (uint32_t)L->FNameEntry.WideMask);
	}
	else
	{
		const FNameArrayLayout* L = static_cast<const FNameArrayLayout*>(MD7::Globals::GLayouts.NamesLayout.get());
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Layout TNameEntryArray\n");
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   Chunks +0x{:03X} -> 0x{:X}\n", (uint32_t)L->Chunks, MD7::Globals::GDecryptCallbacks.NameArray.Chunks({.Address = MD7::Globals::GNames + L->Chunks, .RawValue = MD7::Globals::GMemory->Read<uintptr_t>(MD7::Globals::GNames + L->Chunks)}));
		if (L->NumElements != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   NumElements +0x{:03X} -> {} (optional)\n", (uint32_t)L->NumElements, MD7::Globals::GDecryptCallbacks.NameArray.NumElements({.Address = MD7::Globals::GNames + L->NumElements, .RawValue = MD7::Globals::GMemory->Read<int32>(MD7::Globals::GNames + L->NumElements)}));
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   ElementsPerChunk = 0x{:X}\n", (uint32_t)L->ElementsPerChunk);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::Index +0x{:03X}\n", (uint32_t)L->FNameEntry.Index);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::String +0x{:03X}\n", (uint32_t)L->FNameEntry.String);
		if (L->FNameEntry.WideMask != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames:   FNameEntry::WideMask = 0x{:X} (optional)\n", (uint32_t)L->FNameEntry.WideMask);
	}

	NameArray::SetByIndexFn([](int32 Index) -> uintptr_t
	{
		return MD7::Globals::GProfile->GetNameEntryByIndex(MD7::Globals::GNames, MD7::Globals::GLayouts.NamesLayout, Index);
	});

	FNameEntry::SetGetStrFn([](uintptr_t NameEntry) -> std::wstring
	{
		return MD7::Globals::GProfile->GetNameEntryString(MD7::Globals::GNames, MD7::Globals::GLayouts.NamesLayout, NameEntry);
	});

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Testing IProfile::GetNameEntryByIndex and GetNameEntryString...\n");
		std::string FirstEntryStr = NameArray::GetNameEntry(0).GetString();
		if (FirstEntryStr != "None")
		{
			OutErrorString = "First name entry does not decode to \"None\". Check the GNames pointer, GNames layout, and IProfile::GetNameEntryByIndex/GetNameEntryString implementations.";
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "InitNames: {}\n", OutErrorString);
			return false;
		}
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitNames: Printing first 10 name entries...\n");

		const FNamePoolLayout* PoolLayout = MD7::Globals::GLayouts.NamesLayout->GetType() == ENamesType::Pool
		                                        ? static_cast<const FNamePoolLayout*>(MD7::Globals::GLayouts.NamesLayout.get())
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
				MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "[{:02}] {}\n", Idx, EntryStr);
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
			const uint16 Header        = MD7::Globals::GDecryptCallbacks.NamePool.FNameEntry.Header({.Address = HeaderAddr, .RawValue = MD7::Globals::GMemory->Read<uint16>(HeaderAddr)});
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

	MD7::Globals::GProfile->OverrideSettings(MD7::Globals::GSettings);

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "General::MaxFNameLen: {}\n", MD7::Globals::GSettings.General.MaxFNameLen);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generator::ZipCompressionLevel: {}\n", MD7::Globals::GSettings.Generator.ZipCompressionLevel);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "EngineCore::bEnableEncryptedObjectPropertySupport: {}\n", MD7::Globals::GSettings.EngineCore.bEnableEncryptedObjectPropertySupport);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::UseMemberCustomSpacing: {}\n", MD7::Globals::GSettings.CppGenerator.UseMemberCustomSpacing);
	if (MD7::Globals::GSettings.CppGenerator.UseMemberCustomSpacing)
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::MemberTypeColumnWidth: {}\n", MD7::Globals::GSettings.CppGenerator.MemberTypeColumnWidth);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::MemberNameColumnWidth: {}\n", MD7::Globals::GSettings.CppGenerator.MemberNameColumnWidth);
	}
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::SDKNamespaceName: {}\n", MD7::Globals::GSettings.CppGenerator.SDKNamespaceName.empty() ? "(global)" : MD7::Globals::GSettings.CppGenerator.SDKNamespaceName);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::XORString: {}\n", MD7::Globals::GSettings.CppGenerator.XORString.empty() ? "(none)" : MD7::Globals::GSettings.CppGenerator.XORString);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::bForceNoGWorldInSDK: {}\n", MD7::Globals::GSettings.CppGenerator.bForceNoGWorldInSDK);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::bAddManualOverrideOptions: {}\n", MD7::Globals::GSettings.CppGenerator.bAddManualOverrideOptions);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "CppGenerator::bAddFinalSpecifier: {}\n", MD7::Globals::GSettings.CppGenerator.bAddFinalSpecifier);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "MappingGenerator::bShouldCheckForDuplicatedNames: {}\n", MD7::Globals::GSettings.MappingGenerator.bShouldCheckForDuplicatedNames);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "MappingGenerator::bExcludeEditorOnlyProperties: {}\n", MD7::Globals::GSettings.MappingGenerator.bExcludeEditorOnlyProperties);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Debug::bGenerateAssertionFile: {}\n", MD7::Globals::GSettings.Debug.bGenerateAssertionFile);

	return true;
}

bool Generator::InitOffsets(std::string& OutErrorString)
{
	MD7::Globals::GProfile->OverrideInGenOffsets(MD7::Globals::GOffsets);

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitOffsets: Initializing FInGenOffsets...\n");

	if (!MD7::Globals::GOffsets.Init(OutErrorString))
		return false;

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== FInGenOffsets ===\n");

	if (InternalSettings::bUseFProperty)
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FField::Vft: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FField.Vft);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FField::Class: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FField.Class);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FField::Owner: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FField.Owner);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FField::Next: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FField.Next);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FField::Name: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FField.Name);
		if (MD7::Globals::GOffsets.FField.EditorOnlyMetadata != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FField::EditorOnlyMetadata: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FField.EditorOnlyMetadata);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FFieldClass::Name: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FFieldClass.Name);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FFieldClass::CastFlags: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FFieldClass.CastFlags);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FName::CompIdx: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FName.CompIdx);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FName::Number: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FName.Number);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FName::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FName.SizeOf);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UObject::Vft: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UObject.Vft);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UObject::Flags: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UObject.Flags);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UObject::Index: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UObject.Index);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UObject::Class: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UObject.Class);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UObject::Name: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UObject.Name);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UObject::Outer: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UObject.Outer);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UField::Next: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UField.Next);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UEnum::Names: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UEnum.Names);
		if (InternalSettings::bHasUnderlayingTypeInUEnum)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UEnum::UnderlyingType: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UEnum.UnderlyingType);
	}

	{
		if (MD7::Globals::GOffsets.UStruct.StructBaseChain != -1)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UStruct::StructBaseChain: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UStruct.StructBaseChain);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UStruct::SuperStruct: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UStruct.SuperStruct);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UStruct::Children: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UStruct.Children);

		if (InternalSettings::bUseFProperty)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UStruct::ChildProperties: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UStruct.ChildProperties);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UStruct::Size: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UStruct.Size);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UStruct::MinAlignment: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UStruct.MinAlignment);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UFunction::FunctionFlags: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UFunction.FunctionFlags);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UFunction::NumParams: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UFunction.NumParams);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UFunction::ParamSize: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UFunction.ParamSize);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UFunction::ExecFunction: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UFunction.ExecFunction);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UClass::CastFlags: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UClass.CastFlags);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UClass::ClassDefaultObject: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UClass.ClassDefaultObject);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UClass::ImplementedInterfaces: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.UClass.ImplementedInterfaces);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Property::ArrayDim: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.Property.ArrayDim);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Property::ElementSize: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.Property.ElementSize);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Property::PropertyFlags: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.Property.PropertyFlags);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Property::Offset_Internal: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.Property.Offset_Internal);
	}

	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "ByteProperty::Enum: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.ByteProperty.Enum);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "BoolProperty::Base: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.BoolProperty.Base);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "ObjectProperty::PropertyClass: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.ObjectProperty.PropertyClass);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "ClassProperty::MetaClass: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.ClassProperty.MetaClass);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "StructProperty::Struct: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.StructProperty.Struct);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "ArrayProperty::Inner: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.ArrayProperty.Inner);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "DelegateProperty::SignatureFunction: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.DelegateProperty.SignatureFunction);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "MapProperty::Base: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.MapProperty.Base);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "SetProperty::ElementProp: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.SetProperty.ElementProp);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "EnumProperty::Base: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.EnumProperty.Base);
		if (InternalSettings::bUseFProperty)
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FieldPathProperty::FieldClass: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FieldPathProperty.FieldClass);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "OptionalProperty::ValueProperty: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.OptionalProperty.ValueProperty);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FInstancedStruct::ScriptStruct: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FInstancedStruct.ScriptStruct);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FInstancedStruct::StructMemory: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.FInstancedStruct.StructMemory);

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Property::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GOffsets.Property.SizeOf);
	}

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitOffsets: Initializing FInSKOffsets...\n");

	MD7::Globals::GInSDKOffsets.Init();

	MD7::Globals::GProfile->OverrideInSKOffsets(MD7::Globals::GInSDKOffsets);

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== FInSKOffsets ===\n");
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GEngine: 0x{:X}\n", MD7::Globals::GInSDKOffsets.Statics.GEngine);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GWorld: 0x{:X}\n", MD7::Globals::GInSDKOffsets.Statics.GWorld);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GObjects: 0x{:X}\n", MD7::Globals::GInSDKOffsets.Statics.GObjects);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GNames: 0x{:X}\n", MD7::Globals::GInSDKOffsets.Statics.GNames);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "PEIndex: {}\n", MD7::Globals::GInSDKOffsets.Statics.PEIndex);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "PEOffset: 0x{:X}\n", MD7::Globals::GInSDKOffsets.Statics.PEOffset);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FText::TextData: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.FText.TextData);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FText::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.FText.SizeOf);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FText::InTextDataString: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.FText.InTextDataString);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "ULevel::Actors: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.ULevel.Actors);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UDataTable::RowMap: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.UDataTable.RowMap);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "WeakObjectProperty::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.WeakObjectProperty.SizeOf);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "DelegateProperty::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.DelegateProperty.SizeOf);
	if (InternalSettings::bUseFProperty)
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FieldPathProperty::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.FieldPathProperty.SizeOf);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "MulticastInlineDelegateProperty::SizeOf: 0x{:X}\n", (uint32_t)MD7::Globals::GInSDKOffsets.MulticastInlineDelegateProperty.SizeOf);

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "InitOffsets: Printing first 10 objects entries...\n");
	{
		for (int i = 0; i < 10; i++)
		{
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "[{:02}] {}\n", i, ObjectArray::GetByIndex(i).GetName());
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

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GameName: {}\n", InternalSettings::GameName);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "GameVersion: {}\n", InternalSettings::GameVersion);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "EngineVersion: {}.{}.{}\n", InternalSettings::EngineMajor, InternalSettings::EngineMinor, InternalSettings::EnginePatch);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "EngineVersionString: {}\n", InternalSettings::EngineVersionString);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "UnrealModuleFileName: {}\n", InternalSettings::UnrealModuleFileName);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsEnumNameOnly: {}\n", InternalSettings::bIsEnumNameOnly);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsSmallEnumValue: {}\n", InternalSettings::bIsSmallEnumValue);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bHasUnderlayingTypeInUEnum: {}\n", InternalSettings::bHasUnderlayingTypeInUEnum);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsNewUE5EnumNamesContainer: {}\n", InternalSettings::bIsNewUE5EnumNamesContainer);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsWeakObjectPtrWithoutTag: {}\n", InternalSettings::bIsWeakObjectPtrWithoutTag);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseFProperty: {}\n", InternalSettings::bUseFProperty);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseNamePool: {}\n", InternalSettings::bUseNamePool);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsObjectNameBeforeClass: {}\n", InternalSettings::bIsObjectNameBeforeClass);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseCasePreservingName: {}\n", InternalSettings::bUseCasePreservingName);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseOutlineNumberName: {}\n", InternalSettings::bUseOutlineNumberName);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsObjPtrInsteadOfFieldPathProperty: {}\n", InternalSettings::bIsObjPtrInsteadOfFieldPathProperty);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseMaskForFieldOwner: {}\n", InternalSettings::bUseMaskForFieldOwner);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseLargeWorldCoordinates: {}\n", InternalSettings::bUseLargeWorldCoordinates);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseUint8ArrayDim: {}\n", InternalSettings::bUseUint8ArrayDim);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bUseChar16String: {}\n", InternalSettings::bUseChar16String);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsGObjectsPointer: {}\n", InternalSettings::bIsGObjectsPointer);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "bIsGNamesPointer: {}\n", InternalSettings::bIsGNamesPointer);

	return true;
}

void Generator::InitManagers()
{
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "(Early) Initializing PackageManager...\n");
	// Initialize PackageManager with all packages, their names, structs, classes enums, functions and dependencies
	PackageManager::Init();

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Initializing StructManager...\n");
	// Initialize StructManager with all structs and their names
	StructManager::Init();

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Initializing EnumManager...\n");
	// Initialize EnumManager with all enums and their names
	EnumManager::Init();

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Initializing MemberManager...\n");
	// Initialized all Member-Name collisions
	MemberManager::Init();

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "(Late) Initializing PackageManager...\n");
	// Post-Initialize PackageManager after StructManager has been initialized. 'PostInit()' handles Cyclic-Dependencies detection
	PackageManager::PostInit();

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Managers Initialized.\n");
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

		DumperFolder = fs::path(MD7::Globals::GSettings.Generator.SDKGenerationPath) / (FolderGameName + "_v" + FolderGameVersion + "_" + TimestampBuf);

		if (fs::exists(DumperFolder))
			fs::remove_all(DumperFolder);

		fs::create_directories(DumperFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Could not create required folders! Info: \n{}\n", fe.what());
		return false;
	}

	return true;
}

bool Generator::ZipDumperFolder(fs::path& OutZipPath)
{
	OutZipPath.clear();

	if (DumperFolder.empty() || !fs::exists(DumperFolder))
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "ZipDumperFolder: DumperFolder is not set or does not exist.\n");
		return false;
	}

	const fs::path ZipPath = DumperFolder.parent_path() / (DumperFolder.filename().string() + ".zip");

	std::error_code Ec;
	fs::remove(ZipPath, Ec);

	const int32 Level = std::clamp(MD7::Globals::GSettings.Generator.ZipCompressionLevel, 0, 9);
	const bool bZipOk = Utils::Zip::CreateZipWithDirectory(DumperFolder.string(), Level, ZipPath.string());

	if (bZipOk)
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "ZipDumperFolder: Created {}\n", ZipPath.string());
		OutZipPath = ZipPath;
	}
	else
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "ZipDumperFolder: Failed to create zip.\n");
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
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "Could not create required folders! Info: \n{}\n", fe.what());
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
	if (MD7::Globals::GOffsets.FField.EditorOnlyMetadata == -1)
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