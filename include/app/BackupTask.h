// BackupTask.h - 可取消的备份任务单元
// 职责：封装一次可取消的备份任务，供 GUI / 调度器 / 命令行调用。
// 对应需求文档 16 任务取消。
#pragma once

#include <atomic>

#include "core/BackupConfig.h"
#include "core/BackupResult.h"

namespace backup {

class BackupTask {
public:
    explicit BackupTask(BackupConfig config) : config_(std::move(config)) {}

    void cancel() { cancelled_.store(true); }
    bool isCancelled() const { return cancelled_.load(); }

    // 同步执行备份（可在任意线程调用）。
    BackupResult run() const;

    const BackupConfig& config() const { return config_; }
    BackupConfig& config() { return config_; }

private:
    BackupConfig config_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace backup
