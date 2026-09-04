// BackupLock.h - 备份目录单一实例锁
// 职责：防止计划任务触发的备份与手动备份同时写同一 target 目录。
//       锁文件位于 <target>/.backup.lock，内容为持有进程 PID。
//       获取用 CreateFileW(CREATE_NEW) 原子创建；崩溃残留的锁文件
//       在下次获取时自动检测（PID 已退出则清理后重试）。
#pragma once

#include <string>

#include <windows.h>

namespace backup {

// 静态锁操作
class BackupLock {
public:
    // 尝试获取 target 目录的备份锁。
    // 成功返回 true，lockFilePath 输出锁文件路径；失败返回 false，errMsg 输出原因。
    // 如果锁已存在且持有进程仍在运行，返回 false；
    // 如果锁已存在但持有进程已退出（崩溃残留），自动清理旧锁并获取新锁。
    static bool acquire(const std::wstring& targetPath,
                        std::wstring& lockFilePath,
                        std::string& errMsg);

    // 释放锁（删除锁文件）。
    static void release(const std::wstring& lockFilePath);

    // 检查指定 PID 的进程是否仍在运行。
    static bool isProcessRunning(DWORD pid);
};

// RAII 锁守卫：构造时获取锁，析构时自动释放锁。
class BackupLockGuard {
public:
    explicit BackupLockGuard(const std::wstring& targetPath);
    ~BackupLockGuard();

    bool acquired() const { return acquired_; }
    const std::string& error() const { return errMsg_; }

    // 禁止拷贝
    BackupLockGuard(const BackupLockGuard&) = delete;
    BackupLockGuard& operator=(const BackupLockGuard&) = delete;

private:
    std::wstring lockFilePath_;
    bool acquired_;
    std::string errMsg_;
};

}  // namespace backup
