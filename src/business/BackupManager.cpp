// BackupManager.cpp - 备份流程实现
// 流程（需求文档 7.1）：
//   校验配置 -> 扫描 -> 筛选 -> 加载旧 Manifest -> 比较 -> 复制变化文件 ->
//   保存新 Manifest -> 记录历史 -> 生成 BackupResult
#include "business/BackupManager.h"

#include <algorithm>
#include <fstream>

#include "business/FileComparator.h"
#include "business/FileFilter.h"
#include "business/Manifest.h"
#include "core/BackupLock.h"
#include "core/HashCalculator.h"
#include "core/Logger.h"
#include "core/ResidualUtil.h"
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
    // 禁止目标等于源或位于源目录内，否则扫描会把备份产物也当成源内容，自我膨胀。
    {
        // 先解析为规范化绝对路径，避免 C:\a\sub\..\tgt 这类写法绕过包含关系判断。
        const auto normDir = [](const std::wstring& p) {
            std::wstring s = FileSystem::fullPath(p);
            // 保留盘符根 "C:\"（长度 3），其余去掉尾部路径分隔符。
            while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
            return s;
        };
        const std::wstring srcNorm = normDir(config.sourcePath);
        const std::wstring tgtNorm = normDir(config.targetPath);
        const std::wstring prefix = srcNorm + L"\\";
        const bool sameAsSource = (wcsicmpSafe(tgtNorm, srcNorm) == 0);
        const bool insideSource = (tgtNorm.size() > prefix.size() &&
            wcsicmpSafe(tgtNorm.substr(0, prefix.size()), prefix) == 0);
        if (sameAsSource || insideSource) {
            res.errors.push_back(std::wstring(L"目标路径不能等于或位于源目录内: ") + config.targetPath);
            log.error(L"BackupManager", L"目标路径不能等于或位于源目录内: " + config.targetPath);
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

    // ---- 单一实例锁：防止计划任务与手动备份同时写同一 target ----
    BackupLockGuard lock(config.targetPath);
    if (!lock.acquired()) {
        res.errors.push_back(utf8ToWide(lock.error()));
        log.error(L"BackupManager", utf8ToWide(lock.error()));
        res.endTime = formatNowWide();
        return res;
    }

    // ---- 1.5 崩溃恢复 ----
    // 处理上次进程被强杀后遗留的 .baktmp / .baktmp.old 中间状态。
    recoverResidualData(config.targetPath);

    // ---- 2. 扫描 ----
    std::vector<FileInfo> scanned;
    std::vector<std::wstring> scanErrors;
    std::vector<std::wstring> scanWarnings;
    if (!FileScanner::scan(config.sourcePath, scanned, scanErrors, cancel, opts.progress, &scanWarnings)) {
        res.success = false;
        res.errors = std::move(scanErrors);
        res.endTime = formatNowWide();
        return res;
    }
    res.totalScanned = static_cast<uint64_t>(scanned.size());
    res.errors.insert(res.errors.end(), scanErrors.begin(), scanErrors.end());
    res.warnings.insert(res.warnings.end(), scanWarnings.begin(), scanWarnings.end());
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
    // keepDirectories=true：保留目录条目，使空目录能进入 Manifest 并在恢复时重建。
    // 注意：目录不受 include_ext/include_path 等筛选规则限制，始终保留（设计决策：
    // 筛选针对文件内容，目录结构应完整保留以便恢复时重建目录树）。
    std::vector<FileInfo> files = filter.filter(scanned, /*keepDirectories=*/true);

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
        if (c.info.type == FileType::Directory) continue;  // 目录只保留结构，不计入变更统计
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

    // 文件 <-> 目录类型互换：先把挡路的旧文件/旧目录移到旁路，
    // 复制成功且 Manifest 更新后再删除；若本次备份失败/取消，
    // 则在返回前还原，保证旧 Manifest 仍可恢复。
    struct StagedPath {
        std::wstring dst;
        std::wstring stage;
    };
    std::vector<StagedPath> stagedPaths;
    const auto makeStageName = [](const std::wstring& dst) {
        std::wstring stage = dst + L".baktmp.old";
        int n = 1;
        while (FileSystem::exists(stage)) {
            stage = dst + L".baktmp.old" + std::to_wstring(n++);
        }
        return stage;
    };
    const auto stageExistingPath = [&](const std::wstring& dst) {
        if (!FileSystem::exists(dst)) return true;
        const std::wstring stage = makeStageName(dst);
        if (!FileSystem::movePath(dst, stage)) return false;
        stagedPaths.push_back(StagedPath{dst, stage});
        return true;
    };
    const auto rollbackStagedPaths = [&]() {
        for (auto it = stagedPaths.rbegin(); it != stagedPaths.rend(); ++it) {
            if (FileSystem::exists(it->dst)) {
                if (FileSystem::isDirectory(it->dst)) {
                    FileSystem::removeAll(it->dst);   // 本次新建的目录树
                } else {
                    FileSystem::deleteFile(it->dst);  // 本次写入的新文件
                }
            }
            if (!FileSystem::movePath(it->stage, it->dst)) {
                log.error(L"BackupManager",
                          L"还原旧目录失败，数据仍在: " + it->stage + L" -> " + it->dst);
            }
        }
        stagedPaths.clear();
    };
    // 确保目标父目录存在；若父路径被旧文件占据（文件 -> 目录互换），先移走旧文件。
    const auto ensureParentDirectories = [&](const std::wstring& absDst) {
        const size_t pos = absDst.find_last_of(L"\\/");
        if (pos == std::wstring::npos) return true;
        const std::wstring parent = absDst.substr(0, pos);
        if (FileSystem::exists(parent) && !FileSystem::isDirectory(parent)) {
            if (!stageExistingPath(parent)) return false;
        }
        return FileSystem::createDirectories(parent);
    };

    for (const auto& c : changes) {
        if (c.change != FileChangeType::Added && c.change != FileChangeType::Modified) continue;
        // 目录只进入 Manifest，不复制数据
        if (c.info.type == FileType::Directory) {
            Manifest::Entry e;
            e.info = c.info;
            e.dataPath = c.info.relativePath;
            newEntries.push_back(std::move(e));
            continue;
        }
        if (cancel && cancel()) {
            res.cancelled = true;
            res.errors.push_back(L"备份被用户取消");
            log.warn(L"BackupManager", L"备份被用户取消");
            break;
        }
        if (opts.progress) opts.progress(c.info.relativePath);

        const std::wstring absSrc = config.sourcePath + L"\\" + c.info.relativePath;
        const std::wstring absDst = dataDir + L"\\" + c.info.relativePath;

        // 确保目标父目录存在（文件 -> 目录互换时旧文件在此先移走）
        if (!ensureParentDirectories(absDst)) {
            ++res.failed;
            res.errors.push_back(std::wstring(L"无法准备目标父目录: ") + c.info.relativePath);
            log.error(L"BackupManager", L"无法准备目标父目录: " + c.info.relativePath);
            continue;
        }

        // 计算源文件 Hash（同时作为备份完整性基准）
        std::string hash;
        if (!HashCalculator::fileSha256(absSrc, hash)) {
            ++res.failed;
            res.errors.push_back(std::wstring(L"计算 Hash 失败: ") + c.info.relativePath);
            log.error(L"BackupManager", L"计算 Hash 失败: " + c.info.relativePath);
            continue;
        }

        // 若目标位置残留的是旧目录（目录 -> 文件互换或早期数据），先移走再写文件。
        if (FileSystem::isDirectory(absDst)) {
            if (!stageExistingPath(absDst)) {
                ++res.failed;
                res.errors.push_back(std::wstring(L"无法移走旧目录: ") + c.info.relativePath);
                log.error(L"BackupManager", L"无法移走旧目录: " + c.info.relativePath);
                continue;
            }
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
    // 关键：只要有文件失败或被取消，就不覆盖旧 Manifest，保留上一次完整清单。
    // 否则失败/未处理的文件会从清单中消失，恢复时静默缺失。
    if (res.cancelled || res.failed > 0) {
        log.error(L"BackupManager", L"备份" + (res.cancelled ? L"被取消" : L"有 " + std::to_wstring(res.failed) + L" 个失败") + L"，不覆盖旧 Manifest（保留上一次完整清单）");
        rollbackStagedPaths();
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
        rollbackStagedPaths();
        res.endTime = formatNowWide();
        return res;
    }

    // 类型互换时被移走的旧文件/旧目录：Manifest 已成功更新，可安全删除。
    for (const auto& sd : stagedPaths) {
        if (!FileSystem::removeAll(sd.stage)) {
            log.warn(L"BackupManager", L"清理旁路旧数据失败: " + sd.stage);
        }
    }
    stagedPaths.clear();

    // 增量备份：清理 data/ 里已删除文件的旧数据，避免长期占空间
    if (incremental) {
        for (const auto& c : changes) {
            if (c.change != FileChangeType::Deleted) continue;
            const std::wstring oldData = dataDir + L"\\" + c.info.relativePath;
            if (c.info.type == FileType::File) {
                FileSystem::deleteFile(oldData);
            } else if (c.info.type == FileType::Directory && FileSystem::isDirectory(oldData)) {
                // 只有路径上仍是目录才删除整棵树；
                // 目录->文件互换时该路径已是新文件，旧树在 stagedPaths 中另行清理。
                FileSystem::removeAll(oldData);
            }
        }
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

void BackupManager::recoverResidualData(const std::wstring& targetPath) {
    const std::wstring dataDir = dataDirOf(targetPath);
    if (!FileSystem::exists(dataDir)) return;

    // 读取当前 Manifest：用于区分"合法备份文件"与"崩溃残留"，
    // 以及判断 .baktmp.old 对应的新状态是否已提交（类型一致=已提交）。
    Manifest manifest;
    std::string loadErr;
    const bool manifestLoaded = manifest.loadFromFile(manifestPathOf(targetPath), &loadErr);
    if (!manifestLoaded) {
        // 保守原则：没有 Manifest 就无法区分"合法备份文件"与"崩溃残留"，
        // 此时绝不自动删除/移动任何数据，交由用户先运行 verify 或重新做一次备份。
        Logger::instance().warn(
            L"BackupManager",
            L"崩溃恢复: 无法读取 Manifest，跳过自动清理（请先运行 verify 确认备份状态）");
        return;
    }

    std::vector<FileInfo> entries;
    std::vector<std::wstring> errors;
    if (!FileScanner::scan(dataDir, entries, errors)) return;

    Logger& log = Logger::instance();

    for (const auto& e : entries) {
        const std::wstring& name = e.name;
        const std::wstring fullPath = dataDir + L"\\" + e.relativePath;

        // Manifest 白名单：如果该路径本身就是 Manifest 中的合法备份条目，
        // 则绝不能当作崩溃残留处理（用户的正常文件可能命名为 xxx.baktmp.old）。
        if (manifest.find(e.relativePath)) continue;

        // 1. .baktmp.old*：被移走的旧数据，需结合 Manifest 判断是否已提交
        const std::wstring originalName = parseOldResidual(name);
        if (!originalName.empty()) {
            // 计算原相对路径：把 e.relativePath 中的文件名替换为 originalName
            const size_t lastSlash = e.relativePath.find_last_of(L"\\/");
            const std::wstring originalRelPath = (lastSlash != std::wstring::npos)
                ? e.relativePath.substr(0, lastSlash + 1) + originalName
                : originalName;
            const std::wstring originalFullPath = dataDir + L"\\" + originalRelPath;

            // 读取 Manifest 中原路径的条目类型
            FileType manifestType = FileType::Unknown;
            const Manifest::Entry* entry = manifest.find(originalRelPath);
            if (entry) manifestType = entry->info.type;

            // 磁盘上原路径当前的类型
            FileType diskType = FileType::Unknown;
            if (FileSystem::exists(originalFullPath)) {
                diskType = FileSystem::isDirectory(originalFullPath) ? FileType::Directory : FileType::File;
            }

            if (manifestType != FileType::Unknown && diskType == manifestType) {
                // Manifest 类型与磁盘类型一致 → 新状态已提交 → 安全删除旧数据
                FileSystem::removeAll(fullPath);
                log.warn(L"BackupManager", L"崩溃恢复: 清理已提交的旧数据 " + e.relativePath);
            } else {
                // Manifest 类型与磁盘类型不一致 → 新状态未提交 → 还原旧数据
                if (FileSystem::exists(originalFullPath)) {
                    FileSystem::removeAll(originalFullPath);  // 删除未提交的新对象
                }
                if (FileSystem::movePath(fullPath, originalFullPath)) {
                    log.warn(L"BackupManager", L"崩溃恢复: 还原未提交的旧数据 " + e.relativePath + L" -> " + originalRelPath);
                } else {
                    log.error(L"BackupManager", L"崩溃恢复: 还原旧数据失败 " + fullPath);
                }
            }
            continue;
        }

        // 2. .baktmp：只有不在 Manifest 中的才是未完成的临时文件；
        //    用户的正常文件（如 data.baktmp）在 Manifest 中有条目，绝不能误删。
        if (isTempResidual(name)) {
            // 到这里已通过 Manifest 白名单，说明该路径不是合法备份条目
            FileSystem::removeAll(fullPath);
            log.warn(L"BackupManager", L"崩溃恢复: 清理未完成临时文件 " + e.relativePath);
            continue;
        }
    }
}

}  // namespace backup
