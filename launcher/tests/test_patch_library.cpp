// GalEngineKit 补丁库单元测试（V1-D）
// 覆盖：CRC32 标准向量、文件指纹、匹配各分支（无元数据/exe/大小/CRC/引擎）、目录扫描筛选
// 32 位编译，Wine 或 Windows 下运行。通过返回 0，失败返回 1。

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/ge_patch_library.h"

using namespace ge;

static int g_fail = 0;
static void Check(int cond, const char* name) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

// 写文本文件（UTF-8）
static bool WriteFileA(const std::wstring& path, const char* content) {
    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (!fp) return false;
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    return true;
}

// 构造一个带匹配条件的补丁文件
static const char* MakeBp(const char* exe, unsigned long long size, const char* crc, const char* engine) {
    static char buf[512];
    _snprintf(buf, sizeof(buf),
        "; @patch name=unit-test\n"
        "; @match exe=%s size=%llu crc32=%s engine=%s\n"
        "0x1000 : 90 90 -> CC CC\n",
        exe, size, crc, engine);
    return buf;
}

int main() {
    printf("=== GalEngineKit 补丁库单元测试 ===\n");

    // 1) CRC32 标准向量："123456789" -> 0xCBF43926
    const char* vec = "123456789";
    Check(PatchLibrary::Crc32(vec, strlen(vec)) == 0xCBF43926u, "CRC32 标准向量 0xCBF43926");
    Check(PatchLibrary::Crc32("", 0) == 0, "CRC32 空数据为 0");

    // 2) 分块续算与整块一致
    uint32_t whole = PatchLibrary::Crc32(vec, strlen(vec));
    uint32_t part = PatchLibrary::Crc32(vec, 4);
    part = PatchLibrary::Crc32(vec + 4, strlen(vec) - 4, part);
    Check(whole == part, "CRC32 分块续算一致");

    // 3) 匹配分支：目标 = test.exe / 1024 字节 / CRC 0xAABBCCDD / krkr2
    PatchTarget t;
    t.exe_name = L"test.exe";
    t.size = 1024;
    t.crc32 = 0xAABBCCDDu;
    t.engine_id = L"krkr2";

    {
        PatchFile pf;
        std::wstring reason;
        // 无元数据 → 不参与自动匹配
        Check(!PatchLibrary::Matches(pf, t, &reason), "无匹配条件不参与自动扫描");
        // exe 名不匹配
        pf.has_match = true;
        pf.match_exe = L"other.exe";
        Check(!PatchLibrary::Matches(pf, t, &reason), "exe 不匹配被拒绝");
        // 大小不匹配
        pf.match_exe.clear();
        pf.match_size = 2048;
        Check(!PatchLibrary::Matches(pf, t, &reason), "大小不匹配被拒绝");
        // CRC 不匹配
        pf.match_size = 0;
        pf.match_crc32 = 0x11111111u;
        Check(!PatchLibrary::Matches(pf, t, &reason), "CRC 不匹配被拒绝");
        // 引擎不匹配
        pf.match_crc32 = 0;
        pf.match_engine = L"yuris";
        Check(!PatchLibrary::Matches(pf, t, &reason), "引擎不匹配被拒绝");
        // 全匹配
        pf.match_engine = L"krkr2";
        pf.match_exe = L"TEST.EXE";   // 大小写不敏感
        pf.match_size = 1024;
        pf.match_crc32 = 0xAABBCCDDu;
        Check(PatchLibrary::Matches(pf, t, &reason), "条件齐全时匹配通过");
    }

    // 4) 文件指纹
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempDir);
    wcscat(tempDir, L"gek_patch_test");
    CreateDirectoryW(tempDir, NULL);

    std::wstring targetFile = std::wstring(tempDir) + L"\\sample.bin";
    WriteFileA(targetFile, "abcdef");
    uint64_t sz = 0; uint32_t crc = 0;
    Check(PatchLibrary::ComputeFileInfo(targetFile, sz, crc), "读取文件指纹成功");
    Check(sz == 6, "文件大小为 6");
    Check(crc == PatchLibrary::Crc32("abcdef", 6), "文件 CRC32 与直接计算一致");

    // 5) 目录扫描：三个补丁，只有一个匹配
    WriteFileA(std::wstring(tempDir) + L"\\nomatch.bp", MakeBp("other.bin", 6, "9F1F1F4E", "krkr2"));
    WriteFileA(std::wstring(tempDir) + L"\\bare.bp", "0x1000 : 90 -> CC\n");   // 无元数据

    PatchTarget t2 = PatchLibrary::BuildTarget(targetFile, L"krkr2");
    // 用实际 CRC 生成匹配补丁，确保命中
    char content[512];
    _snprintf(content, sizeof(content),
        "; @patch name=unit-test\n; @match exe=sample.bin size=6 crc32=%08X engine=krkr2\n"
        "0x1000 : 90 90 -> CC CC\n", (unsigned)crc);
    WriteFileA(std::wstring(tempDir) + L"\\match.bp", content);

    int scanned = 0;
    std::vector<PatchFile> found = PatchLibrary::Collect(tempDir, t2, &scanned);
    printf("  扫描 %d 个，命中 %d 个\n", scanned, (int)found.size());
    Check(scanned == 3, "扫描到 3 个 .bp");
    Check(found.size() == 1, "只有条件匹配的那一个被采用");
    if (found.size() == 1) {
        Check(found[0].rules.size() == 1, "命中补丁解析出 1 条规则");
        Check(found[0].rules[0].rva == 0x1000, "规则 RVA 解析正确");
    }

    DeleteFileW((std::wstring(tempDir) + L"\\match.bp").c_str());
    DeleteFileW((std::wstring(tempDir) + L"\\nomatch.bp").c_str());
    DeleteFileW((std::wstring(tempDir) + L"\\bare.bp").c_str());
    DeleteFileW(targetFile.c_str());
    RemoveDirectoryW(tempDir);

    printf("\n=== 结果: %s ===\n", g_fail == 0 ? "全部通过" : "存在失败");
    return g_fail == 0 ? 0 : 1;
}