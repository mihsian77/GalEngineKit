// 独立实现：本模块为 GalEngineKit 原创实现
// 示例钩子 DLL：演示统一接口实现，用于验证启动器注入链路
// 实际使用时复制此文件，修改钩子逻辑

#include "ge_hook_api.h"
#include <windows.h>
#include <stdio.h>

// 钩子信息（静态常量，不需要释放）
static const GEK_HookInfo g_hookInfo = {
    GEK_HOOK_API_VERSION,      // api_version
    L"example_hook",            // name
    L"0.1.0",                   // version
    L"*",                       // target_engine（* 表示通用）
    L"示例钩子 DLL - 验证注入链路", // description
    0                           // flags
};

// 日志输出（调试用，输出到调试器和文件）
static void HookLog(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);

    OutputDebugStringW(buffer);

    // 同时写入日志文件
    FILE* fp = nullptr;
    _wfopen_s(&fp, L"galenginekit_hook.log", L"a, ccs=UTF-8");
    if (fp) {
        fwprintf(fp, L"[example_hook] %s\n", buffer);
        fclose(fp);
    }
}

// DllMain：DLL 入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        HookLog(L"DLL_PROCESS_ATTACH - 示例钩子已加载");
        break;

    case DLL_PROCESS_DETACH:
        HookLog(L"DLL_PROCESS_DETACH - 示例钩子已卸载");
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}

// 导出函数 1：获取钩子信息
extern "C" __declspec(dllexport)
const GEK_HookInfo* GEK_HookGetInfo(void) {
    return &g_hookInfo;
}

// 导出函数 2：初始化钩子
extern "C" __declspec(dllexport)
int GEK_HookInitialize(const GEK_HookInitParams* params) {
    if (!params || params->size < sizeof(GEK_HookInitParams)) {
        HookLog(L"初始化失败：参数无效");
        return 1;
    }

    HookLog(L"初始化开始");
    HookLog(L"  配置路径: %s", params->config_path ? params->config_path : L"(null)");
    HookLog(L"  引擎档案: %s", params->profile_name ? params->profile_name : L"(null)");
    HookLog(L"  模块句柄: 0x%p", params->module_handle);

    // TODO: 在这里实现实际的钩子逻辑
    // 例如：
    // - 安装 IAT 钩子
    // - 安装内联钩子
    // - 读取配置文件
    // - 初始化字体替换

    HookLog(L"初始化完成");
    return 0;
}

// 导出函数 3：关闭钩子
extern "C" __declspec(dllexport)
void GEK_HookShutdown(void) {
    HookLog(L"关闭钩子");
    // TODO: 在这里清理钩子资源
    // - 卸载所有钩子
    // - 释放内存
    // - 关闭文件句柄
}
