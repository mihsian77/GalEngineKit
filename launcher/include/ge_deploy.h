// 独立实现：本模块为 GalEngineKit 原创实现
// 一键部署：把对应位数的钩子 dll 放到游戏目录，原文件改名备份，
// 把做了什么记进清单，--undeploy 按清单还原。

#ifndef GALENGINEKIT_DEPLOY_H
#define GALENGINEKIT_DEPLOY_H

#include <windows.h>
#include <string>

namespace ge {

class Deploy {
public:
    // 返回 0 = 成功，2 = 出错（目标 exe 找不到、dll 位数不对、复制失败）
    static int Install(const std::wstring& game_dir);

    // 按清单还原；没有清单时直接报错，不动任何文件
    static int Remove(const std::wstring& game_dir);

    static const wchar_t* ManifestName() { return L".galenginekit-manifest.txt"; }
};

} // namespace ge

#endif // GALENGINEKIT_DEPLOY_H
