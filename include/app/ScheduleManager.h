// ScheduleManager.h - Windows 任务计划程序注册/卸载
// 职责：将备份任务注册为 Windows 计划任务，到点自动启动 backupapp 执行一次备份后退出，
//       无需程序常驻。替代 --schedule 常驻模式，适合长期自用。
#pragma once

#include <string>

namespace backup {

class ScheduleManager {
public:
    // 注册每天定时任务。
    // taskName:  任务显示名（如 "BackupSystem_Daily"）
    // executable: backupapp.exe 完整路径
    // arguments:  备份命令行参数（如 'backup --config "C:\path\config.conf"'）
    // time:       每天触发时间 HH:MM（24小时制）
    // errMsg:     错误信息输出
    // 返回 true 表示注册成功。
    static bool registerDaily(const std::wstring& taskName,
                              const std::wstring& executable,
                              const std::wstring& arguments,
                              const std::wstring& time,
                              std::string* errMsg);

    // 卸载已注册的任务。任务不存在时返回 true（幂等）。
    static bool unregister(const std::wstring& taskName, std::string* errMsg);

    // 查询任务是否已注册。
    static bool isRegistered(const std::wstring& taskName);

    // 获取任务下次运行时间（未注册返回空）。
    static std::wstring getNextRunTime(const std::wstring& taskName);

    // 默认任务名。
    static std::wstring defaultTaskName() { return L"BackupSystem_Daily"; }
};

}  // namespace backup
