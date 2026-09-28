// 独立实现：本模块为 GalEngineKit 原创实现
// 静态字节补丁：在进程挂起状态下修改内存中的指令字节

#ifndef GALENGINEKIT_PATCHER_H
#define GALENGINEKIT_PATCHER_H

#include "ge_types.h"
#include <vector>

namespace ge {

class MemoryPatcher {
public:
    // 应用所有补丁文件
    // on_mismatch_continue: 原始字节比对不一致时是否继续（true=继续并警告，false=中止）
    static ResultCode ApplyAll(
        HANDLE process,
        DWORD process_id,
        const std::vector<PatchFile>& patch_files,
        bool on_mismatch_continue = true);

    // 应用单个补丁规则
    static bool ApplyRule(
        HANDLE process,
        DWORD process_id,
        const PatchRule& rule,
        std::string& out_error);

private:
    // 将十六进制字节转为可读字符串
    static std::string BytesToHex(const std::vector<uint8_t>& bytes);
};

} // namespace ge

#endif // GALENGINEKIT_PATCHER_H
