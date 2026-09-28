// 独立实现：本模块为 GalEngineKit 原创实现
// 配置解析实现

#include "ge_config.h"
#include "ge_logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>

namespace ge {

// 简易 INI 解析器（不依赖 GetPrivateProfileString，便于跨平台和错误定位）
class SimpleIniParser {
public:
    bool Load(const std::wstring& path) {
        std::ifstream file(WideToUtf8(path), std::ios::binary);
        if (!file.is_open()) return false;

        // 读取全部内容
        std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
        file.close();

        // 处理 UTF-8 BOM
        if (content.size() >= 3 && (uint8_t)content[0] == 0xEF &&
            (uint8_t)content[1] == 0xBB && (uint8_t)content[2] == 0xBF) {
            content = content.substr(3);
        }

        // 逐行解析
        std::string current_section;
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            // 去除 \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // 去除首尾空白
            Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            // 节标题
            if (line.front() == '[' && line.back() == ']') {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

            // 键值对
            size_t eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            Trim(key);
            Trim(value);

            data_[current_section][key] = value;
        }
        return true;
    }

    std::wstring GetString(const std::string& section, const std::string& key,
                            const std::wstring& default_val) const {
        auto sit = data_.find(section);
        if (sit == data_.end()) return default_val;
        auto kit = sit->second.find(key);
        if (kit == sit->second.end()) return default_val;
        return Utf8ToWide(kit->second);
    }

    int GetInt(const std::string& section, const std::string& key, int default_val) const {
        std::wstring val = GetString(section, key, L"");
        if (val.empty()) return default_val;
        try {
            return std::stoi(val);
        } catch (...) {
            return default_val;
        }
    }

    bool GetBool(const std::string& section, const std::string& key, bool default_val) const {
        std::wstring val = GetString(section, key, L"");
        if (val.empty()) return default_val;
        std::wstring lower = val;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        return (lower == L"true" || lower == L"1" || lower == L"yes" || lower == L"on");
    }

    bool HasSection(const std::string& section) const {
        return data_.find(section) != data_.end();
    }

private:
    static void Trim(std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos) { s.clear(); return; }
        size_t end = s.find_last_not_of(" \t");
        s = s.substr(start, end - start + 1);
    }

    std::map<std::string, std::map<std::string, std::string>> data_;
};

ResultCode ConfigLoader::LoadFromFile(const std::wstring& config_path, LauncherConfig& out) {
    SimpleIniParser ini;
    if (!ini.Load(config_path)) {
        GE_LOG_ERROR(L"无法读取配置文件: " + config_path);
        return ResultCode::ConfigLoadFailed;
    }

    out.config_file_path = config_path;

    // [Launcher]
    out.target_exe = ini.GetString("Launcher", "TargetEXE", L"");
    out.working_dir = ini.GetString("Launcher", "WorkingDir", L".");
    out.command_line = ini.GetString("Launcher", "CommandLine", L"");
    out.stay_suspended = ini.GetBool("Launcher", "StaySuspended", false);

    std::wstring log_level_str = ini.GetString("Launcher", "LogLevel", L"info");
    std::transform(log_level_str.begin(), log_level_str.end(), log_level_str.begin(), ::towlower);
    if (log_level_str == L"error") out.log_level = LogLevel::Error;
    else if (log_level_str == L"warn" || log_level_str == L"warning") out.log_level = LogLevel::Warn;
    else if (log_level_str == L"debug") out.log_level = LogLevel::Debug;
    else out.log_level = LogLevel::Info;

    // [Inject]
    int dll_count = ini.GetInt("Inject", "DLLCount", 0);
    for (int i = 0; i < dll_count; i++) {
        std::string key = "DLL_" + std::to_string(i);
        std::wstring dll_path = ini.GetString("Inject", key, L"");
        if (!dll_path.empty()) {
            out.dll_paths.push_back(dll_path);
        }
    }

    std::wstring method_str = ini.GetString("Inject", "InjectMethod", L"auto");
    std::transform(method_str.begin(), method_str.end(), method_str.begin(), ::towlower);
    if (method_str == L"import_table") out.inject_method = InjectMethod::ImportTable;
    else if (method_str == L"helper") out.inject_method = InjectMethod::Helper;
    else out.inject_method = InjectMethod::Auto;

    out.timeout_ms = (uint32_t)ini.GetInt("Inject", "TimeoutMs", 15000);

    // [Patch]
    int patch_count = ini.GetInt("Patch", "PatchCount", 0);
    for (int i = 0; i < patch_count; i++) {
        std::string file_key = "Patch_" + std::to_string(i) + "_File";
        std::string desc_key = "Patch_" + std::to_string(i) + "_Description";
        std::wstring patch_path = ini.GetString("Patch", file_key, L"");
        if (patch_path.empty()) continue;

        PatchFile pf;
        pf.path = patch_path;
        pf.description = ini.GetString("Patch", desc_key, L"");
        if (ParsePatchFile(patch_path, pf)) {
            out.patch_files.push_back(pf);
        } else {
            GE_LOG_WARN(L"补丁文件解析失败，跳过: " + patch_path);
        }
    }

    // [Environment] — 遍历该节所有键
    // SimpleIniParser 没有暴露遍历接口，这里通过已知键名读取
    // （实际使用中环境变量键名不固定，需要扩展 parser；此处读取常见键）
    const wchar_t* common_env_keys[] = {
        L"WINEDLLOVERRIDES", L"WINEDEBUG", L"WINEPREFIX", L"WINESERVER",
        L"SDL_VIDEODRIVER", L"GALENGINEKIT_PROFILE", L"PATH", L"TEMP", L"TMP"
    };
    for (const wchar_t* key : common_env_keys) {
        std::wstring val = ini.GetString("Environment", WideToUtf8(key), L"");
        if (!val.empty()) {
            out.environment_vars.push_back({key, val});
        }
    }

    GE_LOG_INFO(L"配置加载完成: " + config_path);
    GE_LOG_INFO(L"  目标: " + out.target_exe);
    GE_LOG_INFO(L"  注入 DLL 数: " + std::to_wstring(out.dll_paths.size()));
    GE_LOG_INFO(L"  补丁文件数: " + std::to_wstring(out.patch_files.size()));

    return ResultCode::Success;
}

ResultCode ConfigLoader::Validate(const LauncherConfig& config) {
    // 1. TargetEXE 必须存在
    if (config.target_exe.empty()) {
        GE_LOG_ERROR(L"配置校验失败: TargetEXE 为空");
        return ResultCode::ConfigValidateFailed;
    }

    // 检查文件是否存在（相对路径基于 working_dir）
    std::wstring full_path = config.target_exe;
    if (full_path.find(L":\\") == std::wstring::npos && full_path[0] != L'\\' && full_path[0] != L'/') {
        full_path = config.working_dir + L"\\" + config.target_exe;
    }
    DWORD attr = GetFileAttributesW(full_path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        GE_LOG_WARN(L"目标文件不存在（将在运行时再次尝试）: " + full_path);
        // 不强制失败，因为可能工作目录在运行时才确定
    }

    // 2. DLL 路径检查
    for (const auto& dll : config.dll_paths) {
        if (dll.empty()) {
            GE_LOG_ERROR(L"配置校验失败: 存在空的 DLL 路径");
            return ResultCode::ConfigValidateFailed;
        }
    }

    // 3. DLL 不重复
    for (size_t i = 0; i < config.dll_paths.size(); i++) {
        for (size_t j = i + 1; j < config.dll_paths.size(); j++) {
            if (config.dll_paths[i] == config.dll_paths[j]) {
                GE_LOG_ERROR(L"配置校验失败: DLL 路径重复: " + config.dll_paths[i]);
                return ResultCode::ConfigValidateFailed;
            }
        }
    }

    GE_LOG_INFO(L"配置校验通过");
    return ResultCode::Success;
}

bool ConfigLoader::ParsePatchFile(const std::wstring& path, PatchFile& out_patch) {
    std::ifstream file(WideToUtf8(path), std::ios::binary);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    file.close();

    std::istringstream stream(content);
    std::string line;
    int line_num = 0;
    while (std::getline(stream, line)) {
        line_num++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        PatchRule rule;
        if (ParsePatchLine(line, rule)) {
            out_patch.rules.push_back(rule);
        } else {
            GE_LOG_WARN(L"补丁文件第 " + std::to_wstring(line_num) + L" 行解析失败: " + Utf8ToWide(line));
        }
    }

    return !out_patch.rules.empty();
}

bool ConfigLoader::ParsePatchLine(const std::string& line, PatchRule& out_rule) {
    // 格式: <地址> : <原始字节> -> <补丁字节>
    // 地址可以是 0x12345 或 module.exe:0x12345

    // 分离地址和字节部分
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) return false;

    std::string addr_part = line.substr(0, colon_pos);
    std::string bytes_part = line.substr(colon_pos + 1);

    // 去除空白
    auto trim = [](std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) { s.clear(); return; }
        size_t b = s.find_last_not_of(" \t");
        s = s.substr(a, b - a + 1);
    };
    trim(addr_part);
    trim(bytes_part);

    // 解析地址
    size_t module_colon = addr_part.find(':');
    if (module_colon != std::string::npos) {
        out_rule.module_name = Utf8ToWide(addr_part.substr(0, module_colon));
        addr_part = addr_part.substr(module_colon + 1);
        trim(addr_part);
    }

    // 解析 RVA（十六进制）
    try {
        out_rule.rva = (uint32_t)std::stoul(addr_part, nullptr, 16);
    } catch (...) {
        return false;
    }

    // 分离原始字节和补丁字节
    size_t arrow_pos = bytes_part.find("->");
    if (arrow_pos == std::string::npos) return false;

    std::string original_hex = bytes_part.substr(0, arrow_pos);
    std::string patch_hex = bytes_part.substr(arrow_pos + 2);
    trim(original_hex);
    trim(patch_hex);

    out_rule.original_bytes = ParseHexBytes(original_hex);
    out_rule.patch_bytes = ParseHexBytes(patch_hex);

    if (out_rule.original_bytes.empty() || out_rule.patch_bytes.empty()) return false;
    if (out_rule.original_bytes.size() != out_rule.patch_bytes.size()) return false;

    return true;
}

std::vector<uint8_t> ConfigLoader::ParseHexBytes(const std::string& hex_str) {
    std::vector<uint8_t> result;
    std::istringstream stream(hex_str);
    std::string token;
    while (stream >> token) {
        if (token.size() != 2) continue;
        try {
            uint8_t byte = (uint8_t)std::stoul(token, nullptr, 16);
            result.push_back(byte);
        } catch (...) {
            continue;
        }
    }
    return result;
}

} // namespace ge
