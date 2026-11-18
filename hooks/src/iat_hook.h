// GalEngineKit IAT 补丁 — 独立实现，MIT
// 从 krkr2_hook.cpp 拆出：遍历 PE 导入表替换 IAT 槽位。
//
// 设计约束（Winlator/Box64 转译环境）：
// 只改导入表数据区，不碰代码段，不触发翻译缓存失效。

#ifndef GEK_IAT_HOOK_H
#define GEK_IAT_HOOK_H

// 在当前进程主模块的导入表里找 dllName!funcName，把 IAT 槽位换成 newFunc。
// 成功时把原函数地址写进 origOut（可为空）。找不到或无法定位返回 false。
bool IatPatch(const char* dllName, const char* funcName, void* newFunc, void** origOut);

#endif // GEK_IAT_HOOK_H
