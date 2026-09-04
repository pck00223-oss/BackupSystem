// BackupManager.cpp - 备份流程实现
// 流程（需求文档 7.1）：
//   校验配置 -> 扫描 -> 筛选 -> 加载旧 Manifest -> 比较 -> 复制变化文件 ->
//   保存新 Manifest -> 记录历史 -> 生成 BackupResult
#include "business/BackupManager.h"

#include <fstream>

#include "business/FileComparator.h"
#include "business/FileFilter.h"
#include "business/Manifest.h"
#include "core/HashCalculator.h"
#include "core/Logger.h"
#include "core/TimeUtil.h"
#include "core/Utf.h"
#include "engine/FileCopier.h"
#include "engine/FileScanner.h"
#include "engine/FileSystem.h"

namespace backup {

std::wstring BackupManager::manifestPathOf(const std::wstring& target) {
    return target + L"\\manifest.txt";
}
std::wstring BackupManager::dataDirOf(const std::wstring& target) {
    return target + L"\\data";
}
std::wstring BackupManager::historyPathOf(const std::wstring& target) {
    return target + L"\\history.log";
}

void BackupManager::appendHistory(const std::wstring& target, const BackupResult& res) {
    std::ofstream ofs(historyPathOf(target).c_str(), std::ios::binary | std::ios::app);
    if (!ofs) return;
    std::string line = "[" + wideToUtf8(res.startTime) + "] ";
    line += "id=" + wideToUtf8(res.backupId) + " ";
    line += "type=" + std::string(res.mode == BackupMode::Incremental ? "incremental" : "full") + " ";
    line += "files=" + std::to_string(res.totalScanned) + " ";
    line += "backed_up=" + std::to_string(res.backedUp) + " ";
    line += "added=" + std::to_string(res.added) + " ";
    line += "modified=" + std::to_string(res.modified) + " ";
    line += "deleted=" + std::to_string(res.deleted) + " ";
    line += "failed=" + std::to_string(res.failed) + " ";
    line += "bytes=" + std::to_string(res.totalBytes) + " ";
    line += "status=" + std::string(res.success ? "success" : "fail") + " ";
    line += "source=" + wideToUtf8(res.sourcePath) + "\n";
    ofs.write(line.data(), static_cast<std::streamsize>(line.size()));
}

BackupResult BackupManager::run(const BackupConfig& config, const Options& opts) {
    BackupResult res;
    res.mode = config.mode;
    res.sourcePath = config.sourcePath;
    res.targetPath = config.targetPath;
    res.backupId = utf8ToWide(makeBackupId());
    res.startTime = formatNowWide();

    Logger& log = Logger::instance();
    const auto& cancel = opts.cancelCheck;

    log.info(L"BackupManager", L"备份开始, id=" + res.backupId + L", mode=" +
                                  (res.mode == BackupMode::Incremental ? L"增量" : L"全量"));

    // ---- 1. 校验 ----
    if (!FileSystem::exists(config.sourcePath)) {
        res.errors.push_back(std::wstring(L"源路径不存在: ") + config.sourcePath);
        log.error(L"BackupManager", L"源路径不存在: " + config.sourcePath);
        res.endTime = formatNowWide();
        return res;
    }
    if (!FileSystem::isDirectory(config.sourcePath)) {
        res.errors.push_back(std::wstring(L"源路径不是目录: ") + config.sourcePath);
        log.error(L"BackupManager", L"源路径不是目录: " + config.sourcePath);
        res.endTime = formatNowWide();
        return res;
    }
    // 禁止目标位于源目录内，否则扫描会把上次备份的数据也当成源内容，自我膨胀。
    {
        std::wstring srcNorm = config.sourcePath;
        std::wstring tgtNorm = config.targetPath;
        while (!srcNorm.empty() && srcNorm.back() == L'\\') srcNorm.pop_back();
        while (!tgtNorm.empty() && tgtNorm.back() == L'\\') tgtNorm.pop_back();
        const std::wstring prefix = srcNorm + L"\\";
        if (tgtNorm.size() > prefix.size() &&
            wcsicmpSafe(tgtNorm.substr(0, prefix.size()), prefix) == 0) {
            res.errors.push_back(std::wstring(L"目标路径不能位于源目录内: ") + config.targetPath);
            log.error(L"BackupManager", L"目标路径不能位于源目录内: " + config.targetPath);
            res.endTime = formatNowWide();
            return res;
        }
    }
    if (!FileSystem::createDirectories(config.targetPath)) {
        res.errors.push_back(std::wstring(L"无法创建目标目录: ") + config.targetPath);
        log.error(L"BackupManager", L"无法创建目标目录: " + config.targetPath);
        res.endTime = formatNowWide();
        return res;
    }

    // ---- 2. 扫描 ----
    std::vector<FileInfo> scanned;
    std::vector<std::wstring> scanErrors;
    if (!FileScanner::scan(config.sourcePath, scanned, scanErrors, cancel, opts.progress)) {
        res.success = false;
        res.errors = std::move(scanErrors);
        res.endTime = formatNowWide();
        return res;
    }
    res.totalScanned = static_cast<uint64_t>(scanned.size());
    res.errors.insert(res.errors.end(), scanErrors.begin(), scanErrors.end());
    // 扫描阶段的错误（如子目录无权访问）意味着备份不完整，必须计入失败。
    if (!scanErrors.empty()) {
        res.failed += static_cast<uint64_t>(scanErrors.size());
        log.warn(L"BackupManager", L"扫描阶段有 " + std::to_wstring(scanErrors.size()) + L" 个错误，备份不完整");
    }
    log.info(L"BackupManager", L"扫描完成: " + std::to_wstring(scanned.size()) + L" 个条目");

    // 扫描后立即响应取消（例如用户在选择阶段即取消）
    if (cancel && cancel()) {
        res.cancelled = true;
        res.errors.push_back(L"备份被用户取消");
        log.warn(L"BackupManager", L"备份被用户取消");
        res.endTime = formatNowWide();
        return res;
    }

    // ---- 3. 筛选 ----
    FileFilter filter;
    filter.setRule(config.filter);
    std::vector<FileInfo> files = filter.filter(scanned, /*keepDirectories=*/false);

    // ---- 4. 加载旧 Manifest，判断是否可增量 ----
    const std::wstring manifestPath = manifestPathOf(config.targetPath);
    Manifest previous;
    const bool hasPrevious = previous.loadFromFile(manifestPath, nullptr);
    const bool incremental = (config.mode == BackupMode::Incremental) && hasPrevious;

    // ---- 5. 比较 ----
    std::vector<ChangeRecord> changes;
    if (incremental) {
        FileComparator::HashProvider hashProvider =
            [&config](const FileInfo& info, std::string& outHash) -> bool {
            return HashCalculator::fileSha256(config.sourcePath + L"\\" + info.relativePath,
                                              outHash);
        };
        changes = FileComparator::compare(files, previous, hashProvider);
        log.info(L"BackupManager", L"增量比较完成");
    } else {
        // 全量：所有文件进入备份集合
        changes.reserve(files.size());
        for (const auto& f : files) {
            ChangeRecord rec;
            rec.info = f;
            rec.change = FileChangeType::Added;
            changes.push_back(rec);
        }
    }

    for (const auto& c : changes) {
        switch (c.change) {
            case FileChangeType::Added: ++res.added; break;
            case FileChangeType::Modified: ++res.modified; break;
            case FileChangeType::Deleted: ++res.deleted; break;
            default: ++res.unchanged; break;
        }
    }
    log.info(L"BackupManager",
             L"变更统计: 新增=" + std::to_wstring(res.added) +
                 L" 修改=" + std::to_wstring(res.modified) +
                 L" 删除=" + std::to_wstring(res.deleted) +
                 L" 未变化=" + std::to_wstring(res.unchanged));

    // ---- 6. 备份数据 ----
    const std::wstring dataDir = dataDirOf(config.targetPath);
    FileSystem::createDirectories(dataDir);

    std::vector<Manifest::Entry> newEntries;

    for (const auto& c : changes) {
        if (c.change != FileChangeType::Added && c.change != FileChangeType::Modified) continue;
        if (cancel && cancel()) {
            res.cancelled = true;
            res.errors.push_back(L"备份被用户取消");
            log.warn(L"BackupManager", L"备份被用户取消");
            break;
        }
        if (opts.progress) opts.progress(c.info.relativePath);

        const std::wstring absSrc = config.sourcePath + L"\\" + c.info.relativePath;
        const std::wstring absDst = dataDir + L"\\" + c.info.relativePath;

        // 确保目标父目录存在
        const size_t pos = absDst.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            FileSystem::createDirectories(absDst.substr(0, pos));
        }

        // 计算源文件 Hash（同时作为备份完整性基准）
        std::string hash;
        if (!HashCalculator::fileSha256(absSrc, hash)) {
            ++res.failed;
            res.errors.push_back(std::wstring(L"计算 Hash 失败: ") + c.info.relativePath);
            log.error(L"BackupManager", L"计算 Hash 失败: " + c.info.relativePath);
            continue;
        }

        std::string copyErr;
        if (!FileCopier::copyFile(absSrc, absDst, &copyErr, cancel)) {
            ++res.failed;
            res.errors.push_back(std::wstring(L"复制失败: ") + c.info.relativePath + L" - " +
                                 utf8ToWide(copyErr));
            log.error(L"BackupManager", L"复制失败: " + c.info.relativePath + L" - " +
                                            utf8ToWide(copyErr));
            continue;
        }

        ++res.backedUp;
        res.totalBytes += c.info.size;

        Manifest::Entry e;
        e.info = c.info;
        e.info.hash = std::move(hash);
        e.info.hashed = true;
        e.dataPath = c.info.relativePath;
        newEntries.push_back(std::move(e));
    }

    // ---- 7. 组装新 Manifest：未变化文件沿用旧记录 ----
    if (incremental) {
        for (const auto& c : changes) {
            if (c.change != FileChangeType::Unchanged) continue;
            const Manifest::Entry* old = previous.find(c.info.relativePath);
            if (old) newEntries.push_back(*old);
        }
    }

    // ---- 8. 保存 Manifest ----
    // 关键：只要有文件失败，就不覆盖旧 Manifest，保留上一次完整清单。
    // 否则失败文件会从清单中消失，恢复时静默缺失。
    if (res.failed > 0) {
        log.error(L"BackupManager", L"有 " + std::to_wstring(res.failed) + L" 个失败，不覆盖旧 Manifest（保留上一次完整清单）");
        res.success = false;
        appendHistory(config.targetPath, res);
        res.endTime = formatNowWide();
        return res;
    }

    Manifest manifest;
    manifest.meta.backupId = wideToUtf8(res.backupId);
    manifest.meta.sourcePath = config.sourcePath;
    manifest.meta.created = formatNowUtf8();
    manifest.meta.backupType = incremental ? "incremental" : "full";
    manifest.meta.fileCount = static_cast<uint64_t>(newEntries.size());
    manifest.entries = std::move(newEntries);
    manifest.rebuildIndex();

    std::string saveErr;
    if (!manifest.saveToFile(manifestPath, &saveErr)) {
        res.success = false;
        res.errors.push_back(std::wstring(L"保存 Manifest 失败: ") + utf8ToWide(saveErr));
        log.error(L"BackupManager", L"保存 Manifest 失败: " + utf8ToWide(saveErr));
        res.endTime = formatNowWide();
        return res;
    }

    // ---- 9. 历史记录 ----
    res.success = (res.failed == 0);
    appendHistory(config.targetPath, res);
    res.endTime = formatNowWide();

    log.info(L"BackupManager",
             (res.success ? std::wstring(L"备份完成: ") : std::wstring(L"备份完成(有失败): ")) +
                 L"写入 " + std::to_wstring(res.backedUp) + L" 个文件, " +
                 std::to_wstring(res.totalBytes) + L" 字节, 失败 " +
                 std::to_wstring(res.failed));
    return res;
}

}  // namespace backup
