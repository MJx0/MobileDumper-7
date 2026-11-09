#include "IProfile.h"

#include <KittyMemoryEx/KittyMemoryEx.hpp>

std::string IProfile::GetGameIdentifier() const
{
	return MD7::Globals::GMemory->GetProcessName();
}

std::string IProfile::GetGameVersion() const
{
	std::string Pattern = ".*/";
	for (char C : MD7::Globals::GMemory->GetProcessName())
	{
		if (C == '.')
			Pattern += '\\';
		Pattern += C;
	}

	Pattern += "-[^/]*/base\\.apk$";

	std::vector<KittyMemoryEx::ProcMap> ApkMaps = KittyMemoryEx::getMaps(MD7::Globals::GMemory->GetProcessID(), KittyMemoryEx::EProcMapFilter::Regex, Pattern);
	if (ApkMaps.empty())
		return "";

	const std::string& ApkPath = ApkMaps[0].pathname;

	std::string Version = "";
	{
		void* Data      = nullptr;
		size_t DataSize = 0;
		if (Utils::Zip::ExtractZipEntryToMemory(ApkPath, "AndroidManifest.xml", &Data, &DataSize))
		{
			Version = Utils::Apk::GetApkVersion(static_cast<const uint8_t*>(Data), DataSize);

			free(Data);
		}
	}

	return Version;
}