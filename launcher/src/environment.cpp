// 独立实现：本模块为 GalEngineKit 原创实现
// 环境变量管理实现

#include "ge_environment.h"
#include "ge_logger.h"
#include <vector>
#include <string>
#include <algorithm>

namespace ge {

std::vector<std::pair<std::wstring, std::wstring>> EnvironmentBuilder::GetCurrentEnvironment() {
    std::vector<std::pair<std::wstring, std::wstring>> result;

    // GetEnvironmentStringsW 返回的格式：每个变量 "KEY=VALUE\0"，以额外的 \0 结束
    LPWCH env_block = GetEnvironmentStringsW();
    if (!env_block) return result;

    LPWCH ptr = env_block;
    while (*ptr) {
        std::wstring entry = ptr;
        size_t eq_pos = entry.find(L'=');
        if (eq_pos != std::wstring::npos && eq_pos > 0) {
            std::wstring key = entry.substr(0, eq_pos);
            std::wstring value = entry.substr(eq_pos + 1);
            result.push_back({key, value});
        }
        ptr += wcslen(ptr) + 1;
    }

    FreeEnvironmentStringsW(env_block);
    return result;
}

LPVOID EnvironmentBuilder::BuildEnvironmentBlock(
    const std::vector<std::pair<std::wstring, std::wstring>>& custom_vars) {

    // 获取当前环境
    auto current = GetCurrentEnvironment();

    // 用自定义变量覆盖同名变量
    for (const auto& custom : custom_vars) {
        bool found = false;
        for (auto& existing : current) {
            if (_wcsicmp(existing.first.c_str(), custom.first.c_str()) == 0) {
                existing.second = custom.second;
                found = true;
                break;
            }
        }
        if (!found) {
            current.push_back(custom);
        }
    }

    // 计算环境块总大小
    // 格式：每个 "KEY=VALUE\0"，最后额外一个 \0
    size_t total_chars = 1; // 最后的结束符
    for (const auto& entry : current) {
        total_chars += entry.first.size() + 1 + entry.second.size() + 1; // KEY=VALUE\0
    }

    // 分配环境块（使用 LocalAlloc，与 CreateProcess 的要求一致）
    size_t total_bytes = total_chars * sizeof(wchar_t);
    LPVOID block = LocalAlloc(LMEM_FIXED, total_bytes);
    if (!block) {
        GE_LOG_ERROR(L"环境块内存分配失败");
        return nullptr;
    }

    // 填充环境块
    wchar_t* write_ptr = (wchar_t*)block;
    for (const auto& entry : current) {
        wcscpy_s(write_ptr, total_chars, entry.first.c_str());
        write_ptr += entry.first.size();
        *write_ptr++ = L'=';
        wcscpy_s(write_ptr, total_chars, entry.second.c_str());
        write_ptr += entry.second.size();
        *write_ptr++ = L'\0';
    }
    *write_ptr = L'\0'; // 结束符

    GE_LOG_INFO(L"环境块构建完成，共 " + std::to_wstring(current.size()) + L" 个变量");
    return block;
}

void EnvironmentBuilder::FreeEnvironmentBlock(LPVOID block) {
    if (block) {
        LocalFree(block);
    }
}

} // namespace ge
