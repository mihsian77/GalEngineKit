// 独立实现：本模块为 GalEngineKit 原创实现
// 文件级 PE 读取实现：只取 DOS 头、NT 头里的 Machine 与 OptionalHeader Magic

#include "ge_pe_file.h"
#include <stdio.h>

namespace ge {

FilePeInfo ReadPeHeader(const std::wstring& path) {
    FilePeInfo info;

    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        info.error = L"打不开文件（路径不对或没有读权限）";
        return info;
    }

    unsigned char buf[1024] = {};
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf), &got, nullptr);
    CloseHandle(f);

    if (got < sizeof(IMAGE_DOS_HEADER)) {
        info.error = L"文件太小，不像 PE";
        return info;
    }
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)buf;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        info.error = L"DOS 头签名不对，不是 PE";
        return info;
    }
    if (dos->e_lfanew <= 0 || (DWORD)dos->e_lfanew + 24 >= got) {
        info.error = L"NT 头偏移越界";
        return info;
    }
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(buf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        info.error = L"NT 头签名不对";
        return info;
    }

    info.valid = true;
    info.machine = nt->FileHeader.Machine;
    info.pe64 = (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    return info;
}

std::wstring MachineName(WORD machine) {
    switch (machine) {
        case IMAGE_FILE_MACHINE_I386:  return L"x86 (32 位)";
        case IMAGE_FILE_MACHINE_AMD64: return L"x64 (64 位)";
        case IMAGE_FILE_MACHINE_ARM64: return L"ARM64";
        case IMAGE_FILE_MACHINE_ARMNT: return L"ARM (32 位)";
        default: {
            wchar_t buf[64];
            swprintf(buf, 64, L"未知 (0x%04X)", machine);
            return buf;
        }
    }
}

} // namespace ge
