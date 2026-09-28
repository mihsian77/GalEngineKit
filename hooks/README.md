# GalEngineKit Hooks

钩子 DLL 开发框架。所有钩子 DLL 遵循统一接口，由启动器注入到目标进程。

## 统一接口

每个钩子 DLL 必须导出以下 3 个函数（定义在 `include/ge_hook_api.h`）：

| 函数 | 说明 |
|---|---|
| `GEK_HookGetInfo()` | 返回钩子信息（名称、版本、目标引擎、描述） |
| `GEK_HookInitialize(params)` | 初始化钩子，安装钩子逻辑 |
| `GEK_HookShutdown()` | 清理钩子资源 |

### 钩子信息结构

```c
typedef struct _GEK_HookInfo {
    uint32_t api_version;       // 接口版本（必须 = GEK_HOOK_API_VERSION）
    const wchar_t* name;        // 钩子名称，如 "krkr2_font"
    const wchar_t* version;     // 钩子版本，如 "0.1.0"
    const wchar_t* target_engine; // 目标引擎，如 "KiriKiri2"，"*" 表示通用
    const wchar_t* description; // 简短描述
    uint32_t flags;             // 保留
} GEK_HookInfo;
```

## 开发新钩子

1. 复制 `src/example_hook.cpp` 为 `src/your_hook.cpp`
2. 修改 `g_hookInfo` 中的名称、版本、目标引擎
3. 在 `GEK_HookInitialize` 中实现钩子逻辑
4. 在 `GEK_HookShutdown` 中清理资源
5. 在 `CMakeLists.txt` 中添加新的 `add_library` 目标

### 钩子实现方式

- **IAT 钩子**：修改目标进程的导入地址表，替换 API 函数地址
- **内联钩子**：修改函数开头字节，跳转到自定义函数（需注意 Box64/Box86 翻译环境）
- **静态补丁**：通过启动器的 `[Patch]` 配置应用字节补丁（不需要钩子 DLL）

### 翻译环境注意事项

- **只钩游戏自身代码**，不钩 Wine 内置 DLL（ARM64 原生代码上打 x86 钩子会崩溃）
- 内联钩子在 Box64 下可能触发翻译缓存失效，优先使用 IAT 钩子或静态补丁
- 钩子 DLL 必须与目标进程位数一致（32 位游戏用 32 位 DLL）

## 编译

```bash
cd hooks
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

输出：`build/bin/Release/example_hook.dll`

## 使用

将编译好的 DLL 放到游戏目录的 `hooks/` 子目录，在启动器配置中指定：

```ini
[Inject]
DLLCount = 1
DLL_0 = hooks/your_hook.dll
```

## 示例钩子

`example_hook.dll` 是一个最小示例，仅用于验证启动器注入链路：
- 加载时输出 `DLL_PROCESS_ATTACH` 日志
- 初始化时输出配置信息
- 不做任何实际钩子

用它验证启动器 → 注入 → DLL 加载 → 初始化的完整链路是否通畅。
