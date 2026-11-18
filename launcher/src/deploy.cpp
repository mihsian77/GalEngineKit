// 独立实现：本模块为 GalEngineKit 原创实现
// 部署实现：选 dll → 备份原文件 → 复制 → 写清单。全部动作都可逆。

#include "ge_deploy.h"
#include "ge_game_dir.h"
#include "ge_pe_file.h"
#include <windows.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>

namespace ge {

namespace {

const wchar_t* kBackupSuffix = L".gek-bak";

struct ManifestEntry {
    bool created = false;          // true: 我们新建的文件
    std::wstring name;             // 部署后的文件名
    std::wstring backup;           // created=false 时：备份文件名
};

// ===== 文本读写（UTF-8）=====
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

bool WriteAllUtf8(const std::wstring& path, const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], n, nullptr, nullptr);

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(f);
    return true;
}

// ===== 清单 =====
std::vector<ManifestEntry> ReadManifest(const std::wstring& path) {
    std::vector<ManifestEntry> entries;
    std::wstring text;
    if (!ReadAllUtf8(path, text)) return entries;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = text.substr(pos, (nl == std::wstring::npos) ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();

        if (line.compare(0, 8, L"created ") == 0) {
            ManifestEntry e; e.created = true; e.name = line.substr(8); entries.push_back(e);
        } else if (line.compare(0, 10, L"backed-up ") == 0) {
            std::wstring rest = line.substr(10);
            size_t sp = rest.find(L' ');
            ManifestEntry e;
            e.created = false;
            e.name = rest.substr(0, sp);
            if (sp != std::wstring::npos) e.backup = rest.substr(sp + 1);
            entries.push_back(e);
        }
    }
    return entries;
}

std::wstring SerializeManifest(const std::vector<ManifestEntry>& entries) {
    std::wstring text = L"# GalEngineKit 部署记录，--undeploy 按这个还原，别手改\r\n";
    for (const auto& e : entries) {
        if (e.created) text += L"created " + e.name + L"\r\n";
        else text += L"backed-up " + e.name + L" " + e.backup + L"\r\n";
    }
    return text;
}

bool HasEntry(const std::vector<ManifestEntry>& entries, const std::wstring& name) {
    for (const auto& e : entries) {
        if (_wcsicmp(e.name.c_str(), name.c_str()) == 0) return true;
    }
    return false;
}

// ===== 源 dll 定位 =====
struct SourcePick {
    std::wstring path;
    bool pe64 = false;
};

// 按 "带位数后缀的发布名 → 裸名" 的顺序找，必须位数匹配才收
bool FindSourceDll(const std::vector<std::wstring>& dirs, const wchar_t* base,
                   bool want64, SourcePick& out, std::wstring& why_not) {
    std::wstring suffix = want64 ? L"-64bit.dll" : L"-32bit.dll";
    std::wstring candidates[2] = { std::wstring(base) + suffix, std::wstring(base) + L".dll" };
    for (const auto& dir : dirs) {
        for (const auto& cname : candidates) {
            std::wstring p = JoinPath(dir, cname);
            if (!FileExists(p)) continue;
            FilePeInfo pe = ReadPeHeader(p);
            if (!pe.valid) { why_not += p + L"（" + pe.error + L"）；"; continue; }
            if (pe.pe64 != want64) {
                why_not += p + L"（位数不对，跳过）；";
                continue;
            }
            out.path = p;
            out.pe64 = pe.pe64;
            return true;
        }
    }
    return false;
}

std::wstring SelfDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    size_t slash = s.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : s.substr(0, slash);
}

bool DeployOne(const std::wstring& dir, const std::wstring& src_path, const std::wstring& dest_name,
               std::vector<ManifestEntry>& entries) {
    std::wstring dest = JoinPath(dir, dest_name);
    bool already_tracked = HasEntry(entries, dest_name);

    if (FileExists(dest) && !already_tracked) {
        std::wstring backup = dest_name + kBackupSuffix;
        std::wstring backup_path = JoinPath(dir, backup);
        if (FileExists(backup_path)) {
            // 备份占着位置说明上一次部署没走完 undeploy，直接沿用旧备份
            ManifestEntry e; e.created = false; e.name = dest_name; e.backup = backup;
            entries.push_back(e);
        } else {
            if (!MoveFileW(dest.c_str(), backup_path.c_str())) return false;
            ManifestEntry e; e.created = false; e.name = dest_name; e.backup = backup;
            entries.push_back(e);
        }
    } else if (!FileExists(dest)) {
        ManifestEntry e; e.created = true; e.name = dest_name;
        entries.push_back(e);
    }

    if (!CopyFileW(src_path.c_str(), dest.c_str(), FALSE)) return false;
    return true;
}

const char* kDefaultHookIni =
    "; GalEngineKit 字体钩子配置\r\n"
    "; TargetFont 换成系统里真实存在的中文字体，否则替换过去也是方块\r\n"
    "[Font]\r\n"
    "TargetFont=Microsoft YaHei\r\n"
    "TargetCharset=134\r\n"
    "ReplaceCharset=1\r\n"
    "Enabled=1\r\n"
    "LogToFile=1\r\n"
    "LogPath=galenginekit_hook.log\r\n"
    "CloneEnabled=0\r\n"
    "CloneSource=\r\n";

void Print(const std::wstring& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (h && WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr)) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        std::string utf8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &utf8[0], n, nullptr, nullptr);
        fwrite(utf8.data(), 1, utf8.size(), stdout);
    }
}

} // namespace

int Deploy::Install(const std::wstring& game_dir) {
    std::wstring dir = game_dir;
    if (dir.empty()) {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        dir = cwd;
    }

    std::wstring exe_name = PickTargetExe(dir);
    if (exe_name.empty()) {
        Print(std::wstring(L"[FAIL] 目录里没有可执行文件：") + dir + L"\r\n");
        return 2;
    }
    FilePeInfo target = ReadPeHeader(JoinPath(dir, exe_name));
    if (!target.valid) {
        Print(std::wstring(L"[FAIL] 读不了目标 exe 的 PE 头：") + target.error + L"\r\n");
        return 2;
    }
    bool want64 = target.pe64;
    Print(std::wstring(L"目标: ") + exe_name + L"（" + MachineName(target.machine) + L"）\r\n");

    std::vector<std::wstring> search = { SelfDir(), dir };
    SourcePick krkr, ver;
    std::wstring why;
    bool has_krkr = FindSourceDll(search, L"krkr2_hook", want64, krkr, why);
    bool has_ver  = FindSourceDll(search, L"version", want64, ver, why);

    if (!has_krkr && !has_ver) {
        Print(L"[FAIL] 找不到位数匹配的钩子 dll。把对应位数的 krkr2_hook / version.dll 放到启动器旁边再试\r\n");
        if (!why.empty()) Print(std::wstring(L"       试过：") + why + L"\r\n");
        return 2;
    }

    std::wstring manifest_path = JoinPath(dir, ManifestName());
    std::vector<ManifestEntry> entries = ReadManifest(manifest_path);
    bool ok = true;

    if (has_krkr) {
        if (DeployOne(dir, krkr.path, L"krkr2_hook.dll", entries)) {
            Print(std::wstring(L"[OK]   krkr2_hook.dll <- ") + krkr.path + L"\r\n");
        } else {
            Print(L"[FAIL] 复制 krkr2_hook.dll 失败\r\n");
            ok = false;
        }
    }
    if (has_ver) {
        // version.dll 是代理形态：游戏目录里原名就叫 version.dll 才能被加载
        if (DeployOne(dir, ver.path, L"version.dll", entries)) {
            Print(std::wstring(L"[OK]   version.dll <- ") + ver.path + L"\r\n");
        } else {
            Print(L"[FAIL] 复制 version.dll 失败\r\n");
            ok = false;
        }
    }
    if (FileExists(JoinPath(dir, L"krkr2_hook.dll") ) && FileExists(JoinPath(dir, L"version.dll")) ) {
        Print(L"[?]    两个形态都放了。同一款游戏只需要一个：注入用 krkr2_hook，免启动器用 version.dll\r\n");
    }

    std::wstring ini = JoinPath(dir, L"krkr2_font.ini");
    if (!FileExists(ini)) {
        HANDLE f = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(f, kDefaultHookIni, (DWORD)strlen(kDefaultHookIni), &written, nullptr);
            CloseHandle(f);
            ManifestEntry e; e.created = true; e.name = L"krkr2_font.ini";
            entries.push_back(e);
            Print(L"[OK]   生成默认 krkr2_font.ini（记得把 TargetFont 改成系统里有的字体）\r\n");
        } else {
            Print(L"[FAIL] 写 krkr2_font.ini 失败\r\n");
            ok = false;
        }
    } else {
        Print(L"[OK]   已有 krkr2_font.ini，没动\r\n");
    }

    if (!WriteAllUtf8(manifest_path, SerializeManifest(entries))) {
        Print(L"[FAIL] 写部署记录失败，部署过的文件需要手动清理\r\n");
        return 2;
    }
    Print(std::wstring(L"部署记录: ") + ManifestName() + L"（还原用 --undeploy）\r\n");
    return ok ? 0 : 2;
}

int Deploy::Remove(const std::wstring& game_dir) {
    std::wstring dir = game_dir;
    if (dir.empty()) {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        dir = cwd;
    }

    std::wstring manifest_path = JoinPath(dir, ManifestName());
    if (!FileExists(manifest_path)) {
        Print(L"[FAIL] 这个目录没有部署记录，不猜、不动任何文件\r\n");
        return 2;
    }

    std::vector<ManifestEntry> entries = ReadManifest(manifest_path);
    bool ok = true;
    // 倒序还原：先撤后放的，避免中间状态打架
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        std::wstring dest = JoinPath(dir, it->name);
        if (it->created) {
            if (FileExists(dest) && !DeleteFileW(dest.c_str())) {
                Print(std::wstring(L"[FAIL] 删不掉 ") + it->name + L"\r\n");
                ok = false;
            } else {
                Print(std::wstring(L"[OK]   移除 ") + it->name + L"\r\n");
            }
        } else {
            std::wstring backup = JoinPath(dir, it->backup);
            if (!FileExists(backup)) {
                Print(std::wstring(L"[?]    备份不存在，跳过还原 ") + it->name + L"\r\n");
                continue;
            }
            if (FileExists(dest)) DeleteFileW(dest.c_str());
            if (MoveFileW(backup.c_str(), dest.c_str())) {
                Print(std::wstring(L"[OK]   还原 ") + it->name + L"（来自 " + it->backup + L"）\r\n");
            } else {
                Print(std::wstring(L"[FAIL] 还原 ") + it->name + L" 失败，备份还在 " + it->backup + L"\r\n");
                ok = false;
            }
        }
    }

    if (ok && DeleteFileW(manifest_path.c_str())) {
        Print(L"[OK]   部署记录已删除\r\n");
    }
    return ok ? 0 : 2;
}

} // namespace ge
