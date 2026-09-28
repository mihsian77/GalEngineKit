// 独立实现：本模块为 GalEngineKit 原创实现
// 命令行入口：解析参数、加载配置、执行启动流程

#include "ge_types.h"
#include "ge_config.h"
#include "ge_logger.h"
#include "ge_launcher.h"
#include "ge_engine_detector.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace ge;

static void PrintUsage() {
    const wchar_t* usage =
        L"GalEngineKit Launcher - galgame 引擎适配与中文化工具箱\n"
        L"\n"
        L"用法: GalEngineKitLauncher.exe [选项] [配置文件]\n"
        L"\n"
        L"选项:\n"
        L"  -h, --help          显示帮助信息\n"
        L"  -v, --version       显示版本信息\n"
        L"  -c, --config <路径> 指定配置文件路径（默认: GalEngineKit.ini）\n"
        L"  -l, --log-level <级别>  设置日志级别: error, warn, info, debug\n"
        L"  --no-wait           不等待目标进程退出（启动后立即返回）\n"
        L"  --dry-run           仅校验配置，不实际启动进程\n"
        L"\n"
        L"示例:\n"
        L"  GalEngineKitLauncher.exe\n"
        L"  GalEngineKitLauncher.exe -c mygame.ini\n"
        L"  GalEngineKitLauncher.exe --log-level debug --no-wait\n";
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), usage, (DWORD)wcslen(usage), nullptr, nullptr);
}

static void PrintVersion() {
    const wchar_t* version =
        L"GalEngineKit Launcher v0.1.0\n"
        L"独立实现，MIT 许可\n"
        L"参考: Detours (MIT), SimpleFontHook (功能规格), VN_Localization_Tutorials (功能规格)\n";
    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), version, (DWORD)wcslen(version), nullptr, nullptr);
}

// 检测是否运行在 Wine 环境下（包括 Winlator/CrossOver 等基于 Wine 的兼容层）
// 通过检查 ntdll.dll 是否导出 wine_get_version 函数
static bool IsRunningUnderWine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    return GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

// 获取 Wine 版本字符串（如果在 Wine 下）
static std::wstring GetWineVersion() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return L"";
    FARPROC proc = GetProcAddress(ntdll, "wine_get_version");
    if (!proc) return L"";
    typedef const char* (*wine_get_version_t)(void);
    const char* version = ((wine_get_version_t)proc)();
    if (!version) return L"";
    // UTF-8 转宽字符
    int len = MultiByteToWideChar(CP_UTF8, 0, version, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, version, -1, &result[0], len);
    return result;
}

// 获取 Windows 版本信息
static std::wstring GetWindowsVersionString() {
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    // GetVersionEx 在 Win10+ 上可能返回 6.2，需要用其他方式
    // 但对于兼容性检测，主要版本号足够
    if (GetVersionExW((LPOSVERSIONINFOW)&osvi)) {
        wchar_t buf[64];
        swprintf_s(buf, L"%lu.%lu (build %lu)",
            osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
        return buf;
    }
    return L"未知";
}

// 智能模式：自动扫描当前目录，识别引擎，查找游戏 exe，生成配置
// 返回 true 表示成功，config 已填充；false 表示失败
static bool AutoDetectConfig(LauncherConfig& config, LogLevel log_level) {
    GE_LOG_INFO(L"未找到配置文件，进入智能识别模式...");

    // 获取当前工作目录
    wchar_t current_dir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, current_dir);
    std::wstring working_dir = current_dir;
    GE_LOG_INFO(L"工作目录: " + working_dir);

    // 1. 识别引擎
    EngineDetectionResult detection = EngineDetector::Detect(working_dir);
    if (detection.found) {
        GE_LOG_INFO(L"引擎识别: " + detection.engine_name +
            L" (ID: " + detection.engine_id +
            L", 置信度: " + std::to_wstring(detection.confidence) + L"%)");
    } else {
        GE_LOG_WARN(L"未识别到已知引擎，将使用通用配置");
    }

    // 2. 查找游戏 exe（排除启动器自身）
    std::wstring target_exe;
    std::vector<std::wstring> all_exes;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((working_dir + L"\\*.exe").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (_wcsicmp(fd.cFileName, L"GalEngineKitLauncher.exe") != 0) {
                    all_exes.push_back(fd.cFileName);
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (all_exes.empty()) {
        GE_LOG_ERROR(L"智能模式：当前目录下未找到游戏可执行文件");
        GE_LOG_INFO(L"请将启动器放到游戏目录中，或使用 -c 指定配置文件");
        return false;
    }

    // 如果有多个 exe，优先排除设置/卸载程序，否则用第一个
    target_exe = all_exes[0];
    for (const auto& exe : all_exes) {
        std::wstring lower = exe;
        std::transform(lower.begin(), lower.end(), lower.begin(), towlower);
        if (lower.find(L"config") == std::wstring::npos &&
            lower.find(L"setup") == std::wstring::npos &&
            lower.find(L"unins") == std::wstring::npos &&
            lower.find(L"tool") == std::wstring::npos) {
            target_exe = exe;
            break;
        }
    }
    GE_LOG_INFO(L"目标程序: " + target_exe);

    // 3. 生成配置
    config.target_exe = target_exe;
    config.working_dir = working_dir;
    config.command_line = L"";
    config.stay_suspended = false;
    config.log_level = log_level;
    config.inject_method = InjectMethod::Auto;

    // 4. 自动匹配钩子 DLL
    std::wstring engine_hook = L"hooks\\" + detection.engine_id + L"_hook.dll";
    if (GetFileAttributesW((working_dir + L"\\" + engine_hook).c_str()) != INVALID_FILE_ATTRIBUTES) {
        config.dll_paths.push_back(engine_hook);
        GE_LOG_INFO(L"自动匹配引擎钩子: " + engine_hook);
    }
    else if (GetFileAttributesW((working_dir + L"\\example_hook.dll").c_str()) != INVALID_FILE_ATTRIBUTES) {
        config.dll_paths.push_back(L"example_hook.dll");
        GE_LOG_INFO(L"使用示例钩子: example_hook.dll（验证注入链路）");
    }
    else {
        GE_LOG_INFO(L"未找到钩子 DLL，将以纯启动模式运行（无注入）");
    }

    GE_LOG_INFO(L"智能配置生成完成");
    if (all_exes.size() > 1) {
        GE_LOG_INFO(L"提示：目录下有多个 exe，已选择 " + target_exe +
            L"。如需修改请编辑 GalEngineKit.ini");
    }

    return true;
}

int main(int argc, char* argv[]) {
    // 获取宽字符命令行参数（兼容 MinGW 和 MSVC，不依赖 wmain/-municode）
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        // 回退：用 ANSI 参数转换
        wargc = argc;
        wargv = (wchar_t**)LocalAlloc(LMEM_FIXED, argc * sizeof(wchar_t*));
        for (int i = 0; i < argc; i++) {
            int len = MultiByteToWideChar(CP_ACP, 0, argv[i], -1, nullptr, 0);
            wargv[i] = (wchar_t*)LocalAlloc(LMEM_FIXED, len * sizeof(wchar_t));
            MultiByteToWideChar(CP_ACP, 0, argv[i], -1, wargv[i], len);
        }
    }

    // 默认参数
    std::wstring config_path = L"GalEngineKit.ini";
    LogLevel log_level = LogLevel::Info;
    bool wait_for_exit = true;
    bool dry_run = false;
    bool has_config = false;
    int exit_code = 0;
    bool config_file_exists = false;  // 提前声明，避免 goto cleanup 跳过带初始化器的声明
    LauncherConfig config;   // 提前声明，避免 goto cleanup 跳过初始化
    ResultCode rc = ResultCode::Success;
    Logger& logger = Logger::Instance();  // 提前声明，避免 goto cleanup 跳过引用绑定

    // 解析命令行参数
    for (int i = 1; i < wargc; i++) {
        std::wstring arg = wargv[i];

        if (arg == L"-h" || arg == L"--help") {
            PrintUsage();
            exit_code = 0;
            goto cleanup;
        }
        if (arg == L"-v" || arg == L"--version") {
            PrintVersion();
            exit_code = 0;
            goto cleanup;
        }
        if (arg == L"--no-wait") {
            wait_for_exit = false;
            continue;
        }
        if (arg == L"--dry-run") {
            dry_run = true;
            continue;
        }
        if (arg == L"-c" || arg == L"--config") {
            if (i + 1 < wargc) {
                config_path = wargv[++i];
                has_config = true;
            } else {
                PrintUsage();
                exit_code = 1;
                goto cleanup;
            }
            continue;
        }
        if (arg == L"-l" || arg == L"--log-level") {
            if (i + 1 < wargc) {
                std::wstring level = wargv[++i];
                if (level == L"error") log_level = LogLevel::Error;
                else if (level == L"warn" || level == L"warning") log_level = LogLevel::Warn;
                else if (level == L"debug") log_level = LogLevel::Debug;
                else log_level = LogLevel::Info;
            } else {
                PrintUsage();
                exit_code = 1;
                goto cleanup;
            }
            continue;
        }

        // 不以 - 开头的参数视为配置文件路径
        if (arg[0] != L'-') {
            config_path = arg;
            has_config = true;
        }
    }

    // 初始化日志
    logger.SetLevel(log_level);

    // 初始化日志文件（在当前目录的 logs/ 下）
    logger.InitFile(L"logs");

    GE_LOG_INFO(L"GalEngineKit Launcher v0.1.0 启动");
    GE_LOG_INFO(L"配置文件: " + config_path);
    GE_LOG_INFO(L"日志级别: " + std::to_wstring((int)log_level));

    // 环境检测
    if (IsRunningUnderWine()) {
        std::wstring wine_ver = GetWineVersion();
        GE_LOG_INFO(L"运行环境: Wine " + wine_ver + L"（兼容层模式）");
        GE_LOG_INFO(L"提示: 在 Wine/Winlator 下使用导入表注入，不钩 Wine 内置 DLL");
    } else {
        GE_LOG_INFO(L"运行环境: 原生 Windows " + GetWindowsVersionString());
    }

    // ===== 配置加载 / 智能识别 =====
    config_file_exists = (GetFileAttributesW(config_path.c_str()) != INVALID_FILE_ATTRIBUTES);

    if (!config_file_exists && !has_config) {
        // === 智能模式：自动扫描目录，识别引擎，生成配置 ===
        if (!AutoDetectConfig(config, log_level)) {
            exit_code = (int)ResultCode::ConfigLoadFailed;
            goto cleanup;
        }
    } else {
        // === 配置文件模式：加载指定配置 ===
        if (!config_file_exists) {
            GE_LOG_ERROR(L"配置文件不存在: " + config_path);
            exit_code = (int)ResultCode::ConfigLoadFailed;
            goto cleanup;
        }

        rc = ConfigLoader::LoadFromFile(config_path, config);
        if (rc != ResultCode::Success) {
            GE_LOG_ERROR(L"配置加载失败");
            exit_code = (int)rc;
            goto cleanup;
        }
        GE_LOG_INFO(L"配置文件加载成功");
    }

    // 覆盖日志级别（如果配置文件中指定了）
    logger.SetLevel(config.log_level);

    // 干运行模式：仅校验配置
    if (dry_run) {
        GE_LOG_INFO(L"干运行模式：仅校验配置");
        rc = ConfigLoader::Validate(config);
        if (rc == ResultCode::Success) {
            GE_LOG_INFO(L"配置校验通过");
            exit_code = 0;
        } else {
            GE_LOG_ERROR(L"配置校验失败");
            exit_code = (int)rc;
        }
        goto cleanup;
    }

    // 执行启动流程
    exit_code = Launcher::Run(config, wait_for_exit);

    if (exit_code != 0) {
        GE_LOG_WARN(L"启动器以非零退出码结束: " + std::to_wstring(exit_code));
    }

cleanup:
    // 释放宽字符参数数组
    if (wargv) {
        // CommandLineToArgvW 分配的内存用 LocalFree 释放
        // 回退路径中每个字符串和数组都用 LocalAlloc 分配，需要逐个释放
        // 但简单起见，进程退出时 OS 会回收，这里只释放数组指针
        LocalFree(wargv);
    }
    return exit_code;
}
