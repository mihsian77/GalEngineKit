// 独立实现：本模块为 GalEngineKit 原创实现
// 体检：不启动游戏，把"为什么没生效"能查的都查一遍，输出可贴给人的报告

#ifndef GALENGINEKIT_DOCTOR_H
#define GALENGINEKIT_DOCTOR_H

#include <windows.h>
#include <string>

namespace ge {

class Doctor {
public:
    // game_dir 为空时用当前工作目录
    // report_path 为空时不写文件；默认由调用方传 "doctor.txt"
    // 返回退出码：0 = 没发现致命问题，2 = 有致命问题（找不到 exe / 不是有效 PE / 位数不匹配）
    static int Run(const std::wstring& game_dir, const std::wstring& report_path);
};

} // namespace ge

#endif // GALENGINEKIT_DOCTOR_H
