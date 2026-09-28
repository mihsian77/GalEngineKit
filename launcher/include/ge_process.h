// 独立实现：本模块为 GalEngineKit 原创实现
// 进程管理：创建挂起进程、读写内存、恢复/终止线程、模块基址查询

#ifndef GALENGINEKIT_PROCESS_H
#define GALENGINEKIT_PROCESS_H

#include "ge_types.h"
#include <string>
#include <vector>

namespace ge {

class ProcessManager {
public:
    // 以挂起方式创建目标进程
    static ResultCode CreateSuspended(
        const std::wstring& exe_path,
        const std::wstring& working_dir,
        const std::wstring& command_line,
        LPVOID environment_block,
        ProcessInfo& out_info);

    // 读取进程内存
    static bool ReadMemory(HANDLE process, uintptr_t address, void* buffer, size_t size);

    // 写入进程内存
    static bool WriteMemory(HANDLE process, uintptr_t address, const void* buffer, size_t size);

    // 在目标进程中分配内存
    static uintptr_t AllocateMemory(HANDLE process, size_t size, DWORD protection = PAGE_READWRITE);

    // 释放目标进程内存
    static void FreeMemory(HANDLE process, uintptr_t address, size_t size);

    // 恢复主线程
    static bool ResumeMainThread(HANDLE thread_handle);

    // 终止进程
    static void Terminate(HANDLE process_handle, DWORD exit_code = 0);

    // 关闭进程和线程句柄
    static void Close(ProcessInfo& info);

    // 获取主模块基址
    static uintptr_t GetMainModuleBase(HANDLE process, DWORD process_id);

    // 获取指定模块基址
    static uintptr_t GetModuleBase(HANDLE process, DWORD process_id, const std::wstring& module_name);

    // 等待进程退出
    static DWORD WaitForExit(HANDLE process, uint32_t timeout_ms = INFINITE);
};

} // namespace ge

#endif // GALENGINEKIT_PROCESS_H
