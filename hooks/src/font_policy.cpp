// GalEngineKit 字体策略实现 — 独立实现，MIT

#include "font_policy.h"
#include "font_clone.h"
#include "hook_config.h"
#include "hook_log.h"
#include <wchar.h>

// ===== 字体注册表（反向伪装用） =====
// 紧凑数组：活动条目位于 [0, count)，淘汰时与末尾交换，查询只扫活动条目。
// 游戏同时存活的字体数量很小，线性扫描开销可接受。
#define FONT_REG_CAP 128
static struct { HFONT h; LOGFONTW orig; } g_fontReg[FONT_REG_CAP];
static int g_fontRegCount = 0;
static CRITICAL_SECTION g_fontRegLock;
static bool g_fontRegLockReady = false;

void FontRegistryInit() {
    InitializeCriticalSection(&g_fontRegLock);
    g_fontRegLockReady = true;
}

void RegisterFont(HFONT h, const LOGFONTW* orig) {
    if (!h || !g_fontRegLockReady) return;
    EnterCriticalSection(&g_fontRegLock);
    if (g_fontRegCount < FONT_REG_CAP) {
        g_fontReg[g_fontRegCount].h = h;
        g_fontReg[g_fontRegCount].orig = *orig;
        g_fontRegCount++;
    }
    LeaveCriticalSection(&g_fontRegLock);
}

bool LookupFont(HFONT h, LOGFONTW* out) {
    if (!h || !g_fontRegLockReady) return false;
    bool found = false;
    EnterCriticalSection(&g_fontRegLock);
    for (int i = 0; i < g_fontRegCount; i++) {
        if (g_fontReg[i].h == h) { *out = g_fontReg[i].orig; found = true; break; }
    }
    LeaveCriticalSection(&g_fontRegLock);
    return found;
}

void EvictFont(HFONT h) {
    if (!h || !g_fontRegLockReady) return;
    EnterCriticalSection(&g_fontRegLock);
    for (int i = 0; i < g_fontRegCount; i++) {
        if (g_fontReg[i].h == h) {
            g_fontReg[i] = g_fontReg[g_fontRegCount - 1];
            g_fontRegCount--;
            break;
        }
    }
    LeaveCriticalSection(&g_fontRegLock);
}

int ActiveFontCount() {
    return g_fontRegCount;
}

// ===== 存在性探测 =====
typedef struct { wchar_t name[LF_FACESIZE]; int state; } FontState;
static FontState g_fontStates[16];
static int g_fontStateCount = 0;

int ProbeFontExists(const wchar_t* faceName,
                    PFN_CreateFontIndirectW createFont,
                    PFN_GetTextFaceW getFace) {
    for (int i = 0; i < g_fontStateCount; i++) {
        if (_wcsicmp(g_fontStates[i].name, faceName) == 0) return g_fontStates[i].state;
    }
    int state = FS_MISSING;
    if (createFont && getFace) {
        LOGFONTW lf = {};
        lf.lfHeight = -20;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcsncpy(lf.lfFaceName, faceName, LF_FACESIZE - 1);
        HFONT f = createFont(&lf);
        if (f) {
            HDC dc = GetDC(NULL);
            HFONT old = (HFONT)SelectObject(dc, f);
            wchar_t actual[LF_FACESIZE] = {};
            getFace(dc, LF_FACESIZE, actual);
            SelectObject(dc, old);
            ReleaseDC(NULL, dc);
            DeleteObject(f);
            state = (_wcsicmp(actual, faceName) == 0) ? FS_EXISTS : FS_MISSING;
        }
    }
    if (g_fontStateCount < 16) {
        wcsncpy(g_fontStates[g_fontStateCount].name, faceName, LF_FACESIZE - 1);
        g_fontStates[g_fontStateCount].name[LF_FACESIZE - 1] = 0;
        g_fontStates[g_fontStateCount].state = state;
        g_fontStateCount++;
    }
    return state;
}

// ===== 克隆 =====
bool CloneEnsure(const wchar_t* faceName) {
    if (!g_cfg.cloneEnabled || !g_cfg.cloneSource[0]) return false;
    if (FontCloneRegister(g_cfg.cloneSource, faceName)) {
        HookLog(L"克隆字体已注册: '%s' <- %s", faceName, g_cfg.cloneSource);
        return true;
    }
    HookLog(L"克隆失败: '%s'，源=%s（退回替换路径）", faceName, g_cfg.cloneSource);
    return false;
}
