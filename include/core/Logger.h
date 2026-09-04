// Logger.h - 统一日志系统
// 职责：记录 INFO / WARNING / ERROR 三级日志，同时输出到控制台与日志文件。
// 对应需求文档 15 日志系统。约束：敏感信息（如密码）不得写入普通日志。
#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace backup {

enum class LogLevel : uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2
};

class Logger {
public:
    static Logger& instance();

    // 初始化日志文件（追加模式）。logFile 为空则仅输出到控制台。
    void init(const std::wstring& logFile);
    void setLevel(LogLevel level) { level_ = level; }

    void log(LogLevel level, const std::wstring& module, const std::wstring& message);
    void info(const std::wstring& module, const std::wstring& message) { log(LogLevel::Info, module, message); }
    void warn(const std::wstring& module, const std::wstring& message) { log(LogLevel::Warning, module, message); }
    void error(const std::wstring& module, const std::wstring& message) { log(LogLevel::Error, module, message); }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mu_;
    std::ofstream file_;
    LogLevel level_ = LogLevel::Info;
};

}  // namespace backup
