// 独立实现：本模块为 GalEngineKit 原创实现
// DLL 注入器：导入表注入（推荐）+ 辅助进程注入（兜底）

#ifndef GALENGINEKIT_INJECTOR_H
#define GALENGINEKIT_INJECTOR_H

#include "ge_types.h"
#include <vector>
#include <string>

namespace ge {

class DllInjector {
public:
    // 向目标进程注入单个 DLL（导入表注入）
    // 进程必须处于挂起状态
    static ResultCode InjectByImportTable(
        HANDLE process,
        DWORD process_id,
        const std::wstring& dll_path);

    // 向目标进程注入多个 DLL（一次性构建新导入表）
    static ResultCode InjectMultipleByImportTable(
        HANDLE process,
        DWORD process_id,
        const std::vector<std::wstring>& dll_paths);

    // 辅助进程注入（兜底方案，使用 CreateRemoteThread + LoadLibraryW）
    // 进程可以处于运行状态
    static ResultCode InjectByRemoteThread(
        HANDLE process,
        const std::wstring& dll_path);

    // 自动注入：先尝试导入表注入，失败则用辅助进程注入
    static ResultCode InjectAuto(
        HANDLE process,
        DWORD process_id,
        const std::vector<std::wstring>& dll_paths,
        InjectMethod method);

private:
    // 构建包含新 DLL 的导入表内存布局
    struct ImportTableLayout {
        uintptr_t address = 0;       // 分配的内存基址
        size_t size = 0;             // 总大小
        uintptr_t descriptors = 0;   // 导入描述符数组地址
        uintptr_t dll_names = 0;     // DLL 名字符串区域
        uintptr_t int_array = 0;     // INT 数组区域
        uintptr_t iat_array = 0;     // IAT 数组区域
    };

    // 在目标进程中分配并构建新导入表
    static bool BuildNewImportTable(
        HANDLE process,
        uintptr_t module_base,
        PeArchitecture arch,
        const std::vector<IMAGE_IMPORT_DESCRIPTOR>& original_descriptors,
        const std::vector<std::wstring>& new_dll_paths,
        ImportTableLayout& out_layout);

    // 验证 DLL 是否已注入（检查模块列表）
    static bool IsDllLoaded(HANDLE process, DWORD process_id, const std::wstring& dll_name);
};

} // namespace ge

#endif // GALENGINEKIT_INJECTOR_H
