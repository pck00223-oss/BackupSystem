// BackupLock.cpp - 备份目录单一实例锁实现
#include "core/BackupLock.h"

#include <fstream>

namespace backup {

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

    // 检查锁文件是否存在
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (::GetFileAttributesExW(lockFilePath.c_str(), GetFileExInfoStandard, &fad)) {
        // 锁文件存在，读取持有进程 PID
        std::ifstream ifs(lockFilePath.c_str());
        DWORD pid = 0;
        if (ifs >> pid) {
            if (isProcessRunning(pid)) {
                errMsg = "another backup process is running (PID " + std::to_string(pid) +
                         "), wait for it to finish or remove " +
                         std::string(lockFilePath.begin(), lockFilePath.end()) + " manually";
                return false;
            }
        }
        // 持有进程已退出（崩溃残留），删除旧锁文件
        ::DeleteFileW(lockFilePath.c_str());
    }

    // 创建新锁文件，写入当前 PID
    std::ofstream ofs(lockFilePath.c_str());
    if (!ofs) {
        errMsg = "cannot create lock file: " + std::string(lockFilePath.begin(), lockFilePath.end());
        return false;
    }
    ofs << ::GetCurrentProcessId() << "\n";
    ofs.close();
    return true;
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
