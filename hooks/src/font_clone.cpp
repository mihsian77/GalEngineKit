// GalEngineKit font_clone — 运行时字体克隆（V1-B）
// 独立实现，MIT License
//
// 变更动机：V1-A 的替换依赖系统里已有 CJK 字体。Winlator 容器常没有日文字体，
//   替换后 GDI 会 fallback 到无 CJK 的字体，中文出不来。这里把任一单体 CJK
//   字体在运行时改名成游戏请求的日文字体名（改 name 表 + 补 OS/2 码页位），
//   用 AddFontMemResourceEx 注册进本进程，游戏按原名就能拿到中日字形，不落盘、
//   不改系统、不影响同机其他程序。
// 影响范围：新增 hooks 源文件；仅被 krkr2_hook 在克隆模式下调用。
// 回滚方案：git revert；或在 ini 里 CloneEnabled=0 关闭，退回 V1-A 替换路径。
//
// 实现要点：
//   - 只支持单体 TTF/OTF（'true'/0x00010000/'OTTO'）。TTC 集合需先抽单 face，
//     遇到 'ttcf' 明确失败，不猜。
//   - name 表整表重建后追加到文件尾，只改目录里 name 那条的 offset/length/
//     checksum；原表数据保留不动。省掉重排所有表偏移的麻烦。
//   - OS/2 版本 >= 1 才有 ulCodePageRange 字段，缺失时跳过码页位（不影响加载）。

#include "font_clone.h"
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ===== 大端读写（sfnt 表格用大端） =====
static inline uint16_t Rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t Rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void Wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void Wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

// sfnt 表校验和：按大端 32 位累加，末尾不足补零
static uint32_t TableChecksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0, i = 0;
    for (; i + 4 <= len; i += 4) sum += Rd32(data + i);
    if (i < len) {
        uint8_t tail[4] = { 0, 0, 0, 0 };
        memcpy(tail, data + i, len - i);
        sum += Rd32(tail);
    }
    return sum;
}

// PostScript 名只能 ASCII：去掉空格与非 ASCII，空了给兜底
static void MakePsName(const wchar_t* face, char* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; face[i] && j + 1 < cap; i++) {
        wchar_t c = face[i];
        if (c < 0x21 || c > 0x7E) continue;
        out[j++] = (char)c;
    }
    if (j == 0) { strncpy(out, "GEKClone", cap - 1); out[cap - 1] = 0; }
    else out[j] = 0;
}

static uint8_t* ReadFileAll(const wchar_t* path, uint32_t* outSize) {
    FILE* fp = _wfopen(path, L"rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return NULL; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (buf && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) { free(buf); buf = NULL; }
    fclose(fp);
    if (buf) *outSize = (uint32_t)sz;
    return buf;
}

// 目录项下标：目录从偏移 12 开始，每项 16 字节
static uint32_t DirIndex(const uint8_t* orig, const uint8_t* rec) {
    return (uint32_t)(rec - (orig + 12)) / 16u;
}

// ===== 已注册克隆缓存（进程生命周期，故意不释放：字体句柄引用这块内存） =====
#define MAX_CLONES 16
typedef struct {
    wchar_t  name[LF_FACESIZE];
    HANDLE   hMem;
    uint8_t* data;
    int      count;
} CloneEntry;
static CloneEntry g_clones[MAX_CLONES];
static int g_cloneCount = 0;

extern "C" bool FontCloneRegister(const wchar_t* sourcePath, const wchar_t* targetName) {
    if (!sourcePath || !targetName || !targetName[0]) return false;

    for (int i = 0; i < g_cloneCount; i++) {
        if (_wcsicmp(g_clones[i].name, targetName) == 0) return g_clones[i].hMem != NULL;
    }
    if (g_cloneCount >= MAX_CLONES) return false;

    uint32_t fileSize = 0;
    uint8_t* orig = ReadFileAll(sourcePath, &fileSize);
    if (!orig) return false;
    if (fileSize < 12 + 16) { free(orig); return false; }

    // 1) 校验 sfnt 头；TTC 集合不处理
    uint32_t tag = Rd32(orig);
    if (tag == 0x74746366u /* ttcf */) { free(orig); return false; }
    if (tag != 0x00010000u && tag != 0x4F54544Fu /* OTTO */ && tag != 0x74726275u /* true */) {
        free(orig); return false;
    }
    uint16_t numTables = Rd16(orig + 4);
    if (numTables == 0 || 12 + (uint32_t)numTables * 16u > fileSize) { free(orig); return false; }

    uint8_t* nameRec = NULL;
    uint8_t* os2Rec = NULL;
    for (uint16_t i = 0; i < numTables; i++) {
        uint8_t* r = orig + 12 + (uint32_t)i * 16u;
        if (memcmp(r, "name", 4) == 0) nameRec = r;
        else if (memcmp(r, "OS/2", 4) == 0) os2Rec = r;
    }
    if (!nameRec) { free(orig); return false; }

    // 2) 目标名是否纯 ASCII，决定是否写 Mac(1,0,0) 记录
    bool asciiTarget = true;
    for (size_t i = 0; targetName[i]; i++) if (targetName[i] > 0x7E) asciiTarget = false;

    char ps[64];
    MakePsName(targetName, ps, sizeof(ps));

    // 5 个 nameID 的取值：1 family / 2 subfamily / 3 unique / 4 full / 6 ps
    wchar_t unique[160];
    _snwprintf(unique, 160, L"%s Regular", targetName);
    unique[159] = 0;
    wchar_t psW[80] = {};
    MultiByteToWideChar(CP_ACP, 0, ps, -1, psW, 80);

    const wchar_t* vals[5] = { targetName, L"Regular", unique, targetName, psW };
    const uint16_t kIds[5] = { 1, 2, 3, 4, 6 };

    uint32_t strBytes = 0;
    for (int i = 0; i < 5; i++) {
        strBytes += (uint32_t)wcslen(vals[i]) * 2;                    // UTF-16BE
        if (asciiTarget) strBytes += (uint32_t)wcslen(vals[i]);       // Mac ASCII
    }
    uint16_t recCount = asciiTarget ? 10 : 5;
    uint32_t nameTableLen = 6 + (uint32_t)recCount * 12u + strBytes;
    uint32_t pad = (4 - (fileSize & 3)) & 3;

    uint8_t* patched = (uint8_t*)calloc(1, fileSize + pad + nameTableLen);
    if (!patched) { free(orig); return false; }
    memcpy(patched, orig, fileSize);

    uint32_t nameOff = fileSize + pad;
    uint8_t* nt = patched + nameOff;
    Wr16(nt + 0, 0);
    Wr16(nt + 2, recCount);
    Wr16(nt + 4, (uint16_t)(6 + (uint32_t)recCount * 12u));

    uint8_t* rec = nt + 6;
    uint16_t stringOffset = (uint16_t)(6 + (uint32_t)recCount * 12u);
    uint8_t* strBase = nt + stringOffset;          // 字符串池起点，记录里 offset 相对这里
    uint8_t* pool = strBase;
    uint16_t ri = 0;

    // 记录须按 platformID/encodingID/languageID/nameID 升序（Mac=1 在 Windows=3 前）
    for (int pass = 0; pass < 2; pass++) {
        bool utf16 = (pass == 1);                  // pass0=Mac ASCII, pass1=Windows UTF-16BE
        if (!utf16 && !asciiTarget) continue;
        for (int i = 0; i < 5; i++) {
            const wchar_t* v = vals[i];
            size_t n = wcslen(v);
            uint8_t* r = rec + (uint32_t)ri * 12u;
            if (utf16) { Wr16(r + 0, 3); Wr16(r + 2, 1); Wr16(r + 4, 0x409); }
            else       { Wr16(r + 0, 1); Wr16(r + 2, 0); Wr16(r + 4, 0); }
            Wr16(r + 6, kIds[i]);
            Wr16(r + 8, (uint16_t)(utf16 ? n * 2 : n));
            Wr16(r + 10, (uint16_t)(pool - strBase));
            if (utf16) {
                for (size_t k = 0; k < n; k++) { Wr16(pool, (uint16_t)v[k]); pool += 2; }
            } else {
                for (size_t k = 0; k < n; k++) *pool++ = (uint8_t)(v[k] & 0x7F);
            }
            ri++;
        }
    }

    // 5) 改目录里 name 那一条
    uint32_t nameIdx = DirIndex(orig, nameRec);
    uint8_t* newNameRec = patched + 12 + nameIdx * 16u;
    Wr32(newNameRec + 4, TableChecksum(nt, nameTableLen));
    Wr32(newNameRec + 8, nameOff);
    Wr32(newNameRec + 12, nameTableLen);

    // 6) 补 OS/2 码页位与 CJK Unicode 范围位（版本 >=1 才有这些字段）
    if (os2Rec) {
        uint32_t os2Idx = DirIndex(orig, os2Rec);
        uint8_t* entry = patched + 12 + os2Idx * 16u;
        uint32_t os2Off = Rd32(entry + 8);
        uint32_t os2Len = Rd32(entry + 12);
        if (os2Len >= 84 && (uint64_t)os2Off + 84 <= (uint64_t)fileSize + pad + nameTableLen) {
            if (Rd16(patched + os2Off) >= 1) {
                uint32_t cp1 = Rd32(patched + os2Off + 78);
                cp1 |= (1u << 0) | (1u << 17) | (1u << 18) | (1u << 19) | (1u << 20);
                Wr32(patched + os2Off + 78, cp1);
                uint32_t ur2 = Rd32(patched + os2Off + 46);
                ur2 |= (1u << (59 - 32));    // CJK Unified Ideographs
                Wr32(patched + os2Off + 46, ur2);
            }
        }
    }
    free(orig);

    // 7) 注册进本进程
    DWORD count = 0;
    HANDLE h = AddFontMemResourceEx(patched, fileSize + pad + nameTableLen, NULL, &count);
    if (!h || count == 0) { free(patched); return false; }

    CloneEntry* e = &g_clones[g_cloneCount++];
    wcsncpy(e->name, targetName, LF_FACESIZE - 1);
    e->name[LF_FACESIZE - 1] = 0;
    e->hMem = h;
    e->data = patched;
    e->count = (int)count;
    return true;
}

extern "C" int FontCloneCount(void) { return g_cloneCount; }

extern "C" void FontCloneUnregisterAll(void) {
    for (int i = 0; i < g_cloneCount; i++) {
        if (g_clones[i].hMem) RemoveFontMemResourceEx(g_clones[i].hMem);
        g_clones[i].hMem = NULL;
    }
    g_cloneCount = 0;
}