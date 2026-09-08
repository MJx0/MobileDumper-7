#pragma once

#include <functional>
#include <string>

/// @brief Public API for running and configuring the cross-platform dumper.
namespace FDumperMain
{
	/// @brief Dumper name.
	static constexpr const char* kProgramName = "MobileDumper-7";
	/// @brief Dumper version.
	static constexpr const char* kProgramVer = "1.0.0";
	/// @brief Dumper repo link.
	static constexpr const char* kProgramRepo = "https://github.com/MJx0/MobileDumper-7";

	/// @brief Sets the callback used to report dumper progress.
	/// @param Callback Receives the progress description.
	void SetOnProgressCallback(const std::function<void(const std::string&)>& Callback);


	/// @brief Runs the cross-platform dumper.
	/// @param OutDumpZip Receives the path to the generated dump ZIP file on success.
	/// @param OutErrorString Receives the error message if the dump fails.
	/// @return @c true if the dump completed successfully; otherwise @c false.
	bool Run(std::string& OutDumpZip, std::string& OutErrorString);
}
