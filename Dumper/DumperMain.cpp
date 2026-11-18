#include "DumperMain.h"

#include <chrono>
#include <filesystem>

#include "Settings.h"

#include "Memory/IMemory.h"
#include "Profile/IProfile.h"
#include "Utils/Logger.h"
#include "Utils/Utils.h"

#include "Generator/Generators/CppGenerator.h"
#include "Generator/Generators/DumpspaceGenerator.h"
#include "Generator/Generators/Generator.h"
#include "Generator/Generators/IDAMappingGenerator.h"
#include "Generator/Generators/MappingGenerator.h"

static std::function<void(const std::string&)> OnProgressCallbackFn = nullptr;

static void OnProgressCallback(const std::string& Description)
{
	if (OnProgressCallbackFn)
		OnProgressCallbackFn(Description);
	{
		if (!Description.empty())
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, Description);
	}
}

void FDumperMain::SetOnProgressCallback(const std::function<void(const std::string&)>& Callback)
{
	OnProgressCallbackFn = Callback;
}

bool FDumperMain::Init(std::string& OutErrorString)
{
	auto CleanFailureReturn = []() -> bool
	{
		MD7::Globals::GLogger.CloseFileStream();
		return false;
	};

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== {} v{} ===\n", kProgramName, kProgramVer);

	OnProgressCallback("Initializing Process Memory...\n");
	{
		if (!MD7::Globals::GMemory || !MD7::Globals::GMemory->Initialize() || !MD7::Globals::GMemory->IsMemoryAccessOk())
		{
			OutErrorString = "Process memory Initialize and access failed!";
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}
	}

	if (!MD7::Globals::GSettings.General.UnrealModuleName.empty())
		OnProgressCallback(fmt::format("Locating Unreal Engine module \"{}\"...\n", MD7::Globals::GSettings.General.UnrealModuleName));
	else
		OnProgressCallback("Locating Unreal Engine module (auto-detecting)...\n");
	{
		constexpr int kMaxRetries    = 5;
		constexpr int kRetryAfterSec = 5;

		ModuleInfo ModInfo{};
		int RetriesLeft = kMaxRetries;
		do
		{
			ModInfo = MD7::Globals::GMemory->GetUnrealModule();
			if (ModInfo.IsValid())
				break;

			MD7::Globals::GLogger.FmtWrite(ELogLevel::Warning, "Failed to find Unreal Engine module, retrying in {} seconds...\n", kRetryAfterSec);

			// A retry while suspended can never see a different result
			// resume briefly so the target can actually make progress, then restore suspension before continuing.
			const bool bWasSuspended = MD7::Globals::GMemory->IsTargetSuspended();
			if (bWasSuspended)
				MD7::Globals::GMemory->ResumeTarget();

			sleep(kRetryAfterSec);

			if (bWasSuspended)
				MD7::Globals::GMemory->SuspendTarget();
		} while (--RetriesLeft > 0);

		if (!ModInfo.IsValid())
		{
			OutErrorString = fmt::format("Failed to find the Unreal Engine module after {} attempts - either the game needs more time to load (wait and try again), or the dumper failed to locate it for an unrelated reason.", kMaxRetries);
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}
	}

	OnProgressCallback("Extracting Game Info...\n");
	{
		InternalSettings::GameName    = MD7::Globals::GProfile->GetGameIdentifier();
		InternalSettings::GameVersion = MD7::Globals::GProfile->GetGameVersion();

		if (InternalSettings::GameName.empty())
		{
			OutErrorString = "Failed to extract game name!";
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Game Name: \"{}\".\n", InternalSettings::GameName);

		if (InternalSettings::GameVersion.empty())
		{
			OutErrorString = "Failed to extract game version!";
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Game Version: \"{}\".\n", InternalSettings::GameVersion);
	}

	const std::string DumperDir = Generator::GetDumperFolder();

	const std::string DumperLog = DumperDir + "/MobileDumper-7.log";
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Log File: {}\n", DumperLog);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	MD7::Globals::GLogger.SetFileStream(DumperLog);

	return true;
}

bool FDumperMain::Run(std::string& OutDumpZip, std::string& OutErrorString)
{
	const std::string DumperDir = Generator::GetDumperFolder();

	std::chrono::high_resolution_clock::time_point DumpStartTime = std::chrono::high_resolution_clock::now();

	auto FinalizeAndReturn = [&](bool bSuccess) -> bool
	{
		MD7::Globals::GLogger.CloseFileStream();

		OnProgressCallback(fmt::format("Zipping dump folder with compression level {}, This could take few minutes...\n", MD7::Globals::GSettings.Generator.ZipCompressionLevel));
		{
			fs::path OutZip;
			const bool bZipOk = Generator::ZipDumperFolder(OutZip);
			OutDumpZip        = bZipOk ? OutZip.string() : DumperDir;
			OnProgressCallback(fmt::format("{}\nDump Location: {}\n", bZipOk ? "Dump zipped successfully." : "Zipping dump failed.", OutDumpZip));
			if (bZipOk)
			{
				fs::remove_all(DumperDir);
			}
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");

		return bSuccess;
	};

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Dumper Info ===\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Dumper Name: {}\n", kProgramName);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Dumper Version: {}\n", kProgramVer);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Dumper Repo: {}\n", kProgramRepo);
	}
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Profile Info ===\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Game: {}\n", InternalSettings::GameName);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Version: {}\n", InternalSettings::GameVersion);
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Output: {}\n", DumperDir);
	}
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");

	OnProgressCallback("Initializing Unreal Module...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Unreal Module ===\n");
		{
			if (!Generator::InitUnrealModule(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing UEAnalyzerKitty...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== UEAnalyzerKitty ===\n");
		{
			if (!Generator::InitUEAnalyzerKitty(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}


	OnProgressCallback("Initializing Decrypt Callbacks...\n");
	{
		{
			MD7::Globals::GDecryptCallbacks = FDecryptCallbacks{};
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Calling IProfile::OverrideDecryptCallbacks...\n");
			MD7::Globals::GProfile->OverrideDecryptCallbacks(MD7::Globals::GDecryptCallbacks);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Objects...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Objects ===\n");
		{
			if (!Generator::InitObjects(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Names...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Names ===\n");
		{
			if (!Generator::InitNames(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	// UEAnalyzerKitty no longer needed
	Generator::ReleaseAnalyzer();

	OnProgressCallback("Initializing Settings...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Settings ===\n");
		{
			if (!Generator::InitSettings(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Offsets...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Offsets ===\n");
		{
			if (!Generator::InitOffsets(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Internal Settings...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Internal Settings ===\n");
		{
			if (!Generator::InitInternalSettings(OutErrorString))
				return FinalizeAndReturn(false);
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Managers...\n");
	{
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Managers ===\n");
		{
			Generator::InitManagers();
		}
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "=== Generating Dumps ===\n");
	if (!MD7::Globals::GSettings.Generator.bGenerateGObjects && !MD7::Globals::GSettings.Generator.bGenerateGObjectsWithProps && !MD7::Globals::GSettings.Generator.bGenerateEditorOnlyMetadata && !MD7::Globals::GSettings.Generator.bGenerateCppSDK && !MD7::Globals::GSettings.Generator.bGenerateMapping && !MD7::Globals::GSettings.Generator.bGenerateIDAMapping && !MD7::Globals::GSettings.Generator.bGenerateDumpspace)
	{
		OutErrorString = "All generators are disabled in settings, nothing to dump!";
		MD7::Globals::GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
		return FinalizeAndReturn(false);
	}

	if (MD7::Globals::GSettings.Generator.bGenerateGObjects)
	{
		OnProgressCallback("Generating GObjects Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::GenerateObjectsDump();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (MD7::Globals::GSettings.Generator.bGenerateGObjectsWithProps)
	{
		OnProgressCallback("Generating GObjects With Props Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::GenerateObjectsWithPropertiesDump();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (MD7::Globals::GSettings.Generator.bGenerateEditorOnlyMetadata)
	{
		OnProgressCallback("Generating EditorOnlyMetadata Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::GenerateEditorOnlyMetadataDump();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (MD7::Globals::GSettings.Generator.bGenerateCppSDK)
	{
		OnProgressCallback("Generating C++ SDK Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<CppGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (MD7::Globals::GSettings.Generator.bGenerateMapping)
	{
		OnProgressCallback("Generating Mappings Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<MappingGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (MD7::Globals::GSettings.Generator.bGenerateIDAMapping)
	{
		OnProgressCallback("Generating IDA Mappings Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<IDAMappingGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (MD7::Globals::GSettings.Generator.bGenerateDumpspace)
	{
		OnProgressCallback("Generating Dumpspace Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<DumpspaceGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	auto Elapsed = std::chrono::high_resolution_clock::now() - DumpStartTime;
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Dumping completed in {}\n", Utils::ChronoDurationToString(Elapsed));

	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "Dump Folder: {}\n", DumperDir);
	MD7::Globals::GLogger.FmtWrite(ELogLevel::Info, "==================\n");

	return FinalizeAndReturn(true);
}