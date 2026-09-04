// TaskScheduler.cpp - 简单调度器实现
#include "app/TaskScheduler.h"

#include <algorithm>
#include <chrono>

#include "business/BackupManager.h"
#include "core/Logger.h"
#include "core/TimeUtil.h"
#include "core/Utf.h"

namespace backup {

void TaskScheduler::addTask(ScheduledTask task) {
    std::lock_guard<std::mutex> lk(mu_);
    tasks_.push_back(std::move(task));
}

bool TaskScheduler::removeTask(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = std::find_if(tasks_.begin(), tasks_.end(),
                                 [&name](const ScheduledTask& t) { return t.name == name; });
    if (it == tasks_.end()) return false;
    tasks_.erase(it);
    return true;
}

void TaskScheduler::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this]() { loop(); });
}

void TaskScheduler::stop() {
    running_.store(false);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void TaskScheduler::runNow(const std::wstring& name) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = std::find_if(tasks_.begin(), tasks_.end(),
                                 [&name](const ScheduledTask& t) { return t.name == name; });
    if (it != tasks_.end()) execute(*it);
}

void TaskScheduler::execute(const ScheduledTask& task) {
    const BackupResult res = BackupManager::run(task.config);
    if (callback_) callback_(task.name, res);
}

void TaskScheduler::loop() {
    std::unique_lock<std::mutex> lk(mu_);
    while (running_.load()) {
        // 取快照
        std::vector<ScheduledTask> snapshot;
        {
            // tasks_ 已被 lk 保护（进入循环即持有）
            snapshot = tasks_;
        }
        lk.unlock();

        const std::string nowHM = currentHHMM();
        const std::string today = dateToday();
        for (auto& t : snapshot) {
            if (!t.enabled || t.scheduleTime.empty()) continue;
            if (t.scheduleTime != nowHM) continue;
            bool alreadyRun = false;
            {
                std::lock_guard<std::mutex> lk2(mu_);
                const auto it = lastRunDate_.find(t.name);
                if (it != lastRunDate_.end() && it->second == today) alreadyRun = true;
            }
            if (alreadyRun) continue;
            {
                std::lock_guard<std::mutex> lk2(mu_);
                lastRunDate_[t.name] = today;
            }
            Logger::instance().info(L"TaskScheduler",
                                    L"定时触发任务: " + t.name + L" @ " + utf8ToWide(nowHM));
            execute(t);
        }

        lk.lock();
        cv_.wait_for(lk, std::chrono::seconds(30));
    }
}

}  // namespace backup
