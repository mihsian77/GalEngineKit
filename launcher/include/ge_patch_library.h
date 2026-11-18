// 独立实现：本模块为 GalEngineKit 原创实现
// 静态补丁库：按目标 exe 指纹筛选 patches/ 下的补丁，避免把补丁打错游戏

#ifndef GALENGINEKIT_PATCH_LIBRARY_H
#define GALENGINEKIT_PATCH_LIBRARY_H

#include "ge_types.h"
#include <string>
#include <vector>

namespace ge {

// 补丁匹配的目标信息
struct PatchTarget {
    std::wstring exe_path;    // 解析后的完整路径（仅日志用）
    std::wstring exe_name;    // 文件名，不含路径，比较用
    uint64_t     size = 0;    // 文件字节数，0=未取到
    uint32_t     crc32 = 0;   // 全文件 CRC32，0=未取到
    std::wstring engine_id;   // 引擎标识，空=未识别
};

class PatchLibrary {
public:
    // CRC32（IEEE 802.3，反射多项式 0xEDB88320）。分块读取时用上次结果续算
    static uint32_t Crc32(const void* data, size_t len, uint32_t seed = 0);

    // 计算文件大小与 CRC32；文件读不到返回 false
    static bool ComputeFileInfo(const std::wstring& path, uint64_t& out_size, uint32_t& out_crc32);

    // 由 exe 路径（可含工作目录拼接）与引擎标识构造目标
    static PatchTarget BuildTarget(const std::wstring& exe_path, const std::wstring& engine_id);

    // 补丁是否匹配目标。不匹配时 out_reason 给出原因（日志用）
    static bool Matches(const PatchFile& pf, const PatchTarget& target, std::wstring* out_reason);

    // 扫描目录下所有 .bp（不递归），返回匹配的补丁，按路径排序
    // out_scanned 返回扫描到的文件总数（含不匹配）
    static std::vector<PatchFile> Collect(const std::wstring& dir,
                                          const PatchTarget& target,
                                          int* out_scanned = nullptr);
};

} // namespace ge

#endif // GALENGINEKIT_PATCH_LIBRARY_H
