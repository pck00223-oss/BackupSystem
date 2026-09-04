// BackupManager.h - 备份流程组织者
// 职责：组织一次备份的完整流程（扫描 -> 筛选 -> 比较 -> 复制 -> 保存 Manifest -> 记录历史）。
// 不直接承担底层文件读取的全部工作（需求文档 6.5/14.3 避免巨型类）。
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/BackupConfig.h"
#include "core/BackupResult.h"

namespace backup {

class BackupManager {
public:
    struct Options {
        std::function<bool()> cancelCheck;                 // 取消检查：true 中止
        std::function<void(const std::wstring&)> progress;  // 进度回调（当前相对路径）
    };

    // 同步执行一次备份。所有异常/失败均汇总到 BackupResult，不向上抛出。
    static BackupResult run(const BackupConfig& config, const Options& opts = Options());

    // 崩溃恢复：扫描 data/ 目录，清理 .baktmp 临时文件，恢复 .baktmp.old 旧数据。
    // 在备份启动时调用，处理上次进程被强杀后遗留的中间状态。
    static void recoverResidualData(const std::wstring& targetPath);

private:
    // 备份根目录下的结构：
    //   <target>/manifest.txt  最新一次备份的完整清单
    //   <target>/data/         文件数据（保持源相对结构）
    //   <target>/history.log   历史执行记录（追加）
    static std::wstring manifestPathOf(const std::wstring& target);
    static std::wstring dataDirOf(const std::wstring& target);
    static std::wstring historyPathOf(const std::wstring& target);

    // 追加一条历史记录。
    static void appendHistory(const std::wstring& target, const BackupResult& res);
};

}  // namespace backup
