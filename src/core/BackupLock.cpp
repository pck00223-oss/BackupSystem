// BackupLock.cpp - 备份目录单一实例锁实现
#include "core/BackupLock.h"

#include "core/Utf.h"

namespace backup {

namespace {

constexpr int kMaxCreateAttempts = 20;
constexpr int kReadRetryTimes = 10;
constexpr DWORD kReadRetryDelayMs = 50;

// 读取现有锁文件中的 PID。
// 返回 true 表示文件可读；foundValidPid 表示内容是否成功解析出 PID。
// 返回 false 表示锁文件正被其他进程写入（无法打开），应视为"正在获取锁"。
bool readExistingLock(const std::wstring& path, DWORD& pid, bool& foundValidPid) {
    for (int i = 0; i < kReadRetryTimes; ++i) {
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buf[32] = {};
            DWORD bytesRead = 0;
            const BOOL ok = ::ReadFile(h, buf, sizeof(buf) - 1, &bytesRead, nullptr);
            ::CloseHandle(h);
            if (ok) {
                const std::string content(buf, bytesRead);
                try {
                    const unsigned long long p = std::stoull(content);
                    if (p != 0 && p <= 0xFFFFFFFFull) {
                        pid = static_cast<DWORD>(p);
                        foundValidPid = true;
                    } else {
                        foundValidPid = false;  // 空/0 → 崩溃残留
                    }
                } catch (...) {
                    foundValidPid = false;  // 非数字内容 → 崩溃残留
                }
                return true;
            }
            // 读失败按"文件正被写入"处理，继续重试
        } else {
            const DWORD err = ::GetLastError();
            if (err == ERROR_FILE_NOT_FOUND) {
                // 文件已被其他进程清理，由上层重试 CREATE_NEW
                foundValidPid = false;
                return true;
            }
        }
        ::Sleep(kReadRetryDelayMs);
    }
    return false;
}

}  // namespace

bool BackupLock::isProcessRunning(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exitCode = 0;
    const bool ok = ::GetExitCodeProcess(h, &exitCode) != 0;
    ::CloseHandle(h);
    return ok && exitCode == STILL_ACTIVE;
}

bool BackupLock::acquire(const std::wstring& targetPath,
                          std::wstring& lockFilePath,
                          std::string& errMsg) {
    lockFilePath = targetPath + L"\\.backup.lock";

    const std::string pidText = std::to_string(::GetCurrentProcessId()) + "\n";

    for (int attempt = 0; attempt < kMaxCreateAttempts; ++attempt) {
        // 原子获取：CREATE_NEW 保证同一时刻只有一个进程能创建锁文件。
        HANDLE h = ::CreateFileW(lockFilePath.c_str(), GENERIC_WRITE,
                                 /*dwShareMode=*/0, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const BOOL ok = ::WriteFile(h, pidText.data(),
                                        static_cast<DWORD>(pidText.size()),
                                        &written, nullptr);
            ::CloseHandle(h);
            if (ok && written == pidText.size()) {
                return true;
            }
            ::DeleteFileW(lockFilePath.c_str());
            errMsg = "failed to write lock file: " + wideToUtf8(lockFilePath);
            return false;
        }

        const DWORD err = ::GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            errMsg = "cannot create lock file (error " + std::to_string(err) +
                     "): " + wideToUtf8(lockFilePath);
            return false;
        }

        // 锁文件已存在：读取持有者 PID 判断是"正在运行"还是"崩溃残留"。
        DWORD existingPid = 0;
        bool foundValidPid = false;
        if (!readExistingLock(lockFilePath, existingPid, foundValidPid)) {
            // 一直无法打开 → 另一个进程正在创建锁（刚 CREATE_NEW 尚未写完 PID）
            errMsg = "another backup process is acquiring the lock, try again";
            return false;
        }

        if (foundValidPid && isProcessRunning(existingPid)) {
            errMsg = "another backup process is running (PID " + std::to_string(existingPid) +
                     "), wait for it to finish or remove " +
                     wideToUtf8(lockFilePath) + " manually";
            return false;
        }

        // 持有进程已退出（或锁文件为空/损坏）→ 删除残留后重试 CREATE_NEW。
        if (!::DeleteFileW(lockFilePath.c_str())) {
            const DWORD delErr = ::GetLastError();
            if (delErr != ERROR_FILE_NOT_FOUND) {
                errMsg = "cannot remove stale lock file (error " + std::to_string(delErr) +
                         "): " + wideToUtf8(lockFilePath);
                return false;
            }
        }
    }

    errMsg = "cannot acquire backup lock after " + std::to_string(kMaxCreateAttempts) +
             " attempts: " + wideToUtf8(lockFilePath);
    return false;
}

void BackupLock::release(const std::wstring& lockFilePath) {
    if (!lockFilePath.empty()) {
        ::DeleteFileW(lockFilePath.c_str());
    }
}

BackupLockGuard::BackupLockGuard(const std::wstring& targetPath) : acquired_(false) {
    acquired_ = BackupLock::acquire(targetPath, lockFilePath_, errMsg_);
}

BackupLockGuard::~BackupLockGuard() {
    if (acquired_) {
        BackupLock::release(lockFilePath_);
    }
}

}  // namespace backup
