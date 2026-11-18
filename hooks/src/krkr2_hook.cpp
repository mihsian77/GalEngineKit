// GalEngineKit krkr2_hook — KiriKiri2 中文字体替换钩子
// 独立实现，MIT License
//
// 变更动机：KiriKiri2 (TVP) 中文显示问题的根源是游戏请求的日文字体
//   （MS Gothic / MS Mincho / Meiryo 等）缺少 CJK 汉字字形。钩子通过
//   拦截 GetProcAddress 动态获取的字体 API，将日文字体替换为系统中文字体。
// 影响范围：仅 hooks 模块 krkr2_hook 目标；不触及 launcher 核心。
// 回滚方案：git revert 本提交；Release 资产回退旧版。
//
// 设计约束（Winlator/Box64 转译环境安全）：
//   - 只改 PE 导入表数据区（IAT），不修改代码段，不触发翻译缓存失效
//   - 不钩 Wine 内置 DLL 的导出，钩子只拦截游戏进程自身的 IAT
//
// 实测结论（NVL Maker 样本）：
//   - krkr.exe 从 gdi32 仅静态导入 BitBlt，字体 API 全部经 GetProcAddress
//     动态获取 → IAT 钩 gdi32 不命中。改为 IAT 钩 kernel32!GetProcAddress，
//     按名字拦截字体 API 返回替换函数。
//
// 模块划分（V1-H 拆分，逻辑原样搬运，未改行为）：
//   hook_log     日志（OutputDebugString + 可选文件）
//   hook_config  krkr2_font.ini 读取与日文字体名单
//   font_policy  HFONT 反向伪装注册表、字体存在性探测、克隆调度
//   iat_hook     PE 导入表遍历与 IAT 槽位替换
//   本文件       拦截函数、钩子安装、导出
//
// 已知限制：GetTextMetricsW 未钩（返回替换字体真实度量）；预渲染字体缓存
//     （mapPrerenderedFont / TFT）未处理，留待后续引擎级钩子。

#include "ge_hook_api.h"
#include "hook_config.h"
#include "hook_log.h"
#include "font_policy.h"
#include "iat_hook.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

// ===== 原始函数指针（转发调用） =====
typedef HFONT(WINAPI* PFN_CreateFontW)(int, int, int, int, DWORD, DWORD, DWORD, DWORD,
                                       DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef int  (WINAPI* PFN_GetObjectW)(HANDLE, int, LPVOID);
typedef BOOL (WINAPI* PFN_DeleteObject)(HGDIOBJ);
typedef FARPROC(WINAPI* PFN_GetProcAddress)(HMODULE, LPCSTR);
static PFN_CreateFontIndirectW g_origCreateFontIndirectW = NULL;
static PFN_CreateFontW g_origCreateFontW = NULL;
static PFN_GetTextFaceW g_origGetTextFaceW = NULL;
static PFN_GetObjectW g_origGetObjectW = NULL;
static PFN_DeleteObject g_origDeleteObject = NULL;
static PFN_GetProcAddress g_origGetProcAddress = NULL;

// ===== 钩子替换函数 =====
static HFONT WINAPI HookCreateFontIndirectW(const LOGFONTW* lf) {
    if (g_cfg.enabled && lf && lf->lfFaceName[0] && IsJapaneseFont(lf->lfFaceName)) {
        LOGFONTW orig = *lf;                 // 保留原始请求，供反向伪装
        // 系统缺这个日文字体时，先克隆顶上（按原名创建，不做替换）
        if (ProbeFontExists(lf->lfFaceName, g_origCreateFontIndirectW, g_origGetTextFaceW) == FS_MISSING
            && CloneEnsure(lf->lfFaceName)) {
            HFONT h = g_origCreateFontIndirectW(lf);
            RegisterFont(h, &orig);
            HookLog(L"CreateFontIndirectW: '%s' 系统缺失，克隆字体命中", lf->lfFaceName);
            return h;
        }
        LOGFONTW newLf = *lf;
        wcsncpy(newLf.lfFaceName, g_cfg.targetFont, LF_FACESIZE - 1);
        newLf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        if (g_cfg.replaceCharset) newLf.lfCharSet = g_cfg.targetCharset;
        HookLog(L"CreateFontIndirectW: '%s' -> '%s' (h=%d weight=%d charset=%d->%d)",
                lf->lfFaceName, g_cfg.targetFont, lf->lfHeight, lf->lfWeight,
                (int)lf->lfCharSet, (int)newLf.lfCharSet);
        HFONT h = g_origCreateFontIndirectW(&newLf);
        RegisterFont(h, &orig);
        return h;
    }
    return g_origCreateFontIndirectW(lf);
}

static HFONT WINAPI HookCreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation,
                                    DWORD cWeight, DWORD bItalic, DWORD bUnderline,
                                    DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision,
                                    DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily,
                                    LPCWSTR pszFaceName) {
    if (g_cfg.enabled && pszFaceName && IsJapaneseFont(pszFaceName)) {
        // 系统缺这个日文字体时，先克隆顶上（按原名创建，不做替换）
        if (ProbeFontExists(pszFaceName, g_origCreateFontIndirectW, g_origGetTextFaceW) == FS_MISSING
            && CloneEnsure(pszFaceName)) {
            LOGFONTW orig = {};
            orig.lfHeight = cHeight; orig.lfWidth = cWidth;
            orig.lfEscapement = cEscapement; orig.lfOrientation = cOrientation;
            orig.lfWeight = (LONG)cWeight;
            orig.lfItalic = (BYTE)bItalic; orig.lfUnderline = (BYTE)bUnderline;
            orig.lfStrikeOut = (BYTE)bStrikeOut; orig.lfCharSet = (BYTE)iCharSet;
            orig.lfOutPrecision = (BYTE)iOutPrecision; orig.lfClipPrecision = (BYTE)iClipPrecision;
            orig.lfQuality = (BYTE)iQuality; orig.lfPitchAndFamily = (BYTE)iPitchAndFamily;
            wcsncpy(orig.lfFaceName, pszFaceName, LF_FACESIZE - 1);
            HFONT h = g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                                        bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
                                        iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
            RegisterFont(h, &orig);
            HookLog(L"CreateFontW: '%s' 系统缺失，克隆字体命中", pszFaceName);
            return h;
        }
        LOGFONTW orig = {};
        orig.lfHeight = cHeight; orig.lfWidth = cWidth;
        orig.lfEscapement = cEscapement; orig.lfOrientation = cOrientation;
        orig.lfWeight = (LONG)cWeight;
        orig.lfItalic = (BYTE)bItalic; orig.lfUnderline = (BYTE)bUnderline;
        orig.lfStrikeOut = (BYTE)bStrikeOut; orig.lfCharSet = (BYTE)iCharSet;
        orig.lfOutPrecision = (BYTE)iOutPrecision; orig.lfClipPrecision = (BYTE)iClipPrecision;
        orig.lfQuality = (BYTE)iQuality; orig.lfPitchAndFamily = (BYTE)iPitchAndFamily;
        wcsncpy(orig.lfFaceName, pszFaceName, LF_FACESIZE - 1);

        DWORD cs = g_cfg.replaceCharset ? (DWORD)g_cfg.targetCharset : iCharSet;
        HookLog(L"CreateFontW: '%s' -> '%s' (h=%d weight=%d charset=%d->%d)",
                pszFaceName, g_cfg.targetFont, cHeight, cWeight, (int)iCharSet, (int)cs);
        HFONT h = g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                                    bItalic, bUnderline, bStrikeOut, cs, iOutPrecision,
                                    iClipPrecision, iQuality, iPitchAndFamily, g_cfg.targetFont);
        RegisterFont(h, &orig);
        return h;
    }
    return g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                             bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
                             iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
}

// ===== 反向伪装：引擎反查字体身份时返回原始字体 =====
static int WINAPI HookGetTextFaceW(HDC hdc, int cCount, LPWSTR pszFace) {
    if (g_cfg.enabled && hdc) {
        HFONT cur = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
        LOGFONTW orig;
        if (cur && LookupFont(cur, &orig)) {
            int n = (int)wcslen(orig.lfFaceName);
            if (!pszFace) return n + 1;
            if (cCount <= 0) return 0;
            int copy = (n < cCount - 1) ? n : (cCount - 1);
            wcsncpy(pszFace, orig.lfFaceName, copy);
            pszFace[copy] = L'\0';
            return copy;
        }
    }
    return g_origGetTextFaceW(hdc, cCount, pszFace);
}

static int WINAPI HookGetObjectW(HANDLE hObj, int cbBuffer, LPVOID pvBuffer) {
    if (g_cfg.enabled && hObj) {
        LOGFONTW orig;
        if (LookupFont((HFONT)hObj, &orig)) {
            if (!pvBuffer) return (int)sizeof(LOGFONTW);
            int n = (cbBuffer < (int)sizeof(LOGFONTW)) ? cbBuffer : (int)sizeof(LOGFONTW);
            memcpy(pvBuffer, &orig, n);
            return n;
        }
    }
    return g_origGetObjectW(hObj, cbBuffer, pvBuffer);
}

static BOOL WINAPI HookDeleteObject(HGDIOBJ hObj) {
    // 无活动字体时直接转发，避免渲染热路径上的无谓扫描
    if (ActiveFontCount() > 0 && hObj) EvictFont((HFONT)hObj);
    return g_origDeleteObject(hObj);
}

// ===== GetProcAddress 拦截 =====
// KiriKiri 的字体 API 全部动态获取，拦截 GetProcAddress 是唯一可行注入点。
// 注意：安装 GetProcAddress 钩子前必须先保存全部原始函数地址，
// 否则后续获取原始函数会递归进入自己的钩子。
static FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    if (g_cfg.enabled && lpProcName && ((ULONG_PTR)lpProcName > 0xFFFF)) {
        // 按名称导入（序号 < 0xFFFF 直接转发）。日志用固定宽字符串，避免宽窄混用。
        if (_stricmp(lpProcName, "CreateFontIndirectW") == 0) {
            HookLog(L"GetProcAddress 拦截: CreateFontIndirectW");
            return (FARPROC)HookCreateFontIndirectW;
        }
        if (_stricmp(lpProcName, "CreateFontW") == 0) {
            HookLog(L"GetProcAddress 拦截: CreateFontW");
            return (FARPROC)HookCreateFontW;
        }
        if (_stricmp(lpProcName, "GetTextFaceW") == 0) {
            return (FARPROC)HookGetTextFaceW;
        }
        if (_stricmp(lpProcName, "GetObjectW") == 0) {
            return (FARPROC)HookGetObjectW;
        }
        if (_stricmp(lpProcName, "DeleteObject") == 0) {
            return (FARPROC)HookDeleteObject;
        }
    }
    return g_origGetProcAddress(hModule, lpProcName);
}

// ===== 钩子安装 =====
static void InstallHooks() {
    // 1. 先获取全部原始 API 地址（GetProcAddress 钩子未装，不会递归）
    HMODULE gdi32 = LoadLibraryA("gdi32.dll");
    if (!gdi32) {
        HookLog(L"错误: 无法加载 gdi32.dll");
        return;
    }
    g_origCreateFontIndirectW = (PFN_CreateFontIndirectW)GetProcAddress(gdi32, "CreateFontIndirectW");
    g_origCreateFontW         = (PFN_CreateFontW)GetProcAddress(gdi32, "CreateFontW");
    g_origGetTextFaceW        = (PFN_GetTextFaceW)GetProcAddress(gdi32, "GetTextFaceW");
    g_origGetObjectW          = (PFN_GetObjectW)GetProcAddress(gdi32, "GetObjectW");
    g_origDeleteObject        = (PFN_DeleteObject)GetProcAddress(gdi32, "DeleteObject");
    HookLog(L"原始 API: CreateFontIndirectW=0x%p GetTextFaceW=0x%p GetObjectW=0x%p DeleteObject=0x%p",
            (void*)g_origCreateFontIndirectW, (void*)g_origGetTextFaceW,
            (void*)g_origGetObjectW, (void*)g_origDeleteObject);

    if (!g_origCreateFontIndirectW || !g_origGetTextFaceW ||
        !g_origGetObjectW || !g_origDeleteObject) {
        HookLog(L"错误: 部分原始字体 API 解析失败，放弃安装");
        return;
    }

    // 2. 钩 GetProcAddress（kernel32 静态导入，IAT 必命中）
    bool ok = IatPatch("kernel32.dll", "GetProcAddress",
                       (void*)HookGetProcAddress, (void**)&g_origGetProcAddress);
    if (ok) {
        HookLog(L"GetProcAddress 钩子安装成功（创建 + 身份查询 + 删除）");
    } else {
        HookLog(L"错误: kernel32 GetProcAddress 未在导入表中");
    }
}

// ===== 共享安装入口 =====
// 两种交付形态都调用它：
//   - 注入形态 krkr2_hook.dll（定义 GEK_KRKR2_STANDALONE，自带 DllMain）
//   - 代理形态 version.dll（version_proxy.cpp 提供 DllMain，转发 version 导出）
// 不含 DisableThreadLibraryCalls：由各形态自己的 DllMain 用自身模块句柄调用。
extern "C" void Krkr2InstallHooks(void) {
    FontRegistryInit();
    HookLogInit(L"galenginekit_hook.log", true);   // 配置读出来之前的日志走默认路径
    HookConfigLoad();
    InstallHooks();
    HookLog(L"krkr2 钩子核心安装完成");
}

#ifdef GEK_KRKR2_STANDALONE

// ===== DLL 入口（独立注入形态） =====
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Krkr2InstallHooks();
    }
    return TRUE;
}

// ===== ge_hook_api 导出 =====
static const GEK_HookInfo g_hookInfo = {
    GEK_HOOK_API_VERSION,
    L"krkr2_font",
    L"0.2.0",
    L"KiriKiri2",
    L"KiriKiri2 中文字体替换钩子（charset 替换 + 反向伪装）",
    0
};

extern "C" __declspec(dllexport)
const GEK_HookInfo* GEK_HookGetInfo(void) {
    return &g_hookInfo;
}

extern "C" __declspec(dllexport)
int GEK_HookInitialize(const GEK_HookInitParams* params) {
    if (!params || params->size < sizeof(GEK_HookInitParams)) {
        HookLog(L"初始化失败：参数无效");
        return 1;
    }
    HookLog(L"初始化: profile=%s", params->profile_name ? params->profile_name : L"(null)");
    return 0;
}

extern "C" __declspec(dllexport)
void GEK_HookShutdown(void) {
    HookLog(L"关闭钩子");
}
#endif // GEK_KRKR2_STANDALONE
