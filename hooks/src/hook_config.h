// GalEngineKit 钩子配置 — 独立实现，MIT
// 从 krkr2_hook.cpp 拆出：krkr2_font.ini 读取、日文字体名单。

#ifndef GEK_HOOK_CONFIG_H
#define GEK_HOOK_CONFIG_H

#include <windows.h>

struct HookConfig {
    wchar_t targetFont[LF_FACESIZE];
    wchar_t logPath[MAX_PATH];
    wchar_t cloneSource[MAX_PATH];
    bool    enabled;
    bool    logToFile;
    bool    replaceCharset;
    bool    cloneEnabled;
    BYTE    targetCharset;
};

extern HookConfig g_cfg;

// 读模块同目录的 krkr2_font.ini，没有就用默认值。结果同时同步到日志模块。
void HookConfigLoad();

// 命中日文字体名单才替换，避免误改普通西文字体
bool IsJapaneseFont(const wchar_t* faceName);

#endif // GEK_HOOK_CONFIG_H
