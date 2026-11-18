// 独立实现：本模块为 GalEngineKit 原创实现
// 静态补丁库实现：文件指纹 + 目录扫描 + 匹配判定

#include "ge_patch_library.h"
#include "ge_config.h"
#include "ge_logger.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

namespace ge {

// ---- CRC32 ----
static uint32_t g_crcTable[256];
static bool g_crcTableReady = false;

static void InitCrcTable() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crcTable[i] = c;
    }
    g_crcTableReady = true;
}

uint32_t PatchLibrary::Crc32(const void* data, size_t len, uint32_t seed) {
    if (!g_crcTableReady) InitCrcTable();
    const uint8_t* p = (const uint8_t*)data;
    uint32_t c = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c = g_crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

bool PatchLibrary::ComputeFileInfo(const std::wstring& path, uint64_t& out_size, uint32_t& out_crc32) {
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) return false;

    uint8_t buf[64 * 1024];
    uint64_t total = 0;
    uint32_t crc = 0;
    size_t rd;
    while ((rd = fread(buf, 1, sizeof(buf), fp)) > 0) {
        total += rd;
        crc = Crc32(buf, rd, crc);
    }
    bool ok = (ferror(fp) == 0);
    fclose(fp);
    if (!ok) return false;

    out_size = total;
    out_crc32 = crc;
    return true;
}

static std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? path : path.substr(pos + 1);
}

PatchTarget PatchLibrary::BuildTarget(const std::wstring& exe_path, const std::wstring& engine_id) {
    PatchTarget t;
    t.exe_path = exe_path;
    t.exe_name = FileNameOf(exe_path);
    t.engine_id = engine_id;
    ComputeFileInfo(exe_path, t.size, t.crc32);
    return t;
}

static bool IEqualsW(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (towlower(a[i]) != towlower(b[i])) return false;
    }
    return true;
}

bool PatchLibrary::Matches(const PatchFile& pf, const PatchTarget& target, std::wstring* out_reason) {
    auto fail = [&](const std::wstring& why) -> bool {
        if (out_reason) *out_reason = why;
        return false;
    };

    // 没有匹配条件：只在手动列出时使用，自动扫描一律跳过——打错补丁会把游戏改坏
    if (!pf.has_match) return fail(L"无匹配条件（仅手动指定时使用）");

    if (!pf.match_exe.empty()) {
        if (target.exe_name.empty()) return fail(L"目标 exe 名未知");
        if (!IEqualsW(pf.match_exe, target.exe_name))
            return fail(L"exe 不匹配: 期望 " + pf.match_exe + L"，实际 " + target.exe_name);
    }
    if (pf.match_size != 0) {
        if (target.size == 0) return fail(L"目标文件大小取不到");
        if (pf.match_size != target.size)
            return fail(L"文件大小不匹配: 期望 " + std::to_wstring(pf.match_size) +
                        L"，实际 " + std::to_wstring(target.size));
    }
    if (pf.match_crc32 != 0) {
        if (target.crc32 == 0) return fail(L"目标文件 CRC32 取不到");
        if (pf.match_crc32 != target.crc32) {
            wchar_t buf[32];
            swprintf(buf, 32, L"%08X", (unsigned)target.crc32);
            return fail(L"CRC32 不匹配: 期望 " +
                        std::to_wstring(pf.match_crc32) + L"，实际 " + buf);
        }
    }
    if (!pf.match_engine.empty()) {
        if (target.engine_id.empty()) return fail(L"引擎未识别，跳过需要引擎条件的补丁");
        if (!IEqualsW(pf.match_engine, target.engine_id))
            return fail(L"引擎不匹配: 期望 " + pf.match_engine + L"，实际 " + target.engine_id);
    }
    if (out_reason) *out_reason = L"匹配";
    return true;
}

std::vector<PatchFile> PatchLibrary::Collect(const std::wstring& dir,
                                             const PatchTarget& target,
                                             int* out_scanned) {
    std::vector<PatchFile> matched;
    if (out_scanned) *out_scanned = 0;

    if (dir.empty()) return matched;

    WIN32_FIND_DATAW fd = {};
    std::wstring pattern = dir + L"\\*.bp";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        GE_LOG_INFO(L"补丁目录没有可扫描内容: " + dir);
        return matched;
    }

    std::vector<std::wstring> files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(files.begin(), files.end());

    int scanned = 0;
    for (const auto& f : files) {
        scanned++;
        PatchFile pf;
        pf.path = f;
        if (!ConfigLoader::LoadPatchFile(f, pf)) {
            GE_LOG_WARN(L"补丁解析失败，跳过: " + FileNameOf(f));
            continue;
        }
        std::wstring reason;
        if (!Matches(pf, target, &reason)) {
            GE_LOG_INFO(L"补丁跳过: " + FileNameOf(f) + L"（" + reason + L"）");
            continue;
        }
        GE_LOG_INFO(L"补丁命中: " + FileNameOf(f) +
                    (pf.name.empty() ? L"" : L" [" + pf.name + L"]") +
                    L"（" + std::to_wstring(pf.rules.size()) + L" 条规则）");
        matched.push_back(pf);
    }

    if (out_scanned) *out_scanned = scanned;
    return matched;
}

} // namespace ge
