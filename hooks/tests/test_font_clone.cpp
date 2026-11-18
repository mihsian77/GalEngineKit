// GalEngineKit font_clone 回归测试（V1-B）
// 验证：把原始 CJK 字体改名成日文字体名并内存注册后，GDI 能按该名取到且有 CJK 字形。
// 用法：test_font_clone.exe <原始单体CJK字体路径> [目标字体名]
// 无参或缺文件时打印 SKIP 并返回 0（CI 无字体环境不判失败）。
// 通过返回 0，失败返回 1。

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/font_clone.h"

static int g_fail = 0;
static void Check(int cond, const char* name) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main(int argc, char** argv) {
    printf("=== GalEngineKit font_clone 回归测试 ===\n");
    if (argc < 2) { printf("SKIP: 未提供源字体路径\n"); return 0; }

    // 相对路径转宽字符
    wchar_t src[MAX_PATH] = {};
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, src, MAX_PATH);
    DWORD attrs = GetFileAttributesW(src);
    if (attrs == INVALID_FILE_ATTRIBUTES) { printf("SKIP: 源字体不存在 %s\n", argv[1]); return 0; }

    const wchar_t* target = L"MS Gothic";

    Check(FontCloneRegister(src, target), "改名并内存注册成功");
    Check(FontCloneCount() >= 1, "克隆缓存已记录");

    // 按目标名创建字体，验证 GDI 认到的是克隆
    LOGFONTW lf = {};
    lf.lfHeight = -20;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = SHIFTJIS_CHARSET;
    wcscpy(lf.lfFaceName, L"MS Gothic");
    HFONT font = CreateFontIndirectW(&lf);
    Check(font != NULL, "按 MS Gothic 创建字体成功");
    if (!font) { FontCloneUnregisterAll(); return 1; }

    HDC dc = GetDC(NULL);
    HFONT old = (HFONT)SelectObject(dc, font);
    wchar_t face[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, face);
    char faceUtf8[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, face, -1, faceUtf8, sizeof(faceUtf8), NULL, NULL);
    printf("  GetTextFaceW: %s\n", faceUtf8);
    Check(wcscmp(face, L"MS Gothic") == 0, "GDI 认到目标字体名");

    WCHAR chars[2] = { 0x4E2D, 0x65E5 };   // 中、日
    WORD gi[2] = { 0 };
    GetGlyphIndicesW(dc, chars, 2, gi, GGI_MARK_NONEXISTING_GLYPHS);
    printf("  字形索引: 中=%u 日=%u (0xFFFF=缺失)\n", gi[0], gi[1]);
    Check(gi[0] != 0xFFFF && gi[1] != 0xFFFF, "克隆字体含中日字形");

    SIZE extent = { 0, 0 };
    GetTextExtentPoint32W(dc, chars, 2, &extent);
    Check(extent.cx > 0 && extent.cy > 0, "文字可排版（非零尺寸）");

    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(NULL, dc);
    FontCloneUnregisterAll();

    printf("\n=== 结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}