// GalEngineKit version.dll 代理 — 免注入交付形态
// 独立实现，MIT License
//
// 变更动机：启动器注入在 Winlator/Box64 转译下依赖辅助线程与 kernel32 同址
//   假设，存在不确定性。version.dll 代理是可靠兜底：游戏本就导入 version.dll
//   （实测 krkr.exe 导入 VERSION.DLL），把本代理放进游戏目录即被自然加载，
//   无需注入。代理把全部 version 导出转发回系统真实 version.dll，游戏版本
//   查询照常工作，同时安装与注入形态同一套 krkr2 字体钩子核心。
// 影响范围：新增 hooks 代理目标；复用 krkr2_hook.cpp 核心（Krkr2InstallHooks）。
// 回滚方案：git revert 本提交；删除游戏目录内代理 version.dll 即恢复原状。
//
// 运行前提（Wine/Winlator 必须，实测得出）：WINEDLLOVERRIDES="version=n,b"。
//   Wine 对 version.dll 有硬编码 builtin 加载顺序（+module trace 可见
//   "get_load_order got hardcoded default" → "load_builtin_dll"）。不加 native
//   override 时，内置 version.dll 会遮蔽游戏目录代理的 PE 导出——GetProcAddress
//   解析到内置函数而非本代理，钩子完全不生效。Winlator 在容器 DLL 覆盖里把
//   version 设为 native（或 n,b）即可。原生 Windows 无此问题，放入游戏目录即用。
//
// 钩子安装时机：DllMain 内 InitVersionProxy 之后立即 Krkr2InstallHooks。
//   native override 激活后代理以真 native 加载，DllMain 里文件 I/O 与 IAT 补丁
//   均正常（实测日志/钩子全部生效）。必须在 DllMain 装好——游戏随后经
//   GetProcAddress 取字体 API 时才会被拦截；延迟到首次转发调用再装会错过早期
//   解析（实测延迟安装时字体 API 拿到的是 gdi32 真实地址，拦截失败）。
//   EnsureHooks 仅作 interlocked 兜底，正常路径 DllMain 已装好。
//
// 转发实现：C 包装函数经函数指针调用真实 API（跨 x86/x64 可移植，不依赖 asm）。
// 已知限制：未导出 undocumented 的 GetFileVersionInfoByHandle（按序号、游戏
//   几乎不用，参考实现也提示其在部分系统缺失）；如某游戏确需，再补 asm thunk。

#include <windows.h>

// 共享钩子核心（krkr2_hook.cpp 在不定义 GEK_KRKR2_STANDALONE 时编入本目标）
extern "C" void Krkr2InstallHooks(void);

// ===== 真实 version.dll 模块与函数指针 =====
static HMODULE g_realVersion = NULL;

static FARPROC pGetFileVersionInfoA;
static FARPROC pGetFileVersionInfoW;
static FARPROC pGetFileVersionInfoSizeA;
static FARPROC pGetFileVersionInfoSizeW;
static FARPROC pGetFileVersionInfoExA;
static FARPROC pGetFileVersionInfoExW;
static FARPROC pGetFileVersionInfoSizeExA;
static FARPROC pGetFileVersionInfoSizeExW;
static FARPROC pVerFindFileA;
static FARPROC pVerFindFileW;
static FARPROC pVerInstallFileA;
static FARPROC pVerInstallFileW;
static FARPROC pVerLanguageNameA;
static FARPROC pVerLanguageNameW;
static FARPROC pVerQueryValueA;
static FARPROC pVerQueryValueW;

// 按系统目录全路径加载真实 version.dll，避免命中游戏目录里的本代理。
static bool InitVersionProxy() {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 13 >= MAX_PATH) return false;
    wcscat(path, L"\\version.dll");

    g_realVersion = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_realVersion) return false;

    #define RESOLVE(name) p##name = GetProcAddress(g_realVersion, #name)
    RESOLVE(GetFileVersionInfoA);
    RESOLVE(GetFileVersionInfoW);
    RESOLVE(GetFileVersionInfoSizeA);
    RESOLVE(GetFileVersionInfoSizeW);
    RESOLVE(GetFileVersionInfoExA);
    RESOLVE(GetFileVersionInfoExW);
    RESOLVE(GetFileVersionInfoSizeExA);
    RESOLVE(GetFileVersionInfoSizeExW);
    RESOLVE(VerFindFileA);
    RESOLVE(VerFindFileW);
    RESOLVE(VerInstallFileA);
    RESOLVE(VerInstallFileW);
    RESOLVE(VerLanguageNameA);
    RESOLVE(VerLanguageNameW);
    RESOLVE(VerQueryValueA);
    RESOLVE(VerQueryValueW);
    #undef RESOLVE
    return true;
}

// ===== 钩子安装状态（DllMain 主装，EnsureHooks 兜底，interlocked 保证只装一次） =====
static volatile LONG g_hooksState = 0;   // 0=未装 1=已装/安装中
static void EnsureHooks() {
    if (InterlockedCompareExchange(&g_hooksState, 1, 0) == 0) {
        Krkr2InstallHooks();
    }
}

// ===== 转发包装（导出名由 version_proxy.def 映射到这些符号） =====
// 每个都是 WINAPI(stdcall)，与游戏期望的调用约定一致；指针为空时返回安全默认值。
// 首行 EnsureHooks() 保证无论哪个 version API 先被调用都能触发钩子安装。

extern "C" BOOL WINAPI gek_GetFileVersionInfoA(LPCSTR f, DWORD h, DWORD l, LPVOID d) {
    EnsureHooks();
    if (!pGetFileVersionInfoA) return FALSE;
    return ((BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID))pGetFileVersionInfoA)(f, h, l, d);
}
extern "C" BOOL WINAPI gek_GetFileVersionInfoW(LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
    EnsureHooks();
    if (!pGetFileVersionInfoW) return FALSE;
    return ((BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID))pGetFileVersionInfoW)(f, h, l, d);
}
extern "C" DWORD WINAPI gek_GetFileVersionInfoSizeA(LPCSTR f, LPDWORD h) {
    EnsureHooks();
    if (!pGetFileVersionInfoSizeA) return 0;
    return ((DWORD(WINAPI*)(LPCSTR, LPDWORD))pGetFileVersionInfoSizeA)(f, h);
}
extern "C" DWORD WINAPI gek_GetFileVersionInfoSizeW(LPCWSTR f, LPDWORD h) {
    EnsureHooks();
    if (!pGetFileVersionInfoSizeW) return 0;
    return ((DWORD(WINAPI*)(LPCWSTR, LPDWORD))pGetFileVersionInfoSizeW)(f, h);
}
extern "C" BOOL WINAPI gek_GetFileVersionInfoExA(DWORD fl, LPCSTR f, DWORD h, DWORD l, LPVOID d) {
    EnsureHooks();
    if (!pGetFileVersionInfoExA) return FALSE;
    return ((BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID))pGetFileVersionInfoExA)(fl, f, h, l, d);
}
extern "C" BOOL WINAPI gek_GetFileVersionInfoExW(DWORD fl, LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
    EnsureHooks();
    if (!pGetFileVersionInfoExW) return FALSE;
    return ((BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID))pGetFileVersionInfoExW)(fl, f, h, l, d);
}
extern "C" DWORD WINAPI gek_GetFileVersionInfoSizeExA(DWORD fl, LPCSTR f, LPDWORD h) {
    EnsureHooks();
    if (!pGetFileVersionInfoSizeExA) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD))pGetFileVersionInfoSizeExA)(fl, f, h);
}
extern "C" DWORD WINAPI gek_GetFileVersionInfoSizeExW(DWORD fl, LPCWSTR f, LPDWORD h) {
    EnsureHooks();
    if (!pGetFileVersionInfoSizeExW) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD))pGetFileVersionInfoSizeExW)(fl, f, h);
}
extern "C" DWORD WINAPI gek_VerFindFileA(DWORD fl, LPCSTR fn, LPCSTR wd, LPCSTR ad,
                                         LPSTR cd, PUINT cl, LPSTR dd, PUINT dl) {
    EnsureHooks();
    if (!pVerFindFileA) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT))pVerFindFileA)
        (fl, fn, wd, ad, cd, cl, dd, dl);
}
extern "C" DWORD WINAPI gek_VerFindFileW(DWORD fl, LPCWSTR fn, LPCWSTR wd, LPCWSTR ad,
                                         LPWSTR cd, PUINT cl, LPWSTR dd, PUINT dl) {
    EnsureHooks();
    if (!pVerFindFileW) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT))pVerFindFileW)
        (fl, fn, wd, ad, cd, cl, dd, dl);
}
extern "C" DWORD WINAPI gek_VerInstallFileA(DWORD fl, LPCSTR sf, LPCSTR df, LPCSTR sd,
                                            LPCSTR dd, LPCSTR cd, LPSTR tf, PUINT tl) {
    EnsureHooks();
    if (!pVerInstallFileA) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT))pVerInstallFileA)
        (fl, sf, df, sd, dd, cd, tf, tl);
}
extern "C" DWORD WINAPI gek_VerInstallFileW(DWORD fl, LPCWSTR sf, LPCWSTR df, LPCWSTR sd,
                                            LPCWSTR dd, LPCWSTR cd, LPWSTR tf, PUINT tl) {
    EnsureHooks();
    if (!pVerInstallFileW) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT))pVerInstallFileW)
        (fl, sf, df, sd, dd, cd, tf, tl);
}
extern "C" DWORD WINAPI gek_VerLanguageNameA(DWORD lang, LPSTR buf, DWORD cch) {
    EnsureHooks();
    if (!pVerLanguageNameA) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPSTR, DWORD))pVerLanguageNameA)(lang, buf, cch);
}
extern "C" DWORD WINAPI gek_VerLanguageNameW(DWORD lang, LPWSTR buf, DWORD cch) {
    EnsureHooks();
    if (!pVerLanguageNameW) return 0;
    return ((DWORD(WINAPI*)(DWORD, LPWSTR, DWORD))pVerLanguageNameW)(lang, buf, cch);
}
extern "C" BOOL WINAPI gek_VerQueryValueA(LPCVOID blk, LPCSTR sub, LPVOID* buf, PUINT len) {
    EnsureHooks();
    if (!pVerQueryValueA) return FALSE;
    return ((BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT))pVerQueryValueA)(blk, sub, buf, len);
}
extern "C" BOOL WINAPI gek_VerQueryValueW(LPCVOID blk, LPCWSTR sub, LPVOID* buf, PUINT len) {
    EnsureHooks();
    if (!pVerQueryValueW) return FALSE;
    return ((BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT))pVerQueryValueW)(blk, sub, buf, len);
}

// ===== DLL 入口 =====
// 转发就绪 + 安装钩子核心（须在游戏调 GetProcAddress 取字体 API 之前装好）。
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitVersionProxy();
        EnsureHooks();
    }
    // 不在 DLL_PROCESS_DETACH 里 FreeLibrary（loader lock 风险）；
    // 真实 version.dll 随进程存活即可。
    return TRUE;
}