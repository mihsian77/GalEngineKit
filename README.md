# GalEngineKit

> **galgame 引擎适配与中文化工具箱** — 一个 exe，零配置，自动识别游戏引擎，注入钩子，搞定字体与汉化。


---

## 核心特性

### 🎯 智能引擎识别 — 零配置

把 `GalEngineKitLauncher.exe` 丢进游戏目录，双击运行。启动器自动：

- 扫描目录特征文件、归档格式、可执行文件名
- 内置多种常见引擎识别规则（KiriKiri2 / YU-RIS / Ren'Py / Unity / Artemis / CatSystem2 / BGI / Majiro / Tyrano / RPG Maker VX Ace / RPG Maker MV）
- 自动选择目标 exe（排除设置程序、卸载程序）
- 自动匹配对应引擎的钩子 DLL（`hooks/<engine_id>_hook.dll`）
- 生成配置并直接运行，**不需要写任何 ini 文件**

有 `GalEngineKit.ini` 时自动切换到配置模式，向后兼容。

### 🪝 自研导入表注入 — 翻译环境友好

不依赖 Detours，独立实现导入表注入：

1. 以挂起方式创建目标进程
2. 解析 PE 头，定位导入表
3. 在目标进程分配新内存，构建完整导入表（新 DLL 描述符 + 原始描述符副本 + DLL 名 + INT/IAT）
4. 修改 PE 头导入表数据目录指向新内存
5. 恢复主线程，Windows 加载器自动加载钩子 DLL

**关键优势**：注入发生在进程执行前，纯内存写入，不触发 Box64/Box86 翻译缓存失效，对 Wine/Winlator 等动态翻译环境友好。

### 🔧 静态字节补丁 — 最可靠的修改方式

在进程恢复前修改内存中的指令字节，应用前比对原始字节防止版本不匹配。支持 `module:rva` 地址格式，适用于任何引擎。

### 📦 单文件 exe — 拷贝即用

- MinGW 静态链接（`-static -static-libgcc -static-libstdc++`）
- 不依赖 VC++ 运行时、不依赖任何外部 DLL
- 32 位 / 64 位双版本
- 内嵌原创图标和版本信息

### 🌍 全环境兼容

| 环境 | 状态 | 说明 |
|---|---|---|
| Windows 7 / 8 / 10 / 11 | ✅ | 仅使用 Windows 2000+ API |
| Wine (Linux/macOS) | ✅ | 自动检测 Wine 环境并输出版本 |
| **Winlator (Android)** | ✅ | 核心目标平台，导入表注入针对 Box64/Box86 优化 |
| CrossOver / Crossover | ✅ | 基于 Wine，兼容 |

启动时自动检测运行环境（原生 Windows / Wine），在日志中输出环境信息和适配提示。

---

## 快速开始

### 1. 下载

从 [Releases](https://github.com/mihsian77/GalEngineKit/releases) 下载：

- `GalEngineKitLauncher-32bit.exe` — 32 位游戏用（大多数 galgame）
- `GalEngineKitLauncher-64bit.exe` — 64 位游戏用
- `example_hook-32bit.dll` / `example_hook-64bit.dll` — 示例钩子（验证注入链路用）

### 2. 使用（零配置）

```
游戏目录/
├── 游戏.exe
├── GalEngineKitLauncher-32bit.exe   ← 放这里
└── example_hook-32bit.dll            ← 可选，验证注入用
```

双击 `GalEngineKitLauncher-32bit.exe`，启动器自动识别引擎并运行游戏。

### 3. 验证

成功标志：
- 游戏正常启动
- 目录下生成 `galenginekit_hook.log`（示例钩子 DLL 加载成功）
- `logs/` 目录下有启动器日志，包含引擎识别结果和环境信息

---

## 支持的引擎

| 引擎 | 识别 ID | 状态 | 文本渲染 | 推荐方案 |
|---|---|---|---|---|
| KiriKiri2 / KAG | `krkr2` | ✅ verified | GDI+ | 引擎钩子 |
| YU-RIS | `yuris` | ✅ verified | 位图字体图集 | 引擎钩子 |
| Artemis Engine | `artemis` | 📝 draft | GDI | 引擎钩子 |
| Ren'Py | `renpy` | 📝 draft | FreeType | 字体替换 |
| Unity | `unity` | 📝 draft | 混合 | 引擎钩子 |
| CatSystem2 | `catsystem2` | 📝 draft | GDI | 引擎钩子 |
| BGI / Ethornell | `bgi` | 📝 draft | GDI | 引擎钩子 |
| Majiro | `majiro` | 📝 draft | GDI | 引擎钩子 |
| TyranoBuilder | `tyrano` | 📝 draft | Web 渲染 | 字体替换 |
| RPG Maker VX Ace | `rpgmaker_vxa` | 📝 draft | GDI | 字体替换 |
| RPG Maker MV/MZ | `rpgmaker_mv` | 📝 draft | Web 渲染 | 字体替换 |

引擎档案位于 `profiles/` 目录，JSON 格式，包含识别特征、文本渲染路径、字体方案、钩子目标等信息。新增引擎只需添加一个 JSON 档案。

---

## 高级用法

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

### 静态补丁格式

补丁文件（`.bp`）每行一条规则：

```
; 注释
<地址> : <原始字节> -> <补丁字节>
```

- 地址: `0x12345`（RVA）或 `module.exe:0x12345`
- 字节: 空格分隔的十六进制

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
    L"0.1.0",               // 版本
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

### 添加引擎档案

在 `profiles/` 目录添加 `<engine_id>.json`，参考 `schemas/engine-profile.schema.json`。用 `tools/validate_profile.py` 校验：

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
│   └── CMakeLists.txt
├── profiles/                    # 引擎档案库（JSON）
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
- [ ] Phase 3: 真实引擎钩子 DLL（KiriKiri2 字体替换 → YU-RIS → 更多）
- [ ] Phase 4: GUI 版本 + 容器配置助手 + 一键部署

---

## 许可

MIT License. Copyright (c) 2026 GalEngineKit contributors.
