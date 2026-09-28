// 独立实现：本模块为 GalEngineKit 原创实现
// 静态字节补丁实现

#include "ge_patcher.h"
#include "ge_process.h"
#include "ge_logger.h"
#include <sstream>
#include <iomanip>

namespace ge {

ResultCode MemoryPatcher::ApplyAll(
    HANDLE process,
    DWORD process_id,
    const std::vector<PatchFile>& patch_files,
    bool on_mismatch_continue) {

    if (patch_files.empty()) {
        GE_LOG_INFO(L"没有需要应用的补丁");
        return ResultCode::Success;
    }

    int total = 0, applied = 0, skipped = 0, failed = 0;

    for (const auto& patch_file : patch_files) {
        GE_LOG_INFO(L"应用补丁文件: " + patch_file.path +
            (patch_file.description.empty() ? L"" : L" (" + patch_file.description + L")"));
        GE_LOG_INFO(L"  共 " + std::to_wstring(patch_file.rules.size()) + L" 条规则");

        for (size_t i = 0; i < patch_file.rules.size(); i++) {
            const auto& rule = patch_file.rules[i];
            total++;

            std::string error;
            if (ApplyRule(process, process_id, rule, error)) {
                applied++;
                GE_LOG_INFO(L"  [√] 规则 " + std::to_wstring(i + 1) + L": " +
                    Utf8ToWide(rule.description.empty() ?
                        ("0x" + BytesToHex(rule.original_bytes)) : rule.description));
            } else {
                if (on_mismatch_continue) {
                    skipped++;
                    GE_LOG_WARN(L"  [!] 规则 " + std::to_wstring(i + 1) + L" 跳过: " + Utf8ToWide(error));
                } else {
                    failed++;
                    GE_LOG_ERROR(L"  [×] 规则 " + std::to_wstring(i + 1) + L" 失败: " + Utf8ToWide(error));
                    return ResultCode::PatchApplyFailed;
                }
            }
        }
    }

    GE_LOG_INFO(L"补丁应用完成: 总计 " + std::to_wstring(total) +
        L"，成功 " + std::to_wstring(applied) +
        L"，跳过 " + std::to_wstring(skipped) +
        L"，失败 " + std::to_wstring(failed));

    return ResultCode::Success;
}

bool MemoryPatcher::ApplyRule(
    HANDLE process,
    DWORD process_id,
    const PatchRule& rule,
    std::string& out_error) {

    // 1. 定位模块基址
    uintptr_t module_base;
    if (rule.module_name.empty()) {
        module_base = ProcessManager::GetMainModuleBase(process, process_id);
    } else {
        module_base = ProcessManager::GetModuleBase(process, process_id, rule.module_name);
    }

    if (module_base == 0) {
        out_error = "无法获取模块基址: " + WideToUtf8(rule.module_name.empty() ? L"(主模块)" : rule.module_name);
        return false;
    }

    // 2. 计算目标地址
    uintptr_t target_address = module_base + rule.rva;

    // 3. 读取原始字节
    std::vector<uint8_t> actual_bytes(rule.original_bytes.size());
    if (!ProcessManager::ReadMemory(process, target_address, actual_bytes.data(), actual_bytes.size())) {
        std::ostringstream oss;
        oss << "读取内存失败，地址: 0x" << std::hex << target_address;
        out_error = oss.str();
        return false;
    }

    // 4. 比对原始字节
    if (actual_bytes != rule.original_bytes) {
        std::ostringstream oss;
        oss << "原始字节不匹配，地址: 0x" << std::hex << target_address
            << "，期望: " << BytesToHex(rule.original_bytes)
            << "，实际: " << BytesToHex(actual_bytes);
        out_error = oss.str();
        return false;
    }

    // 5. 写入补丁字节（先修改内存保护）
    DWORD old_protect;
    if (!VirtualProtectEx(process, (LPVOID)target_address, rule.patch_bytes.size(),
        PAGE_EXECUTE_READWRITE, &old_protect)) {
        std::ostringstream oss;
        oss << "VirtualProtectEx 失败，地址: 0x" << std::hex << target_address
            << "，错误码: " << GetLastError();
        out_error = oss.str();
        return false;
    }

    bool write_success = ProcessManager::WriteMemory(process, target_address,
        rule.patch_bytes.data(), rule.patch_bytes.size());

    // 恢复内存保护
    VirtualProtectEx(process, (LPVOID)target_address, rule.patch_bytes.size(),
        old_protect, &old_protect);

    if (!write_success) {
        std::ostringstream oss;
        oss << "写入补丁字节失败，地址: 0x" << std::hex << target_address;
        out_error = oss.str();
        return false;
    }

    // 6. 刷新指令缓存
    FlushInstructionCache(process, (LPCVOID)target_address, rule.patch_bytes.size());

    return true;
}

std::string MemoryPatcher::BytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i > 0) oss << " ";
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return oss.str();
}

} // namespace ge
