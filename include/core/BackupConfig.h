// BackupConfig.h - 备份任务配置
// 职责：保存一次备份任务所需的全部配置（源、目标、模式、筛选、调度、恢复冲突策略）。
// 对应需求文档 12 配置设计。压缩/加密字段在此预留，第二阶段扩展。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/FileInfo.h"

namespace backup {

enum class BackupMode : uint8_t {
    Full = 0,        // 全量备份
    Incremental = 1  // 增量备份
};

// 筛选规则（需求文档 3.3 自定义备份）。
// 说明：include 集合为空表示"不限制"；exclude 优先级高于 include。
struct FilterRule {
    std::vector<std::wstring> includeExtensions;  // 包含的扩展名（小写，含点，如 .cpp）
    std::vector<std::wstring> excludeExtensions;  // 排除的扩展名
    std::vector<std::wstring> includeSubPaths;    // 仅备份这些相对路径前缀
    std::vector<std::wstring> excludeSubPaths;    // 排除这些相对路径前缀
    uint64_t minSize = 0;                         // 最小字节数，0 表示不限
    uint64_t maxSize = 0;                         // 最大字节数，0 表示不限
    bool modifiedAfterEnabled = false;            // 是否启用"仅备份修改时间晚于"筛选
    uint64_t modifiedAfterUnix = 0;               // 时间阈值（Unix 秒）
    bool skipEmptyFiles = false;                  // 是否跳过空文件

    // 判断单个文件是否匹配规则（目录由调用方决定是否保留）。
    bool isMatch(const FileInfo& info) const;
};

// 一次备份任务的完整配置。
struct BackupConfig {
    std::wstring sourcePath;      // 源目录
    std::wstring targetPath;      // 备份根目录（manifest.txt 与 data/ 存放于此）
    BackupMode mode = BackupMode::Full;
    FilterRule filter;            // 筛选规则
    std::wstring scheduleTime;    // 定时执行时间 "HH:MM"，空表示手动/立即
    bool overwriteOnRestore = false;  // 恢复时目标文件已存在且内容不同：true 覆盖，false 跳过

    // ---- 扩展预留（Phase 7/8）----
    // std::string compression;   // "none" / "zstd"
    // std::string encryption;    // "none" / "aes"
    // std::wstring password;     // 注意：不得写入普通日志
};

}  // namespace backup
