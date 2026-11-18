// GalEngineKit 字体策略 — 独立实现，MIT
// 从 krkr2_hook.cpp 拆出：HFONT 反向伪装注册表、日文字体存在性探测、字体克隆。

#ifndef GEK_FONT_POLICY_H
#define GEK_FONT_POLICY_H

#include <windows.h>

typedef HFONT(WINAPI* PFN_CreateFontIndirectW)(const LOGFONTW*);
typedef int  (WINAPI* PFN_GetTextFaceW)(HDC, int, LPWSTR);

enum { FS_UNKNOWN = 0, FS_EXISTS = 1, FS_MISSING = 2 };

// 注册表：记录 HFONT 对应的原始 LOGFONT，供引擎反查时伪装回原字体
void FontRegistryInit();
void RegisterFont(HFONT h, const LOGFONTW* orig);
bool LookupFont(HFONT h, LOGFONTW* out);
void EvictFont(HFONT h);
int  ActiveFontCount();

// 探测系统里是否真有这个字体：按原名建一次，反查实际 face 名，结果按名缓存
int ProbeFontExists(const wchar_t* faceName,
                    PFN_CreateFontIndirectW createFont,
                    PFN_GetTextFaceW getFace);

// 按需把源字体改名为该日文字体名并注册；失败返回 false，调用方退回替换路径
bool CloneEnsure(const wchar_t* faceName);

#endif // GEK_FONT_POLICY_H
