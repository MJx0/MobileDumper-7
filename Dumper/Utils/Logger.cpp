#include "Logger.h"

#include <filesystem>
#include <iostream>

#if defined(__ANDROID__)
#include <android/log.h>
#elif defined(__APPLE__)
#include <os/log.h>
#endif

std::string LogDetail::WStringToUtf8(const std::wstring& Ws)
{
	std::string Result;
	Result.reserve(Ws.size());
	for (wchar_t Wc : Ws)
	{
		const uint32_t Cp = static_cast<uint32_t>(Wc);
		if (Cp < 0x80)
		{
			Result += static_cast<char>(Cp);
		}
		else if (Cp < 0x800)
		{
			Result += static_cast<char>(0xC0 | (Cp >> 6));
			Result += static_cast<char>(0x80 | (Cp & 0x3F));
		}
		else if (Cp < 0x10000)
		{
			Result += static_cast<char>(0xE0 | (Cp >> 12));
			Result += static_cast<char>(0x80 | ((Cp >> 6) & 0x3F));
			Result += static_cast<char>(0x80 | (Cp & 0x3F));
		}
		else
		{
			Result += static_cast<char>(0xF0 | (Cp >> 18));
			Result += static_cast<char>(0x80 | ((Cp >> 12) & 0x3F));
			Result += static_cast<char>(0x80 | ((Cp >> 6) & 0x3F));
			Result += static_cast<char>(0x80 | (Cp & 0x3F));
		}
	}
	return Result;
}

FLogger::FLogger(const std::string& Path)
{
	std::filesystem::create_directories(std::filesystem::path(Path).parent_path());
	_FileStream.open(Path, std::ios::out | std::ios::trunc);
}

void FLogger::FmtWrite(ELogLevel LogLevel, const std::string& Msg)
{
#ifdef NDEBUG
	if (LogLevel == ELogLevel::Debug)
		return;
#endif

#if defined(__ANDROID__)
	switch (LogLevel)
	{
	case ELogLevel::Debug:
		__android_log_print(ANDROID_LOG_DEBUG, "MobileDumper-7", "%s", Msg.c_str());
		break;
	case ELogLevel::Info:
		__android_log_print(ANDROID_LOG_INFO, "MobileDumper-7", "%s", Msg.c_str());
		break;
	case ELogLevel::Warning:
		__android_log_print(ANDROID_LOG_WARN, "MobileDumper-7", "%s", Msg.c_str());
		break;
	case ELogLevel::Error:
		__android_log_print(ANDROID_LOG_ERROR, "MobileDumper-7", "%s", Msg.c_str());
		break;
	default:
		break;
	}

#ifdef DUMPER_BUILD_EXECUTABLE
#define COLOR_RESET  "\033[0m"
#define COLOR_BOLD   "\033[1m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RED    "\033[31m"
#define COLOR_GRAY   "\033[90m"
	std::stringstream SStream;
	switch (LogLevel)
	{
	case ELogLevel::Debug:
		SStream << COLOR_CYAN << Msg << COLOR_RESET;
		std::cout << SStream.str();
		break;
	case ELogLevel::Info:
		SStream << Msg << COLOR_RESET;
		std::cout << SStream.str();
		break;
	case ELogLevel::Warning:
		SStream << COLOR_BOLD << COLOR_YELLOW << Msg << COLOR_RESET;
		std::cout << SStream.str();
		break;
	case ELogLevel::Error:
		SStream << COLOR_BOLD << COLOR_RED << Msg << COLOR_RESET;
		std::cout << SStream.str();
		break;
	default:
		break;
	}
#endif

#elif defined(__APPLE__)
	if (LogLevel != ELogLevel::Error)
	{
		os_log(OS_LOG_DEFAULT, "MobileDumper-7: %{public}s", Msg.c_str());
	}
	else
	{
		os_log_error(OS_LOG_DEFAULT, "MobileDumper-7: %{public}s", Msg.c_str());
	}
#endif

	{
		std::lock_guard<std::mutex> Lock(_Mutex);

		if (_FileStream.is_open())
		{
			_FileStream << Msg;

			// Flushing every line costs a write syscall per log call, and the dump emits
			// thousands. Warnings and errors still flush so a crash keeps its tail.
			// if (LogLevel == ELogLevel::Warning || LogLevel == ELogLevel::Error)
			_FileStream.flush();
		}
	}

	// Invoked outside the lock: the callback must stay free to log without deadlocking.
	if (_OnDidLogMessage)
		_OnDidLogMessage(LogLevel, Msg);
}

void FLogger::CloseFileStream()
{
	std::lock_guard<std::mutex> Lock(_Mutex);
	_FileStream.flush();
	_FileStream.close();
}
