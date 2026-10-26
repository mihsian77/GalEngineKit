// GalEngineKit krkr2_hook — KiriKiri2 中文字体替换钩子
// 独立实现，MIT License
//
// 变更动机：KiriKiri2 (TVP) 中文显示问题的根源是游戏请求的日文字体
//   （MS Gothic / MS Mincho / Meiryo 等）缺少 CJK 汉字字形。钩子通过
//   IAT 拦截 CreateFontW / CreateFontIndirectW，将日文字体替换为
//   系统中文字体，保留原有大小/粗细/样式属性。
// 影响范围：仅 hooks 模块新增 krkr2_hook 目标；不触及 launcher 核心。
// 回滚方案：git revert 本提交；Release 资产回退旧版。
//
// 设计约束（Winlator/Box64 转译环境安全）：
//   - 只改 PE 导入表数据区（IAT），不修改代码段，不触发翻译缓存失效
//   - 不钩 Wine 内置 DLL 的导出，钩子只拦截游戏进程自身的 IAT
// 已知限制（Phase 2 处理）：
//   - TVP 若通过 GetProcAddress 动态获取字体 API，IAT 钩子不命中，
//     需改用 EAT 钩子 / GetProcAddress 拦截（实测确认后再实施）
//   - 预渲染字体缓存（mapPrerenderedFont / TFT）未处理，若游戏预渲染
//     中文所需字形，替换可能不生效，需引擎级钩子配合

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
static PFN_CreateFontIndirectW g_origCreateFontIndirectW = NULL;
static PFN_CreateFontW g_origCreateFontW = NULL;

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

    HookLog(L"配置加载完成: 目标字体='%s' enabled=%d log=%d", g_targetFont, g_enabled, g_logToFile);
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
        LOGFONTW newLf = *lf;
        wcsncpy(newLf.lfFaceName, g_targetFont, LF_FACESIZE - 1);
        newLf.lfFaceName[LF_FACESIZE - 1] = L'\0';
        HookLog(L"CreateFontIndirectW: '%s' -> '%s' (h=%d w=%d weight=%d italic=%d)",
                lf->lfFaceName, g_targetFont, lf->lfHeight, lf->lfWidth,
                lf->lfWeight, lf->lfItalic);
        return g_origCreateFontIndirectW(&newLf);
    }
    return g_origCreateFontIndirectW(lf);
}

static HFONT WINAPI HookCreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation,
                                    DWORD cWeight, DWORD bItalic, DWORD bUnderline,
                                    DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision,
                                    DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily,
                                    LPCWSTR pszFaceName) {
    if (g_enabled && pszFaceName && IsJapaneseFont(pszFaceName)) {
        HookLog(L"CreateFontW: '%s' -> '%s' (h=%d weight=%d italic=%d)",
                pszFaceName, g_targetFont, cHeight, cWeight, bItalic);
        return g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                                 bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
                                 iClipPrecision, iQuality, iPitchAndFamily, g_targetFont);
    }
    return g_origCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight,
                             bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision,
                             iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
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

// ===== 钩子安装 =====
static void InstallHooks() {
    bool ok1 = PatchIat("gdi32.dll", "CreateFontIndirectW",
                        (void*)HookCreateFontIndirectW, (void**)&g_origCreateFontIndirectW);
    bool ok2 = PatchIat("gdi32.dll", "CreateFontW",
                        (void*)HookCreateFontW, (void**)&g_origCreateFontW);

    if (ok1 || ok2) {
        HookLog(L"IAT 钩子安装: CreateFontIndirectW=%d CreateFontW=%d", ok1, ok2);
    } else {
        HookLog(L"警告: gdi32 字体 API 未在导入表中找到（可能动态加载，需 Phase 2 EAT 方案）");
    }
}

// ===== DLL 入口 =====
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
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
    L"0.1.0",
    L"KiriKiri2",
    L"KiriKiri2 中文字体替换钩子（IAT 钩住 GDI 字体创建）",
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
