// TaskScheduler.h - 简单任务调度器
// 职责：管理备份任务，支持立即执行与"每天指定时间执行"。
// 不实现复杂线程池、分布式调度或任务依赖系统（需求文档 3.9）。
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/BackupConfig.h"
#include "core/BackupResult.h"

namespace backup {

struct ScheduledTask {
    std::wstring name;         // 任务名
    BackupConfig config;
    std::string scheduleTime;  // "HH:MM"，空表示不自动定时
    bool enabled = true;
};

class TaskScheduler {
public:
    // 任务执行完成后的回调（供 GUI / 日志使用）。
    using TaskCallback = std::function<void(const std::wstring& name, const BackupResult& result)>;

    void setCallback(TaskCallback cb) { callback_ = std::move(cb); }

    void addTask(ScheduledTask task);
    bool removeTask(const std::wstring& name);

    // 启动后台调度线程：每 30 秒检查一次，命中 HH:MM 且当天未执行则触发。
    void start();
    void stop();

    // 立即执行指定任务（手动触发）。
    void runNow(const std::wstring& name);

    bool isRunning() const { return running_.load(); }

private:
    void loop();
    void execute(const ScheduledTask& task);

    std::vector<ScheduledTask> tasks_;
    std::map<std::wstring, std::string> lastRunDate_;  // name -> 最后执行日期
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelFlag_{false};  // stop() 时置位，取消正在执行的备份
    std::thread thread_;
    TaskCallback callback_;
};

}  // namespace backup
