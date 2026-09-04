// Logger.cpp - 日志系统实现
#include "core/Logger.h"

#include <iostream>

#include "core/TimeUtil.h"
#include "core/Utf.h"

namespace backup {

Logger::~Logger() {
    if (file_.is_open()) file_.close();
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::wstring& logFile) {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_.is_open()) file_.close();
    if (!logFile.empty()) {
        file_.open(logFile.c_str(), std::ios::binary | std::ios::app);
    }
}

void Logger::log(LogLevel level, const std::wstring& module, const std::wstring& message) {
    if (level < level_) return;

    const char* lvName = "INFO";
    if (level == LogLevel::Warning) lvName = "WARNING";
    if (level == LogLevel::Error) lvName = "ERROR";

    // 格式：[yyyy-MM-dd HH:mm:ss] [LEVEL] [Module] message
    const std::string line = "[" + formatNowUtf8() + "] [" + lvName + "] [" +
                             wideToUtf8(module) + "] " + wideToUtf8(message) + "\n";

    std::lock_guard<std::mutex> lk(mu_);
    if (file_.is_open()) {
        file_.write(line.data(), static_cast<std::streamsize>(line.size()));
        file_.flush();
    }
    std::cout << line;
    std::cout.flush();
}

}  // namespace backup
