// 独立实现：本模块为 GalEngineKit 原创实现
// 体检模块：按"目标 exe → 引擎 → 钩子文件 → 字体 → 补丁 → 运行环境"的顺序自查，
// 每项给出结论和依据。玩家把 doctor.txt 贴出来，就能定位大多数"没生效"。

#include "ge_doctor.h"
#include "ge_config.h"
#include "ge_engine_detector.h"
#include "ge_patch_library.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>

namespace ge {

namespace {

// ===== 输出收集 =====
struct Report {
    std::wstring text;
    int warnings = 0;
    int fatals = 0;

    void Raw(const std::wstring& s) { text += s; text += L"\r\n"; }

    // 通过 / 提示 / 失败三种结论，前缀固定，方便人眼扫
    void Pass(const std::wstring& s) { Raw(L"[OK]   " + s); }
    void Warn(const std::wstring& s) { warnings++; Raw(L"[?]    " + s); }
    void Fail(const std::wstring& s) { fatals++; Raw(L"[FAIL] " + s); }
    void Info(const std::wstring& s) { Raw(L"       " + s); }
    void Section(const std::wstring& s) { Raw(L""); Raw(L"==== " + s + L" ===="); }
};

static void OutConsole(const std::wstring& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h) {
        DWORD written = 0;
        if (WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr)) return;
    }
    // 输出被重定向（CI / 管道）时走 UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &utf8[0], len, nullptr, nullptr);
        fwrite(utf8.data(), 1, utf8.size(), stdout);
    }
}

static bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], len, nullptr, nullptr);

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    // BOM：记事本老版本才认，Win10 之后没有也正常，但带上不亏
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    WriteFile(f, bom, 3, &written, nullptr);
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(f);
    return true;
}

static bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (name.empty()) return dir;
    if (name[0] == L'\\' || name[0] == L'/') return name;
    return dir + L"\\" + name;
}

static std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

static std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf(buf, _countof(buf) - 1, fmt, args);
    va_end(args);
    buf[_countof(buf) - 1] = L'\0';
    return buf;
}

// ===== 文件级 PE 解析 =====
// 注意：launcher/src/pe.cpp 里的 PeHelper 读的是目标进程内存，体检要在启动前
// 直接读文件头，所以这里单独实现一小段，只取 Machine 和 Magic。
struct PeInfo {
    bool valid = false;
    WORD machine = 0;
    bool pe64 = false;
    std::wstring error;
};

static PeInfo ReadPeFromFile(const std::wstring& path) {
    PeInfo info;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        info.error = L"打开文件失败";
        return info;
    }
    unsigned char buf[1024] = {};
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf), &got, nullptr);
    CloseHandle(f);

    if (got < sizeof(IMAGE_DOS_HEADER)) {
        info.error = L"文件太小，不是 PE";
        return info;
    }
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)buf;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew + 24 >= (LONG)got) {
        info.error = L"DOS 头无效，不是 PE";
        return info;
    }
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(buf + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        info.error = L"NT 头签名无效";
        return info;
    }
    info.valid = true;
    info.machine = nt->FileHeader.Machine;
    info.pe64 = (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    return info;
}

static std::wstring MachineName(WORD machine) {
    switch (machine) {
        case IMAGE_FILE_MACHINE_I386:  return L"x86 (32 位)";
        case IMAGE_FILE_MACHINE_AMD64: return L"x64 (64 位)";
        case IMAGE_FILE_MACHINE_ARM64: return L"ARM64";
        case IMAGE_FILE_MACHINE_ARMNT: return L"ARM (32 位)";
        default: return Format(L"未知 (0x%04X)", machine);
    }
}

// ===== 字体枚举 =====
static int CALLBACK FontEnumProc(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp) {
    if (lf && lf->lfFaceName[0]) {
        std::vector<std::wstring>* v = (std::vector<std::wstring>*)lp;
        v->push_back(lf->lfFaceName);
    }
    return 1;
}

static std::vector<std::wstring> EnumInstalledFonts() {
    std::vector<std::wstring> fonts;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return fonts;
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, FontEnumProc, (LPARAM)&fonts, 0);
    DeleteDC(dc);
    return fonts;
}

static bool FontPresent(const std::vector<std::wstring>& fonts, const std::wstring& name) {
    for (const auto& f : fonts) {
        if (_wcsicmp(f.c_str(), name.c_str()) == 0) return true;
    }
    return false;
}

// 读 krkr2_font.ini（钩子自己的配置，和启动器配置不是一个文件）
static std::wstring ReadHookIni(const std::wstring& ini_path, const wchar_t* key,
                                const wchar_t* def) {
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Font", key, def, buf, MAX_PATH, ini_path.c_str());
    return buf;
}

// ===== 补丁预演 =====
static void PreviewPatches(const std::wstring& game_dir, const std::wstring& exe_path,
                           const std::wstring& engine_id, Report& r) {
    std::wstring patch_dir = JoinPath(game_dir, L"patches");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(patch_dir, L"*.bp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        r.Warn(L"没有 patches 目录或目录下没有 .bp 补丁，本次启动不会打补丁");
        return;
    }

    PatchTarget target = PatchLibrary::BuildTarget(exe_path, engine_id);
    int total = 0, manual_only = 0, matched = 0;
    std::vector<std::wstring> rejected;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring path = JoinPath(patch_dir, fd.cFileName);
        PatchFile pf;
        if (!ConfigLoader::LoadPatchFile(path, pf)) {
            rejected.push_back(std::wstring(fd.cFileName) + L"：解析失败");
            total++;
            continue;
        }
        total++;
        if (!pf.has_match) {
            manual_only++;
            continue;
        }
        std::wstring reason;
        if (PatchLibrary::Matches(pf, target, &reason)) {
            matched++;
            r.Info(std::wstring(L"命中 ") + fd.cFileName +
                   (pf.name.empty() ? L"" : L"（" + pf.name + L"）"));
        } else {
            rejected.push_back(std::wstring(fd.cFileName) + L"：" + reason);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (matched > 0) {
        r.Pass(Format(L"补丁预演：%d/%d 命中，%d 条无 @match（仅手动使用）",
                      matched, total, manual_only));
    } else {
        r.Warn(Format(L"补丁预演：0/%d 命中，%d 条无 @match（仅手动使用）",
                      total, manual_only));
    }
    for (const auto& s : rejected) r.Info(L"跳过 " + s);
}

} // namespace

int Doctor::Run(const std::wstring& game_dir, const std::wstring& report_path) {
    std::wstring dir = game_dir;
    if (dir.empty()) {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        dir = cwd;
    }

    Report r;
    r.Raw(L"GalEngineKit 体检报告");
    r.Info(L"游戏目录: " + dir);
    r.Info(L"启动器位数: " + std::wstring(sizeof(void*) == 8 ? L"64 位" : L"32 位"));

    // ===== 1. 找目标 exe =====
    r.Section(L"目标程序");
    std::wstring exe_name;
    std::wstring ini_path = JoinPath(dir, L"GalEngineKit.ini");
    if (FileExists(ini_path)) {
        LauncherConfig cfg;
        if (ConfigLoader::LoadFromFile(ini_path, cfg) == ResultCode::Success) {
            exe_name = cfg.target_exe;
            r.Info(L"配置来源: GalEngineKit.ini，target_exe=" +
                   (exe_name.empty() ? std::wstring(L"(空)") : exe_name));
        } else {
            r.Warn(L"GalEngineKit.ini 存在但读取失败，改为按目录扫描");
        }
    }

    if (exe_name.empty()) {
        // 与智能模式同一套规则：跳过配置/安装/卸载类程序
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(JoinPath(dir, L"*.exe").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            std::vector<std::wstring> candidates;
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring name = fd.cFileName;
                if (_wcsicmp(name.c_str(), L"GalEngineKitLauncher.exe") == 0) continue;
                candidates.push_back(name);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            for (const auto& c : candidates) {
                std::wstring low = Lower(c);
                if (low.find(L"config") == std::wstring::npos &&
                    low.find(L"setup") == std::wstring::npos &&
                    low.find(L"unins") == std::wstring::npos &&
                    low.find(L"tool") == std::wstring::npos) {
                    exe_name = c;
                    break;
                }
            }
            if (exe_name.empty() && !candidates.empty()) exe_name = candidates[0];
            if (candidates.size() > 1) {
                r.Info(L"目录下有多个 exe，本次按规则选了 " + exe_name);
            }
        }
    }

    if (exe_name.empty()) {
        r.Fail(L"目录里没有可执行文件。把启动器放进游戏目录，或用 -c 指定配置");
        if (!report_path.empty()) WriteUtf8File(report_path, r.text);
        OutConsole(r.text);
        return 2;
    }
    std::wstring exe_path = JoinPath(dir, exe_name);
    r.Pass(L"目标程序: " + exe_path);

    // ===== 2. PE 位数 =====
    r.Section(L"程序位数");
    PeInfo pe = ReadPeFromFile(exe_path);
    if (!pe.valid) {
        r.Fail(L"读 PE 失败：" + pe.error + L"。这个 exe 可能被壳保护或已损坏");
    } else {
        r.Info(L"机器类型: " + MachineName(pe.machine));
        bool launcher64 = (sizeof(void*) == 8);
        bool target64 = pe.pe64;
        if (launcher64 != target64) {
            r.Fail(L"位数不匹配：目标 " + std::wstring(target64 ? L"64 位" : L"32 位") +
                   L"，当前启动器 " + std::wstring(launcher64 ? L"64 位" : L"32 位") +
                   L"。换成另一份启动器，注入才能成功");
        } else {
            r.Pass(L"位数与启动器一致，可注入");
        }
        if (pe.machine == IMAGE_FILE_MACHINE_ARM64) {
            r.Warn(L"目标是 ARM64 原生程序，本工具按 x86/x64 设计，不支持");
        }
    }

    // ===== 3. 引擎识别 =====
    r.Section(L"引擎识别");
    EngineDetectionResult det = EngineDetector::Detect(dir);
    if (det.found) {
        r.Pass(L"识别为 " + det.engine_name + L"（" + det.engine_id +
               L"，置信度 " + std::to_wstring(det.confidence) + L"%）");
        for (const auto& e : det.evidence) r.Info(L"依据: " + e);
    } else {
        r.Warn(L"未识别到已知引擎。此时不会自动匹配引擎钩子，字体问题需要手动配");
    }

    // ===== 4. 钩子文件 =====
    r.Section(L"钩子文件");
    std::wstring krkr_dll = JoinPath(dir, L"krkr2_hook.dll");
    std::wstring ver_dll = JoinPath(dir, L"version.dll");
    if (FileExists(krkr_dll)) {
        PeInfo kp = ReadPeFromFile(krkr_dll);
        if (kp.valid) {
            bool match = pe.valid && (kp.pe64 == pe.pe64);
            if (match) r.Pass(L"krkr2_hook.dll 存在，位数与目标一致");
            else r.Fail(L"krkr2_hook.dll 位数与目标不一致，注入会失败");
        } else {
            r.Warn(L"krkr2_hook.dll 存在但读不出来（" + kp.error + L"）");
        }
    } else {
        r.Info(L"没有 krkr2_hook.dll（走注入形态时可以没有；走代理形态必须换成 version.dll）");
    }

    if (FileExists(ver_dll)) {
        // 游戏目录里出现 version.dll，多半是别的工具留下的代理，需要人工确认归属
        r.Warn(L"目录里已有 version.dll。若这是别的工具的代理，先备份再部署，否则两个代理会互相覆盖");
    }

    // 钩子配置（字体）
    std::wstring hook_ini = JoinPath(dir, L"krkr2_font.ini");
    std::wstring target_font = L"Microsoft YaHei";
    std::wstring clone_source;
    bool clone_enabled = false;
    if (FileExists(hook_ini)) {
        target_font = ReadHookIni(hook_ini, L"TargetFont", L"Microsoft YaHei");
        clone_source = ReadHookIni(hook_ini, L"CloneSource", L"");
        clone_enabled = ReadHookIni(hook_ini, L"CloneEnabled", L"0") != L"0";
    }

    // ===== 5. 字体 =====
    r.Section(L"字体");
    std::vector<std::wstring> fonts = EnumInstalledFonts();
    r.Info(L"系统可用字体族: " + std::to_wstring(fonts.size()) + L" 个");
    if (fonts.empty()) {
        r.Fail(L"一个字体都枚举不出来，GDI 环境异常（容器里常见：没装字体）");
    }
    if (FontPresent(fonts, target_font)) {
        r.Pass(L"替换目标字体存在: " + target_font);
    } else {
        r.Fail(L"替换目标字体不存在: " + target_font + L"。装了字体再启动，否则替换过去也是方块");
    }

    // 日文字体：存在的话钩子照样替换，不存在则说明必须靠克隆或装字体
    static const wchar_t* kProbe[] = {L"MS Gothic", L"MS Mincho", L"Meiryo", L"Yu Gothic"};
    std::wstring present, missing;
    for (const wchar_t* name : kProbe) {
        if (FontPresent(fonts, name)) present += std::wstring(name) + L" ";
        else missing += std::wstring(name) + L" ";
    }
    if (!present.empty()) r.Info(L"系统已有日文字体: " + present);
    if (!missing.empty()) r.Info(L"系统缺少日文字体: " + missing + L"（会被替换，不用管）");

    if (clone_enabled) {
        if (clone_source.empty()) {
            r.Fail(L"CloneEnabled=1 但没填 CloneSource");
        } else if (!FileExists(clone_source)) {
            r.Fail(L"克隆源不存在: " + clone_source);
        } else {
            r.Pass(L"克隆源存在: " + clone_source);
        }
    }

    // ===== 6. 补丁预演 =====
    r.Section(L"补丁");
    PreviewPatches(dir, exe_path, det.engine_id, r);

    // ===== 7. 运行环境 =====
    r.Section(L"运行环境");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FARPROC wine_ver = ntdll ? GetProcAddress(ntdll, "wine_get_version") : nullptr;
    if (wine_ver) {
        typedef const char* (*WineGetVersion)(void);
        const char* v = ((WineGetVersion)(void*)wine_ver)();
        std::wstring ver;
        if (v) {
            int n = MultiByteToWideChar(CP_UTF8, 0, v, -1, nullptr, 0);
            ver.resize(n > 0 ? n : 0);
            if (n > 0) MultiByteToWideChar(CP_UTF8, 0, v, -1, &ver[0], n);
            while (!ver.empty() && ver.back() == L'\0') ver.pop_back();
        }
        r.Pass(L"运行在 Wine 兼容层（" + ver + L"），符合预期");
        r.Info(L"Wine 环境下用导入表注入，不钩 Wine 内置 DLL；字体缺失属常态，靠替换或克隆");
    } else {
        OSVERSIONINFOEXW osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        if (GetVersionExW((LPOSVERSIONINFOW)&osvi)) {
            r.Info(Format(L"原生 Windows %lu.%lu build %lu",
                          osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber));
        } else {
            r.Info(L"原生 Windows（版本号取不到）");
        }
    }

    // ===== 结论 =====
    r.Section(L"结论");
    if (r.fatals > 0) {
        r.Raw(Format(L"发现 %d 个致命问题、%d 个提示。先解决 [FAIL] 项。", r.fatals, r.warnings));
    } else if (r.warnings > 0) {
        r.Raw(Format(L"没有致命问题，%d 个提示可选择性处理。可以直接启动。", r.warnings));
    } else {
        r.Raw(L"没发现问题。");
    }

    OutConsole(r.text);
    if (!report_path.empty()) {
        if (WriteUtf8File(report_path, r.text)) {
            OutConsole(L"\r\n报告已写入: " + report_path + L"\r\n");
        }
    }
    return r.fatals > 0 ? 2 : 0;
}

} // namespace ge
