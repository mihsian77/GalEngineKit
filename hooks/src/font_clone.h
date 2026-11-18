// GalEngineKit font_clone — 运行时字体克隆（V1-B）
// 独立实现，MIT License

#pragma once
#include <windows.h>

// 把 sourcePath 指向的单体 TTF/OTF 改名成 targetName，用 AddFontMemResourceEx
// 注册进本进程。成功返回 true；同名重复调用直接复用已有注册。
// 失败情形：文件读不到、TTC 集合、sfnt 头非法、缺 name 表、GDI 拒绝注册。
extern "C" bool FontCloneRegister(const wchar_t* sourcePath, const wchar_t* targetName);

// 当前已注册的克隆字体数量
extern "C" int FontCloneCount(void);

// 移除全部克隆（进程退出前一般不需要；测试清理用）
extern "C" void FontCloneUnregisterAll(void);
