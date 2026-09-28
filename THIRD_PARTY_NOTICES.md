# 第三方依赖声明（Third Party Notices）

本文件列出 GalEngineKit 项目中使用的第三方软件组件、其来源及许可条款。所有第三方依赖的许可均与本项目的 MIT 许可兼容。

## 运行时依赖

### Detours

- **名称**：Microsoft Detours
- **作者**：Microsoft Corporation
- **来源**：https://github.com/microsoft/Detours
- **许可**：MIT License
- **用途**：通用钩子启动器的进程注入功能（PE 导入表修改、挂起进程 DLL 注入）
- **许可原文**：

```
Copyright (c) Microsoft Corporation.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 开发与工具依赖

### jsonschema (Python)

- **名称**：jsonschema
- **作者**：Julian Berman
- **来源**：https://github.com/python-jsonschema/jsonschema
- **许可**：MIT License
- **用途**：`tools/validate_profile.py` 档案校验脚本，用于校验引擎档案 JSON 是否符合 schema
- **安装方式**：`pip install jsonschema`（仅开发/校验时需要，不随项目二进制分发）

## 参考项目（非依赖）

以下项目在 GalEngineKit 的设计过程中被参考，但其**源代码、文档文本和二进制产物均未被复制或包含在本项目中**。参考仅限于功能规格、接口设计和引擎格式等客观事实。

| 项目 | 作者 | 许可状态 | 参考内容 |
|---|---|---|---|
| [SimpleFontHook](https://github.com/SuQiandYing/SimpleFontHook) | SuQiandYing | 无 LICENSE（默认保留版权） | 字体替换分层设计、引擎适配思路、YU-RIS 字体图集机制 |
| [VN_Localization_Tutorials](https://github.com/ZQF-ReVN/VN_Localization_Tutorials) | ZQF-ReVN / Dir-A | 无 LICENSE（默认保留版权） | 引擎逆向方法论、钩子启动器模式、YU-RIS 数据工具链 |
| [YurisTools](https://github.com/Dir-A/YurisTools) | Dir-A | 无 LICENSE（默认保留版权） | YU-RIS 数据格式（功能规格参考） |
| [GARbro](https://github.com/morkt/GARbro) | morkt | 无 LICENSE（默认保留版权） | YPF 归档格式（ArcYPF.cs，客观格式参考） |

以上项目均未在本项目的二进制分发中包含任何代码或资源。所有引擎适配逻辑均为 GalEngineKit 独立实现。

## 声明

- 本项目不声称与上述任何第三方项目存在关联、背书或赞助关系；
- 所有第三方商标和产品名称均为其各自所有者的财产；
- 如发现本文件遗漏了任何第三方依赖，请通过 GitHub Issue 告知。
