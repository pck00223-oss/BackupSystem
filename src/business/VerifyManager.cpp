// VerifyManager.cpp - 备份完整性校验实现
#include "business/VerifyManager.h"

#include "business/Manifest.h"
#include "core/HashCalculator.h"
#include "core/ResidualUtil.h"
#include "core/Utf.h"
#include "engine/FileCopier.h"
#include "engine/FileScanner.h"
#include "engine/FileSystem.h"

namespace backup {

namespace {

// 尝试用源文件修复一个损坏/缺失的备份条目。
// 成功返回 true（已用源文件覆盖 data/ 中的对应文件），失败返回 false。
bool tryRepairEntry(const Manifest::Entry& e,
                    const std::wstring& dataDir,
                    const std::wstring& sourcePath,
                    std::wstring& errDetail) {
    if (sourcePath.empty()) {
        errDetail = L"未指定源目录 (--source)";
        return false;
    }
    const std::wstring srcFile = sourcePath + L"\\" + e.info.relativePath;
    if (!FileSystem::exists(srcFile)) {
        errDetail = L"源文件不存在: " + srcFile;
        return false;
    }
    // 源文件 Hash 必须与 Manifest 一致，否则说明源文件也变了，不能用它修复
    std::string srcHash;
    if (!HashCalculator::fileSha256(srcFile, srcHash)) {
        errDetail = L"无法计算源文件 Hash: " + srcFile;
        return false;
    }
    if (srcHash != e.info.hash) {
        errDetail = L"源文件 Hash 与 Manifest 不一致（源文件可能已变化），无法自动修复";
        return false;
    }
    // 确保目标目录存在
    const std::wstring dataPath = dataDir + L"\\" + e.dataPath;
    const size_t lastSlash = dataPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        FileSystem::createDirectories(dataPath.substr(0, lastSlash));
    }
    // 用 FileCopier 原子复制源文件到 data/
    std::string copyErr;
    if (!FileCopier::copyFile(srcFile, dataPath, &copyErr, nullptr)) {
        errDetail = utf8ToWide(copyErr);
        return false;
    }
    return true;
}

}  // namespace

VerifyResult VerifyManager::run(const std::wstring& backupRoot, const VerifyOptions& opts) {
    VerifyResult res;

    // 1. 读取 Manifest
    Manifest manifest;
    std::string loadErr;
    const std::wstring manifestPath = backupRoot + L"\\manifest.txt";
    if (!manifest.loadFromFile(manifestPath, &loadErr)) {
        res.errors.push_back(std::wstring(L"无法读取 Manifest: ") + utf8ToWide(loadErr));
        res.success = false;
        return res;
    }

    const std::wstring dataDir = backupRoot + L"\\data";

    // 2. 逐条校验 Manifest 中的文件条目
    for (const auto& e : manifest.entries) {
        if (opts.cancelCheck && opts.cancelCheck()) {
            res.errors.push_back(L"校验被取消");
            res.success = false;
            return res;
        }

        // 目录和符号链接不做文件级校验
        if (e.info.type != FileType::File) {
            ++res.skipped;
            continue;
        }
        ++res.total;

        if (opts.progress) opts.progress(e.info.relativePath);

        // 使用 Manifest 中记录的 dataPath（与 RestoreManager 一致），
        // 单镜像方案中 dataPath 默认等于 relativePath，但以 Manifest 记录为准。
        const std::wstring dataPath = dataDir + L"\\" + e.dataPath;

        // 2a. 检查文件是否存在
        if (!FileSystem::exists(dataPath)) {
            const bool encryptedEntry = (e.cipherSize > 0);
            if (opts.repair && !encryptedEntry) {
                std::wstring repairErr;
                if (tryRepairEntry(e, dataDir, opts.sourcePath, repairErr)) {
                    ++res.repaired;
                    ++res.passed;
                    res.repairedDetails.push_back(std::wstring(L"[已修复] 缺失文件已从源目录重建: ") + e.info.relativePath);
                    continue;
                }
                ++res.missing;
                res.errors.push_back(std::wstring(L"[缺失] ") + e.info.relativePath + L" (修复失败: " + repairErr + L")");
                continue;
            }
            ++res.missing;
            res.errors.push_back(std::wstring(L"[缺失] ") + e.info.relativePath);
            continue;
        }

        // 2b. 检查大小是否一致（快速失败，避免读大文件后才发现）
        // 加密文件：data/ 中存的是 IV+密文，用 cipherSize 校验；未加密文件用 info.size。
        const bool encryptedEntry = (e.cipherSize > 0);
        const uint64_t expectedSize = encryptedEntry ? e.cipherSize : e.info.size;
        uint64_t actualSize = 0;
        {
            FileInfo fi;
            if (FileSystem::getFileInfo(dataPath, fi)) {
                actualSize = fi.size;
            }
        }
        if (actualSize != expectedSize) {
            if (opts.repair && !encryptedEntry) {
                std::wstring repairErr;
                if (tryRepairEntry(e, dataDir, opts.sourcePath, repairErr)) {
                    ++res.repaired;
                    ++res.passed;
                    res.repairedDetails.push_back(std::wstring(L"[已修复] 大小不符文件已从源目录重建: ") + e.info.relativePath);
                    continue;
                }
                ++res.corrupted;
                res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath +
                                      L" (大小不符: 期望 " + std::to_wstring(expectedSize) +
                                      L", 实际 " + std::to_wstring(actualSize) + L"; 修复失败: " + repairErr + L")");
                continue;
            }
            ++res.corrupted;
            res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath +
                                  L" (大小不符: 期望 " + std::to_wstring(expectedSize) +
                                  L", 实际 " + std::to_wstring(actualSize) + L")");
            continue;
        }

        // 2c. 计算 Hash 并比较（加密文件用密文 Hash，未加密文件用明文 Hash）
        const std::string& expectedHash = encryptedEntry ? e.cipherHash : e.info.hash;
        std::string actualHash;
        if (!HashCalculator::fileSha256(dataPath, actualHash)) {
            ++res.corrupted;
            res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath + L" (无法计算 Hash)");
            continue;
        }
        if (actualHash != expectedHash) {
            if (opts.repair && !encryptedEntry) {
                std::wstring repairErr;
                if (tryRepairEntry(e, dataDir, opts.sourcePath, repairErr)) {
                    ++res.repaired;
                    ++res.passed;
                    res.repairedDetails.push_back(std::wstring(L"[已修复] Hash 不一致文件已从源目录重建: ") + e.info.relativePath);
                    continue;
                }
                ++res.corrupted;
                res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath +
                                      L" (Hash 不一致: 期望 " + utf8ToWide(expectedHash.substr(0, 16)) +
                                      L"..., 实际 " + utf8ToWide(actualHash.substr(0, 16)) + L"...; 修复失败: " + repairErr + L")");
                continue;
            }
            ++res.corrupted;
            res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath +
                                  L" (Hash 不一致: 期望 " + utf8ToWide(expectedHash.substr(0, 16)) +
                                  L"..., 实际 " + utf8ToWide(actualHash.substr(0, 16)) + L"...)");
            continue;
        }

        ++res.passed;
    }

    // 3. 崩溃残留检测：扫描 data/ 目录，查找不在 Manifest 中的 .baktmp / .baktmp.old 残留。
    //    关键：用户的正常文件（如 data.baktmp）在 Manifest 中有条目，绝不能误报为残留。
    if (FileSystem::exists(dataDir)) {
        std::vector<FileInfo> entries;
        std::vector<std::wstring> scanErrors;
        if (FileScanner::scan(dataDir, entries, scanErrors)) {
            for (const auto& e : entries) {
                if (opts.cancelCheck && opts.cancelCheck()) {
                    res.errors.push_back(L"残留检测被取消");
                    res.success = false;
                    return res;
                }
                const std::wstring& name = e.name;
                // 只有不在 Manifest 中的文件才可能是残留（用户的正常文件在 Manifest 中有条目）
                if (manifest.find(e.relativePath)) continue;

                if (!parseOldResidual(name).empty()) {
                    ++res.residual;
                    res.errors.push_back(std::wstring(L"[残留] 崩溃遗留旧数据: ") + e.relativePath +
                                          L" (运行备份可自动恢复)");
                } else if (isTempResidual(name)) {
                    ++res.residual;
                    res.errors.push_back(std::wstring(L"[残留] 未完成临时文件: ") + e.relativePath +
                                          L" (运行备份可自动清理)");
                }
            }
        }
    }

    res.success = (res.missing == 0 && res.corrupted == 0 && res.residual == 0);
    return res;
}

}  // namespace backup
