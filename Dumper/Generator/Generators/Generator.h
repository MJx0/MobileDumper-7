#pragma once

#include <filesystem>

#include "../../Engine/Unreal/UnrealObjects.h"
#include "../../UEAnalyzerKitty/UEAnalyzer.h"

#include "../HashStringTable.h"

#include "../Managers/DependencyManager.h"
#include "../Managers/MemberManager.h"


namespace fs = std::filesystem;

template <typename GeneratorType>
concept GeneratorImplementation = requires(GeneratorType t) {
	/* Require static variables of type */
	GeneratorType::PredefinedMembers;
	requires(std::same_as<decltype(GeneratorType::PredefinedMembers), PredefinedMemberLookupMapType>);

	GeneratorType::MainFolderName;
	requires(std::same_as<decltype(GeneratorType::MainFolderName), std::string>);
	GeneratorType::SubfolderName;
	requires(std::same_as<decltype(GeneratorType::SubfolderName), std::string>);

	GeneratorType::MainFolder;
	requires(std::same_as<decltype(GeneratorType::MainFolder), fs::path>);
	GeneratorType::Subfolder;
	requires(std::same_as<decltype(GeneratorType::Subfolder), fs::path>);

	/* Require static functions */
	GeneratorType::Generate();

	GeneratorType::InitPredefinedMembers();
	GeneratorType::InitPredefinedFunctions();
};

class Generator
{
private:
	friend class GeneratorTest;

private:
	static inline fs::path DumperFolder;
	static inline UEAnalyzerKitty::UEAnalyzer Analyzer;

public:
	static bool InitUnrealModule(std::string& OutErrorString);
	static bool InitUEAnalyzerKitty(std::string& OutErrorString);
	static bool InitObjects(std::string& OutErrorString);
	static bool InitNames(std::string& OutErrorString);
	static bool InitSettings(std::string& OutErrorString);
	static bool InitOffsets(std::string& OutErrorString);
	static bool InitInternalSettings(std::string& OutErrorString);
	static void InitManagers();

	/* Releases UEAnalyzerKitty's heavy scan state. Safe once InitObjects/InitNames are done -
	 * nothing after them calls Analyzer.Find(), and every result it returns is a self-contained
	 * copy, not a view into that state. */
	static void ReleaseAnalyzer();

private:
	static bool SetupDumperFolder();
	static bool SetupFolders(std::string& FolderName, fs::path& OutFolder);
	static bool SetupFolders(std::string& FolderName, fs::path& OutFolder, std::string& SubfolderName, fs::path& OutSubFolder);

public:
	static bool ZipDumperFolder(fs::path& OutZipPath);

	inline static std::string GetDumperFolder()
	{
		if (DumperFolder.empty())
		{
			SetupDumperFolder();
		}

		return DumperFolder;
	}

	static void GenerateObjectsDump(bool bWithPathname = false);
	static void GenerateObjectsWithPropertiesDump(bool bWithPathname = false);
	static void GenerateEditorOnlyMetadataDump();

public:
	template <GeneratorImplementation GeneratorType>
	static void Generate()
	{
		if (DumperFolder.empty())
		{
			if (!SetupDumperFolder())
				return;
		}

		if (!SetupFolders(GeneratorType::MainFolderName, GeneratorType::MainFolder, GeneratorType::SubfolderName, GeneratorType::Subfolder))
			return;

		GeneratorType::InitPredefinedMembers();
		GeneratorType::InitPredefinedFunctions();

		MemberManager::SetPredefinedMemberLookupPtr(&GeneratorType::PredefinedMembers);

		GeneratorType::Generate();
	};
};
