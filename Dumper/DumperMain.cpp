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
			GLogger.FmtWrite(ELogLevel::Info, Description);
	}
}

void FDumperMain::SetOnProgressCallback(const std::function<void(const std::string&)>& Callback)
{
	OnProgressCallbackFn = Callback;
}

bool FDumperMain::Run(std::string& OutDumpZip, std::string& OutErrorString)
{
	auto CleanFailureReturn = []() -> bool
	{
		GLogger.CloseFileStream();
		return false;
	};

	GLogger.FmtWrite(ELogLevel::Info, "=== {} v{} ===\n", kProgramName, kProgramVer);

	std::chrono::high_resolution_clock::time_point DumpStartTime = std::chrono::high_resolution_clock::now();

	OnProgressCallback("Initializing Process Memory...\n");
	{
		if (!GMemory || !GMemory->Initialize() || !GMemory->IsMemoryAccessOk())
		{
			OutErrorString = "Memory Initialize and access failed!";
			GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}
	}

	OnProgressCallback("Extracting Game Info...\n");
	{
		InternalSettings::GameName    = GProfile->GetGameIdentifier();
		InternalSettings::GameVersion = GProfile->GetGameVersion();

		if (InternalSettings::GameName.empty())
		{
			OutErrorString = "Failed to extract game name!";
			GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}

		if (InternalSettings::GameVersion.empty())
		{
			OutErrorString = "Failed to extract game version!";
			GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
			return CleanFailureReturn();
		}
	}

	const std::string DumperDir = Generator::GetDumperFolder();

	const std::string DumperLog = DumperDir + "/MobileDumper7.log";
	GLogger.FmtWrite(ELogLevel::Info, "Log File: {}\n", DumperLog);
	GLogger.SetFileStream(DumperLog);

	GLogger.FmtWrite(ELogLevel::Info, "=== Dumper Info ===\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "Dumper Name: {}\n", kProgramName);
		GLogger.FmtWrite(ELogLevel::Info, "Dumper Version: {}\n", kProgramVer);
		GLogger.FmtWrite(ELogLevel::Info, "Dumper Repo: {}\n", kProgramRepo);
	}
	GLogger.FmtWrite(ELogLevel::Info, "==================\n");

	GLogger.FmtWrite(ELogLevel::Info, "=== Profile Info ===\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "Game: {}\n", InternalSettings::GameName);
		GLogger.FmtWrite(ELogLevel::Info, "Version: {}\n", InternalSettings::GameVersion);
		GLogger.FmtWrite(ELogLevel::Info, "Output: {}\n", GSettings.Generator.SDKGenerationPath);
	}
	GLogger.FmtWrite(ELogLevel::Info, "==================\n");

	OnProgressCallback("Initializing Unreal Module...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Unreal Module ===\n");
		{
			if (!Generator::InitUnrealModule(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing UEAnalyzerKitty...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== UEAnalyzerKitty ===\n");
		{
			if (!Generator::InitUEAnalyzerKitty(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Objects...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Objects ===\n");
		{
			if (!Generator::InitObjects(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Names...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Names ===\n");
		{
			if (!Generator::InitNames(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	// UEAnalyzerKitty no longer needed
	Generator::ReleaseAnalyzer();

	OnProgressCallback("Initializing Settings...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Settings ===\n");
		{
			if (!Generator::InitSettings(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Offsets...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Offsets ===\n");
		{
			if (!Generator::InitOffsets(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Internal Settings...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Internal Settings ===\n");
		{
			if (!Generator::InitInternalSettings(OutErrorString))
				return CleanFailureReturn();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	OnProgressCallback("Initializing Managers...\n");
	{
		GLogger.FmtWrite(ELogLevel::Info, "=== Managers ===\n");
		{
			Generator::InitManagers();
		}
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	GLogger.FmtWrite(ELogLevel::Info, "=== Generating Dumps ===\n");
	if (!GSettings.Generator.bGenerateGObjects && !GSettings.Generator.bGenerateGObjectsWithProps && !GSettings.Generator.bGenerateEditorOnlyMetadata && !GSettings.Generator.bGenerateCppSDK && !GSettings.Generator.bGenerateMapping && !GSettings.Generator.bGenerateIDAMapping && !GSettings.Generator.bGenerateDumpspace)
	{
		OutErrorString = "All generators are disabled in settings, nothing to dump!";
		GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", OutErrorString);
		return CleanFailureReturn();
	}

	if (GSettings.Generator.bGenerateGObjects)
	{
		OnProgressCallback("Generating GObjects Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::GenerateObjectsDump();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (GSettings.Generator.bGenerateGObjectsWithProps)
	{
		OnProgressCallback("Generating GObjects With Props Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::GenerateObjectsWithPropertiesDump();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (GSettings.Generator.bGenerateEditorOnlyMetadata)
	{
		OnProgressCallback("Generating EditorOnlyMetadata Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::GenerateEditorOnlyMetadataDump();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (GSettings.Generator.bGenerateCppSDK)
	{
		OnProgressCallback("Generating C++ SDK Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<CppGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (GSettings.Generator.bGenerateMapping)
	{
		OnProgressCallback("Generating Mappings Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<MappingGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (GSettings.Generator.bGenerateIDAMapping)
	{
		OnProgressCallback("Generating IDA Mappings Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<IDAMappingGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	if (GSettings.Generator.bGenerateDumpspace)
	{
		OnProgressCallback("Generating Dumpspace Dump...\n");
		{
			std::chrono::high_resolution_clock::time_point Start = std::chrono::high_resolution_clock::now();
			Generator::Generate<DumpspaceGenerator>();
			auto Elapsed = std::chrono::high_resolution_clock::now() - Start;
			GLogger.FmtWrite(ELogLevel::Info, "Generated in {}\n", Utils::ChronoDurationToString(Elapsed));
		}
	}

	auto Elapsed = std::chrono::high_resolution_clock::now() - DumpStartTime;
	GLogger.FmtWrite(ELogLevel::Info, "Dumping completed in {}\n", Utils::ChronoDurationToString(Elapsed));

	GLogger.FmtWrite(ELogLevel::Info, "Dump Folder: {}\n", DumperDir);
	GLogger.FmtWrite(ELogLevel::Info, "==================\n");

	// log file will be zipped
	GLogger.CloseFileStream();

	{
		OnProgressCallback("Zipping dump folder...\n");
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
		GLogger.FmtWrite(ELogLevel::Info, "==================\n");
	}

	return true;
}