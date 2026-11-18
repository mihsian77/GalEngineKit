// GalEngineKit krkr2_hook 功能回归测试（开发验证 + CI 用）
// 模拟 KiriKiri 的动态 API 获取模式，验证两件事：
//   A. 替换确实发生：MS Gothic + Shift-JIS charset 被换成目标中文字体 + GB2312
//      （通过 galenginekit_hook.log 的替换记录证明）
//   B. 反向伪装生效：引擎经 GetTextFaceW / GetObjectW 反查时拿到的仍是原字体名
//      与原 charset；非日文字体（Arial）不被替换也不被伪装。
// 32 位编译（目标游戏为 32 位）。全部断言通过返回 0，任一失败返回 1。
//
// 运行：把本 exe 与 hooks/krkr2_hook.dll 放在同一游戏目录结构下，
//   wine ./test_font_hook.exe   （CWD 需能找到 hooks\krkr2_hook.dll）

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HFONT(WINAPI* PFN_CreateFontIndirectW)(const LOGFONTW*);
typedef int  (WINAPI* PFN_GetTextFaceW)(HDC, int, LPWSTR);
typedef int  (WINAPI* PFN_GetObjectW)(HANDLE, int, LPVOID);
typedef BOOL (WINAPI* PFN_DeleteObject)(HGDIOBJ);

static int g_fail = 0;

static void PrintWide(const char* tag, const wchar_t* s) {
    char buf[256];
    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL);
    printf("%s%s\n", tag, buf);
}

static void Check(int cond, const char* name) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main() {
    printf("=== GalEngineKit krkr2_hook 回归测试 ===\n");

    HMODULE hook = LoadLibraryA("hooks\\krkr2_hook.dll");
    if (!hook) {
        printf("[FAIL] 无法加载 hooks\\krkr2_hook.dll (err=%lu)\n", GetLastError());
        return 1;
    }
    printf("[PASS] 钩子 DLL 已加载\n");

    // 模拟 KiriKiri：全部经 GetProcAddress 动态获取（会被钩子拦截）
    HMODULE gdi = LoadLibraryA("gdi32.dll");
    PFN_CreateFontIndirectW createFont = (PFN_CreateFontIndirectW)GetProcAddress(gdi, "CreateFontIndirectW");
    PFN_GetTextFaceW getFace = (PFN_GetTextFaceW)GetProcAddress(gdi, "GetTextFaceW");
    PFN_GetObjectW getObj = (PFN_GetObjectW)GetProcAddress(gdi, "GetObjectW");
    PFN_DeleteObject delObj = (PFN_DeleteObject)GetProcAddress(gdi, "DeleteObject");
    if (!createFont || !getFace || !getObj || !delObj) {
        printf("[FAIL] 字体 API 解析失败\n");
        return 1;
    }

    HDC hdc = GetDC(NULL);

    // --- 用例 1：日文字体 MS Gothic + Shift-JIS charset ---
    LOGFONTW lf = {};
    lf.lfHeight = -20;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = SHIFTJIS_CHARSET;   // 128
    wcscpy(lf.lfFaceName, L"MS Gothic");
    HFONT font = createFont(&lf);
    Check(font != NULL, "创建 MS Gothic 字体成功");
    HFONT old = (HFONT)SelectObject(hdc, font);

    // 反向伪装：GetTextFaceW 应返回原字体名 MS Gothic
    wchar_t face[LF_FACESIZE] = {};
    getFace(hdc, LF_FACESIZE, face);
    PrintWide("  GetTextFaceW 返回: ", face);
    Check(wcscmp(face, L"MS Gothic") == 0, "反向伪装: GetTextFaceW 返回原字体名 MS Gothic");

    // 反向伪装：GetObjectW 应返回原始 LOGFONT（原 charset=128、原名）
    LOGFONTW got = {};
    int cb = getObj(font, sizeof(LOGFONTW), &got);
    Check(cb == sizeof(LOGFONTW), "反向伪装: GetObjectW 返回 LOGFONTW 尺寸");
    PrintWide("  GetObjectW 字体名: ", got.lfFaceName);
    printf("  GetObjectW charset: %d\n", (int)got.lfCharSet);
    Check(wcscmp(got.lfFaceName, L"MS Gothic") == 0, "反向伪装: GetObjectW 返回原字体名");
    Check(got.lfCharSet == SHIFTJIS_CHARSET, "反向伪装: GetObjectW 返回原 charset(128)");

    // --- 用例 2：非日文字体，不应被替换/伪装 ---
    // 选 Wine 已安装的 Liberation Sans：若用 Arial，Wine 缺字会把它替换成
    // Liberation Sans，GetTextFaceW 返回替换后的真实名，干扰断言。
    LOGFONTW lf2 = {};
    lf2.lfHeight = -20;
    lf2.lfCharSet = DEFAULT_CHARSET;
    wcscpy(lf2.lfFaceName, L"Liberation Sans");
    HFONT font2 = createFont(&lf2);
    Check(font2 != NULL, "创建 Liberation Sans 字体成功");
    SelectObject(hdc, font2);
    wchar_t face2[LF_FACESIZE] = {};
    getFace(hdc, LF_FACESIZE, face2);
    PrintWide("  Liberation Sans GetTextFaceW 返回: ", face2);
    Check(wcscmp(face2, L"Liberation Sans") == 0, "非日文字体不被替换/伪装");
    Check(wcscmp(face2, L"Microsoft YaHei") != 0, "非日文字体未被换成目标中文字体");

    // --- 清理：经 GetProcAddress 拿到 DeleteObject（走钩子，触发注册表淘汰） ---
    SelectObject(hdc, old);
    Check(delObj(font) != 0, "DeleteObject(font) 成功");
    Check(delObj(font2) != 0, "DeleteObject(font2) 成功");
    ReleaseDC(NULL, hdc);

    // --- 输出钩子日志（证明替换与 charset 改写确实发生） ---
    printf("\n=== galenginekit_hook.log ===\n");
    FILE* fp = fopen("galenginekit_hook.log", "r");
    int sawReplace = 0, sawCharset = 0;
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            printf("%s", line);
            if (strstr(line, "MS Gothic") && strstr(line, "Microsoft YaHei")) sawReplace = 1;
            if (strstr(line, "charset=128->134")) sawCharset = 1;
        }
        fclose(fp);
    } else {
        printf("(未找到日志文件)\n");
    }
    Check(sawReplace, "日志证明: MS Gothic 被替换为 Microsoft YaHei");
    Check(sawCharset, "日志证明: charset 由 128 改写为 134");

    FreeLibrary(hook);
    printf("\n=== 结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}
