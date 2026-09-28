// 独立实现：本模块为 GalEngineKit 原创实现
// PE 格式解析辅助实现

#include "ge_pe.h"
#include "ge_process.h"
#include "ge_logger.h"

namespace ge {

bool PeHelper::ReadDosHeader(HANDLE process, uintptr_t module_base, IMAGE_DOS_HEADER& out_dos) {
    return ProcessManager::ReadMemory(process, module_base, &out_dos, sizeof(IMAGE_DOS_HEADER));
}

bool PeHelper::ReadNtHeaders(HANDLE process, uintptr_t module_base,
    IMAGE_NT_HEADERS32& out_nt32, PeArchitecture& out_arch) {

    IMAGE_DOS_HEADER dos;
    if (!ReadDosHeader(process, module_base, dos)) {
        GE_LOG_ERROR(L"读取 DOS 头失败");
        return false;
    }

    if (dos.e_magic != IMAGE_DOS_SIGNATURE) {
        GE_LOG_ERROR(L"无效的 DOS 签名: 0x" + std::to_wstring(dos.e_magic));
        return false;
    }

    uintptr_t nt_address = module_base + dos.e_lfanew;

    // 先读取 NT 头的前 4 字节（Signature）+ FileHeader（20字节）+ 可选头 Magic（2字节）
    // 总共读取 IMAGE_NT_HEADERS32 大小，然后根据 Magic 判断位数
    if (!ProcessManager::ReadMemory(process, nt_address, &out_nt32, sizeof(IMAGE_NT_HEADERS32))) {
        GE_LOG_ERROR(L"读取 NT 头失败");
        return false;
    }

    if (out_nt32.Signature != IMAGE_NT_SIGNATURE) {
        GE_LOG_ERROR(L"无效的 NT 签名: 0x" + std::to_wstring(out_nt32.Signature));
        return false;
    }

    WORD magic = out_nt32.OptionalHeader.Magic;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        out_arch = PeArchitecture::Pe32;
    } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        out_arch = PeArchitecture::Pe64;
    } else {
        GE_LOG_ERROR(L"未知的可选头 Magic: 0x" + std::to_wstring(magic));
        out_arch = PeArchitecture::Unknown;
        return false;
    }

    return true;
}

PeArchitecture PeHelper::GetArchitecture(HANDLE process, uintptr_t module_base) {
    IMAGE_NT_HEADERS32 nt32;
    PeArchitecture arch;
    if (!ReadNtHeaders(process, module_base, nt32, arch)) {
        return PeArchitecture::Unknown;
    }
    return arch;
}

bool PeHelper::ReadDataDirectory(HANDLE process, uintptr_t module_base,
    int directory_index, uint32_t& out_rva, uint32_t& out_size) {

    IMAGE_DOS_HEADER dos;
    if (!ReadDosHeader(process, module_base, dos)) return false;

    uintptr_t nt_address = module_base + dos.e_lfanew;

    // 读取 FileHeader 以获取可选头大小
    IMAGE_FILE_HEADER file_header;
    uintptr_t file_header_address = nt_address + offsetof(IMAGE_NT_HEADERS32, FileHeader);
    if (!ProcessManager::ReadMemory(process, file_header_address, &file_header, sizeof(IMAGE_FILE_HEADER))) {
        return false;
    }

    // 读取可选头的 Magic 以判断位数
    WORD magic;
    uintptr_t magic_address = nt_address + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
        offsetof(IMAGE_OPTIONAL_HEADER32, Magic);
    if (!ProcessManager::ReadMemory(process, magic_address, &magic, sizeof(WORD))) {
        return false;
    }

    // 数据目录在可选头中的偏移
    uintptr_t data_dir_offset;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        data_dir_offset = offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    } else {
        data_dir_offset = offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
    }

    uintptr_t data_dir_address = nt_address + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
        data_dir_offset + directory_index * sizeof(IMAGE_DATA_DIRECTORY);

    IMAGE_DATA_DIRECTORY data_dir;
    if (!ProcessManager::ReadMemory(process, data_dir_address, &data_dir, sizeof(IMAGE_DATA_DIRECTORY))) {
        return false;
    }

    out_rva = data_dir.VirtualAddress;
    out_size = data_dir.Size;
    return true;
}

bool PeHelper::WriteDataDirectory(HANDLE process, uintptr_t module_base,
    int directory_index, uint32_t rva, uint32_t size) {

    IMAGE_DOS_HEADER dos;
    if (!ReadDosHeader(process, module_base, dos)) return false;

    uintptr_t nt_address = module_base + dos.e_lfanew;

    WORD magic;
    uintptr_t magic_address = nt_address + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
        offsetof(IMAGE_OPTIONAL_HEADER32, Magic);
    if (!ProcessManager::ReadMemory(process, magic_address, &magic, sizeof(WORD))) {
        return false;
    }

    uintptr_t data_dir_offset;
    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        data_dir_offset = offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory);
    } else {
        data_dir_offset = offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory);
    }

    uintptr_t data_dir_address = nt_address + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
        data_dir_offset + directory_index * sizeof(IMAGE_DATA_DIRECTORY);

    IMAGE_DATA_DIRECTORY data_dir;
    data_dir.VirtualAddress = rva;
    data_dir.Size = size;

    // 写入数据目录前需要确保内存可写（PE 头通常是只读的）
    DWORD old_protect;
    if (!VirtualProtectEx(process, (LPVOID)data_dir_address, sizeof(IMAGE_DATA_DIRECTORY),
        PAGE_READWRITE, &old_protect)) {
        GE_LOG_ERROR(L"VirtualProtectEx 失败（数据目录）");
        return false;
    }

    bool success = ProcessManager::WriteMemory(process, data_dir_address, &data_dir, sizeof(IMAGE_DATA_DIRECTORY));

    // 恢复保护
    VirtualProtectEx(process, (LPVOID)data_dir_address, sizeof(IMAGE_DATA_DIRECTORY), old_protect, &old_protect);

    return success;
}

bool PeHelper::ReadImportDescriptors(HANDLE process, uintptr_t import_address,
    PeArchitecture arch, std::vector<IMAGE_IMPORT_DESCRIPTOR>& out_descriptors) {

    out_descriptors.clear();

    // 逐个读取导入描述符，直到全零
    uintptr_t current = import_address;
    while (true) {
        IMAGE_IMPORT_DESCRIPTOR desc;
        if (!ProcessManager::ReadMemory(process, current, &desc, sizeof(IMAGE_IMPORT_DESCRIPTOR))) {
            GE_LOG_ERROR(L"读取导入描述符失败，地址: 0x" + std::to_wstring(current));
            return false;
        }

        // 全零表示结束
        if (desc.OriginalFirstThunk == 0 && desc.TimeDateStamp == 0 &&
            desc.ForwarderChain == 0 && desc.Name == 0 && desc.FirstThunk == 0) {
            break;
        }

        out_descriptors.push_back(desc);
        current += sizeof(IMAGE_IMPORT_DESCRIPTOR);

        // 安全限制，防止无限循环
        if (out_descriptors.size() > 1024) {
            GE_LOG_ERROR(L"导入描述符数量超过 1024，可能已损坏");
            return false;
        }
    }

    GE_LOG_DEBUG(L"读取到 " + std::to_wstring(out_descriptors.size()) + L" 个导入描述符");
    return true;
}

bool PeHelper::WriteImportDescriptors(HANDLE process, uintptr_t address,
    const std::vector<IMAGE_IMPORT_DESCRIPTOR>& descriptors) {

    // 写入所有描述符 + 一个全零结束符
    std::vector<IMAGE_IMPORT_DESCRIPTOR> buffer = descriptors;
    IMAGE_IMPORT_DESCRIPTOR null_desc = {};
    buffer.push_back(null_desc);

    size_t total_size = buffer.size() * sizeof(IMAGE_IMPORT_DESCRIPTOR);
    return ProcessManager::WriteMemory(process, address, buffer.data(), total_size);
}

} // namespace ge
