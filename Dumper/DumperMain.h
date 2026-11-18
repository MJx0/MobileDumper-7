#pragma once

#include <functional>
#include <string>

/// @brief Public API for running and configuring the cross-platform dumper.
namespace FDumperMain
{
	/// @brief Dumper name.
	static constexpr const char* kProgramName = "MobileDumper-7";
	/// @brief Dumper version.
	static constexpr const char* kProgramVer = "2.0.1";
	/// @brief Dumper repo link.
	static constexpr const char* kProgramRepo = "https://github.com/MJx0/MobileDumper-7";

	/// @brief Sets the callback used to report dumper progress.
	/// @param Callback Receives the progress description.
	void SetOnProgressCallback(const std::function<void(const std::string&)>& Callback);


	/// @brief Validates memory access, locates UE module, extracts game info from the active profile,
	/// creates the per-game dump folder, and opens the log file. 
	/// Must be called once, and must succeed before Run().
	/// @param OutErrorString Receives the error message if initialization fails.
	/// @return @c true if initialization completed successfully; otherwise @c false.
	bool Init(std::string& OutErrorString);

	/// @brief Runs the cross-platform dumper. Requires a prior successful call to Init().
	/// @param OutDumpZip Receives the path to the generated dump ZIP file, zipped from whatever
	/// partial dump folder exists even on failure. Left untouched if Init() itself failed.
	/// @param OutErrorString Receives the error message if the dump fails.
	/// @return @c true if the dump completed successfully; otherwise @c false.
	bool Run(std::string& OutDumpZip, std::string& OutErrorString);
}
