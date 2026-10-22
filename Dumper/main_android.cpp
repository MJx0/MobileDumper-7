#ifdef __ANDROID__

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <jni.h>
#include <memory>
#include <optional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <KittyMemoryEx/KittyUtils.hpp>

#include "DumperMain.h"
#include "Settings.h"

#include "Architecture/IArchDecoder.h"
#include "Memory/IMemory.h"
#include "Memory/MemoryAndroid.h"
#include "Profile/IProfile.h"
#include "Utils/Utils.h"

#ifdef DUMPER_BUILD_EXECUTABLE
#include "Utils/argsparse/argsparse.hpp"
#define COLOR_RESET  "\033[0m"
#define COLOR_BOLD   "\033[1m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED    "\033[31m"
#define COLOR_GRAY   "\033[90m"
#define COLOR_PURPLE "\033[35m"
#endif

#include "Profile/CustomProfiles/Shared/PUBG.h"
#include "Profile/CustomProfiles/Shared/DeltaForce.h"
#include "Profile/CustomProfiles/Shared/Valorant.h"
#include "Profile/CustomProfiles/Shared/NewState.h"

inline std::vector<std::shared_ptr<IProfile>> UECustomProfiles;

std::vector<std::shared_ptr<IProfile>>& GetUECustomProfiles()
{
	if (UECustomProfiles.empty())
	{
		UECustomProfiles.push_back(std::make_shared<PUBGProfile>());
		UECustomProfiles.push_back(std::make_shared<DeltaForceProfile>());
		UECustomProfiles.push_back(std::make_shared<ValorantProfile>());
		UECustomProfiles.push_back(std::make_shared<NewStateProfile>());
	}

	return UECustomProfiles;
}

/// @brief Stops the target process for the duration of the dump.
class FProcessSuspension
{
	pid_t Pid = 0;

public:
	FProcessSuspension(const FProcessSuspension&)            = delete;
	FProcessSuspension& operator=(const FProcessSuspension&) = delete;

	explicit FProcessSuspension(pid_t InPid)
	{
		if (InPid <= 0)
			return;

		if (kill(InPid, SIGSTOP) != 0)
		{
			GLogger.FmtWrite(ELogLevel::Warning, "Failed to suspend process {}: {}\n", InPid, strerror(errno));
			return;
		}

		Pid = InPid;
		GLogger.FmtWrite(ELogLevel::Info, "Suspended process {}.\n", Pid);
	}

	~FProcessSuspension()
	{
		if (Pid <= 0)
			return;

		if (kill(Pid, SIGCONT) != 0)
			GLogger.FmtWrite(ELogLevel::Warning, "Failed to resume process {}: {}\n", Pid, strerror(errno));
		else
			GLogger.FmtWrite(ELogLevel::Info, "Resumed process {}.\n", Pid);
	}
};

bool RunDump(int GamePid, const std::string& GamePackage, EKittyMemOP MemOp = EK_MEM_OP_SYSCALL, const char* OutputDir = nullptr, bool bDumpLib = false, bool bSuspend = false)
{
	auto StartTime = std::chrono::high_resolution_clock::now();

	GSettings.Generator.SDKGenerationPath  = OutputDir ? OutputDir : KittyUtils::Android::getAppExternalFilesDir(GamePackage);
	GSettings.Generator.SDKGenerationPath += "/MobileDumper-7";
	{
		std::error_code Ec;
		std::filesystem::create_directories(GSettings.Generator.SDKGenerationPath, Ec);
		if (Ec)
		{
			GLogger.FmtWrite(ELogLevel::Error, "Failed to create output dir at \"{}\"\n", GSettings.Generator.SDKGenerationPath, Ec.message());
			GLogger.FmtWrite(ELogLevel::Error, "Error: \"{}\"\n", Ec.message());
			FDumperMain::SetOnProgressCallback(nullptr);
			GLogger.CloseFileStream();
			return false;
		}
	}

	GMemory = std::make_unique<FMemoryAndroid>(GamePid, MemOp);

	static_cast<FMemoryAndroid*>(GMemory.get())->SetProcessRealName(GamePackage);

#if defined(__LP64__)
	GArchDecoder = CreateArchDecoder(EArch::Arm64);
#else
	GArchDecoder = CreateArchDecoder(EArch::Arm32);
#endif

	for (const auto& it : GetUECustomProfiles())
	{
		for (const auto& Identifier : it->GetSupportedGames())
		{
			if (Identifier == GamePackage)
			{
				GLogger.FmtWrite(ELogLevel::Info, "Using Custom UE Profile Override ({}).\n", Identifier);
				GProfile = it;
				goto DumpLabel;
			}
		}
	}

	GLogger.FmtWrite(ELogLevel::Info, "Using Generic UE Profile.\n");
	GProfile = std::make_shared<IProfile>();

DumpLabel:
	std::string OutDumpZip, OutErr;
	bool bSuccess = false;
	bool bIsBitOk = false;
	try
	{
		std::optional<FProcessSuspension> Suspension;
		if (bSuspend && GamePid != getpid())
			Suspension.emplace(GamePid);

		bool bIsLocal64bit  = !KittyMemoryEx::getMaps(getpid(), EProcMapFilter::Contains, "/lib64/").empty();
		bool bIsRemote64bit = !KittyMemoryEx::getMaps(GamePid, EProcMapFilter::Contains, "/lib64/").empty();

		bIsBitOk = bIsLocal64bit == bIsRemote64bit;

		if (bIsBitOk)
		{
			bSuccess = FDumperMain::Run(OutDumpZip, OutErr);
		}
		else
		{
			bSuccess = false;
			OutErr   = std::format("Dumper is {}bit but target app is {}bit!",
                                 bIsLocal64bit ? "64" : "32",
                                 bIsRemote64bit ? "64" : "32");
		}
	}
	catch (const std::exception& E)
	{
		OutErr = E.what();
	}
	catch (...)
	{
		OutErr = "Unknown exception";
	}

	if (bDumpLib && GMemory && bIsBitOk)
	{
		const ModuleInfo Module = GMemory->GetUnrealModule();
		if (!Module.IsValid())
		{
			GLogger.FmtWrite(ELogLevel::Error, "Memory dump failed, Unable to locate Unreal module!\n");
		}
		else
		{
			const std::string ModuleStem = std::filesystem::path(Module.GetPathName()).stem().string();

			const std::string Destination = fmt::format("{}/{}_{}_{}_{:X}-{:X}.so",
			                                            GSettings.Generator.SDKGenerationPath,
			                                            GamePackage,
			                                            GProfile ? GProfile->GetGameVersion() : "",
			                                            ModuleStem.empty() ? "UnrealModule" : ModuleStem,
			                                            Module.GetStart(),
			                                            Module.GetEnd());

			GLogger.FmtWrite(ELogLevel::Info, "Dumping Unreal module to \"{}\"...\n", Destination);

			if (GMemory->DumpUnrealModule(Destination))
				GLogger.FmtWrite(ELogLevel::Info, "Dumped Unreal module (0x{:X} - 0x{:X}, {} mapped).\n", Module.GetStart(), Module.GetEnd(), Utils::SizeToString(Module.GetSize()));
			else
				GLogger.FmtWrite(ELogLevel::Error, "Failed to dump the Unreal module.\n");
		}
	}

	auto Elapsed = std::chrono::high_resolution_clock::now() - StartTime;
	GLogger.FmtWrite(ELogLevel::Info, "Duration: {}\n", Utils::ChronoDurationToString(Elapsed));

	if (bSuccess)
	{
		GLogger.FmtWrite(ELogLevel::Info, "Dump succeded.\n");
	}
	else
	{
		GLogger.FmtWrite(ELogLevel::Error, "Dump failed!\n");
		GLogger.FmtWrite(ELogLevel::Error, "Failure reason: \"{}\"\n", OutErr);
	}

	// clean up
	{
		FDumperMain::SetOnProgressCallback(nullptr);

		GProfile.reset();
		GetUECustomProfiles().clear();

		GArchDecoder.reset();
		GMemory.reset();

		GLogger.CloseFileStream();
	}

	return bSuccess;
}

#ifndef DUMPER_BUILD_EXECUTABLE

#if 1
__attribute__((constructor)) static void OnLibraryLoad()
{
	std::thread([]()
	{
		int SleepSec = 60;
		GLogger.FmtWrite(ELogLevel::Info, "Starting after {} seconds.\n", SleepSec);
		sleep(SleepSec);

		bool bDumpLib = false;
		RunDump(getpid(), getprogname(), EK_MEM_OP_SYSCALL, nullptr, bDumpLib, false);
	}).detach();
}
#else
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM*, void*)
{
	std::thread([]()
	{
		int SleepSec = 60;
		GLogger.FmtWrite(ELogLevel::Info, "Starting after {} seconds.\n", SleepSec);
		sleep(SleepSec);

		bool bDumpLib = false;
		RunDump(getpid(), getprogname(), EK_MEM_OP_SYSCALL, nullptr, bDumpLib, false);
	}).detach();

	return JNI_VERSION_1_6;
}
#endif

#else

bool ReadIntegerInput(int& OutputValue);
bool ChooseUserProcess(pid_t& OutPid, std::string& OutId);
bool ChooseProcessFromMultiple(const std::vector<pid_t>& Pids, const std::string& PackageName, pid_t& OutPid);
bool ChooseMemoryAccessType(int& OutMemAccessType);
bool ChooseOutputDirectory(std::string& OutOutputDir);

int main(int Argc, char** Args)
{
	setbuf(stdout, nullptr);
	setbuf(stderr, nullptr);
	setbuf(stdin, nullptr);

	argparse::ArgumentParser Program(FDumperMain::kProgramName, FDumperMain::kProgramVer);

	std::string GamePackge, OutputDir;
	pid_t GamePid     = 0;
	bool bDumpLib     = false;
	bool bSuspendGame = false;
	int MemAccessType = 0;

	Program.add_argument("-p", "--package")
	    .help("Specify game package.")
	    .store_into(GamePackge)
	    .metavar("<name>");

	Program.add_argument("-o", "--output")
	    .help("Output directory path.")
	    .store_into(OutputDir)
	    .metavar("<path>");

	Program.add_argument("-d", "--dump")
	    .help("Dump UE library from memory.")
	    .default_value(false)
	    .implicit_value(true)
	    .store_into(bDumpLib);

	Program.add_argument("-s", "--suspend")
	    .help("Send SIGSTOP to the game while dumping, then SIGCONT.")
	    .default_value(false)
	    .implicit_value(true)
	    .store_into(bSuspendGame);

	Program.add_argument("-m", "--mem")
	    .help("Specify memory access type (1: process_vm_readv, 2: pread).")
	    .scan<'i', int>()
	    .choices(1, 2)
	    .store_into(MemAccessType);

	try
	{
		Program.parse_args(Argc, Args);
	}
	catch (const std::exception& Err)
	{
		GLogger.FmtWrite(ELogLevel::Error, "✘ Command Line Error: {}\n\n", Err.what());
		GLogger.FmtWrite(ELogLevel::Info, "{}", Program.help().str());
		return 1;
	}

	if (GamePackge.empty())
	{
		if (!ChooseUserProcess(GamePid, GamePackge))
		{
			GLogger.FmtWrite(ELogLevel::Error, "No user process was selected.\n");
			return 1;
		}
	}
	else
	{
		auto GamePids = KittyMemoryEx::getProcessIDs(GamePackge);

		// Fallback: cmdline can be spoofed, UID can't.
		if (GamePids.empty())
		{
			uid_t PackageUid = 0;
			if (Utils::Android::GetPackageUid(GamePackge, PackageUid))
			{
				GamePids = Utils::Android::GetProcessIDsByUid(PackageUid);
				if (!GamePids.empty())
					GLogger.FmtWrite(ELogLevel::Warning, "No process matched \"{}\" by cmdline (possibly spoofed), Found {} by UID {} instead.\n", GamePackge, GamePids.size(), PackageUid);
			}
		}

		if (GamePids.empty())
		{
			GLogger.FmtWrite(ELogLevel::Error, "Couldn't find target package \"{}\" in the running processes.\n", GamePackge);
			return 1;
		}

		if (!ChooseProcessFromMultiple(GamePids, GamePackge, GamePid))
		{
			GLogger.FmtWrite(ELogLevel::Error, "Failed to select process ID.\n");
			return 1;
		}
	}

	if (MemAccessType == 0)
	{
		if (!ChooseMemoryAccessType(MemAccessType))
		{
			GLogger.FmtWrite(ELogLevel::Error, "Failed to select memory access type.\n");
			return 1;
		}
	}

	if (OutputDir.empty())
	{
		if (!ChooseOutputDirectory(OutputDir))
		{
			GLogger.FmtWrite(ELogLevel::Error, "Output directory path is not specified.\n");
			return 1;
		}

		if (access(OutputDir.c_str(), W_OK) != 0)
		{
			GLogger.FmtWrite(ELogLevel::Error, "Output directory path (\"{}\") is not writeable!\n", OutputDir);
			return 1;
		}
	}

	std::cout << std::endl;

	EKittyMemOP MemOp = (MemAccessType == 2) ? EK_MEM_OP_IO : EK_MEM_OP_SYSCALL;

	GLogger.FmtWrite(ELogLevel::Info, "Process ID: {}\n", GamePid);
	GLogger.FmtWrite(ELogLevel::Info, "Process Name: {}\n", GamePackge);
	GLogger.FmtWrite(ELogLevel::Info, "Memory Access: {}\n", MemOp == EK_MEM_OP_IO ? "pread" : "process_vm_readv");
	GLogger.FmtWrite(ELogLevel::Info, "Output Directory: {}\n", OutputDir);
	GLogger.FmtWrite(ELogLevel::Info, "Dump UE Library: {}\n", bDumpLib ? "Yes" : "No");
	GLogger.FmtWrite(ELogLevel::Info, "Suspend Game: {}\n", bSuspendGame ? "Yes" : "No");

	bool bSuccess = RunDump(GamePid, GamePackge, MemOp, OutputDir.c_str(), bDumpLib, bSuspendGame);
	return bSuccess ? 0 : 1;
}

bool ReadIntegerInput(int& OutputValue)
{
	std::string Input;
	if (!std::getline(std::cin, Input))
	{
		return false;
	}
	try
	{
		OutputValue = std::stoi(Input);
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

bool ChooseUserProcess(pid_t& OutPid, std::string& OutId)
{
	struct UserProcessEntry
	{
		pid_t Pid;
		std::string Name;
		std::string Role;
	};

	std::vector<UserProcessEntry> UserProcesses;

	DIR* ProcDir = opendir("/proc");
	if (!ProcDir)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Failed to open /proc — is the process running as root?\n");
		return false;
	}

	struct dirent* DirEntry;
	while ((DirEntry = readdir(ProcDir)) != nullptr)
	{
		// Only numeric subdirectories are PIDs.
		const char* DirName = DirEntry->d_name;
		pid_t Pid           = 0;
		for (const char* C = DirName; *C; ++C)
		{
			if (!isdigit(static_cast<unsigned char>(*C)))
			{
				Pid = -1;
				break;
			}
			Pid = Pid * 10 + (*C - '0');
		}
		if (Pid <= 0)
			continue;

		char CmdlinePath[255];
		snprintf(CmdlinePath, sizeof(CmdlinePath), "/proc/%d/cmdline", Pid);
		FILE* CmdlineFile = fopen(CmdlinePath, "r");
		if (!CmdlineFile)
			continue;

		char CmdlineBuf[512] = {};
		size_t CmdlineLen    = fread(CmdlineBuf, 1, sizeof(CmdlineBuf) - 1, CmdlineFile);
		fclose(CmdlineFile);

		if (CmdlineLen == 0)
			continue;

		CmdlineBuf[CmdlineLen] = '\0';
		std::string ProcName(CmdlineBuf);

		// cmdline can be spoofed, prefer the name UID resolves to.
		KittyMemoryEx::ProcStatus Status{};
		KittyMemoryEx::ProcStatus::parse(Pid, &Status);
		std::string RealName;
		if (Utils::Android::GetPackageNameByUid(static_cast<uid_t>(Status.getInt("Uid")), RealName))
			ProcName = RealName;

		if (!std::isalpha(ProcName[0]))
			continue;
		if (ProcName.find('.') == std::string::npos)
			continue;

		// An empty set means neither lookup worked, so filter nothing rather than hiding
		// every process.
		const std::unordered_set<std::string>& UserPackages = Utils::Android::GetUserInstalledPackages();
		if (!UserPackages.empty() && !UserPackages.contains(ProcName))
			continue;

		// Only matters when the set above fell back to scanning the data partition, which cannot
		// tell an updated system app from an installed one. Both namespaces are reserved.
		if (ProcName.starts_with("com.android.") || ProcName.starts_with("com.google.android."))
			continue;

		// User apps have external data directory
		if (!fs::exists(KittyUtils::Android::getAppExternalFilesDir(ProcName)))
			continue;

		std::string State = Status.getString("State");
		if (State.empty() || State[0] == 'Z' || State[0] == 'X')
			continue;

		errno = 0;
		if (kill(Pid, 0) != 0 && errno != EPERM)
			continue;

		UserProcesses.push_back({Pid, ProcName, Utils::Android::GetProcessRole(Pid, ProcName)});
	}
	closedir(ProcDir);

	if (UserProcesses.empty())
	{
		GLogger.FmtWrite(ELogLevel::Warning, "No user processes detected. Ensure the target game is running.\n");
		return false;
	}

	std::sort(UserProcesses.begin(), UserProcesses.end(), [](const UserProcessEntry& A, const UserProcessEntry& B)
	{ return A.Name < B.Name; });

	std::stringstream LayoutStream;
	LayoutStream << "\n"
	             << COLOR_BOLD << COLOR_CYAN << "┌── User Processes" << COLOR_RESET << "\n"
	             << COLOR_CYAN << "│" << COLOR_RESET << "   Select the target process:\n"
	             << COLOR_CYAN << "│" << COLOR_RESET << "\n";

	size_t MaxProcessNameLength = 0;
	size_t MaxPidLength         = 0;
	bool bAnyRoleKnown          = false;

	for (const auto& Process : UserProcesses)
	{
		MaxProcessNameLength  = std::max(MaxProcessNameLength, Process.Name.length());
		MaxPidLength          = std::max(MaxPidLength, std::to_string(Process.Pid).length());
		bAnyRoleKnown        |= Process.Role != "unregistered";
	}

	for (size_t i = 0; i < UserProcesses.size(); ++i)
	{
		const std::string AppExternalFiles = KittyUtils::Android::getAppExternalFilesDir(UserProcesses[i].Name);
		const std::string UE4GameFolder    = AppExternalFiles + "/UE4Game";
		const std::string UE5GameFolder    = AppExternalFiles + "/UnrealGame";
		const bool bIsLikelyUEGame         = access(UE4GameFolder.c_str(), F_OK) != -1 || access(UE5GameFolder.c_str(), F_OK) != -1;
		const bool bHasOBB                 = access(KittyUtils::Android::getAppObbDir(UserProcesses[i].Name).c_str(), F_OK) != -1;
		const char* Color                  = bIsLikelyUEGame ? COLOR_GREEN : (bHasOBB ? COLOR_CYAN : COLOR_PURPLE);

		LayoutStream << COLOR_CYAN << "├──" << COLOR_RESET
		             << " ["
		             << std::right
		             << std::setfill('0') << std::setw(2) << (i + 1)
		             << std::setfill(' ') << "]  "
		             << COLOR_BOLD << Color << std::left << std::setw(MaxPidLength)
		             << UserProcesses[i].Pid << COLOR_RESET
		             << "  " << COLOR_BOLD << Color << std::left << std::setw(MaxProcessNameLength)
		             << UserProcesses[i].Name << COLOR_RESET;

		if (bAnyRoleKnown)
			LayoutStream << COLOR_GRAY << "  [" << UserProcesses[i].Role << "]" << COLOR_RESET;

		LayoutStream << "\n";
	}

	LayoutStream << COLOR_CYAN << "└──" << COLOR_RESET << " Select index "
	             << COLOR_GRAY << "[1-" << UserProcesses.size() << "]" << COLOR_RESET << ": ";

	std::cout << LayoutStream.str();

	int SelectedIndex = 0;
	if (!ReadIntegerInput(SelectedIndex) || SelectedIndex < 1 || SelectedIndex > static_cast<int>(UserProcesses.size()))
	{
		GLogger.FmtWrite(ELogLevel::Error, "Invalid selection index.\n");
		return false;
	}

	OutPid = UserProcesses[SelectedIndex - 1].Pid;
	OutId  = UserProcesses[SelectedIndex - 1].Name;
	return true;
}

bool ChooseProcessFromMultiple(const std::vector<pid_t>& Pids, const std::string& PackageName, pid_t& OutPid)
{
	if (Pids.empty())
	{
		return false;
	}

	if (Pids.size() == 1)
	{
		OutPid = Pids.front();
		return true;
	}

	std::vector<std::string> Roles;
	size_t MaxPidLength = 0;
	bool bAnyRoleKnown  = false;
	for (const pid_t Pid : Pids)
	{
		Roles.push_back(Utils::Android::GetProcessRole(Pid, PackageName));
		MaxPidLength   = std::max(MaxPidLength, std::to_string(Pid).length());
		bAnyRoleKnown |= Roles.back() != "unregistered";
	}

	std::stringstream LayoutStream;
	LayoutStream << "\n"
	             << COLOR_BOLD << COLOR_YELLOW << "┌── Multiple Instances Detected" << COLOR_RESET << "\n"
	             << COLOR_YELLOW << "│" << COLOR_RESET << "   Select the target process identifier:\n"
	             << COLOR_YELLOW << "│" << COLOR_RESET << "\n";

	for (size_t i = 0; i < Pids.size(); ++i)
	{
		LayoutStream << COLOR_YELLOW << "├──" << COLOR_RESET
		             << " ["
		             << std::right
		             << std::setfill('0') << std::setw(2) << (i + 1)
		             << std::setfill(' ') << "]  "
		             << COLOR_BOLD << COLOR_YELLOW << std::left << std::setw(MaxPidLength)
		             << Pids[i] << COLOR_RESET
		             << "  " << COLOR_BOLD << COLOR_YELLOW << PackageName << COLOR_RESET;

		if (bAnyRoleKnown)
			LayoutStream << COLOR_GRAY << "  [" << Roles[i] << "]" << COLOR_RESET;

		LayoutStream << "\n";
	}

	LayoutStream << COLOR_YELLOW << "└──" << COLOR_RESET << " Select index "
	             << COLOR_GRAY << "[1-" << Pids.size() << "]" << COLOR_RESET << ": ";

	std::cout << LayoutStream.str();

	int SelectedIndex = 0;
	if (!ReadIntegerInput(SelectedIndex) || SelectedIndex < 1 || SelectedIndex > static_cast<int>(Pids.size()))
	{
		GLogger.FmtWrite(ELogLevel::Error, "Invalid selection index.");
		return false;
	}

	OutPid = Pids[SelectedIndex - 1];
	return true;
}

bool ChooseMemoryAccessType(int& OutMemAccessType)
{
	std::stringstream LayoutStream;
	LayoutStream << "\n"
	             << COLOR_BOLD << COLOR_GREEN << "┌── Memory Access Type" << COLOR_RESET << "\n"
	             << COLOR_GREEN << "│" << COLOR_RESET << "   Choose the preferred memory access type:\n"
	             << COLOR_GREEN << "│" << COLOR_RESET << "\n"
	             << COLOR_GREEN << "├──" << COLOR_RESET << " [" << COLOR_BOLD << "1" << COLOR_RESET << "] process_vm_readv\n"
	             << COLOR_GREEN << "├──" << COLOR_RESET << " [" << COLOR_BOLD << "2" << COLOR_RESET << "] pread\n"
	             << COLOR_GREEN << "└──" << COLOR_RESET << " Select type "
	             << COLOR_GRAY << "[1-2]" << COLOR_RESET << ": ";

	std::cout << LayoutStream.str();

	int SelectedType = 0;
	if (!ReadIntegerInput(SelectedType) || SelectedType < 1 || SelectedType > 2)
	{
		GLogger.FmtWrite(ELogLevel::Error, "Selected memory access type is out of range.\n");
		return false;
	}

	OutMemAccessType = SelectedType;
	return true;
}

bool ChooseOutputDirectory(std::string& OutOutputDir)
{
	std::stringstream LayoutStream;
	LayoutStream << "\n"
	             << COLOR_BOLD << COLOR_GREEN << "┌── Output Directory" << COLOR_RESET << "\n"
	             << COLOR_GREEN << "│" << COLOR_RESET << "   Specify where output files should be written:\n"
	             << COLOR_GREEN << "│" << COLOR_RESET << "\n"
	             << COLOR_GREEN << "└──" << COLOR_RESET << " Enter path: ";

	std::cout << LayoutStream.str();

	std::getline(std::cin >> std::ws, OutOutputDir);

	if (OutOutputDir.empty())
	{
		GLogger.FmtWrite(ELogLevel::Error, "Output directory path cannot be empty.\n");
		return false;
	}

	return true;
}

#endif

#endif // __ANDROID__
