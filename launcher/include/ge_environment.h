// 独立实现：本模块为 GalEngineKit 原创实现
// 环境变量管理：构建目标进程的环境块

#ifndef GALENGINEKIT_ENVIRONMENT_H
#define GALENGINEKIT_ENVIRONMENT_H

#include "ge_types.h"
#include <vector>

namespace ge {

class EnvironmentBuilder {
public:
    // 构建目标进程的环境块
    // 规则：自定义变量覆盖当前进程同名变量，其余继承当前环境
    // 返回的环境块需要调用 LocalFree 释放
    static LPVOID BuildEnvironmentBlock(
        const std::vector<std::pair<std::wstring, std::wstring>>& custom_vars);

    // 释放环境块
    static void FreeEnvironmentBlock(LPVOID block);

private:
    // 获取当前进程的所有环境变量
    static std::vector<std::pair<std::wstring, std::wstring>> GetCurrentEnvironment();
};

} // namespace ge

#endif // GALENGINEKIT_ENVIRONMENT_H
