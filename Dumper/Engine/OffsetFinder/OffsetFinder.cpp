#include "OffsetFinder.h"

#include <chrono>
#include <thread>
#include <cctype>

#include "../Unreal/ObjectArray.h"

std::unordered_map<std::string, uintptr_t> OffsetFinder::FindStaticOffsets(const std::vector<std::string>& ClassNames)
{
	std::unordered_map<std::string, uintptr_t> FoundOffsets;

	if (ClassNames.empty())
		return FoundOffsets;

	// Resolve all requested class names up front
	std::unordered_map<std::string, UEClass> Classes;
	Classes.reserve(ClassNames.size());
	for (const std::string& Name : ClassNames)
	{
		Classes[Name] = ObjectArray::FindClassFast(Name.c_str());
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: Found '{}' instance at {}.\n", Name, Classes[Name].GetAddress());
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

	const ModuleInfo ModInfo = MD7::Globals::GMemory->GetUnrealModule();

	// One combined scan for every instance address instead of one full-module scan per class.
	std::vector<uintptr_t> Needles;
	Needles.reserve(Instances.size());
	for (const auto& [Name, Instance] : Instances)
		Needles.push_back(reinterpret_cast<uintptr_t>(Instance.GetAddress()));

	std::unordered_map<uintptr_t, std::vector<uintptr_t>> AllResults;
	ModInfo.ForEachSegment([&Needles, &AllResults](const MemRegionInfo& Region) -> bool
	{
		if (Region.IsReadable() && !Region.IsExecutable())
		{
			std::unordered_map<uintptr_t, std::vector<uintptr_t>> SegmentResults = MD7::Globals::GMemory->FindAllAlignedValuesInRangeMulti(
			    Needles,
			    sizeof(uintptr_t),
			    Region.GetStart(),
			    Region.GetSize());

			for (auto& [Addr, Hits] : SegmentResults)
			{
				std::vector<uintptr_t>& Merged = AllResults[Addr];
				Merged.insert(Merged.end(), Hits.begin(), Hits.end());
			}
		}

		// Don't stop at the first match: different classes' needles can live in different segments.
		return false;
	});

	for (const auto& [Name, Instance] : Instances)
	{
		const uintptr_t ObjAddr = reinterpret_cast<uintptr_t>(Instance.GetAddress());
		std::vector<uintptr_t>& Results = AllResults[ObjAddr];

		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: Found {} candidates for '{}'.\n", Results.size(), Name);

		// Encrypted slot fallback: retry by decrypting each candidate instead of comparing it raw.
		// Skipped when the hook is still the identity default - decrypt(Val) == Val there, so this
		// would just re-scan the whole module for free, finding nothing the first pass didn't.
		if (Results.empty() && (Name == "Engine" || Name == "World"))
		{
			const FDecryptFn<uintptr_t>& Decrypt = (Name == "Engine")
			                                            ? MD7::Globals::GDecryptCallbacks.Statics.GEngine
			                                            : MD7::Globals::GDecryptCallbacks.Statics.GWorld;

			using FDecryptFnPtr            = uintptr_t (*)(const FDecryptContext<uintptr_t>&);
			const FDecryptFnPtr* AsPlainFn = Decrypt.target<FDecryptFnPtr>();
			const bool bHasRealDecrypt     = !AsPlainFn || *AsPlainFn != &DecryptIdentity<uintptr_t>;

			if (bHasRealDecrypt)
			{
				ModInfo.ForEachSegment([&Decrypt, &Results, ObjAddr](const MemRegionInfo& Region) -> bool
				{
					if (!Region.IsReadable() || Region.IsExecutable())
						return false;

					constexpr size_t ChunkBytes = 4u * 1024u * 1024u;
					std::vector<uint8_t> Buffer;
					uintptr_t Cur        = Region.GetStart();
					const uintptr_t End  = Region.GetStart() + Region.GetSize();

					while (Cur + sizeof(uintptr_t) <= End)
					{
						const size_t ChunkSize = std::min(static_cast<size_t>(End - Cur), ChunkBytes);
						Buffer.resize(ChunkSize);

						if (MD7::Globals::GMemory->ReadBytes(Cur, Buffer.data(), ChunkSize))
						{
							for (size_t Off = 0; Off + sizeof(uintptr_t) <= ChunkSize; Off += sizeof(uintptr_t))
							{
								uintptr_t Raw;
								std::memcpy(&Raw, Buffer.data() + Off, sizeof(uintptr_t));

								if (Decrypt({.Address = Cur + Off, .RawValue = Raw}) == ObjAddr)
								{
									Results.push_back(Cur + Off);
									return true;
								}
							}
						}
						Cur += ChunkSize;
					}
					return false;
				});
			}

			if (!Results.empty())
				MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: Found an encrypted candidate for '{}' via decryption.\n", Name);
		}

		uintptr_t Result = 0;

		if (Results.size() == 1)
		{
			Result = Results[0];
		}
		else if (Results.size() == 2)
		{
			// If Results[0] changes within 50 ms it's volatile (e.g. GActiveLogWorld); resume briefly if suspended, or a frozen process can't show the change.
			const uintptr_t CandidateAddr = reinterpret_cast<uintptr_t>(Results[0]);

			const bool bWasSuspended = MD7::Globals::GMemory->IsTargetSuspended();
			if (bWasSuspended)
				MD7::Globals::GMemory->ResumeTarget();

			uintptr_t CurrentValue = MD7::Globals::GMemory->Read<uintptr_t>(CandidateAddr);
			for (int i = 0; CurrentValue == ObjAddr && i < 50; ++i)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				CurrentValue = MD7::Globals::GMemory->Read<uintptr_t>(CandidateAddr);
			}

			if (bWasSuspended)
				MD7::Globals::GMemory->SuspendTarget();

			if (CurrentValue == ObjAddr)
			{
				Result = Results[0];
			}
			else
			{
				Result = Results[1];
				MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: Filtered volatile pointer at 0x{:X} for '{}'.\n", CandidateAddr, Name);
			}
		}
		else
		{
			continue;
		}

		// Result is already the slot's address - convert straight to a module offset.
		const uintptr_t Offset = MD7::Globals::GMemory->GetUnrealModule().AddressToOffset(Result);
		FoundOffsets[Name]     = Offset;
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "FindStaticOffsets: '{}' offset at 0x{:X}.\n", Name, Offset);
	}

	return FoundOffsets;
}
