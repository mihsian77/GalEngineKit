// 独立实现：本模块为 GalEngineKit 原创实现

#include "ge_game_dir.h"
#include <algorithm>

namespace ge {

namespace {

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

bool LooksLikeUtility(const std::wstring& name) {
    std::wstring low = Lower(name);
    return low.find(L"config") != std::wstring::npos ||
           low.find(L"setup") != std::wstring::npos ||
           low.find(L"unins") != std::wstring::npos ||
           low.find(L"tool") != std::wstring::npos;
}

} // namespace

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (name.empty()) return dir;
    if (name[0] == L'\\' || name[0] == L'/') return name;
    // 已经是 "D:\..." 这种带盘符的绝对路径
    if (name.size() >= 2 && name[1] == L':') return name;
    return dir + L"\\" + name;
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::vector<std::wstring> ListGameExes(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_wcsicmp(fd.cFileName, L"GalEngineKitLauncher.exe") == 0) continue;
        out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

std::wstring PickTargetExe(const std::wstring& dir, bool* out_ambiguous) {
    std::vector<std::wstring> all = ListGameExes(dir);
    if (out_ambiguous) *out_ambiguous = all.size() > 1;
    if (all.empty()) return L"";

    for (const auto& name : all) {
        if (!LooksLikeUtility(name)) return name;
    }
    return all[0];
}

} // namespace ge
