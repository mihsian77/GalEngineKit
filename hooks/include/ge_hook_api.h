// 独立实现：本模块为 GalEngineKit 原创实现
// 钩子 DLL 统一接口定义
// 所有 GalEngineKit 钩子 DLL 必须实现以下导出函数

#ifndef GALENGINEKIT_HOOK_API_H
#define GALENGINEKIT_HOOK_API_H

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 钩子 DLL 版本（用于接口兼容性检查）
#define GEK_HOOK_API_VERSION 1

// 钩子信息结构
typedef struct _GEK_HookInfo {
    uint32_t api_version;       // 必须等于 GEK_HOOK_API_VERSION
    const wchar_t* name;        // 钩子名称（如 "krkr2_font"）
    const wchar_t* version;     // 钩子版本（如 "0.1.0"）
    const wchar_t* target_engine; // 目标引擎（如 "KiriKiri2"、"YU-RIS"、"*" 表示通用）
    const wchar_t* description; // 简短描述
    uint32_t flags;             // 保留标志
} GEK_HookInfo;

// 初始化参数
typedef struct _GEK_HookInitParams {
    uint32_t size;              // 结构体大小（用于版本兼容）
    const wchar_t* config_path; // 配置文件路径（GALENGINEKIT_CONFIG 环境变量）
    const wchar_t* profile_name; // 引擎档案名称（可选）
    HMODULE module_handle;      // 钩子 DLL 自身模块句柄
    void* reserved;             // 保留
} GEK_HookInitParams;

// 钩子 DLL 必须导出的函数

// 1. 获取钩子信息（启动器注入后调用，用于验证和日志）
// 返回: 指向静态 GEK_HookInfo 结构的指针（不需要释放）
typedef const GEK_HookInfo* (*PFN_GEK_HookGetInfo)(void);

// 2. 初始化钩子（DllMain DLL_PROCESS_ATTACH 之后调用）
// params: 初始化参数
// 返回: 0 表示成功，非零表示错误码
typedef int (*PFN_GEK_HookInitialize)(const GEK_HookInitParams* params);

// 3. 关闭钩子（DllMain DLL_PROCESS_DETACH 之前调用）
typedef void (*PFN_GEK_HookShutdown)(void);

// 导出函数名（启动器通过 GetProcAddress 查找）
#define GEK_EXPORT_GetInfo    "GEK_HookGetInfo"
#define GEK_EXPORT_Initialize "GEK_HookInitialize"
#define GEK_EXPORT_Shutdown   "GEK_HookShutdown"

// 便捷宏：在钩子 DLL 中声明导出函数
#define GEK_DECLARE_EXPORTS() \
    extern "C" __declspec(dllexport) const GEK_HookInfo* GEK_HookGetInfo(void); \
    extern "C" __declspec(dllexport) int GEK_HookInitialize(const GEK_HookInitParams* params); \
    extern "C" __declspec(dllexport) void GEK_HookShutdown(void)

#ifdef __cplusplus
}
#endif

#endif // GALENGINEKIT_HOOK_API_H
