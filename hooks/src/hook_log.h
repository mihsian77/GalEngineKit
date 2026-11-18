// GalEngineKit 钩子日志 — 独立实现，MIT
// 从 krkr2_hook.cpp 拆出：OutputDebugString + 可选文件日志。
// 日志路径和开关由配置加载后重设，加载前的日志走默认路径。

#ifndef GEK_HOOK_LOG_H
#define GEK_HOOK_LOG_H

#include <windows.h>

void HookLogInit(const wchar_t* path, bool toFile);
void HookLog(const wchar_t* fmt, ...);

#endif // GEK_HOOK_LOG_H
