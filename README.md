# GalEngineKit

> galgame 引擎适配工具。一个 exe 放游戏目录，自动认引擎、注入钩子、处理字体；
> 出问题时用体检和日志回读告诉你卡在哪一步。


---

## 核心特性

### 引擎识别与自动配置

把 `GalEngineKitLauncher.exe` 丢进游戏目录，双击运行。启动器自动：

- 扫描目录特征文件、归档格式、可执行文件名
- 内置多种常见引擎识别规则（KiriKiri2 / YU-RIS / Ren'Py / Unity / Artemis / CatSystem2 / BGI / Majiro / Tyrano / RPG Maker VX Ace / RPG Maker MV）
- 自动选择目标 exe（排除设置程序、卸载程序）
- 自动匹配对应引擎的钩子 DLL（`hooks/<engine_id>_hook.dll`）；找不到就退回 `example_hook.dll`（只写一行日志验证注入，不做处理），目前只有 krkr2 有钩子
- 生成配置并直接运行，**不需要写任何 ini 文件**

有 `GalEngineKit.ini` 时自动切换到配置模式，向后兼容。

### 导入表注入

不依赖 Detours，独立实现导入表注入：

1. 以挂起方式创建目标进程
2. 解析 PE 头，定位导入表
3. 在目标进程分配新内存，构建完整导入表（新 DLL 描述符 + 原始描述符副本 + DLL 名 + INT/IAT）
4. 修改 PE 头导入表数据目录指向新内存
5. 恢复主线程，Windows 加载器自动加载钩子 DLL

注入发生在进程执行前，纯内存写入，不触发 Box64/Box86 翻译缓存失效，对 Wine/Winlator 等动态翻译环境友好。

### 静态字节补丁

在进程恢复前修改内存中的指令字节，应用前比对原始字节防止版本不匹配。支持 `module:rva` 地址格式，适用于任何引擎。

### 单文件 exe

- MinGW 静态链接（`-static -static-libgcc -static-libstdc++`）
- 不依赖 VC++ 运行时、不依赖任何外部 DLL
- 32 位 / 64 位双版本
- 内嵌原创图标和版本信息

### 环境兼容

| 环境 | 状态 | 说明 |
|---|---|---|
| Wine 6.x / 10.x (Linux) | ✅ 实测 | 注入、代理、字体克隆、补丁链路都在这上面跑过 |
| Windows 7 / 8 / 10 / 11 | ⚠️ 未实测 | 代码只用 Windows 2000+ API，但没在真机跑过 |
| **Winlator (Android)** | ⚠️ 未实测 | 设计目标就是它，导入表注入在进程执行前写内存，不碰已翻译代码；但没在真机容器里验证过 |
| CrossOver | ⚠️ 未实测 | 基于 Wine，理论上同 Wine 分支 |

启动时自动检测运行环境（原生 Windows / Wine），在日志里输出环境信息和提示。

上表的"未实测"不是客套：真实 Windows 加载器行为、Winlator 容器里的字体渲染都还没验过，别当成已支持。

---

## 快速开始

### 1. 下载

从 [Releases](https://github.com/mihsian77/GalEngineKit/releases) 下载：

- `GalEngineKit-v0.2.0-32bit.zip` — 32 位游戏用，老 galgame 大多是这种
- `GalEngineKit-v0.2.0-64bit.zip` — 64 位游戏用

每个包里是同一套、位数不同：启动器、字体钩子（注入形态）、字体钩子的代理形态
（`version-*.dll`，改名 `version.dll` 用）、示例钩子、以及一份 `使用说明.txt`。
不确定游戏是几位就随便下一个，跑 `--doctor` 会告诉你。

### 2. 解压到游戏目录

整个文件夹解压到游戏目录，和游戏 exe 放在一起。下面的命令都在游戏目录里执行。

### 3. 先体检

```
GalEngineKitLauncher-32bit.exe --doctor
```

不启动游戏，只查一遍：程序位数和启动器是否对得上、认出来是什么引擎（附依据）、
系统里有没有替换用的中文字体、补丁会不会命中、跑在 Wine 还是原生 Windows。
结果打到屏幕并写进 `doctor.txt`。有问题先按 `[FAIL]` 项处理，别急着启动。

### 4. 部署钩子（可选）

```
GalEngineKitLauncher-32bit.exe --deploy
```

把位数匹配的钩子 dll 复制到游戏目录，原有的同名文件先改名 `.gek-bak`，
并把动作记进 `.galenginekit-manifest.txt`。想撤掉就 `--undeploy`，按记录还原。

不想用启动器的话，直接把 `version-32bit.dll` 改名成 `version.dll` 放进游戏目录
（代理形态，见下文）。

### 5. 启动

```
游戏目录/
├── 游戏.exe
├── GalEngineKitLauncher-32bit.exe   ← 放这里
└── krkr2_hook.dll                    ← --deploy 放进去的，或者手动放
```

双击 `GalEngineKitLauncher-32bit.exe`，启动器自动识别引擎并运行游戏。

### 6. 看结果

启动器在游戏退出后会自动读一遍钩子日志，直接告诉你结论：

```
钩子结果: 钩子装上了，字体替换命中 12 次
```

也可以单独查：`GalEngineKitLauncher-32bit.exe --report`。没有日志时会明确说
"钩子没加载过"或"LogToFile 关掉了"，不会含糊过去。

- `galenginekit_hook.log` — 钩子的加载和替换记录
- `logs/` — 启动器日志，含引擎识别结果和环境信息
- `doctor.txt` — 体检报告

---

## 支持的引擎

识别和实现是两件事，分开列：

| 引擎 | 识别 ID | 现成钩子 | 说明 |
|---|---|---|---|
| KiriKiri2 / KAG | `krkr2` | 注入 `krkr2_hook.dll` / 代理 `version.dll` | 字体替换、反向伪装、字体克隆都做了；Wine 上用样本 exe 验过钩子行为，真实游戏渲染没验 |
| YU-RIS | `yuris` | 无 | 只有识别，钩子没做 |
| Artemis | `artemis` | 无 | 只有识别 |
| Ren'Py | `renpy` | 无 | 只有识别；这类通常是字体文件替换，不需要钩子 |
| Unity | `unity` | 无 | 只有识别 |
| CatSystem2 | `catsystem2` | 无 | 只有识别 |
| BGI / Ethornell | `bgi` | 无 | 只有识别 |
| Majiro | `majiro` | 无 | 只有识别 |
| TyranoBuilder | `tyrano` | 无 | 只有识别 |
| RPG Maker VX Ace | `rpgmaker_vxa` | 无 | 只有识别 |
| RPG Maker MV/MZ | `rpgmaker_mv` | 无 | 只有识别 |

识别规则是打分制，写在 `launcher/src/engine_detector.cpp`，看目录特征文件、归档格式和 exe 文件名。

### 当前能力边界

写清楚免得误判：

- 字体钩子只有 KiriKiri2，走的是 GDI 字体创建路径。KiriKiri 的预渲染字体缓存（TFT / `mapPrerenderedFont`）没处理，开了缓存的游戏文字不一定跟着变
- 注入形态和代理形态共用同一套钩子核心，能力上限一样
- 静态补丁是通用能力，但仓库里没有真实游戏补丁。没有游戏本体造不出可信补丁，`patches/` 目前只有格式文档和模板，实际用要靠玩家/贡献者按自己手上的 exe 生成
- Winlator 真机上的字体渲染没验证过，只在 Wine 上验过钩子行为和字形索引

## 高级用法

### 代理形态（version.dll，免注入）

除了启动器注入，还提供 `version.dll` 代理形态：把代理 `version.dll` 放进游戏目录即可，不需要启动器、不需要注入。很多 galgame（如 KiriKiri2）本就导入 `version.dll`，游戏启动时自然加载代理。代理把 version 导出转发回系统真实 `version.dll`（版本查询照常），同时装上与注入形态同一套字体钩子核心。

```
游戏目录/
├── 游戏.exe
└── version.dll   ← 代理，放这里
```

**原生 Windows**：放入游戏目录直接运行游戏即可。

**Wine / Winlator（必须设 DLL 覆盖）**：Wine 对 `version.dll` 有硬编码的 builtin 加载顺序，不设 native 覆盖时内置 `version.dll` 会遮蔽代理导出，钩子不生效。两种设法：

- 命令行：`WINEDLLOVERRIDES="version=n,b" wine 游戏.exe`
- Winlator：容器设置 → DLL 覆盖 → 把 `version` 设为 `native`（或 `native,builtin`）

代理在 DllMain 里装好钩子，游戏随后经 `GetProcAddress` 取字体 API 时被拦截。已在 Wine 下验证转发、日文字体替换与反向伪装。

已知限制：未导出 undocumented 的 `GetFileVersionInfoByHandle`（按序号调用、游戏几乎不用）；个别游戏若确需再补。

### 配置文件

当需要精细控制时，创建 `GalEngineKit.ini`：

```ini
[Launcher]
TargetEXE = game.exe
WorkingDir = .
StaySuspended = false
LogLevel = info

[Inject]
DLLCount = 1
DLL_0 = hooks/krkr2_hook.dll
InjectMethod = auto          ; import_table | helper | auto

[Patch]
PatchCount = 0
; Patch_0_File = patches/fix.bp

[Environment]
; WINEDLLOVERRIDES = winmm=n,b
```

完整配置说明见 `launcher/examples/GalEngineKit.ini.example`。

### 字体克隆（容器缺日文字体时）

Winlator 容器常没有日文字体，替换成中文字体后 GDI 会 fallback 到无 CJK 的替身，中文照样出不来。开克隆可以让钩子自己顶上：

```ini
[Font]
CloneEnabled = 1
CloneSource = fonts\SourceCJK.ttf   ; 单体 TTF/OTF，TTC 需先抽单 face
```

处理顺序：先探系统是否真有这个日文字体（按名建一次再反查实际字体名）——有就按原逻辑替换；缺就克隆，把源字体在内存里改成游戏请求的字体名（改 name 表 + 补 OS/2 CJK 码页位），`AddFontMemResourceEx` 注册进进程，不落盘、不改系统；克隆失败退回替换。

实测（Wine）：源字体改名成 `MS Gothic` 后，游戏按原名取到字体且中日字形齐全。

### 静态补丁格式

补丁文件（`.bp`）每行一条规则，文件头可以带匹配条件：

```bp
; @patch name=krkr2-xxx desc=一句话说明
; @match exe=game.exe size=123456 crc32=89ABCDEF engine=krkr2
0x12345 : 8B 45 FC -> 33 C0 90
```

- 地址: `0x12345`（RVA）或 `module.exe:0x12345`
- 字节: 空格分隔的十六进制
- `@match` 条件：`exe` / `size` / `crc32` / `engine`，可只写其中几项。**带条件的补丁放进 `patches/` 会被自动选中**（`[Patch] AutoScan = 1`，默认开）；没有条件的补丁只在 ini 里手动列时生效
- 原始字节对不上时该条跳过，不会硬写

```ini
[Patch]
AutoScan = 1
PatchDir = patches
```

示例：
```bp
; 将 JE 改为 JMP
game.exe:0x00012345: 74 03 -> EB 03
```

### 命令行参数

```
GalEngineKitLauncher.exe [选项] [配置文件]

  -h, --help              显示帮助
  -v, --version           显示版本
  -c, --config <路径>     指定配置文件
  -l, --log-level <级别>  日志级别: error, warn, info, debug
  --no-wait               不等待目标进程退出
  --dry-run               仅校验配置，不启动进程
  --doctor [目录]         体检：程序位数、引擎、字体、补丁、运行环境；写 doctor.txt
  --deploy [目录]         部署钩子 dll，备份同名文件，记录到清单
  --undeploy [目录]       按清单还原并删除清单
  --report [目录]         读钩子日志给出结论
```

---

## 开发指南

### 编写钩子 DLL

所有钩子 DLL 遵循统一接口（`hooks/include/ge_hook_api.h`）：

```c
#include "ge_hook_api.h"

static const GEK_HookInfo g_info = {
    GEK_HOOK_API_VERSION,
    L"krkr2_font",          // 钩子名称
    L"0.2.0",               // 版本
    L"KiriKiri2",           // 目标引擎
    L"KiriKiri2 字体替换钩子",
    0
};

extern "C" __declspec(dllexport)
const GEK_HookInfo* GEK_HookGetInfo(void) { return &g_info; }

extern "C" __declspec(dllexport)
int GEK_HookInitialize(const GEK_HookInitParams* params) {
    // 安装钩子逻辑
    return 0;
}

extern "C" __declspec(dllexport)
void GEK_HookShutdown(void) {
    // 清理
}
```

钩子 DLL 命名为 `<engine_id>_hook.dll`（如 `krkr2_hook.dll`），放到游戏目录的 `hooks/` 子目录下，智能模式会自动匹配。

### 构建

```bash
# 启动器
cd launcher
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .

# 钩子 DLL
cd ../../hooks
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

CI 自动构建 32 位和 64 位版本，见 `.github/workflows/ci.yml`。

### 测试

测试默认不构建，加 `-DGEK_BUILD_TESTS=ON` 才会生成测试程序，产物和 DLL 一起落在 `build/bin`。

```bash
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGEK_BUILD_TESTS=ON
cmake --build .
```

CI 里跑两项：补丁库单测（指纹、匹配条件、目录筛选）、字体克隆单测（用系统里的 CJK 字体当源）。

字体钩子和 `version.dll` 代理这两个测试没进 CI：它们断言要求前缀里真装着一个中文字体，在 CI 里靠注册表造一个假名字骗过断言没意义，只在本地 Windows 环境跑。代理那个还要 `WINEDLLOVERRIDES="version=n,b"`，否则 Wine 加载内置 version.dll，钩子根本不进来。

### 引擎档案（profiles/）

`profiles/` 是资料库，记识别特征、渲染路径和容器建议，供人查，也给以后把识别规则数据化留个底。**运行时识别不读这里**——改 JSON 不会让启动器多认出一个引擎，要加引擎得动 `launcher/src/engine_detector.cpp`。

档案格式用 `schemas/engine-profile.schema.json` 校验：

```bash
python3 tools/validate_profile.py profiles/new_engine.json
```

---

## 项目结构

```
GalEngineKit/
├── launcher/                    # 通用钩子启动器
│   ├── include/                 # 头文件
│   │   ├── ge_types.h           # 公共类型
│   │   ├── ge_engine_detector.h # 引擎识别
│   │   ├── ge_injector.h        # DLL 注入（导入表+辅助进程）
│   │   ├── ge_pe.h              # PE 格式解析
│   │   ├── ge_patcher.h         # 静态字节补丁
│   │   ├── ge_process.h         # 进程管理
│   │   └── ...
│   ├── src/                     # 实现
│   ├── resources/               # 图标和资源
│   ├── examples/                # 配置示例
│   └── CMakeLists.txt
├── hooks/                       # 钩子 DLL 开发框架
│   ├── include/ge_hook_api.h    # 统一钩子接口
│   ├── src/example_hook.cpp     # 示例钩子
│   ├── src/krkr2_hook.cpp       # 拦截函数 + 钩子安装 + 导出
│   ├── src/font_clone.cpp       # 内存字体克隆
│   ├── src/font_policy.cpp      # 反向伪装注册表、字体探测
│   ├── src/hook_config.cpp      # krkr2_font.ini 与日文字体名单
│   ├── src/hook_log.cpp         # 日志
│   ├── src/iat_hook.cpp         # PE 导入表替换
│   ├── src/version_proxy.cpp    # version.dll 代理
│   ├── tests/                   # 钩子回归测试
│   └── CMakeLists.txt
├── patches/                     # 静态补丁目录（格式说明 + 模板）
├── profiles/                    # 引擎档案（资料，不参与运行时识别）
│   ├── krkr2.json
│   ├── yuris.json
│   └── artemis.json
├── schemas/                     # JSON Schema
├── tools/                       # 工具脚本
├── docs/                        # 文档
│   └── specs/launcher.md        # 启动器规格文档
├── .github/workflows/ci.yml     # CI 构建
├── THIRD_PARTY_NOTICES.md       # 第三方声明
├── CONTRIBUTING.md              # 贡献指南
├── LICENSE                      # MIT
└── README.md                    # 本文件
```

---

## 技术原理

### 为什么用导入表注入而不是内联钩子？

在 Winlator 等动态二进制翻译环境下：

- **内联钩子**（修改函数开头字节跳转到自定义函数）：修改的是已翻译的代码，可能触发 Box64 翻译缓存失效，且钩在 Wine 内置 DLL（ARM64 原生代码）上会崩溃
- **导入表注入**：在进程执行前修改 PE 导入表，纯内存写入，不触发翻译缓存失效；钩子 DLL 只钩游戏自身代码，不碰 Wine 内置 DLL
- **静态字节补丁**：最可靠的修改方式，不依赖任何 API 钩子

### 翻译环境钩子策略

1. **只钩游戏自身代码**，不钩 Wine 内置 DLL
2. 优先使用 IAT 钩子（修改导入地址表）和静态补丁
3. 内联钩子仅用于游戏自身函数，且需验证翻译缓存行为
4. 启动器自动检测 Wine 环境，在日志中输出适配提示

---

## 合规声明

- **独立实现**：所有代码为原创实现，未复制任何第三方项目的代码或文档
- **参考项目**（仅研究功能规格，未使用代码）：SimpleFontHook、VN_Localization_Tutorials（RiaLoader）
- **第三方依赖**：无运行时依赖；构建使用 MinGW-w64（开源工具链）
- **许可**：MIT License

详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 [docs/clean-room-policy.md](docs/clean-room-policy.md)。

---

## 路线图

- [x] Phase 0: 项目骨架 + 引擎档案 schema
- [x] Phase 1: 引擎档案库（KiriKiri2 / YU-RIS / Artemis）
- [x] Phase 2: 通用钩子启动器（导入表注入 + 静态补丁 + 智能识别）
- [x] Phase 3: KiriKiri2 字体钩子（注入 + `version.dll` 代理 + 字体克隆）
- [ ] Phase 3.5: 其它引擎钩子（YU-RIS 起步）
- [ ] Phase 4: GUI 版本 + 容器配置助手 + 一键部署

---

## 许可

MIT License. Copyright (c) 2026 GalEngineKit contributors.
