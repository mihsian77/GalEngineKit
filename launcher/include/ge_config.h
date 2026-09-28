// 独立实现：本模块为 GalEngineKit 原创实现
// 配置解析：读取 INI 格式配置文件，构建 LauncherConfig

#ifndef GALENGINEKIT_CONFIG_H
#define GALENGINEKIT_CONFIG_H

#include "ge_types.h"

namespace ge {

class ConfigLoader {
public:
    // 从文件加载配置
    static ResultCode LoadFromFile(const std::wstring& config_path, LauncherConfig& out_config);

    // 校验配置合法性（规格文档 3.2 节）
    static ResultCode Validate(const LauncherConfig& config);

private:
    // 读取 INI 字符串
    static std::wstring ReadString(HANDLE file_handle, const wchar_t* section,
        const wchar_t* key, const wchar_t* default_val);
    static int ReadInt(HANDLE file_handle, const wchar_t* section,
        const wchar_t* key, int default_val);
    static bool ReadBool(HANDLE file_handle, const wchar_t* section,
        const wchar_t* key, bool default_val);

    // 解析补丁文件
    static bool ParsePatchFile(const std::wstring& path, PatchFile& out_patch);
    static bool ParsePatchLine(const std::string& line, PatchRule& out_rule);
    static std::vector<uint8_t> ParseHexBytes(const std::string& hex_str);
};

} // namespace ge

#endif // GALENGINEKIT_CONFIG_H
