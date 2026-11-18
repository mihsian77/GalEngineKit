// 独立实现：本模块为 GalEngineKit 原创实现
// 公共类型定义：结果码、通用结构体、跨模块共享常量

#ifndef GALENGINEKIT_TYPES_H
#define GALENGINEKIT_TYPES_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace ge {

// 启动器结果码（与规格文档 9.3 节一致）
enum class ResultCode : int {
    Success = 0,
    ConfigLoadFailed = 1,
    ConfigValidateFailed = 2,
    ProcessCreateFailed = 3,
    DllInjectFailed = 4,
    ThreadResumeFailed = 5,
    PatchApplyFailed = 6,
    UserCancelled = 10,
    InternalError = 99
};

// 日志级别
enum class LogLevel : int {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3
};

// 注入方式
enum class InjectMethod : int {
    ImportTable,    // 导入表注入（推荐）
    Helper,         // 辅助进程注入（兜底）
    Auto            // 自动：先导入表，失败则辅助
};

// 补丁规则
struct PatchRule {
    std::wstring module_name;     // 模块名，空表示主模块
    uint32_t rva = 0;             // 相对虚拟地址
    std::vector<uint8_t> original_bytes;
    std::vector<uint8_t> patch_bytes;
    std::string description;
};

// 补丁文件解析结果
struct PatchFile {
    std::wstring path;
    std::wstring description;
    std::wstring name;                    // @patch name=
    // 匹配条件（来自 ; @match 注释行）。全空表示无条件，仅供手动列出使用
    std::wstring match_exe;               // 目标 exe 文件名（不含路径）
    uint64_t     match_size = 0;          // 0 = 不校验
    uint32_t     match_crc32 = 0;         // 0 = 不校验
    std::wstring match_engine;            // 引擎标识，如 krkr2
    bool         has_match = false;
    std::vector<PatchRule> rules;
};

// 启动器完整配置
struct LauncherConfig {
    // [Launcher]
    std::wstring target_exe;
    std::wstring working_dir;
    std::wstring command_line;
    bool stay_suspended = false;
    LogLevel log_level = LogLevel::Info;

    // [Inject]
    std::vector<std::wstring> dll_paths;
    InjectMethod inject_method = InjectMethod::Auto;
    uint32_t timeout_ms = 15000;

    // [Patch]
    std::vector<PatchFile> patch_files;
    bool auto_scan_patches = true;        // 自动扫描补丁目录
    std::wstring patch_dir = L"patches";  // 相对工作目录

    // [Environment]
    std::vector<std::pair<std::wstring, std::wstring>> environment_vars;

    // 派生字段
    std::wstring config_file_path;  // 配置文件绝对路径，通过环境变量传递给目标进程
};

// 进程信息
struct ProcessInfo {
    HANDLE process_handle = nullptr;
    HANDLE thread_handle = nullptr;
    DWORD process_id = 0;
    DWORD thread_id = 0;
};

// 通用工具：宽字符串转 UTF-8
inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], size, nullptr, nullptr);
    return utf8;
}

// 通用工具：UTF-8 转宽字符串
inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], size);
    return wide;
}

} // namespace ge

#endif // GALENGINEKIT_TYPES_H
