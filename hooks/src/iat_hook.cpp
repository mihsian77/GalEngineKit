// GalEngineKit IAT 补丁实现 — 独立实现，MIT

#include "iat_hook.h"
#include <windows.h>
#include <string.h>

// ===== IAT 补丁（PE 导入表遍历，兼容 x86/x64） =====
bool IatPatch(const char* dllName, const char* funcName, void* newFunc, void** origOut) {
    HMODULE hMod = GetModuleHandleW(NULL);
    if (!hMod) return false;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hMod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((BYTE*)hMod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_DATA_DIRECTORY* impDir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!impDir->VirtualAddress || !impDir->Size) return false;

    IMAGE_IMPORT_DESCRIPTOR* desc =
        (IMAGE_IMPORT_DESCRIPTOR*)((BYTE*)hMod + impDir->VirtualAddress);

    for (; desc->Name; desc++) {
        const char* dll = (const char*)((BYTE*)hMod + desc->Name);
        if (_stricmp(dll, dllName) != 0) continue;

        // 没有 OriginalFirstThunk 时，IAT 里存的是已解析的函数地址而不是名字 RVA，
        // 按 RVA 去读 IMAGE_IMPORT_BY_NAME 会踩到野指针。NLB/加壳 PE 常见，直接跳过。
        if (!desc->OriginalFirstThunk) continue;

        IMAGE_THUNK_DATA* thunkINT = (IMAGE_THUNK_DATA*)((BYTE*)hMod + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA* thunkIAT = (IMAGE_THUNK_DATA*)((BYTE*)hMod + desc->FirstThunk);

        for (size_t i = 0; ; i++) {
            ULONG_PTR val = thunkINT[i].u1.AddressOfData;
            if (val == 0) break;
            if (val & IMAGE_ORDINAL_FLAG) continue;  // 按序号导入，跳过

            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)((BYTE*)hMod + val);
            if (strcmp((const char*)ibn->Name, funcName) != 0) continue;

            // 命中：替换 IAT 槽位
            DWORD oldProtect;
            if (!VirtualProtect(&thunkIAT[i].u1.Function, sizeof(ULONG_PTR),
                                PAGE_READWRITE, &oldProtect))
                return false;

            *origOut = (void*)thunkIAT[i].u1.Function;
            thunkIAT[i].u1.Function = (ULONG_PTR)newFunc;

            VirtualProtect(&thunkIAT[i].u1.Function, sizeof(ULONG_PTR),
                           oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), &thunkIAT[i].u1.Function,
                                  sizeof(ULONG_PTR));
            return true;
        }
        return false;  // DLL 存在但函数未静态导入
    }
    return false;  // DLL 未导入
}

