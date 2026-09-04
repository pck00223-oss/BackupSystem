// VerifyManager.cpp - 备份完整性校验实现
#include "business/VerifyManager.h"

#include "business/Manifest.h"
#include "core/HashCalculator.h"
#include "core/ResidualUtil.h"
#include "core/Utf.h"
#include "engine/FileScanner.h"
#include "engine/FileSystem.h"

namespace backup {

VerifyResult VerifyManager::run(const std::wstring& backupRoot, const Options& opts) {
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
            ++res.missing;
            res.errors.push_back(std::wstring(L"[缺失] ") + e.info.relativePath);
            continue;
        }

        // 2b. 检查大小是否一致（快速失败，避免读大文件后才发现）
        uint64_t actualSize = 0;
        {
            FileInfo fi;
            if (FileSystem::getFileInfo(dataPath, fi)) {
                actualSize = fi.size;
            }
        }
        if (actualSize != e.info.size) {
            ++res.corrupted;
            res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath +
                                  L" (大小不符: 期望 " + std::to_wstring(e.info.size) +
                                  L", 实际 " + std::to_wstring(actualSize) + L")");
            continue;
        }

        // 2c. 计算 Hash 并比较
        std::string actualHash;
        if (!HashCalculator::fileSha256(dataPath, actualHash)) {
            ++res.corrupted;
            res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath + L" (无法计算 Hash)");
            continue;
        }
        if (actualHash != e.info.hash) {
            ++res.corrupted;
            res.errors.push_back(std::wstring(L"[损坏] ") + e.info.relativePath +
                                  L" (Hash 不一致: 期望 " + utf8ToWide(e.info.hash.substr(0, 16)) +
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
