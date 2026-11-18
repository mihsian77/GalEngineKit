// GalEngineKit 钩子日志实现 — 独立实现，MIT

#include "hook_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>
#include <wchar.h>

static wchar_t g_logPath[MAX_PATH] = L"galenginekit_hook.log";
static bool g_logToFile = true;

// 超过上限时把旧文件改名，避免日志随游戏时长无限增长
static void RotateIfTooLarge() {
    HANDLE f = CreateFileW(g_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size = {};
    bool tooBig = false;
    if (GetFileSizeEx(f, &size)) tooBig = (size.QuadPart > HOOK_LOG_MAX_BYTES);
    CloseHandle(f);
    if (!tooBig) return;

    wchar_t old[MAX_PATH];
    wcsncpy(old, g_logPath, MAX_PATH - 1);
    old[MAX_PATH - 1] = L'\0';
    wcscat(old, L".old");
    // 覆盖上一次的 .old，保留一代历史足够定位问题
    MoveFileExW(g_logPath, old, MOVEFILE_REPLACE_EXISTING);
}

void HookLogInit(const wchar_t* path, bool toFile) {
    if (path && path[0]) {
        wcsncpy(g_logPath, path, MAX_PATH - 1);
        g_logPath[MAX_PATH - 1] = L'\0';
    }
    g_logToFile = toFile;
    if (g_logToFile) RotateIfTooLarge();
}

static const wchar_t* LevelText(HookLogLevel level) {
    switch (level) {
        case HOOK_LOG_WARN:  return L"WARN ";
        case HOOK_LOG_ERROR: return L"ERROR";
        default:             return L"INFO ";
    }
}

static void WriteLine(HookLogLevel level, const wchar_t* fmt, va_list args) {
    wchar_t body[1024];
    _vsnwprintf(body, _countof(body) - 1, fmt, args);
    body[_countof(body) - 1] = L'\0';

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    _snwprintf(line, _countof(line) - 1, L"%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] [krkr2_hook] %s",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               st.wMilliseconds, LevelText(level), body);
    line[_countof(line) - 1] = L'\0';

    OutputDebugStringW(line);
    OutputDebugStringW(L"\n");

    if (!g_logToFile) return;

    // 转 UTF-8 写入日志文件（MinGW 的 fwprintf 不支持 ccs= 参数）
    int len = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return;

    FILE* fp = _wfopen(g_logPath, L"a");
    if (!fp) return;
    char* utf8 = (char*)_malloca(len);
    if (utf8) {
        WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, len, NULL, NULL);
        fputs(utf8, fp);
        fputc('\n', fp);
        _freea(utf8);
    }
    fclose(fp);
}

void HookLog(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    WriteLine(HOOK_LOG_INFO, fmt, args);
    va_end(args);
}

void HookLogWarn(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    WriteLine(HOOK_LOG_WARN, fmt, args);
    va_end(args);
}

void HookLogError(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    WriteLine(HOOK_LOG_ERROR, fmt, args);
    va_end(args);
}
