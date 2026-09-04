// TaskScheduler.cpp - 简单调度器实现
#include "app/TaskScheduler.h"

#include <algorithm>
#include <chrono>
#include <ctime>

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

// 计算"今天 HH:MM"对应的时间点，用于错过补跑判断与精确唤醒。
static std::chrono::system_clock::time_point todayAt(const std::string& hhmm) {
    const auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);
    try {
        tm.tm_hour = std::stoi(hhmm.substr(0, 2));
        tm.tm_min = std::stoi(hhmm.substr(3, 2));
    } catch (...) {
        return now;
    }
    tm.tm_sec = 0;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

void TaskScheduler::loop() {
    std::unique_lock<std::mutex> lk(mu_);
    while (running_.load()) {
        const auto now = std::chrono::system_clock::now();
        const std::string today = dateToday();

        // 取快照
        std::vector<ScheduledTask> snapshot = tasks_;
        lk.unlock();

        // 下一次唤醒时间：默认 1 分钟后（所有任务今天都执行过时等明天）
        auto nextWakeup = now + std::chrono::minutes(1);

        for (auto& t : snapshot) {
            if (!t.enabled || t.scheduleTime.empty()) continue;

            const auto scheduled = todayAt(t.scheduleTime);

            // 检查今天是否已执行
            bool alreadyRun = false;
            {
                std::lock_guard<std::mutex> lk2(mu_);
                const auto it = lastRunDate_.find(t.name);
                if (it != lastRunDate_.end() && it->second == today) alreadyRun = true;
            }

            if (alreadyRun) continue;

            // 错过补跑：当前时间 >= 计划时间即执行（含休眠醒来后补跑）
            if (now >= scheduled) {
                {
                    std::lock_guard<std::mutex> lk2(mu_);
                    lastRunDate_[t.name] = today;
                }
                Logger::instance().info(L"TaskScheduler",
                                        L"定时触发任务: " + t.name + L" @ " + utf8ToWide(t.scheduleTime));
                execute(t);
            } else if (scheduled < nextWakeup) {
                // 还没到时间，精确唤醒到计划时间点
                nextWakeup = scheduled;
            }
        }

        lk.lock();
        cv_.wait_until(lk, nextWakeup);
    }
}

}  // namespace backup
