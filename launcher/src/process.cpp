// 独立实现：本模块为 GalEngineKit 原创实现
// 进程管理实现

#include "ge_process.h"
#include "ge_logger.h"
#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace ge {

ResultCode ProcessManager::CreateSuspended(
    const std::wstring& exe_path,
    const std::wstring& working_dir,
    const std::wstring& command_line,
    LPVOID environment_block,
    ProcessInfo& out_info) {

    // 构建完整命令行："exe_path" [command_line]
    std::wstring full_cmd = L"\"" + exe_path + L"\"";
    if (!command_line.empty()) {
        full_cmd += L" " + command_line;
    }

    // 命令行需要可修改的缓冲区（CreateProcessW 可能修改）
    std::vector<wchar_t> cmd_buffer(full_cmd.begin(), full_cmd.end());
    cmd_buffer.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    DWORD creation_flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;

    GE_LOG_INFO(L"创建挂起进程: " + exe_path);
    GE_LOG_DEBUG(L"  工作目录: " + working_dir);
    GE_LOG_DEBUG(L"  命令行: " + full_cmd);

    BOOL success = CreateProcessW(
        nullptr,                    // lpApplicationName
        cmd_buffer.data(),          // lpCommandLine
        nullptr,                    // lpProcessAttributes
        nullptr,                    // lpThreadAttributes
        FALSE,                      // bInheritHandles
        creation_flags,             // dwCreationFlags
        environment_block,          // lpEnvironment
        working_dir.c_str(),        // lpCurrentDirectory
        &si,                        // lpStartupInfo
        &pi                         // lpProcessInformation
    );

    if (!success) {
        DWORD error = GetLastError();
        GE_LOG_ERROR(L"CreateProcessW 失败，错误码: " + std::to_wstring(error));
        return ResultCode::ProcessCreateFailed;
    }

    out_info.process_handle = pi.hProcess;
    out_info.thread_handle = pi.hThread;
    out_info.process_id = pi.dwProcessId;
    out_info.thread_id = pi.dwThreadId;

    GE_LOG_INFO(L"进程创建成功，PID: " + std::to_wstring(out_info.process_id));
    return ResultCode::Success;
}

bool ProcessManager::ReadMemory(HANDLE process, uintptr_t address, void* buffer, size_t size) {
    SIZE_T bytes_read = 0;
    BOOL success = ReadProcessMemory(process, (LPCVOID)address, buffer, size, &bytes_read);
    if (!success || bytes_read != size) {
        GE_LOG_DEBUG(L"ReadProcessMemory 失败，地址: 0x" +
            std::to_wstring(address) + L"，期望: " + std::to_wstring(size) +
            L"，实际: " + std::to_wstring(bytes_read));
        return false;
    }
    return true;
}

bool ProcessManager::WriteMemory(HANDLE process, uintptr_t address, const void* buffer, size_t size) {
    SIZE_T bytes_written = 0;
    BOOL success = WriteProcessMemory(process, (LPVOID)address, buffer, size, &bytes_written);
    if (!success || bytes_written != size) {
        GE_LOG_DEBUG(L"WriteProcessMemory 失败，地址: 0x" +
            std::to_wstring(address) + L"，期望: " + std::to_wstring(size) +
            L"，实际: " + std::to_wstring(bytes_written));
        return false;
    }
    return true;
}

uintptr_t ProcessManager::AllocateMemory(HANDLE process, size_t size, DWORD protection) {
    LPVOID addr = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, protection);
    if (!addr) {
        GE_LOG_ERROR(L"VirtualAllocEx 失败，大小: " + std::to_wstring(size));
        return 0;
    }
    GE_LOG_DEBUG(L"内存分配成功，地址: 0x" + std::to_wstring((uintptr_t)addr) +
        L"，大小: " + std::to_wstring(size));
    return (uintptr_t)addr;
}

void ProcessManager::FreeMemory(HANDLE process, uintptr_t address, size_t size) {
    if (address) {
        VirtualFreeEx(process, (LPVOID)address, size, MEM_RELEASE);
    }
}

bool ProcessManager::ResumeMainThread(HANDLE thread_handle) {
    DWORD suspend_count = ResumeThread(thread_handle);
    if (suspend_count == (DWORD)-1) {
        GE_LOG_ERROR(L"ResumeThread 失败");
        return false;
    }
    GE_LOG_INFO(L"主线程已恢复（挂起计数: " + std::to_wstring(suspend_count) + L"）");
    return true;
}

void ProcessManager::Terminate(HANDLE process_handle, DWORD exit_code) {
    if (process_handle) {
        TerminateProcess(process_handle, exit_code);
        GE_LOG_INFO(L"进程已终止，退出码: " + std::to_wstring(exit_code));
    }
}

void ProcessManager::Close(ProcessInfo& info) {
    if (info.thread_handle) {
        CloseHandle(info.thread_handle);
        info.thread_handle = nullptr;
    }
    if (info.process_handle) {
        CloseHandle(info.process_handle);
        info.process_handle = nullptr;
    }
}

uintptr_t ProcessManager::GetMainModuleBase(HANDLE process, DWORD process_id) {
    return GetModuleBase(process, process_id, L"");
}

uintptr_t ProcessManager::GetModuleBase(HANDLE process, DWORD process_id, const std::wstring& module_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snapshot == INVALID_HANDLE_VALUE) {
        GE_LOG_ERROR(L"CreateToolhelp32Snapshot 失败");
        return 0;
    }

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);

    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &me)) {
        do {
            if (module_name.empty() || _wcsicmp(me.szModule, module_name.c_str()) == 0) {
                base = (uintptr_t)me.modBaseAddr;
                GE_LOG_DEBUG(L"模块基址: " + std::wstring(me.szModule) +
                    L" = 0x" + std::to_wstring(base));
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }

    CloseHandle(snapshot);

    if (base == 0) {
        GE_LOG_WARN(L"未找到模块: " + (module_name.empty() ? L"(主模块)" : module_name));
    }
    return base;
}

DWORD ProcessManager::WaitForExit(HANDLE process, uint32_t timeout_ms) {
    DWORD wait_result = WaitForSingleObject(process, timeout_ms);
    if (wait_result == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        GetExitCodeProcess(process, &exit_code);
        GE_LOG_INFO(L"进程已退出，退出码: " + std::to_wstring(exit_code));
        return exit_code;
    } else if (wait_result == WAIT_TIMEOUT) {
        GE_LOG_INFO(L"等待进程退出超时");
        return STILL_ACTIVE;
    }
    return 0;
}

} // namespace ge
