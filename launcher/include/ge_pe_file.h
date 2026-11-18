// 独立实现：本模块为 GalEngineKit 原创实现
// 文件级 PE 读取：启动前对磁盘上的 exe/dll 取位数信息（PeHelper 读的是进程内存，用途不同）

#ifndef GALENGINEKIT_PE_FILE_H
#define GALENGINEKIT_PE_FILE_H

#include <windows.h>
#include <string>

namespace ge {

struct FilePeInfo {
    bool     valid = false;
    WORD     machine = 0;
    bool     pe64 = false;
    std::wstring error;   // valid=false 时说明原因
};

// 只读文件头，不解包不映射
FilePeInfo ReadPeHeader(const std::wstring& path);

// 机器类型转人话，例如 "x86 (32 位)"
std::wstring MachineName(WORD machine);

} // namespace ge

#endif // GALENGINEKIT_PE_FILE_H
