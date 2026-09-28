// 独立实现：本模块为 GalEngineKit 原创实现
// 启动流程编排：配置 → 进程 → 补丁 → 注入 → 恢复 → 等待

#ifndef GALENGINEKIT_LAUNCHER_H
#define GALENGINEKIT_LAUNCHER_H

#include "ge_types.h"

namespace ge {

class Launcher {
public:
    // 执行完整启动流程
    // config: 已加载的配置
    // wait_for_exit: 是否等待目标进程退出
    // 返回目标进程退出码（如果等待），或 ResultCode
    static int Run(const LauncherConfig& config, bool wait_for_exit = true);

private:
    // 各阶段
    static ResultCode Stage_CreateProcess(const LauncherConfig& config,
        ProcessInfo& out_process, LPVOID& out_env_block);
    static ResultCode Stage_ApplyPatches(const LauncherConfig& config, ProcessInfo& process);
    static ResultCode Stage_InjectDlls(const LauncherConfig& config, ProcessInfo& process);
    static ResultCode Stage_ResumeProcess(const LauncherConfig& config, ProcessInfo& process);
    static void Stage_Cleanup(ProcessInfo& process, LPVOID env_block);
};

} // namespace ge

#endif // GALENGINEKIT_LAUNCHER_H
