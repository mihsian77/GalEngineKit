// 独立实现：本模块为 GalEngineKit 原创实现
// 日志系统：四级日志，同时输出到控制台和文件，线程安全

#ifndef GALENGINEKIT_LOGGER_H
#define GALENGINEKIT_LOGGER_H

#include "ge_types.h"
#include <string>

namespace ge {

class Logger {
public:
    // 获取单例
    static Logger& Instance();

    // 初始化日志文件（可选，不调用则只输出到控制台）
    bool InitFile(const std::wstring& log_dir);

    // 设置日志级别
    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;

    // 输出日志
    void Log(LogLevel level, const char* file, int line, const std::wstring& message);
    void Log(LogLevel level, const char* file, int line, const std::string& message);

    // 便捷宏
    #define GE_LOG_ERROR(msg) ::ge::Logger::Instance().Log(::ge::LogLevel::Error, __FILE__, __LINE__, msg)
    #define GE_LOG_WARN(msg)  ::ge::Logger::Instance().Log(::ge::LogLevel::Warn,  __FILE__, __LINE__, msg)
    #define GE_LOG_INFO(msg)  ::ge::Logger::Instance().Log(::ge::LogLevel::Info,  __FILE__, __LINE__, msg)
    #define GE_LOG_DEBUG(msg) ::ge::Logger::Instance().Log(::ge::LogLevel::Debug, __FILE__, __LINE__, msg)

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void WriteToConsole(LogLevel level, const std::wstring& formatted);
    void WriteToFile(const std::wstring& formatted);

    CRITICAL_SECTION cs_;
    LogLevel level_;
    HANDLE file_handle_;
    std::wstring file_path_;
};

} // namespace ge

#endif // GALENGINEKIT_LOGGER_H
