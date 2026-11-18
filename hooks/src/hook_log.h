// GalEngineKit 钩子日志 — 独立实现，MIT
// 格式固定：时间 级别 来源 内容，便于 grep 和贴出来求助。
// 文件超过上限时轮转一次，不无限增长。

#ifndef GEK_HOOK_LOG_H
#define GEK_HOOK_LOG_H

#include <windows.h>

// 日志文件上限，超过就重命名成 .old 再重新开始
#define HOOK_LOG_MAX_BYTES (1024 * 1024)

enum HookLogLevel { HOOK_LOG_INFO = 0, HOOK_LOG_WARN = 1, HOOK_LOG_ERROR = 2 };

void HookLogInit(const wchar_t* path, bool toFile);
void HookLog(const wchar_t* fmt, ...);        // INFO
void HookLogWarn(const wchar_t* fmt, ...);    // WARN
void HookLogError(const wchar_t* fmt, ...);   // ERROR

#endif // GEK_HOOK_LOG_H
