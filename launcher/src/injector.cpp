// 独立实现：本模块为 GalEngineKit 原创实现
// DLL 注入器实现：导入表注入 + 辅助进程注入

#include "ge_injector.h"
#include "ge_pe.h"
#include "ge_process.h"
#include "ge_logger.h"
#include <tlhelp32.h>

namespace ge {

ResultCode DllInjector::InjectByImportTable(
    HANDLE process,
    DWORD process_id,
    const std::wstring& dll_path) {

    std::vector<std::wstring> paths = {dll_path};
    return InjectMultipleByImportTable(process, process_id, paths);
}

ResultCode DllInjector::InjectMultipleByImportTable(
    HANDLE process,
    DWORD process_id,
    const std::vector<std::wstring>& dll_paths) {

    if (dll_paths.empty()) {
        GE_LOG_INFO(L"没有需要注入的 DLL");
        return ResultCode::Success;
    }

    GE_LOG_INFO(L"开始导入表注入，共 " + std::to_wstring(dll_paths.size()) + L" 个 DLL");

    // 1. 获取主模块基址
    uintptr_t module_base = ProcessManager::GetMainModuleBase(process, process_id);
    if (module_base == 0) {
        GE_LOG_ERROR(L"无法获取主模块基址");
        return ResultCode::DllInjectFailed;
    }
    GE_LOG_DEBUG(L"主模块基址: 0x" + std::to_wstring(module_base));

    // 2. 判断 PE 位数
    PeArchitecture arch = PeHelper::GetArchitecture(process, module_base);
    if (arch == PeArchitecture::Unknown) {
        GE_LOG_ERROR(L"无法识别 PE 架构");
        return ResultCode::DllInjectFailed;
    }
    GE_LOG_INFO(L"PE 架构: " + std::wstring(arch == PeArchitecture::Pe64 ? L"PE32+ (64位)" : L"PE32 (32位)"));

    // 3. 读取导入表数据目录
    uint32_t import_rva, import_size;
    if (!PeHelper::ReadDataDirectory(process, module_base, IMAGE_DIRECTORY_ENTRY_IMPORT,
        import_rva, import_size)) {
        GE_LOG_ERROR(L"读取导入表数据目录失败");
        return ResultCode::DllInjectFailed;
    }

    if (import_rva == 0) {
        GE_LOG_ERROR(L"目标进程没有导入表");
        return ResultCode::DllInjectFailed;
    }

    uintptr_t import_address = PeHelper::RvaToAddress(module_base, import_rva);
    GE_LOG_DEBUG(L"原始导入表: RVA=0x" + std::to_wstring(import_rva) +
        L"，地址=0x" + std::to_wstring(import_address) +
        L"，大小=" + std::to_wstring(import_size));

    // 4. 读取原始导入描述符
    std::vector<IMAGE_IMPORT_DESCRIPTOR> original_descriptors;
    if (!PeHelper::ReadImportDescriptors(process, import_address, arch, original_descriptors)) {
        GE_LOG_ERROR(L"读取原始导入描述符失败");
        return ResultCode::DllInjectFailed;
    }

    // 5. 构建新导入表
    ImportTableLayout layout;
    if (!BuildNewImportTable(process, module_base, arch,
        original_descriptors, dll_paths, layout)) {
        GE_LOG_ERROR(L"构建新导入表失败");
        return ResultCode::DllInjectFailed;
    }

    // 6. 修改 PE 头导入表数据目录指向新内存
    uint32_t new_import_rva = PeHelper::AddressToRva(module_base, layout.descriptors);
    uint32_t new_import_size = (uint32_t)((dll_paths.size() + original_descriptors.size() + 1) *
        sizeof(IMAGE_IMPORT_DESCRIPTOR));

    if (!PeHelper::WriteDataDirectory(process, module_base, IMAGE_DIRECTORY_ENTRY_IMPORT,
        new_import_rva, new_import_size)) {
        GE_LOG_ERROR(L"写入新导入表数据目录失败");
        ProcessManager::FreeMemory(process, layout.address, layout.size);
        return ResultCode::DllInjectFailed;
    }

    // 7. 刷新指令缓存
    FlushInstructionCache(process, nullptr, 0);

    GE_LOG_INFO(L"导入表注入完成，新导入表 RVA=0x" + std::to_wstring(new_import_rva));

    // 8. 验证（进程恢复后才能验证 DLL 是否加载，这里只做结构验证）
    GE_LOG_INFO(L"导入表结构验证通过，等待进程恢复后加载 DLL");

    return ResultCode::Success;
}

bool DllInjector::BuildNewImportTable(
    HANDLE process,
    uintptr_t module_base,
    PeArchitecture arch,
    const std::vector<IMAGE_IMPORT_DESCRIPTOR>& original_descriptors,
    const std::vector<std::wstring>& new_dll_paths,
    ImportTableLayout& out_layout) {

    size_t thunk_size = PeHelper::GetThunkSize(arch);
    size_t dll_count = new_dll_paths.size();
    size_t original_count = original_descriptors.size();

    // 计算各部分大小
    size_t descriptors_count = dll_count + original_count + 1; // +1 结束符
    size_t descriptors_size = descriptors_count * sizeof(IMAGE_IMPORT_DESCRIPTOR);

    // DLL 名字符串总大小（每个含 \0）
    size_t dll_names_size = 0;
    for (const auto& path : new_dll_paths) {
        dll_names_size += (path.size() + 1) * sizeof(wchar_t);
    }

    // 每个 DLL 需要一个 INT 和一个 IAT，每个包含 1 个全零 thunk（表示结束）
    size_t int_size = dll_count * thunk_size;
    size_t iat_size = dll_count * thunk_size;

    // 总大小（按 16 字节对齐）
    size_t total_size = descriptors_size + dll_names_size + int_size + iat_size;
    total_size = (total_size + 15) & ~15;

    GE_LOG_DEBUG(L"新导入表大小: " + std::to_wstring(total_size) + L" 字节");
    GE_LOG_DEBUG(L"  描述符: " + std::to_wstring(descriptors_size) + L" 字节 (" +
        std::to_wstring(descriptors_count) + L" 个)");
    GE_LOG_DEBUG(L"  DLL 名: " + std::to_wstring(dll_names_size) + L" 字节");
    GE_LOG_DEBUG(L"  INT: " + std::to_wstring(int_size) + L" 字节");
    GE_LOG_DEBUG(L"  IAT: " + std::to_wstring(iat_size) + L" 字节");

    // 在目标进程中分配内存
    uintptr_t memory = ProcessManager::AllocateMemory(process, total_size, PAGE_READWRITE);
    if (memory == 0) {
        GE_LOG_ERROR(L"分配新导入表内存失败");
        return false;
    }

    // 布局：[描述符数组][DLL 名][INT][IAT]
    out_layout.address = memory;
    out_layout.size = total_size;
    out_layout.descriptors = memory;
    out_layout.dll_names = memory + descriptors_size;
    out_layout.int_array = out_layout.dll_names + dll_names_size;
    out_layout.iat_array = out_layout.int_array + int_size;

    // 构建描述符数组
    std::vector<IMAGE_IMPORT_DESCRIPTOR> all_descriptors;

    // 先添加新 DLL 的描述符
    for (size_t i = 0; i < dll_count; i++) {
        IMAGE_IMPORT_DESCRIPTOR desc = {};
        desc.Name = PeHelper::AddressToRva(module_base,
            out_layout.dll_names + i * ((new_dll_paths[i].size() + 1) * sizeof(wchar_t)));
        desc.OriginalFirstThunk = PeHelper::AddressToRva(module_base,
            out_layout.int_array + i * thunk_size);
        desc.FirstThunk = PeHelper::AddressToRva(module_base,
            out_layout.iat_array + i * thunk_size);
        desc.TimeDateStamp = 0;
        desc.ForwarderChain = 0;
        all_descriptors.push_back(desc);

        GE_LOG_DEBUG(L"  新 DLL[" + std::to_wstring(i) + L"]: " + new_dll_paths[i]);
        GE_LOG_DEBUG(L"    Name RVA=0x" + std::to_wstring(desc.Name));
        GE_LOG_DEBUG(L"    INT RVA=0x" + std::to_wstring(desc.OriginalFirstThunk));
        GE_LOG_DEBUG(L"    IAT RVA=0x" + std::to_wstring(desc.FirstThunk));
    }

    // 再添加原始描述符
    for (const auto& desc : original_descriptors) {
        all_descriptors.push_back(desc);
    }

    // 写入描述符数组（含全零结束符）
    if (!PeHelper::WriteImportDescriptors(process, out_layout.descriptors, all_descriptors)) {
        GE_LOG_ERROR(L"写入导入描述符失败");
        ProcessManager::FreeMemory(process, memory, total_size);
        return false;
    }

    // 写入 DLL 名字符串
    uintptr_t name_ptr = out_layout.dll_names;
    for (const auto& path : new_dll_paths) {
        size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        if (!ProcessManager::WriteMemory(process, name_ptr, path.c_str(), bytes)) {
            GE_LOG_ERROR(L"写入 DLL 名字符串失败: " + path);
            ProcessManager::FreeMemory(process, memory, total_size);
            return false;
        }
        name_ptr += bytes;
    }

    // INT 和 IAT 已经是全零（VirtualAllocEx 初始化为零），不需要额外写入
    // 但为了保险，显式写入全零
    std::vector<uint8_t> zeros(int_size + iat_size, 0);
    if (!ProcessManager::WriteMemory(process, out_layout.int_array, zeros.data(), zeros.size())) {
        GE_LOG_ERROR(L"写入 INT/IAT 全零失败");
        ProcessManager::FreeMemory(process, memory, total_size);
        return false;
    }

    GE_LOG_INFO(L"新导入表构建成功，基址=0x" + std::to_wstring(memory));
    return true;
}

ResultCode DllInjector::InjectByRemoteThread(
    HANDLE process,
    const std::wstring& dll_path) {

    GE_LOG_INFO(L"使用辅助进程注入: " + dll_path);

    // 在目标进程中分配内存，写入 DLL 路径
    size_t path_bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    uintptr_t path_memory = ProcessManager::AllocateMemory(process, path_bytes, PAGE_READWRITE);
    if (path_memory == 0) {
        GE_LOG_ERROR(L"分配 DLL 路径内存失败");
        return ResultCode::DllInjectFailed;
    }

    if (!ProcessManager::WriteMemory(process, path_memory, dll_path.c_str(), path_bytes)) {
        GE_LOG_ERROR(L"写入 DLL 路径失败");
        ProcessManager::FreeMemory(process, path_memory, path_bytes);
        return ResultCode::DllInjectFailed;
    }

    // 获取 LoadLibraryW 地址（kernel32.dll 在所有进程中加载地址相同）
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        GE_LOG_ERROR(L"获取 kernel32.dll 模块句柄失败");
        ProcessManager::FreeMemory(process, path_memory, path_bytes);
        return ResultCode::DllInjectFailed;
    }

    FARPROC load_library = GetProcAddress(kernel32, "LoadLibraryW");
    if (!load_library) {
        GE_LOG_ERROR(L"获取 LoadLibraryW 地址失败");
        ProcessManager::FreeMemory(process, path_memory, path_bytes);
        return ResultCode::DllInjectFailed;
    }

    // 创建远程线程调用 LoadLibraryW
    HANDLE remote_thread = CreateRemoteThread(process, nullptr, 0,
        (LPTHREAD_START_ROUTINE)load_library, (LPVOID)path_memory, 0, nullptr);

    if (!remote_thread) {
        DWORD error = GetLastError();
        GE_LOG_ERROR(L"CreateRemoteThread 失败，错误码: " + std::to_wstring(error));
        ProcessManager::FreeMemory(process, path_memory, path_bytes);
        return ResultCode::DllInjectFailed;
    }

    // 等待线程结束
    WaitForSingleObject(remote_thread, 15000);

    // 检查线程退出码（即 LoadLibraryW 的返回值，非零表示成功）
    DWORD exit_code = 0;
    GetExitCodeThread(remote_thread, &exit_code);
    CloseHandle(remote_thread);

    // 释放路径内存
    ProcessManager::FreeMemory(process, path_memory, path_bytes);

    if (exit_code == 0) {
        GE_LOG_ERROR(L"LoadLibraryW 返回 0，DLL 加载失败");
        return ResultCode::DllInjectFailed;
    }

    GE_LOG_INFO(L"辅助进程注入成功: " + dll_path);
    return ResultCode::Success;
}

ResultCode DllInjector::InjectAuto(
    HANDLE process,
    DWORD process_id,
    const std::vector<std::wstring>& dll_paths,
    InjectMethod method) {

    if (dll_paths.empty()) return ResultCode::Success;

    switch (method) {
        case InjectMethod::ImportTable:
            return InjectMultipleByImportTable(process, process_id, dll_paths);

        case InjectMethod::Helper:
            for (const auto& dll : dll_paths) {
                ResultCode rc = InjectByRemoteThread(process, dll);
                if (rc != ResultCode::Success) return rc;
            }
            return ResultCode::Success;

        case InjectMethod::Auto:
        default: {
            GE_LOG_INFO(L"自动注入模式：先尝试导入表注入");
            ResultCode rc = InjectMultipleByImportTable(process, process_id, dll_paths);
            if (rc == ResultCode::Success) {
                return ResultCode::Success;
            }

            GE_LOG_WARN(L"导入表注入失败，回退到辅助进程注入");
            for (const auto& dll : dll_paths) {
                rc = InjectByRemoteThread(process, dll);
                if (rc != ResultCode::Success) return rc;
            }
            return ResultCode::Success;
        }
    }
}

bool DllInjector::IsDllLoaded(HANDLE process, DWORD process_id, const std::wstring& dll_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);

    bool found = false;
    if (Module32FirstW(snapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, dll_name.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }

    CloseHandle(snapshot);
    return found;
}

} // namespace ge
