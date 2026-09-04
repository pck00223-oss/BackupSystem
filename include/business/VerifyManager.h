// VerifyManager.h - 备份完整性校验
// 职责：按 Manifest 逐条校验 data/ 仓库中的文件是否存在、Hash 是否一致，
//       报告缺失/损坏条目，弥补增量备份信任旧数据的薄弱点。
// 对应需求文档"完整性校验"设计。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace backup {

struct VerifyResult {
    uint64_t total = 0;       // Manifest 中的文件条目总数
    uint64_t passed = 0;      // 存在且 Hash 一致
    uint64_t missing = 0;     // data/ 中文件不存在
    uint64_t corrupted = 0;   // 文件存在但 Hash 不一致
    uint64_t skipped = 0;     // 非文件条目（目录/符号链接），不校验
    bool success = false;      // missing == 0 && corrupted == 0
    std::vector<std::wstring> errors;  // 详细错误列表
};

class VerifyManager {
public:
    // 校验备份根目录（含 manifest.txt 与 data/）的完整性。
    // backupRoot: 备份根目录路径
    static VerifyResult run(const std::wstring& backupRoot);
};

}  // namespace backup
