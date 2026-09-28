// 独立实现：本模块为 GalEngineKit 原创实现
// 日志系统实现

#include "ge_logger.h"
#include <time.h>
#include <stdio.h>

namespace ge {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : level_(LogLevel::Info), file_handle_(INVALID_HANDLE_VALUE) {
    InitializeCriticalSection(&cs_);
}

Logger::~Logger() {
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
    }
    DeleteCriticalSection(&cs_);
}

bool Logger::InitFile(const std::wstring& log_dir) {
    // 创建日志目录
    CreateDirectoryW(log_dir.c_str(), nullptr);

    // 生成日志文件名：galenginekit_YYYYMMDD_HHMMSS.log
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t filename[MAX_PATH];
    swprintf_s(filename, MAX_PATH, L"%s\\galenginekit_%04d%02d%02d_%02d%02d%02d.log",
        log_dir.c_str(), st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    file_path_ = filename;
    file_handle_ = CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    // 写入 UTF-16 LE BOM
    uint8_t bom[2] = {0xFF, 0xFE};
    DWORD written;
    WriteFile(file_handle_, bom, 2, &written, nullptr);
    return true;
}

void Logger::SetLevel(LogLevel level) {
    level_ = level;
}

LogLevel Logger::GetLevel() const {
    return level_;
}

static const wchar_t* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return L"ERROR";
        case LogLevel::Warn:  return L"WARN ";
        case LogLevel::Info:  return L"INFO ";
        case LogLevel::Debug: return L"DEBUG";
        default: return L"?????";
    }
}

static WORD LevelToColor(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case LogLevel::Warn:  return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case LogLevel::Info:  return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case LogLevel::Debug: return FOREGROUND_INTENSITY;
        default: return 0;
    }
}

void Logger::Log(LogLevel level, const char* file, int line, const std::wstring& message) {
    if (static_cast<int>(level) > static_cast<int>(level_)) return;

    // 格式化时间戳
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[32];
    swprintf_s(timestamp, 32, L"%02d:%02d:%02d.%03d",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    // 提取文件名（不含路径）
    std::wstring wide_file = Utf8ToWide(file ? file : "");
    size_t pos = wide_file.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        wide_file = wide_file.substr(pos + 1);
    }

    wchar_t formatted[2048];
    swprintf_s(formatted, 2048, L"[%s] [%s] [%s:%d] %s\r\n",
        timestamp, LevelToString(level), wide_file.c_str(), line, message.c_str());

    EnterCriticalSection(&cs_);
    WriteToConsole(level, formatted);
    WriteToFile(formatted);
    LeaveCriticalSection(&cs_);
}

void Logger::Log(LogLevel level, const char* file, int line, const std::string& message) {
    Log(level, file, line, Utf8ToWide(message));
}

void Logger::WriteToConsole(LogLevel level, const std::wstring& formatted) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    bool has_console = GetConsoleScreenBufferInfo(hConsole, &csbi) != 0;

    if (has_console) {
        SetConsoleTextAttribute(hConsole, LevelToColor(level));
    }

    DWORD written;
    WriteConsoleW(hConsole, formatted.c_str(), (DWORD)formatted.size(), &written, nullptr);

    if (has_console) {
        SetConsoleTextAttribute(hConsole, csbi.wAttributes);
    }
}

void Logger::WriteToFile(const std::wstring& formatted) {
    if (file_handle_ == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file_handle_, formatted.c_str(), (DWORD)(formatted.size() * sizeof(wchar_t)), &written, nullptr);
}

} // namespace ge
