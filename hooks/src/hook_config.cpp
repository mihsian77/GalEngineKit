// GalEngineKit 钩子配置实现 — 独立实现，MIT

#include "hook_config.h"
#include "hook_log.h"
#include <wchar.h>

HookConfig g_cfg = {
    L"Microsoft YaHei",                     // targetFont
    L"galenginekit_hook.log",               // logPath
    L"",                                    // cloneSource
    true,                                   // enabled
    true,                                   // logToFile
    true,                                   // replaceCharset
    false,                                  // cloneEnabled
    GB2312_CHARSET                          // targetCharset (134)
};

// 日文字体黑名单（精确匹配，命中即替换）
static const wchar_t* const g_jpBlacklist[] = {
    L"MS Gothic", L"MS PGothic", L"MS UI Gothic",
    L"MS Mincho", L"MS PMincho", L"MS UIGothic",
    L"Meiryo", L"Meiryo UI",
    L"Yu Gothic", L"Yu Gothic UI", L"Yu Mincho", L"Yu Mincho Demibold",
    L"IPA Gothic", L"IPA Mincho",
    L"HGP", L"HGS", L"HG",               // 前缀：华康 HG 系（galgame 常用）
    L"Osaka",
};

bool IsJapaneseFont(const wchar_t* faceName) {
    if (!faceName || !faceName[0]) return false;
    for (size_t i = 0; i < _countof(g_jpBlacklist); i++) {
        if (_wcsicmp(faceName, g_jpBlacklist[i]) == 0) return true;
    }
    return false;
}

void HookConfigLoad() {
    wchar_t iniPath[MAX_PATH];
    GetModuleFileNameW(NULL, iniPath, MAX_PATH);
    wchar_t* slash = wcsrchr(iniPath, L'\\');
    if (slash) wcscpy(slash + 1, L"krkr2_font.ini");
    else wcscpy(iniPath, L"krkr2_font.ini");

    DWORD attrs = GetFileAttributesW(iniPath);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        HookLog(L"未找到 %s，使用默认配置", iniPath);
        return;
    }

    GetPrivateProfileStringW(L"Font", L"TargetFont", g_cfg.targetFont,
                             g_cfg.targetFont, LF_FACESIZE, iniPath);
    GetPrivateProfileStringW(L"Font", L"LogPath", g_cfg.logPath,
                             g_cfg.logPath, MAX_PATH, iniPath);
    g_cfg.enabled = (GetPrivateProfileIntW(L"Font", L"Enabled", 1, iniPath) != 0);
    g_cfg.logToFile = (GetPrivateProfileIntW(L"Font", L"LogToFile", 1, iniPath) != 0);
    g_cfg.replaceCharset = (GetPrivateProfileIntW(L"Font", L"ReplaceCharset", 1, iniPath) != 0);
    g_cfg.targetCharset = (BYTE)GetPrivateProfileIntW(L"Font", L"TargetCharset",
                                                      GB2312_CHARSET, iniPath);
    g_cfg.cloneEnabled = (GetPrivateProfileIntW(L"Font", L"CloneEnabled", 0, iniPath) != 0);
    GetPrivateProfileStringW(L"Font", L"CloneSource", L"", g_cfg.cloneSource,
                             MAX_PATH, iniPath);
    // 相对路径以模块（游戏）目录为基准
    if (g_cfg.cloneSource[0] && g_cfg.cloneSource[0] != L'/' && g_cfg.cloneSource[0] != L'\\'
        && !(g_cfg.cloneSource[0] && g_cfg.cloneSource[1] == L':')) {
        wchar_t base[MAX_PATH];
        GetModuleFileNameW(NULL, base, MAX_PATH);
        wchar_t* dirSlash = wcsrchr(base, L'\\');
        if (dirSlash) {
            wchar_t rel[MAX_PATH];
            wcsncpy(rel, g_cfg.cloneSource, MAX_PATH - 1); rel[MAX_PATH - 1] = 0;
            *(dirSlash + 1) = 0;
            wcscat(base, rel);
            wcsncpy(g_cfg.cloneSource, base, MAX_PATH - 1); g_cfg.cloneSource[MAX_PATH - 1] = 0;
        }
    }

    HookLogInit(g_cfg.logPath, g_cfg.logToFile);

    HookLog(L"配置加载完成: 目标字体='%s' charset=%d enabled=%d log=%d",
            g_cfg.targetFont, (int)g_cfg.targetCharset, g_cfg.enabled, g_cfg.logToFile);
}
