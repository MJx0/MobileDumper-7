#include "OffsetFinder.h"

#include <chrono>
#include <thread>
#include <cctype>

#include "../Unreal/ObjectArray.h"

void OffsetFinder::FindStaticOffsets(std::unordered_map<std::string, uintptr_t>& ClassNamesToOffsets)
{
	if (ClassNamesToOffsets.empty())
		return;

	// Resolve all requested class names up front
	std::unordered_map<std::string, UEClass> Classes;
	Classes.reserve(ClassNamesToOffsets.size());
	for (auto& [Name, _] : ClassNamesToOffsets)
	{
		Classes[Name] = ObjectArray::FindClassFast(Name.c_str());
		GLogger.FmtWrite(ELogLevel::Info, "{} instance at {} \n", Name, Classes[Name].GetAddress());
	}

	// Find one live (non-CDO) instance per class in a single pass
	std::unordered_map<std::string, UEObject> Instances;
	Instances.reserve(Classes.size());

	const int MaxNumObjectsConsidered = ObjectArray::Num();
	int NumObjectsConsidered          = 0;

	for (UEObject Obj : ObjectArray())
	{
		if (NumObjectsConsidered++ >= MaxNumObjectsConsidered)
			break;

		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject))
			continue;

		for (auto& [Name, Cls] : Classes)
		{
			if (Instances.count(Name) || !Cls)
				continue;

			if (Obj.IsA(Cls))
				Instances[Name] = Obj;
		}

		if (Instances.size() == Classes.size())
			break;
	}

	const ModuleInfo ModInfo = GMemory->GetUnrealModule();

	for (auto& [Name, Instance] : Instances)
	{
		std::vector<uintptr_t> Results;

		ModInfo.ForEachSegment([&Instance, &Results](const MemRegionInfo& Region) -> bool
		{
			if (Region.IsReadable() && !Region.IsExecutable())
			{
				std::vector<uintptr_t> TempResults = GMemory->FindAllAlignedValuesInRange(
				    reinterpret_cast<uintptr_t>(Instance.GetAddress()),
				    sizeof(uintptr_t),
				    Region.GetStart(),
				    Region.GetSize());

				if (!TempResults.empty())
				{
					Results = std::move(TempResults);
					return true;
				}
			}

			return false;
		});

		GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: Found {} candidates for '{}'\n", Results.size(), Name);

		uintptr_t Result = 0;

		if (Results.size() == 1)
		{
			Result = Results[0];
		}
		else if (Results.size() == 2)
		{
			// If Results[0] changes within 50 ms it's a volatile pointer (e.g. GActiveLogWorld);
			// in that case Results[1] is the stable static global.
			const uintptr_t CandidateAddr = reinterpret_cast<uintptr_t>(Results[0]);
			const uintptr_t ObjAddr       = reinterpret_cast<uintptr_t>(Instance.GetAddress());
			uintptr_t CurrentValue        = GMemory->Read<uintptr_t>(CandidateAddr);

			for (int i = 0; CurrentValue == ObjAddr && i < 50; ++i)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				CurrentValue = GMemory->Read<uintptr_t>(CandidateAddr);
			}
			if (CurrentValue == ObjAddr)
			{
				Result = Results[0];
			}
			else
			{
				Result = Results[1];
				GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: filtered volatile pointer at 0x{:X} for '{}'\n", CandidateAddr, Name);
			}
		}
		else
		{
			continue;
		}

		ClassNamesToOffsets[Name] = GMemory->GetUnrealModule().AddressToOffset(Result);
		GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: '{}' offset = 0x{:X}\n", Name, ClassNamesToOffsets[Name]);
	}
}
