// 独立实现：本模块为 GalEngineKit 原创实现
// PE 格式解析辅助：读取 DOS/NT/可选头、数据目录、导入表

#ifndef GALENGINEKIT_PE_H
#define GALENGINEKIT_PE_H

#include "ge_types.h"
#include <windows.h>

namespace ge {

// PE 位数
enum class PeArchitecture {
    Unknown,
    Pe32,    // 32 位
    Pe64     // 64 位 (PE32+)
};

// 导入表数据目录信息
struct ImportDirectoryInfo {
    uint32_t rva = 0;       // 导入表 RVA
    uint32_t size = 0;      // 导入表大小
    uintptr_t address = 0;  // 导入表在目标进程中的绝对地址
};

class PeHelper {
public:
    // 读取目标进程主模块的 DOS 头
    static bool ReadDosHeader(HANDLE process, uintptr_t module_base, IMAGE_DOS_HEADER& out_dos);

    // 读取 NT 头（Signature + FileHeader + 可选头的 Magic 字段）
    static bool ReadNtHeaders(HANDLE process, uintptr_t module_base,
        IMAGE_NT_HEADERS32& out_nt32, PeArchitecture& out_arch);

    // 判断 PE 位数
    static PeArchitecture GetArchitecture(HANDLE process, uintptr_t module_base);

    // 读取指定数据目录（通过目录索引，如 IMAGE_DIRECTORY_ENTRY_IMPORT）
    static bool ReadDataDirectory(HANDLE process, uintptr_t module_base,
        int directory_index, uint32_t& out_rva, uint32_t& out_size);

    // 写入指定数据目录
    static bool WriteDataDirectory(HANDLE process, uintptr_t module_base,
        int directory_index, uint32_t rva, uint32_t size);

    // RVA 转目标进程绝对地址
    static uintptr_t RvaToAddress(uintptr_t module_base, uint32_t rva) {
        return module_base + rva;
    }

    // 绝对地址转 RVA
    static uint32_t AddressToRva(uintptr_t module_base, uintptr_t address) {
        return (uint32_t)(address - module_base);
    }

    // 读取导入描述符数组（直到全零结束）
    static bool ReadImportDescriptors(HANDLE process, uintptr_t import_address,
        PeArchitecture arch, std::vector<IMAGE_IMPORT_DESCRIPTOR>& out_descriptors);

    // 写入导入描述符数组
    static bool WriteImportDescriptors(HANDLE process, uintptr_t address,
        const std::vector<IMAGE_IMPORT_DESCRIPTOR>& descriptors);

    // 获取 thunk 大小（32位=4, 64位=8）
    static size_t GetThunkSize(PeArchitecture arch) {
        return (arch == PeArchitecture::Pe64) ? 8 : 4;
    }
};

} // namespace ge

#endif // GALENGINEKIT_PE_H
