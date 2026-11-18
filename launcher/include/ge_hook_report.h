// 独立实现：本模块为 GalEngineKit 原创实现
// 钩子日志回读：游戏退出后读 galenginekit_hook.log，把关键行翻成一句结论。
// 日志格式由 hooks/src/hook_log.cpp 决定，这里只按固定前缀匹配，不解析格式细节。

#ifndef GALENGINEKIT_HOOK_REPORT_H
#define GALENGINEKIT_HOOK_REPORT_H

#include <windows.h>
#include <string>

namespace ge {

class HookReport {
public:
    // 读 game_dir 下的 galenginekit_hook.log
    // 返回 false 表示日志不存在（钩子根本没加载，或日志被关掉了）
    static bool Analyze(const std::wstring& game_dir, std::wstring& out_summary);
};

} // namespace ge

#endif // GALENGINEKIT_HOOK_REPORT_H
