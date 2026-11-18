// GalEngineKit 字体克隆钩子级测试（V1-B）
// 验证：系统缺日文字体时，钩子按 ini 配置把源 CJK 字体改名注册顶上，
//       游戏按原名拿到字体且有中日字形；日志记录克隆注册。
// 前提：当前目录有 hooks/krkr2_hook.dll、krkr2_font.ini（CloneEnabled=1）、源字体。
// 通过返回 0，失败返回 1。

#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef HFONT(WINAPI* PFN_CreateFontIndirectW)(const LOGFONTW*);
typedef int  (WINAPI* PFN_GetTextFaceW)(HDC, int, LPWSTR);

static int g_fail = 0;
static void Check(int cond, const char* name) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main() {
    printf("=== GalEngineKit 字体克隆钩子级测试 ===\n");

    HMODULE hook = LoadLibraryA("hooks\\krkr2_hook.dll");
    if (!hook) { printf("[FAIL] 加载钩子 DLL 失败 (err=%lu)\n", GetLastError()); return 1; }
    Check(1, "钩子 DLL 已加载");

    HMODULE gdi = LoadLibraryA("gdi32.dll");
    PFN_CreateFontIndirectW createFont =
        (PFN_CreateFontIndirectW)GetProcAddress(gdi, "CreateFontIndirectW");
    PFN_GetTextFaceW getFace = (PFN_GetTextFaceW)GetProcAddress(gdi, "GetTextFaceW");
    Check(createFont && getFace, "字体 API 解析成功（应被钩子拦截）");
    if (!createFont || !getFace) return 1;

    // 请求系统缺失的日文字体
    LOGFONTW lf = {};
    lf.lfHeight = -20;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = SHIFTJIS_CHARSET;
    wcscpy(lf.lfFaceName, L"MS Gothic");
    HFONT font = createFont(&lf);
    Check(font != NULL, "创建 MS Gothic 字体成功");
    if (!font) return 1;

    HDC dc = GetDC(NULL);
    HFONT old = (HFONT)SelectObject(dc, font);
    wchar_t face[LF_FACESIZE] = {};
    getFace(dc, LF_FACESIZE, face);
    char u8[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, face, -1, u8, sizeof(u8), NULL, NULL);
    printf("  GetTextFaceW: %s\n", u8);
    Check(wcscmp(face, L"MS Gothic") == 0, "系统缺失时由克隆顶上（未退回 WenQuanYi 类替身）");

    WCHAR chars[2] = { 0x4E2D, 0x65E5 };
    WORD gi[2] = { 0 };
    GetGlyphIndicesW(dc, chars, 2, gi, GGI_MARK_NONEXISTING_GLYPHS);
    printf("  字形索引: 中=%u 日=%u\n", gi[0], gi[1]);
    Check(gi[0] != 0xFFFF && gi[1] != 0xFFFF, "克隆字体含中日字形");

    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(NULL, dc);

    // 日志佐证
    FILE* fp = fopen("galenginekit_hook.log", "r");
    int sawClone = 0, sawReplace = 0;
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "克隆字体已注册")) sawClone = 1;
            if (strstr(line, "-> 'Microsoft YaHei'")) sawReplace = 1;
        }
        fclose(fp);
    }
    Check(sawClone, "日志记录克隆注册");
    Check(!sawReplace, "未走到替换路径（避免伪装名字冒充克隆）");

    printf("\n=== 结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}