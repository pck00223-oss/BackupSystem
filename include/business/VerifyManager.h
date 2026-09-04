// VerifyManager.h - 备份完整性校验
// 职责：按 Manifest 逐条校验 data/ 仓库中的文件是否存在、Hash 是否一致，
//       报告缺失/损坏条目，检测崩溃残留（.baktmp / .baktmp.old），
//       弥补增量备份信任旧数据的薄弱点。
//       支持 --repair 模式：发现损坏/缺失时，用源目录文件自动重建。
// 对应需求文档"完整性校验"设计。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace backup {

struct VerifyResult {
    uint64_t total = 0;       // Manifest 中的文件条目总数
    uint64_t passed = 0;      // 存在且 Hash 一致
    uint64_t missing = 0;     // data/ 中文件不存在
    uint64_t corrupted = 0;   // 文件存在但 Hash 不一致
    uint64_t residual = 0;    // 崩溃残留文件（.baktmp / .baktmp.old）
    uint64_t repaired = 0;    // 自动修复成功的条目数
    uint64_t skipped = 0;     // 非文件条目（目录/符号链接），不校验
    bool success = false;      // missing == 0 && corrupted == 0 && residual == 0
    std::vector<std::wstring> errors;  // 详细错误列表
};

struct VerifyOptions {
    std::function<bool()> cancelCheck;                  // 取消检查：true 中止
    std::function<void(const std::wstring&)> progress;  // 进度回调（当前相对路径）
    bool repair = false;                                 // 发现损坏/缺失时，用源文件自动修复
    std::wstring sourcePath;                             // 源目录路径（repair 时需要）
};

class VerifyManager {
public:
    // 校验备份根目录（含 manifest.txt 与 data/）的完整性。
    // 包括：Manifest 条目校验 + 崩溃残留检测。
    // opts.repair=true 时，发现损坏/缺失且源目录存在时，自动用源文件重建。
    static VerifyResult run(const std::wstring& backupRoot, const VerifyOptions& opts = VerifyOptions());
};

}  // namespace backup
