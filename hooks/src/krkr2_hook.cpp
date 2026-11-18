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
// V1-A 深化（本次）：
//   1. charset 替换：替换字体时同步把 lfCharSet 从日文字符集改为 GB2312，
//      否则 GDI 仍按 Shift-JIS 选字形，中文可能不显示。
//   2. 反向伪装：注册表记录 HFONT→原始 LOGFONT；引擎经 GetTextFaceW /
//      GetObjectW 反查字体身份时返回原字体名与原 charset，避免引擎察觉
//      字体被换而触发重建或走偏。DeleteObject 时清理注册表防止句柄复用误判。
//   已知限制：GetTextMetricsW 未钩（返回替换字体真实度量）；预渲染字体缓存
//     （mapPrerenderedFont / TFT）未处理，留待后续引擎级钩子。

#include "ge_hook_api.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// ===== 配置 =====
static wchar_t g_targetFont[LF_FACESIZE] = L"Microsoft YaHei";
static bool g_enabled = true;
static bool g_logToFile = true;
static wchar_t g_logPath[MAX_PATH] = L"galenginekit_hook.log";
static bool g_replaceCharset = true;
static BYTE g_targetCharset = GB2312_CHARSET;   // 134

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

// ===== 原始函数指针（转发调用） =====
typedef HFONT(WINAPI* PFN_CreateFontIndirectW)(const LOGFONTW*);
typedef HFONT(WINAPI* PFN_CreateFontW)(int, int, int, int, DWORD, DWORD, DWORD, DWORD,
                                       DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef int  (WINAPI* PFN_GetTextFaceW)(HDC, int, LPWSTR);
typedef int  (WINAPI* PFN_GetObjectW)(HANDLE, int, LPVOID);
typedef BOOL (WINAPI* PFN_DeleteObject)(HGDIOBJ);
typedef FARPROC(WINAPI* PFN_GetProcAddress)(HMODULE, LPCSTR);
static PFN_CreateFontIndirectW g_origCreateFontIndirectW = NULL;
static PFN_CreateFontW g_origCreateFontW = NULL;
static PFN_GetTextFaceW g_origGetTextFaceW = NULL;
static PFN_GetObjectW g_origGetObjectW = NULL;
static PFN_DeleteObject g_origDeleteObject = NULL;
static PFN_GetProcAddress g_origGetProcAddress = NULL;

// ===== 字体注册表（反向伪装用） =====
// 紧凑数组：活动条目位于 [0, count)，淘汰时与末尾交换，查询只扫活动条目。
// 游戏同时存活的字体数量很小，线性扫描开销可接受。
#define FONT_REG_CAP 128
static struct { HFONT h; LOGFONTW orig; } g_fontReg[FONT_REG_CAP];
static int g_fontRegCount = 0;
static CRITICAL_SECTION g_fontRegLock;
static bool g_fontRegLockReady = false;

static void RegisterFont(HFONT h, const LOGFONTW* orig) {
    if (!h || !g_fontRegLockReady) return;
    EnterCriticalSection(&g_fontRegLock);
    if (g_fontRegCount < FONT_REG_CAP) {
        g_fontReg[g_fontRegCount].h = h;
        g_fontReg[g_fontRegCount].orig = *orig;
        g_fontRegCount++;
    }
    LeaveCriticalSection(&g_fontRegLock);
}

static bool LookupFont(HFONT h, LOGFONTW* out) {
    if (!h || !g_fontRegLockReady) return false;
    bool found = false;
    EnterCriticalSection(&g_fontRegLock);
    for (int i = 0; i < g_fontRegCount; i++) {
        if (g_fontReg[i].h == h) { *out = g_fontReg[i].orig; found = true; break; }
    }
    LeaveCriticalSection(&g_fontRegLock);
    return found;
}

static void EvictFont(HFONT h) {
    if (!h || !g_fontRegLockReady) return;
    EnterCriticalSection(&g_fontRegLock);
    for (int i = 0; i < g_fontRegCount; i++) {
        if (g_fontReg[i].h == h) {
            g_fontReg[i] = g_fontReg[g_fontRegCount - 1];
            g_fontRegCount--;
            break;
        }
    }
    LeaveCriticalSection(&g_fontRegLock);
}

// ===== 日志 =====
static void HookLogV(const wchar_t* fmt, va_list args) {
    wchar_t buffer[1024];
    _vsnwprintf(buffer, _countof(buffer), fmt, args);

    OutputDebugStringW(buffer);

    if (g_logToFile) {
        // 转 UTF-8 写入日志文件（MinGW 的 fwprintf 不支持 ccs= 参数）
        int len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            FILE* fp = _wfopen(g_logPath, L"a");
            if (fp) {
                char* utf8 = (char*)_malloca(len);
                if (utf8) {
                    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, len, NULL, NULL);
                    fputs("[krkr2_hook] ", fp);
                    fputs(utf8, fp);
                    fputc('\n', fp);
                    _freea(utf8);
                }
                fclose(fp);
            }
        }
    }
}

static void HookLog(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    HookLogV(fmt, args);
    va_end(args);
}

// ===== 配置加载（krkr2_font.ini，与启动器同目录） =====
static void LoadConfig() {
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

    GetPrivateProfileStringW(L"Font", L"TargetFont", g_targetFont,
                             g_targetFont, LF_FACESIZE, iniPath);
    GetPrivateProfileStringW(L"Font", L"LogPath", g_logPath,
                             g_logPath, MAX_PATH, iniPath);
    g_enabled = (GetPrivateProfileIntW(L"Font", L"Enabled", 1, iniPath) != 0);
    g_logToFile = (GetPrivateProfileIntW(L"Font", L"LogToFile", 1, iniPath) != 0);
    g_replaceCharset = (GetPrivateProfileIntW(L"Font", L"ReplaceCharset", 1, iniPath) != 0);
    g_targetCharset = (BYTE)GetPrivateProfileIntW(L"Font", L"TargetCharset",
                                                  GB2312_CHARSET, iniPath);

    HookLog(L"配置加载完成: 目标字体='%s' charset=%d enabled=%d log=%d",
            g_targetFont, (int)g_targetCharset, g_enabled, g_logToFile);
}

// ===== 字体名判断 =====
static bool IsJapaneseFont(const wchar_t* faceName) {
    if (!faceName || !faceName[0]) return false;
    for (size_t i = 0; i < _countof(g_jpBlacklist); i++) {
        if (_wcsicmp(faceName, g_jpBlacklist[i]) == 0) return true;
    }
    return false;
}

// ===== 钩子替换函数 =====
static HFONT WINAPI HookCreateFontIndirectW(const LOGFONTW* lf) {
    if (g_enabled && lf && lf->lfFaceName[0] && IsJapaneseFont(lf->lfFaceName)) {
        LOGFONTW orig = *lf;                 // 保留原始请求，供反向伪装
        LOGFONTW newLf = *lf;
        wcsncpy(newLf.lfFaceName, g_targetFont, LF_FACESIZE - 1);
        newLf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        if (g_replaceCharset) newLf.lfCharSet = g_targetCharset;
        HookLog(L"CreateFontIndirectW: '%s' -> '%s' (h=%d weight=%d charset=%d->%d)",
                lf->lfFaceName, g_targetFont, lf->lfHeight, lf->lfWeight,
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
    if (g_enabled && pszFaceName && IsJapaneseFont(pszFaceName)) {
        LOGFONTW orig = {};
        orig.lfHeight = cHeight; orig.lfWidth = cWidth;
        orig.lfEscapement = cEscapement; orig.lfOrientation = cOrientation;
        orig.lfWeight = (LONG)cWeight;
        orig.lfItalic = (BYTE)bItalic; orig.lfUnderline = (BYTE)bUnderline;
        orig.lfStrikeOut = (BYTE)bStrikeOut; orig.lfCharSet = (BYTE)iCharSet;
        orig.lfOutPrecision = (BYTE)iOutPrecision; orig.lfClipPrecision = (BYTE)iClipPrecision;
        orig.lfQuality = (BYTE)iQuality; orig.lfPitchAndFamily = (BYTE)iPitchAndFamily;
        wcsncpy(orig.lfFaceName, pszFaceName, LF_FACESIZE - 1);

        DWORD cs = g_replaceCharset ? (DWORD)g_targetCharset : iCharSet;
        HookLog(L"CreateFontW: '%s' -> '%s' (h=%d weight=%d charset=%d->%d)",
                pszFaceName, g_targetFont, cHeight, cWeight, (int)iCharSet, (int)cs);
        HFONT h = g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                                    bItalic, bUnderline, bStrikeOut, cs, iOutPrecision,
                                    iClipPrecision, iQuality, iPitchAndFamily, g_targetFont);
        RegisterFont(h, &orig);
        return h;
    }
    return g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                             bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
                             iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
}

// ===== 反向伪装：引擎反查字体身份时返回原始字体 =====
static int WINAPI HookGetTextFaceW(HDC hdc, int cCount, LPWSTR pszFace) {
    if (g_enabled && hdc) {
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
    if (g_enabled && hObj) {
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
    if (g_fontRegCount > 0 && hObj) EvictFont((HFONT)hObj);
    return g_origDeleteObject(hObj);
}

// ===== IAT 补丁（PE 导入表遍历，兼容 x86/x64） =====
static bool PatchIat(const char* dllName, const char* funcName, void* newFunc, void** origOut) {
    HMODULE hMod = GetModuleHandleW(NULL);
    if (!hMod) return false;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)hMod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_DATA_DIRECTORY* impDir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!impDir->VirtualAddress || !impDir->Size) return false;

    IMAGE_IMPORT_DESCRIPTOR* desc =
        (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)hMod + impDir->VirtualAddress);

    for (; desc->Name; desc++) {
        const char* dll = (const char*)((BYTE*)hMod + desc->Name);
        if (_stricmp(dll, dllName) != 0) continue;

        IMAGE_THUNK_DATA* thunkINT = desc->OriginalFirstThunk
            ? (IMAGE_THUNK_DATA*)((BYTE*)hMod + desc->OriginalFirstThunk) : NULL;
        IMAGE_THUNK_DATA* thunkIAT = (IMAGE_THUNK_DATA*)((BYTE*)hMod + desc->FirstThunk);

        for (size_t i = 0; ; i++) {
            ULONG_PTR val = thunkINT ? thunkINT[i].u1.AddressOfData : thunkIAT[i].u1.AddressOfData;
            if (val == 0) break;
            if (val & IMAGE_ORDINAL_FLAG) continue;  // 按序号导入，跳过

            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)((BYTE*)hMod + val);
            if (strcmp((const char*)ibn->Name, funcName) != 0) continue;

            // 命中：替换 IAT 槽位
            DWORD oldProtect;
            if (!VirtualProtect(&thunkIAT[i].u1.Function, sizeof(ULONG_PTR),
                                PAGE_READWRITE, &oldProtect))
                return false;

            *origOut = (void*)thunkIAT[i].u1.Function;
            thunkIAT[i].u1.Function = (ULONG_PTR)newFunc;

            VirtualProtect(&thunkIAT[i].u1.Function, sizeof(ULONG_PTR),
                           oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), &thunkIAT[i].u1.Function,
                                  sizeof(ULONG_PTR));
            return true;
        }
        return false;  // DLL 存在但函数未静态导入
    }
    return false;  // DLL 未导入
}

// ===== GetProcAddress 拦截 =====
// KiriKiri 的字体 API 全部动态获取，拦截 GetProcAddress 是唯一可行注入点。
// 注意：安装 GetProcAddress 钩子前必须先保存全部原始函数地址，
// 否则后续获取原始函数会递归进入自己的钩子。
static FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    if (g_enabled && lpProcName && ((ULONG_PTR)lpProcName > 0xFFFF)) {
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
    bool ok = PatchIat("kernel32.dll", "GetProcAddress",
                       (void*)HookGetProcAddress, (void**)&g_origGetProcAddress);
    if (ok) {
        HookLog(L"GetProcAddress 钩子安装成功（创建 + 身份查询 + 删除）");
    } else {
        HookLog(L"错误: kernel32 GetProcAddress 未在导入表中");
    }
}

// ===== DLL 入口 =====
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_fontRegLock);
        g_fontRegLockReady = true;
        LoadConfig();
        InstallHooks();
        HookLog(L"krkr2_hook 加载完成");
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