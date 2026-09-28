# GalEngineKit Launcher

galgame 引擎适配与中文化工具箱的**通用钩子启动器**（命令行版本）。

> 独立实现，MIT 许可。参考 Detours（MIT）、SimpleFontHook 和 VN_Localization_Tutorials 的功能规格，未复制任何第三方代码或文档。

## 功能

- **配置驱动**：通过 INI 文件描述游戏路径、注入 DLL、静态补丁、环境变量
- **导入表注入**（推荐）：在进程挂起时修改 PE 导入表，执行前加载钩子 DLL，对动态二进制翻译环境（Box64/Box86）友好
- **辅助进程注入**（兜底）：CreateRemoteThread + LoadLibraryW，导入表注入失败时自动回退
- **静态字节补丁**：在进程恢复前修改内存中的指令字节，应用前比对原始字节防止版本不匹配
- **环境变量管理**：自定义变量覆盖当前环境，支持 Wine DLL override 等
- **多 DLL 注入**：按配置顺序注入多个钩子 DLL
- **线程安全日志**：四级日志（error/warn/info/debug），同时输出到控制台和文件

## 编译

### 前置要求

- CMake 3.15+
- C++17 编译器
  - Windows: MSVC（Visual Studio 2019+）或 MinGW-w64
  - Linux 交叉编译: MinGW-w64（`gcc-mingw-w64`）

### Windows (MSVC)

```cmd
cd launcher
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

输出: `build/bin/Release/GalEngineKitLauncher.exe`

### Windows (MinGW)

```bash
cd launcher
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Linux 交叉编译 (MinGW-w64)

32 位:
```bash
cd launcher
mkdir build32 && cd build32
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw32.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

64 位:
```bash
cd launcher
mkdir build64 && cd build64
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## 使用

### 基本用法

```cmd
GalEngineKitLauncher.exe -c 游戏目录\GalEngineKit.ini
```

### 命令行选项

| 选项 | 说明 |
|---|---|
| `-h, --help` | 显示帮助 |
| `-v, --version` | 显示版本 |
| `-c, --config <路径>` | 指定配置文件（默认: GalEngineKit.ini） |
| `-l, --log-level <级别>` | 日志级别: error, warn, info, debug |
| `--no-wait` | 不等待目标进程退出 |
| `--dry-run` | 仅校验配置，不启动进程 |

### 配置文件

复制 `examples/GalEngineKit.ini.example` 为 `GalEngineKit.ini`，修改为你的游戏配置。

关键配置项：

```ini
[Launcher]
TargetEXE = game.exe          ; 游戏主程序
WorkingDir = .                 ; 工作目录

[Inject]
DLLCount = 1
DLL_0 = hooks/krkr2_font.dll  ; 要注入的钩子 DLL
InjectMethod = auto            ; import_table / helper / auto

[Patch]
PatchCount = 0                 ; 静态补丁数量

[Environment]
; WINEDLLOVERRIDES = winmm=n,b ; Wine 环境变量
```

### 静态补丁格式

补丁文件（`.bp`）每行一条规则：

```
; 注释
<地址> : <原始字节> -> <补丁字节>
```

- 地址: `0x12345`（RVA）或 `module.exe:0x12345`
- 字节: 空格分隔的十六进制，如 `90 90`
- 原始字节和补丁字节长度必须相同

示例:
```bp
; 将 JE 改为 JMP（跳过版本检查）
game.exe:0x00012345: 74 03 -> EB 03

; NOP 掉函数调用
0x00005678: E8 12 34 56 78 -> 90 90 90 90 90
```

## 项目结构

```
launcher/
├── CMakeLists.txt          # CMake 构建脚本
├── README.md               # 本文件
├── include/                # 头文件
│   ├── ge_types.h          # 公共类型定义
│   ├── ge_logger.h         # 日志系统
│   ├── ge_config.h         # 配置解析
│   ├── ge_environment.h    # 环境变量管理
│   ├── ge_process.h        # 进程管理
│   ├── ge_pe.h             # PE 格式解析
│   ├── ge_injector.h       # DLL 注入器
│   ├── ge_patcher.h        # 静态字节补丁
│   └── ge_launcher.h       # 启动流程编排
├── src/                    # 实现
│   ├── main.cpp            # 命令行入口
│   ├── logger.cpp
│   ├── config.cpp
│   ├── environment.cpp
│   ├── process.cpp
│   ├── pe.cpp
│   ├── injector.cpp
│   ├── patcher.cpp
│   └── launcher.cpp
└── examples/
    └── GalEngineKit.ini.example  # 配置示例
```

## 技术说明

### 导入表注入原理

1. 以 `CREATE_SUSPENDED` 创建目标进程
2. 读取目标进程 PE 头，定位导入表数据目录
3. 读取原始导入描述符数组
4. 在目标进程中分配新内存，构建新导入表：
   - 新 DLL 的导入描述符（Name/OriginalFirstThunk/FirstThunk）
   - 原始导入描述符副本
   - 全零结束符
   - DLL 路径字符串
   - INT/IAT（全零 thunk，表示只加载 DLL 不导入函数）
5. 修改 PE 头导入表数据目录指向新内存
6. 恢复主线程，Windows 加载器自动加载钩子 DLL

### 翻译环境适配

- 导入表注入发生在进程执行前，纯内存写入，不触发 Box64/Box86 翻译缓存失效
- 钩子 DLL 必须只钩游戏自身代码，不钩 Wine 内置 DLL（ARM64 原生代码上打 x86 钩子会崩溃）
- 静态字节补丁作为最可靠的修改方式，不依赖任何 API 钩子

## 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 配置加载失败 |
| 2 | 配置校验失败 |
| 3 | 进程创建失败 |
| 4 | DLL 注入失败 |
| 5 | 线程恢复失败 |
| 6 | 补丁应用失败 |
| 10 | 用户取消 |

## 许可

MIT License。详见项目根目录 [LICENSE](../LICENSE)。

第三方依赖：无（仅使用 Windows 系统 API）。Detours 为可选替代方案，本项目未使用其代码。
