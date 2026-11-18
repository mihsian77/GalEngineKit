// GalEngineKit 钩子日志实现 — 独立实现，MIT

#include "hook_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

static wchar_t g_logPath[MAX_PATH] = L"galenginekit_hook.log";
static bool g_logToFile = true;

void HookLogInit(const wchar_t* path, bool toFile) {
    if (path && path[0]) {
        wcsncpy(g_logPath, path, MAX_PATH - 1);
        g_logPath[MAX_PATH - 1] = L'\0';
    }
    g_logToFile = toFile;
}

static void HookLogV(const wchar_t* fmt, va_list args) {
    wchar_t buffer[1024];
    _vsnwprintf(buffer, _countof(buffer), fmt, args);

    OutputDebugStringW(buffer);

    if (g_logToFile) {
        // 转 UTF-8 写入日志文件（MinGW 的 fwprintf 不支持 ccs= 参数）
        int len = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            FILE* fp = _wfopen(g_logPath, L"a");
            if (fp) {
                char* utf8 = (char*)_malloca(len);
                if (utf8) {
                    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, len, NULL, NULL);
                    fputs("[krkr2_hook] ", fp);
                    fputs(utf8, fp);
                    fputc('\n', fp);
                    _freea(utf8);
                }
                fclose(fp);
            }
        }
    }
}

void HookLog(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    HookLogV(fmt, args);
    va_end(args);
}
