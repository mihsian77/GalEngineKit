// 独立实现：本模块为 GalEngineKit 原创实现
// 游戏目录辅助：定位目标 exe、拼路径。doctor 和 deploy 共用同一套挑选规则，
// 避免两处规则不一致导致"体检说 A，部署却认 B"。

#ifndef GALENGINEKIT_GAME_DIR_H
#define GALENGINEKIT_GAME_DIR_H

#include <windows.h>
#include <string>
#include <vector>

namespace ge {

// 目录下所有 exe（排除启动器自身）
std::vector<std::wstring> ListGameExes(const std::wstring& dir);

// 按规则挑一个：跳过 config/setup/uninstall/tool 类程序，剩下的取第一个
// 找不到返回空串
std::wstring PickTargetExe(const std::wstring& dir, bool* out_ambiguous = nullptr);

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);

bool FileExists(const std::wstring& path);

} // namespace ge

#endif // GALENGINEKIT_GAME_DIR_H
