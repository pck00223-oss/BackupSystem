// VerifyManager.cpp - 备份完整性校验实现
#include "business/VerifyManager.h"

#include "business/Manifest.h"
#include "core/HashCalculator.h"
#include "core/Utf.h"
#include "engine/FileSystem.h"

namespace backup {

VerifyResult VerifyManager::run(const std::wstring& backupRoot) {
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

    // 2. 逐条校验
    for (const auto& e : manifest.entries) {
        // 目录和符号链接不做文件级校验
        if (e.info.type != FileType::File) {
            ++res.skipped;
            continue;
        }
        ++res.total;

        const std::wstring dataPath = dataDir + L"\\" + e.info.relativePath;

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

    res.success = (res.missing == 0 && res.corrupted == 0);
    return res;
}

}  // namespace backup
