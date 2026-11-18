// 独立实现：本模块为 GalEngineKit 原创实现

#include "ge_hook_report.h"
#include "ge_game_dir.h"
#include <string>
#include <vector>
#include <stdio.h>

namespace ge {

namespace {

bool ReadAllUtf8(const std::wstring& path, std::wstring& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string utf8;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0) {
        utf8.append(buf, got);
    }
    CloseHandle(f);
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    out.assign(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], n);
    return true;
}

bool Contains(const std::wstring& hay, const wchar_t* needle) {
    return hay.find(needle) != std::wstring::npos;
}

// 日志一行一条，直接数出现次数即可。前缀里不会重复出现这些串
int CountOccurrences(const std::wstring& text, const wchar_t* needle) {
    int count = 0;
    size_t pos = 0;
    while (true) {
        size_t hit = text.find(needle, pos);
        if (hit == std::wstring::npos) break;
        count++;
        pos = hit + 1;
    }
    return count;
}

} // namespace

bool HookReport::Analyze(const std::wstring& game_dir, std::wstring& out_summary) {
    std::wstring log_path = JoinPath(game_dir, L"galenginekit_hook.log");
    std::wstring text;
    if (!ReadAllUtf8(log_path, text)) return false;

    bool installed = Contains(text, L"GetProcAddress 钩子安装成功");
    bool not_in_table = Contains(text, L"GetProcAddress 未在导入表中");
    bool api_missing = Contains(text, L"部分原始字体 API 解析失败");
    int replaced = CountOccurrences(text, L"CreateFontIndirectW: '") +
                   CountOccurrences(text, L"CreateFontW: '");
    int clone_ok = CountOccurrences(text, L"克隆字体已注册");
    int clone_fail = CountOccurrences(text, L"克隆失败");
    bool default_ini = Contains(text, L"未找到") && Contains(text, L"krkr2_font.ini");

    out_summary.clear();

    if (!installed) {
        if (not_in_table) {
            out_summary = L"钩子没装上：游戏主模块的导入表里没有 kernel32!GetProcAddress，"
                          L"大概率不是 KiriKiri2，或者这版游戏用了别的加载方式";
        } else if (api_missing) {
            out_summary = L"钩子没装上：gdi32 的字体 API 没解析出来，环境本身有问题";
        } else {
            out_summary = L"钩子没装上：日志里没有安装成功的记录，检查 dll 是否真的被加载";
        }
        return true;
    }

    if (replaced == 0) {
        out_summary = L"钩子装上了，但一次字体替换都没发生：游戏没请求黑名单里的日文字体，"
                      L"或者它自己打包了字体（这种情况下替换钩子管不到）";
    } else {
        out_summary = L"钩子装上了，字体替换命中 " + std::to_wstring(replaced) + L" 次";
        if (clone_ok > 0) out_summary += L"，克隆字体顶上 " + std::to_wstring(clone_ok) + L" 次";
        if (clone_fail > 0) out_summary += L"，克隆失败 " + std::to_wstring(clone_fail) + L" 次";
        out_summary += L"。字还是不对的话，先确认替换目标字体真的存在";
    }

    if (default_ini) {
        out_summary += L"（没有读到 krkr2_font.ini，用的是默认配置）";
    }
    return true;
}

} // namespace ge
