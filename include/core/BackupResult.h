// BackupResult.h - 备份/恢复任务结果
// 职责：汇总一次备份或恢复任务的结果数据，供历史记录与 GUI 展示。
// 对应需求文档 3.10 备份历史 与 7.1 流程的 BackupResult / RestoreResult。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/BackupConfig.h"

namespace backup {

struct BackupResult {
    bool success = false;
    bool cancelled = false;
    BackupMode mode = BackupMode::Full;
    std::wstring sourcePath;
    std::wstring targetPath;
    std::wstring backupId;      // 备份标识（yyyyMMdd-HHmmss）
    uint64_t totalScanned = 0;  // 扫描到的文件数
    uint64_t backedUp = 0;      // 本次实际写入的文件数
    uint64_t added = 0;
    uint64_t modified = 0;
    uint64_t deleted = 0;
    uint64_t unchanged = 0;
    uint64_t failed = 0;
    uint64_t totalBytes = 0;    // 本次写入字节数
    std::wstring startTime;     // yyyy-MM-dd HH:mm:ss
    std::wstring endTime;
    std::vector<std::wstring> errors;  // 失败条目与原因
};

struct RestoreResult {
    bool success = false;
    bool cancelled = false;
    std::wstring backupRoot;    // 备份根目录
    std::wstring restorePath;   // 恢复目标目录
    uint64_t restored = 0;      // 成功恢复的文件数
    uint64_t skipped = 0;       // 跳过的文件数（已存在且一致 / 冲突不覆盖）
    uint64_t failed = 0;
    uint64_t verified = 0;      // 恢复后 Hash 校验通过数
    uint64_t hashMismatch = 0;  // Hash 校验不一致数
    std::wstring startTime;
    std::wstring endTime;
    std::vector<std::wstring> errors;    // 失败条目与原因（影响 success）
    std::vector<std::wstring> warnings;  // 非致命警告（如冲突跳过，不影响 success）
};

}  // namespace backup
