// GalEngineKit version.dll 代理回归测试（开发验证 + CI 用）
// 验证免注入代理形态三件事：
//   A. 代理被加载，且 version 导出转发到系统真实 version.dll（非递归命中自身）
//      —— VerLanguageNameW 不依赖文件；另对 krkr.exe 查版本信息（若有资源）。
//      若加载的是代理自身，转发会无限递归栈溢出，故能返回真实结果即证明转发正确。
//   B. 同一套字体钩子核心已安装：日文字体被替换 + 反向伪装返回原名。
//   C. 日志佐证替换与 charset 改写。
// 32 位编译。全部断言通过返回 0，任一失败返回 1。
//
// 运行前提（Wine/Winlator 必须）：WINEDLLOVERRIDES="version=n,b"
//   Wine 对 version.dll 有硬编码 builtin 加载顺序，不加 native override 时
//   内置 version.dll 会遮蔽游戏目录代理的导出，钩子不生效。Winlator 在容器
//   DLL 覆盖里把 version 设为 native 即可。
// 运行：CWD 需有代理 version.dll 与 krkr.exe，
//   WINEDLLOVERRIDES="version=n,b" wine ./test_version_proxy.exe

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef DWORD(WINAPI* PFN_VerLanguageNameW)(DWORD, LPWSTR, DWORD);
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
typedef BOOL (WINAPI* PFN_GetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL (WINAPI* PFN_VerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
typedef HFONT(WINAPI* PFN_CreateFontIndirectW)(const LOGFONTW*);
typedef int  (WINAPI* PFN_GetTextFaceW)(HDC, int, LPWSTR);

static int g_fail = 0;
static void Check(int cond, const char* name) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}
static void PrintWide(const char* tag, const wchar_t* s) {
    char buf[512];
    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL);
    printf("%s%s\n", tag, buf);
}

int main() {
    printf("=== GalEngineKit version.dll 代理回归测试 ===\n");

    // A1. 加载游戏目录里的代理 version.dll
    HMODULE ver = LoadLibraryA("version.dll");
    if (!ver) {
        printf("[FAIL] 无法加载代理 version.dll (err=%lu)\n", GetLastError());
        return 1;
    }
    Check(1, "代理 version.dll 已加载");

    // A2. 转发 VerLanguageNameW（无需文件，返回真实语言名即证明转发到系统 DLL）
    PFN_VerLanguageNameW langName = (PFN_VerLanguageNameW)GetProcAddress(ver, "VerLanguageNameW");
    Check(langName != NULL, "代理导出 VerLanguageNameW 可解析");
    if (langName) {
        wchar_t lang[256] = {};
        DWORD n = langName(0x0409, lang, 256);   // English (United States)
        PrintWide("  语言名: ", lang);
        Check(n > 0 && lang[0] != L'\0', "转发 VerLanguageNameW 返回真实语言名（非递归）");
    }

    // A3. 对 krkr.exe 查版本信息（best-effort：无版本资源则跳过）
    PFN_GetFileVersionInfoSizeW sizeW = (PFN_GetFileVersionInfoSizeW)GetProcAddress(ver, "GetFileVersionInfoSizeW");
    PFN_GetFileVersionInfoW infoW = (PFN_GetFileVersionInfoW)GetProcAddress(ver, "GetFileVersionInfoW");
    PFN_VerQueryValueW queryW = (PFN_VerQueryValueW)GetProcAddress(ver, "VerQueryValueW");
    if (sizeW && infoW && queryW) {
        DWORD handle = 0;
        DWORD sz = sizeW(L"krkr.exe", &handle);
        if (sz > 0) {
            void* data = malloc(sz);
            if (data && infoW(L"krkr.exe", 0, sz, data)) {
                void* buf = NULL; UINT len = 0;
                BOOL ok = queryW(data, L"\\", &buf, &len);
                Check(ok && buf && len >= sizeof(VS_FIXEDFILEINFO), "转发 GetFileVersionInfoW/VerQueryValueW 取到版本块");
            } else {
                printf("  (GetFileVersionInfoW 失败，跳过)\n");
            }
            free(data);
        } else {
            printf("  (krkr.exe 无版本资源，跳过版本块断言)\n");
        }
    }

    // B. 字体钩子核心已安装：经 GetProcAddress 动态获取字体 API（被钩子拦截）
    //    反向伪装返回原字体名，是代理钩子生效的最强证明（系统 DLL 不会这样做）。
    HMODULE gdi = LoadLibraryA("gdi32.dll");
    PFN_CreateFontIndirectW createFont = (PFN_CreateFontIndirectW)GetProcAddress(gdi, "CreateFontIndirectW");
    PFN_GetTextFaceW getFace = (PFN_GetTextFaceW)GetProcAddress(gdi, "GetTextFaceW");
    Check(createFont != NULL && getFace != NULL, "字体 API 解析成功");
    if (createFont && getFace) {
        LOGFONTW lf = {};
        lf.lfHeight = -20; lf.lfWeight = FW_NORMAL; lf.lfCharSet = SHIFTJIS_CHARSET;
        wcscpy(lf.lfFaceName, L"MS Gothic");
        HFONT font = createFont(&lf);
        Check(font != NULL, "代理形态下创建 MS Gothic 字体成功");
        HDC hdc = GetDC(NULL);
        HFONT old = (HFONT)SelectObject(hdc, font);
        wchar_t face[LF_FACESIZE] = {};
        getFace(hdc, LF_FACESIZE, face);
        PrintWide("  GetTextFaceW 返回: ", face);
        Check(wcscmp(face, L"MS Gothic") == 0, "代理形态下反向伪装返回原字体名");
        SelectObject(hdc, old);
        DeleteObject(font);
        ReleaseDC(NULL, hdc);
    }

    // C. 日志佐证替换与 charset 改写
    FILE* fp = fopen("galenginekit_hook.log", "r");
    int sawReplace = 0, sawCharset = 0;
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "MS Gothic") && strstr(line, "Microsoft YaHei")) sawReplace = 1;
            if (strstr(line, "charset=128->134")) sawCharset = 1;
        }
        fclose(fp);
    }
    Check(sawReplace, "日志证明: 代理形态下 MS Gothic 被替换");
    Check(sawCharset, "日志证明: 代理形态下 charset 128->134");

    FreeLibrary(ver);
    printf("\n=== 结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}