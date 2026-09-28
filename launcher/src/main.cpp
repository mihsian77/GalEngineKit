// 独立实现：本模块为 GalEngineKit 原创实现
// 命令行入口：解析参数、加载配置、执行启动流程

#include "ge_types.h"
#include "ge_config.h"
#include "ge_logger.h"
#include "ge_launcher.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

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
    int exit_code = 0;

    // 解析命令行参数
    for (int i = 1; i < wargc; i++) {
        std::wstring arg = wargv[i];

        if (arg == L"-h" || arg == L"--help") {
            PrintUsage();
            return 0;
        }
        if (arg == L"-v" || arg == L"--version") {
            PrintVersion();
            return 0;
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
            if (i + 1 < argc) {
                config_path = argv[++i];
                has_config = true;
            } else {
                PrintUsage();
                return 1;
            }
            continue;
        }
        if (arg == L"-l" || arg == L"--log-level") {
            if (i + 1 < argc) {
                std::wstring level = argv[++i];
                if (level == L"error") log_level = LogLevel::Error;
                else if (level == L"warn" || level == L"warning") log_level = LogLevel::Warn;
                else if (level == L"debug") log_level = LogLevel::Debug;
                else log_level = LogLevel::Info;
            } else {
                PrintUsage();
                return 1;
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
    Logger& logger = Logger::Instance();
    logger.SetLevel(log_level);

    // 初始化日志文件（在当前目录的 logs/ 下）
    logger.InitFile(L"logs");

    GE_LOG_INFO(L"GalEngineKit Launcher v0.1.0 启动");
    GE_LOG_INFO(L"配置文件: " + config_path);
    GE_LOG_INFO(L"日志级别: " + std::to_wstring((int)log_level));

    // 加载配置
    LauncherConfig config;
    ResultCode rc = ConfigLoader::LoadFromFile(config_path, config);
    if (rc != ResultCode::Success) {
        GE_LOG_ERROR(L"配置加载失败");
        exit_code = (int)rc;
        goto cleanup;
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
