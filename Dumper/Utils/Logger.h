#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include <fmt/format.h>
#include <fmt/xchar.h>

namespace LogDetail
{
	// wchar_t is 4-byte UTF-32 on Android/iOS — encode each codepoint as UTF-8.
	std::string WStringToUtf8(const std::wstring& Ws);
}

enum class ELogLevel
{
	Debug,
	Info,
	Warning,
	Error
};

class FLogger
{
	std::ofstream _FileStream;
	std::function<void(ELogLevel LogLevel, const std::string& Msg)> _OnDidLogMessage = nullptr;

	// The dumper logs from a worker thread while the UI logs from the main thread, and
	// CloseFileStream() can land between another thread's is_open() check and its write.
	std::mutex _Mutex;

public:
	explicit FLogger() = default;
	explicit FLogger(const std::string& Path);

	inline void SetFileStream(const std::string& FilePath)
	{
		std::lock_guard<std::mutex> Lock(_Mutex);
		_FileStream.close();
		std::filesystem::create_directories(std::filesystem::path(FilePath).parent_path());
		_FileStream.open(FilePath, std::ios::out | std::ios::trunc);
	}

	inline void SetOnDidLogMessage(const std::function<void(ELogLevel LogLevel, const std::string& Msg)>& Func)
	{
		std::lock_guard<std::mutex> Lock(_Mutex);
		_OnDidLogMessage = Func;
	}

	void FmtWrite(ELogLevel LogLevel, const std::string& Msg);

	inline void FmtWrite(ELogLevel LogLevel, const std::wstring& Msg)
	{
		FmtWrite(LogLevel, LogDetail::WStringToUtf8(Msg));
	}

	template <typename... Args>
	    requires(sizeof...(Args) > 0)
	inline void FmtWrite(ELogLevel LogLevel, fmt::wformat_string<Args...> FmtStr, Args&&... FmtArgs)
	{
		FmtWrite(LogLevel, fmt::format(FmtStr, std::forward<Args>(FmtArgs)...));
	}

	template <typename... Args>
	    requires(sizeof...(Args) > 0)
	inline void FmtWrite(ELogLevel LogLevel, fmt::format_string<Args...> FmtStr, Args&&... FmtArgs)
	{
		FmtWrite(LogLevel, fmt::format(FmtStr, std::forward<Args>(FmtArgs)...));
	}

	void CloseFileStream();
};

inline FLogger GLogger{};
