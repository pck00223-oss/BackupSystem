// RestoreManager.h - 数据恢复组织者
// 职责：读取 Manifest，创建目录结构，恢复文件，恢复元数据并做 Hash 校验。
// 对应需求文档 6.6 RestoreManager 与 7.2 数据还原流程。
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/BackupResult.h"

namespace backup {

struct RestoreConfig {
    std::wstring backupRoot;   // 备份根目录（含 manifest.txt 与 data/）
    std::wstring restorePath;  // 恢复目标目录（不存在则创建）
    bool overwrite = false;    // 目标已存在且内容不同：true 覆盖，false 跳过
    std::wstring snapshot;     // 快照时间戳，空=从最新（根目录）恢复，非空=从指定快照恢复
};

class RestoreManager {
public:
    using CancelCheck = std::function<bool()>;
    using Progress = std::function<void(const std::wstring& relativePath)>;

    // 同步执行恢复。错误汇总到 RestoreResult，不向上抛出。
    static RestoreResult run(const RestoreConfig& config,
                             const CancelCheck& cancel = nullptr,
                             const Progress& progress = nullptr);
};

}  // namespace backup
