// 独立实现：本模块为 GalEngineKit 原创实现
// 引擎自动识别：扫描游戏目录特征文件 + PE 导入表，判断引擎类型

#ifndef GALENGINEKIT_ENGINE_DETECTOR_H
#define GALENGINEKIT_ENGINE_DETECTOR_H

#include "ge_types.h"
#include <vector>
#include <string>

namespace ge {

// 引擎识别结果
struct EngineDetectionResult {
    std::wstring engine_id;     // 引擎标识，如 "krkr2", "yuris", "unknown"
    std::wstring engine_name;   // 引擎名称，如 "KiriKiri2", "YU-RIS"
    int confidence = 0;         // 置信度 0-100
    std::vector<std::wstring> evidence; // 匹配证据列表
    bool found = false;         // 是否识别到已知引擎
};

class EngineDetector {
public:
    // 扫描游戏目录，自动识别引擎
    // directory: 游戏根目录
    // 返回识别结果（confidence 最高的引擎）
    static EngineDetectionResult Detect(const std::wstring& directory);

    // 获取所有匹配的引擎（按置信度排序）
    static std::vector<EngineDetectionResult> DetectAll(const std::wstring& directory);

private:
    // 引擎识别规则
    struct EngineRule {
        std::wstring engine_id;
        std::wstring engine_name;
        std::vector<std::wstring> file_patterns;   // 特征文件名（精确匹配或通配）
        std::vector<std::wstring> dir_patterns;    // 特征目录名
        std::vector<std::wstring> archive_exts;    // 归档扩展名（如 ".xp3", ".ypf"）
        std::vector<std::wstring> exe_names;       // 常见 exe 文件名
        int base_score;                             // 基础分值
    };

    // 获取所有内置识别规则
    static std::vector<EngineRule> GetBuiltinRules();

    // 检查目录中是否存在指定文件（支持简单通配 *.ext）
    static bool HasFile(const std::wstring& directory, const std::wstring& pattern);

    // 检查目录中是否存在指定子目录
    static bool HasDirectory(const std::wstring& directory, const std::wstring& dir_name);

    // 检查目录中是否有指定扩展名的文件
    static bool HasArchiveExtension(const std::wstring& directory, const std::wstring& ext);

    // 列出目录中的所有 exe 文件
    static std::vector<std::wstring> ListExeFiles(const std::wstring& directory);
};

} // namespace ge

#endif // GALENGINEKIT_ENGINE_DETECTOR_H
