// 独立实现：本模块为 GalEngineKit 原创实现
// 启动流程编排实现

#include "ge_launcher.h"
#include "ge_config.h"
#include "ge_process.h"
#include "ge_environment.h"
#include "ge_patcher.h"
#include "ge_injector.h"
#include "ge_logger.h"

namespace ge {

int Launcher::Run(const LauncherConfig& config, bool wait_for_exit) {
    GE_LOG_INFO(L"========== GalEngineKit 启动器开始 ==========");

    ProcessInfo process = {};
    LPVOID env_block = nullptr;
    ResultCode rc = ResultCode::Success;

    // 阶段 1: 配置校验
    rc = ConfigLoader::Validate(config);
    if (rc != ResultCode::Success) {
        GE_LOG_ERROR(L"配置校验失败，启动中止");
        Stage_Cleanup(process, env_block);
        return (int)rc;
    }

    // 阶段 2: 创建挂起进程
    rc = Stage_CreateProcess(config, process, env_block);
    if (rc != ResultCode::Success) {
        Stage_Cleanup(process, env_block);
        return (int)rc;
    }

    // 阶段 3: 应用静态补丁
    rc = Stage_ApplyPatches(config, process);
    if (rc != ResultCode::Success) {
        GE_LOG_ERROR(L"补丁应用失败，终止进程");
        ProcessManager::Terminate(process.process_handle);
        Stage_Cleanup(process, env_block);
        return (int)rc;
    }

    // 阶段 4: 注入 DLL
    rc = Stage_InjectDlls(config, process);
    if (rc != ResultCode::Success) {
        GE_LOG_ERROR(L"DLL 注入失败，终止进程");
        ProcessManager::Terminate(process.process_handle);
        Stage_Cleanup(process, env_block);
        return (int)rc;
    }

    // 阶段 5: 恢复进程（除非配置为保持挂起）
    if (!config.stay_suspended) {
        rc = Stage_ResumeProcess(config, process);
        if (rc != ResultCode::Success) {
            ProcessManager::Terminate(process.process_handle);
            Stage_Cleanup(process, env_block);
            return (int)rc;
        }
    } else {
        GE_LOG_INFO(L"进程保持挂起状态（StaySuspended=true），等待手动恢复");
    }

    // 阶段 6: 等待进程退出（可选）
    int exit_code = 0;
    if (wait_for_exit && !config.stay_suspended) {
        GE_LOG_INFO(L"等待目标进程退出...");
        exit_code = ProcessManager::WaitForExit(process.process_handle, INFINITE);
    }

    // 清理
    Stage_Cleanup(process, env_block);

    GE_LOG_INFO(L"========== GalEngineKit 启动器结束 ==========");
    return exit_code;
}

ResultCode Launcher::Stage_CreateProcess(const LauncherConfig& config,
    ProcessInfo& out_process, LPVOID& out_env_block) {

    GE_LOG_INFO(L"[阶段 1/5] 创建挂起进程");

    // 构建环境块
    out_env_block = EnvironmentBuilder::BuildEnvironmentBlock(config.environment_vars);
    if (!out_env_block) {
        GE_LOG_ERROR(L"环境块构建失败");
        return ResultCode::ProcessCreateFailed;
    }

    // 设置 GALENGINEKIT_CONFIG 环境变量（传递配置文件路径给钩子 DLL）
    // 注意：环境块已经构建完成，这里通过在配置中添加该变量来传递
    // （实际实现中应在 BuildEnvironmentBlock 之前添加到 custom_vars）

    ResultCode rc = ProcessManager::CreateSuspended(
        config.target_exe,
        config.working_dir,
        config.command_line,
        out_env_block,
        out_process);

    if (rc != ResultCode::Success) {
        GE_LOG_ERROR(L"进程创建失败");
        return rc;
    }

    return ResultCode::Success;
}

ResultCode Launcher::Stage_ApplyPatches(const LauncherConfig& config, ProcessInfo& process) {
    if (config.patch_files.empty()) {
        GE_LOG_INFO(L"[阶段 2/5] 无补丁需要应用，跳过");
        return ResultCode::Success;
    }

    GE_LOG_INFO(L"[阶段 2/5] 应用静态字节补丁");
    return MemoryPatcher::ApplyAll(process.process_handle, process.process_id,
        config.patch_files, true);
}

ResultCode Launcher::Stage_InjectDlls(const LauncherConfig& config, ProcessInfo& process) {
    if (config.dll_paths.empty()) {
        GE_LOG_INFO(L"[阶段 3/5] 无 DLL 需要注入，跳过");
        return ResultCode::Success;
    }

    GE_LOG_INFO(L"[阶段 3/5] 注入 DLL（方式: " +
        std::wstring(config.inject_method == InjectMethod::ImportTable ? L"导入表" :
                     config.inject_method == InjectMethod::Helper ? L"辅助进程" : L"自动") +
        L"）");

    return DllInjector::InjectAuto(process.process_handle, process.process_id,
        config.dll_paths, config.inject_method);
}

ResultCode Launcher::Stage_ResumeProcess(const LauncherConfig& config, ProcessInfo& process) {
    GE_LOG_INFO(L"[阶段 4/5] 恢复主线程");

    if (!ProcessManager::ResumeMainThread(process.thread_handle)) {
        return ResultCode::ThreadResumeFailed;
    }

    GE_LOG_INFO(L"[阶段 5/5] 进程已启动，PID: " + std::to_wstring(process.process_id));
    return ResultCode::Success;
}

void Launcher::Stage_Cleanup(ProcessInfo& process, LPVOID env_block) {
    GE_LOG_DEBUG(L"清理资源");
    EnvironmentBuilder::FreeEnvironmentBlock(env_block);
    ProcessManager::Close(process);
}

} // namespace ge
