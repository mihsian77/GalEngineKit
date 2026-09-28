// 独立实现：本模块为 GalEngineKit 原创实现
// 引擎自动识别实现

#include "ge_engine_detector.h"
#include "ge_logger.h"
#include <windows.h>
#include <algorithm>

namespace ge {

std::vector<EngineDetector::EngineRule> EngineDetector::GetBuiltinRules() {
    return {
        // KiriKiri2 / KAG
        {
            L"krkr2", L"KiriKiri2",
            { L"data.xp3", L"bgm.xp3", L"voice.xp3", L"scene.pck" },  // file_patterns
            { L"savedata", L"savedata\\krkr" },                          // dir_patterns
            { L".xp3" },                                                   // archive_exts
            { L"krkr2.exe", L"krkr2.vent" },                             // exe_names
            20                                                             // base_score
        },
        // YU-RIS
        {
            L"yuris", L"YU-RIS",
            { L"ysbin\\yscfg.ybn", L"ysbin\\ysbin.ybn", L"cgj.ybn" },
            { L"ysbin" },
            { L".ypf" },
            { L"yuris.exe" },
            25
        },
        // Ren'Py
        {
            L"renpy", L"Ren'Py",
            { L"game\\script.rpy", L"game\\script.rpyc", L"renpy\\__init__.py" },
            { L"game", L"renpy", L"lib" },
            { L".rpa" },
            {},
            20
        },
        // Unity
        {
            L"unity", L"Unity",
            { L"globalgamemanagers", L"level0", L"sharedassets0.assets" },
            { L"*_Data", L"*_Data\\Managed" },
            { L".assets" },
            {},
            25
        },
        // Artemis Engine
        {
            L"artemis", L"Artemis Engine",
            { L"artemis.ini", L"game.arc" },
            {},
            { L".arc" },
            {},
            15
        },
        // CatSystem2
        {
            L"catsystem2", L"CatSystem2",
            { L"boot.dat", L"cs2.dll", L"cs2.exe" },
            { L"cs2" },
            { L".int" },
            { L"cs2.exe" },
            20
        },
        // BGI / Ethornell
        {
            L"bgi", L"BGI/Ethornell",
            { L"bgi.ini", L"game.arc" },
            {},
            { L".arc" },
            {},
            10
        },
        // Majiro
        {
            L"majiro", L"Majiro",
            { L"majiro.ini", L"config.mjo" },
            {},
            { L".mjo", L".majiro" },
            { L"majiro.exe" },
            15
        },
        // TyranoBuilder / TyranoScript
        {
            L"tyrano", L"TyranoBuilder",
            { L"data\\tyrano", L"tyrano\\index.html" },
            { L"data", L"tyrano" },
            {},
            {},
            15
        },
        // RPG Maker VX Ace
        {
            L"rpgmaker_vxa", L"RPG Maker VX Ace",
            { L"Game.rgss3a", L"Game.ini" },
            { L"Data" },
            { L".rvdata2" },
            { L"Game.exe" },
            15
        },
        // RPG Maker MV/MZ
        {
            L"rpgmaker_mv", L"RPG Maker MV/MZ",
            { L"www\\index.html", L"www\\data\\Map001.json", L"data\\Map001.json" },
            { L"www", L"data" },
            {},
            {},
            15
        },
    };
}

bool EngineDetector::HasFile(const std::wstring& directory, const std::wstring& pattern) {
    std::wstring full_path = directory + L"\\" + pattern;

    // 检查是否包含通配符
    if (pattern.find(L'*') != std::wstring::npos) {
        // 通配符匹配：用 FindFirstFile
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(full_path.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            FindClose(hFind);
            return true;
        }
        return false;
    }

    // 精确匹配：检查文件属性
    DWORD attrs = GetFileAttributesW(full_path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool EngineDetector::HasDirectory(const std::wstring& directory, const std::wstring& dir_name) {
    // 支持通配符（如 *_Data）
    if (dir_name.find(L'*') != std::wstring::npos) {
        std::wstring search_path = directory + L"\\" + dir_name;
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(search_path.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            bool found = false;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    found = true;
                    break;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
            return found;
        }
        return false;
    }

    std::wstring full_path = directory + L"\\" + dir_name;
    DWORD attrs = GetFileAttributesW(full_path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool EngineDetector::HasArchiveExtension(const std::wstring& directory, const std::wstring& ext) {
    std::wstring search_path = directory + L"\\*" + ext;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        FindClose(hFind);
        return true;
    }
    return false;
}

std::vector<std::wstring> EngineDetector::ListExeFiles(const std::wstring& directory) {
    std::vector<std::wstring> result;
    std::wstring search_path = directory + L"\\*.exe";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                result.push_back(fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return result;
}

std::vector<EngineDetectionResult> EngineDetector::DetectAll(const std::wstring& directory) {
    std::vector<EngineDetectionResult> results;
    auto rules = GetBuiltinRules();

    // 列出目录中的 exe 文件（用于匹配 exe_names）
    auto exe_files = ListExeFiles(directory);

    for (const auto& rule : rules) {
        EngineDetectionResult result;
        result.engine_id = rule.engine_id;
        result.engine_name = rule.engine_name;
        result.confidence = rule.base_score;

        // 匹配特征文件
        for (const auto& pattern : rule.file_patterns) {
            if (HasFile(directory, pattern)) {
                result.confidence += 20;
                result.evidence.push_back(L"特征文件: " + pattern);
            }
        }

        // 匹配特征目录
        for (const auto& pattern : rule.dir_patterns) {
            if (HasDirectory(directory, pattern)) {
                result.confidence += 15;
                result.evidence.push_back(L"特征目录: " + pattern);
            }
        }

        // 匹配归档扩展名
        for (const auto& ext : rule.archive_exts) {
            if (HasArchiveExtension(directory, ext)) {
                result.confidence += 15;
                result.evidence.push_back(L"归档格式: *" + ext);
            }
        }

        // 匹配 exe 文件名
        for (const auto& exe_name : rule.exe_names) {
            for (const auto& found_exe : exe_files) {
                if (_wcsicmp(found_exe.c_str(), exe_name.c_str()) == 0) {
                    result.confidence += 25;
                    result.evidence.push_back(L"可执行文件: " + exe_name);
                }
            }
        }

        // 只有置信度超过基础分才认为有匹配
        if (result.confidence > rule.base_score) {
            result.found = true;
            results.push_back(result);
        }
    }

    // 按置信度降序排序
    std::sort(results.begin(), results.end(),
        [](const EngineDetectionResult& a, const EngineDetectionResult& b) {
            return a.confidence > b.confidence;
        });

    return results;
}

EngineDetectionResult EngineDetector::Detect(const std::wstring& directory) {
    auto all = DetectAll(directory);

    if (all.empty()) {
        EngineDetectionResult unknown;
        unknown.engine_id = L"unknown";
        unknown.engine_name = L"未知引擎";
        unknown.confidence = 0;
        unknown.found = false;
        unknown.evidence.push_back(L"未匹配到已知引擎特征");
        return unknown;
    }

    GE_LOG_INFO(L"引擎识别完成: " + all[0].engine_name +
        L" (置信度: " + std::to_wstring(all[0].confidence) + L")");
    for (const auto& ev : all[0].evidence) {
        GE_LOG_DEBUG(L"  证据: " + ev);
    }

    if (all.size() > 1 && all[1].confidence > 0) {
        GE_LOG_DEBUG(L"  其他可能: " + all[1].engine_name +
            L" (置信度: " + std::to_wstring(all[1].confidence) + L")");
    }

    return all[0];
}

} // namespace ge
