// TestEnv.h - GoogleTest 测试环境隔离工具
// 职责：
//   - TempDir：RAII 临时目录，析构自动清理；
//   - EnvGuard：保存/还原环境变量与当前工作目录，避免测试污染进程环境；
//   - LoggerGuard：每个用例前后重置全局 Logger 单例状态；
//   - dpapiAvailable：探测 DPAPI 是否可用（CI/服务账户下可跳过相关用例）。
#pragma once

#include <gtest/gtest.h>
#include <windows.h>
#include <dpapi.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/Logger.h"
#include "engine/FileSystem.h"

namespace testenv {

// 创建唯一的临时目录（带尾部反斜杠）。
inline std::wstring makeUniqueTempDir(const std::wstring& tag) {
    wchar_t tempPath[MAX_PATH] = {};
    if (::GetTempPathW(MAX_PATH, tempPath) == 0) return L".\\";
    static LONG counter = 0;
    const LONG id = ::InterlockedIncrement(&counter);
    std::wstring dir = std::wstring(tempPath) + L"backup_test_" + tag + L"_" +
                       std::to_wstring(::GetCurrentProcessId()) + L"_" + std::to_wstring(id) + L"\\";
    backup::FileSystem::createDirectories(dir);
    return dir;
}

// RAII 临时目录：析构时递归删除。
class TempDir {
public:
    explicit TempDir(const std::wstring& tag) : path_(makeUniqueTempDir(tag)) {}
    ~TempDir() { cleanup(); }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::wstring& path() const { return path_; }
    std::wstring file(const std::wstring& relative) const { return path_ + relative; }
    bool exists() const { return backup::FileSystem::exists(path_); }

    void cleanup() {
        if (!path_.empty()) {
            backup::FileSystem::removeAll(path_);
            path_.clear();
        }
    }

private:
    std::wstring path_;
};

// 环境变量 / 工作目录守卫：构造时记录，析构时还原。
class EnvGuard {
public:
    EnvGuard() {
        wchar_t cwd[MAX_PATH] = {};
        if (::GetCurrentDirectoryW(MAX_PATH, cwd) != 0) originalCwd_ = cwd;
    }

    ~EnvGuard() {
        for (auto it = vars_.rbegin(); it != vars_.rend(); ++it) {
            ::SetEnvironmentVariableW(it->first.c_str(),
                                      it->second.empty() ? nullptr : it->second.c_str());
        }
        if (!originalCwd_.empty()) ::SetCurrentDirectoryW(originalCwd_.c_str());
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

    // 设置环境变量（首次触碰时记录原值，析构还原）。
    void set(const std::wstring& name, const std::wstring& value) {
        remember(name);
        ::SetEnvironmentVariableW(name.c_str(), value.c_str());
    }

    void clear(const std::wstring& name) {
        remember(name);
        ::SetEnvironmentVariableW(name.c_str(), nullptr);
    }

private:
    void remember(const std::wstring& name) {
        const bool alreadyTracked = std::any_of(
            vars_.begin(), vars_.end(),
            [&name](const std::pair<std::wstring, std::wstring>& v) { return v.first == name; });
        if (alreadyTracked) return;
        wchar_t buf[32767] = {};
        const DWORD len = ::GetEnvironmentVariableW(name.c_str(), buf, 32767);
        if (len == 0) {
            vars_.emplace_back(name, std::wstring());
        } else {
            vars_.emplace_back(name, std::wstring(buf, len));
        }
    }

    std::wstring originalCwd_;
    std::vector<std::pair<std::wstring, std::wstring>> vars_;
};

// 全局 Logger 单例的状态守卫。
class LoggerGuard {
public:
    LoggerGuard() { reset(); }
    ~LoggerGuard() { reset(); }

    LoggerGuard(const LoggerGuard&) = delete;
    LoggerGuard& operator=(const LoggerGuard&) = delete;

private:
    static void reset() {
        backup::Logger::instance().init(L"");
        backup::Logger::instance().setLevel(backup::LogLevel::Info);
    }
};

// 探测 DPAPI（CryptProtectData）在当前用户上下文是否可用。
inline bool dpapiAvailable() {
    const char data[] = "probe";
    DATA_BLOB in{static_cast<DWORD>(sizeof(data) - 1),
                 reinterpret_cast<BYTE*>(const_cast<char*>(data))};
    DATA_BLOB out{};
    const BOOL ok = ::CryptProtectData(&in, L"probe", nullptr, nullptr, nullptr, 0, &out);
    if (ok && out.pbData) ::LocalFree(out.pbData);
    return ok != FALSE;
}

// 通用 fixture：每个用例前后重置 Logger，避免单例状态跨用例污染。
class BackupTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        backup::Logger::instance().init(L"");
        backup::Logger::instance().setLevel(backup::LogLevel::Info);
    }
    void TearDown() override {
        backup::Logger::instance().init(L"");
        backup::Logger::instance().setLevel(backup::LogLevel::Info);
    }
};

}  // namespace testenv
