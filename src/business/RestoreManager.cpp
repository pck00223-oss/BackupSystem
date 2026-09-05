// RestoreManager.cpp - 数据恢复实现
// 流程（需求文档 7.2）：读取 Manifest -> 创建目录 -> 恢复文件 -> 恢复元数据 -> Hash 校验
#include "business/RestoreManager.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "business/Manifest.h"
#include "business/SnapshotManager.h"
#include "core/AesEncryptor.h"
#include "core/HashCalculator.h"
#include "core/Logger.h"
#include "core/TimeUtil.h"
#include "core/Utf.h"
#include "engine/FileCopier.h"
#include "engine/FileSystem.h"

namespace backup {

namespace {

// 解密并写入文件：读取加密文件（IV+密文），AES-256-CBC 解密后写入目标。
bool decryptAndWriteFile(const std::wstring& src, const std::wstring& dst,
                          const std::string& password, std::string& err) {
    std::ifstream ifs(src.c_str(), std::ios::binary);
    if (!ifs) { err = "cannot open encrypted file"; return false; }

    AesEncryptor aes(password);

    uint8_t iv[16] = {};
    ifs.read(reinterpret_cast<char*>(iv), 16);
    if (ifs.gcount() != 16) { err = "encrypted file is too short"; return false; }

    // 用 GetTempFileNameW 生成唯一临时文件名，避免与合法文件撞名，再原子替换
    const size_t pos = dst.find_last_of(L"\\/");
    const std::wstring dir = (pos == std::wstring::npos) ? L"." : dst.substr(0, pos);
    wchar_t tmpPath[MAX_PATH];
    if (::GetTempFileNameW(dir.c_str(), L"dec", 0, tmpPath) == 0) {
        err = "cannot create temp file (error " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    const std::wstring tmp(tmpPath);
    std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
    if (!ofs) { err = "cannot open temp file for write"; ::DeleteFileW(tmp.c_str()); return false; }

    uint8_t prev[16] = {};
    std::memcpy(prev, iv, 16);

    constexpr size_t kChunkSize = 1024 * 1024;
    std::vector<uint8_t> readBuf(kChunkSize);
    std::vector<uint8_t> pending;
    std::vector<uint8_t> plain;
    pending.reserve(kChunkSize + 32);

    while (ifs) {
        ifs.read(reinterpret_cast<char*>(readBuf.data()),
                 static_cast<std::streamsize>(readBuf.size()));
        const std::streamsize got = ifs.gcount();
        if (got > 0) {
            pending.insert(pending.end(), readBuf.begin(), readBuf.begin() + got);
        }
        // 保留最后一个完整块用于校验 PKCS7 填充，其余先解密写出
        while (pending.size() >= 32) {
            const size_t processable = pending.size() - 16;
            plain.resize(processable);
            aes.decryptCbcBlocks(pending.data(), processable / 16,
                                 plain.data(), prev);
            ofs.write(reinterpret_cast<const char*>(plain.data()),
                      static_cast<std::streamsize>(plain.size()));
            pending.erase(pending.begin(),
                          pending.begin() + static_cast<ptrdiff_t>(processable));
        }
        if (!ifs && !ifs.eof()) {
            ofs.close();
            ::DeleteFileW(tmp.c_str());
            err = "read encrypted file failed";
            return false;
        }
    }

    // 末尾应恰好剩一个完整密文块
    if (pending.size() != 16) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "encrypted data length is invalid";
        return false;
    }
    plain.resize(16);
    aes.decryptCbcBlocks(pending.data(), 1, plain.data(), prev);
    const uint8_t pad = plain[15];
    if (pad == 0 || pad > 16) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "decryption failed (wrong password or corrupted data)";
        return false;
    }
    if (!std::all_of(plain.begin() + (16 - pad), plain.end(),
                     [pad](uint8_t b) { return b == pad; })) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "decryption failed (wrong password or corrupted data)";
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(plain.data()), 16 - pad);
    ofs.flush();
    if (!ofs) { ofs.close(); ::DeleteFileW(tmp.c_str()); err = "write decrypted file failed"; return false; }
    ofs.close();

    if (!::MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        err = "atomic replace failed (error " + std::to_string(::GetLastError()) + ")";
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace

RestoreResult RestoreManager::run(const RestoreConfig& config,
                                  const CancelCheck& cancel,
                                  const Progress& progress) {
    RestoreResult res;
    res.backupRoot = config.backupRoot;
    res.restorePath = config.restorePath;
    res.startTime = formatNowWide();

    Logger& log = Logger::instance();
    log.info(L"RestoreManager", L"恢复开始");

    // 如果指定了快照时间戳，从快照目录读取 manifest 和 data；否则从根目录读取。
    std::wstring effectiveRoot = config.backupRoot;
    if (!config.snapshot.empty()) {
        const std::wstring snapDir = SnapshotManager::snapshotDir(config.backupRoot, config.snapshot);
        if (!FileSystem::exists(snapDir)) {
            res.success = false;
            res.errors.push_back(std::wstring(L"指定的快照不存在: ") + config.snapshot);
            log.error(L"RestoreManager", L"指定的快照不存在: " + config.snapshot);
            res.endTime = formatNowWide();
            return res;
        }
        effectiveRoot = snapDir;
        log.info(L"RestoreManager", L"从快照恢复: " + config.snapshot);
    }

    const std::wstring manifestPath = effectiveRoot + L"\\manifest.txt";
    Manifest manifest;
    std::string err;
    if (!manifest.loadFromFile(manifestPath, &err)) {
        res.success = false;
        res.errors.push_back(std::wstring(L"读取 Manifest 失败（备份数据无效）: ") + utf8ToWide(err));
        log.error(L"RestoreManager", L"读取 Manifest 失败: " + utf8ToWide(err));
        res.endTime = formatNowWide();
        return res;
    }

    if (!FileSystem::createDirectories(config.restorePath)) {
        res.success = false;
        res.errors.push_back(std::wstring(L"无法创建恢复目录: ") + config.restorePath);
        log.error(L"RestoreManager", L"无法创建恢复目录: " + config.restorePath);
        res.endTime = formatNowWide();
        return res;
    }

    const std::wstring dataDir = effectiveRoot + L"\\data";
    log.info(L"RestoreManager",
             L"Manifest 包含 " + std::to_wstring(manifest.entries.size()) + L" 个条目, 源=" +
                 manifest.meta.sourcePath);

    for (const auto& e : manifest.entries) {
        // 目录条目：重建空目录结构
        if (e.info.type == FileType::Directory) {
            FileSystem::createDirectories(config.restorePath + L"\\" + e.info.relativePath);
            continue;
        }
        if (e.info.type != FileType::File) continue;  // 跳过 Symlink 等特殊类型
        if (cancel && cancel()) {
            res.cancelled = true;
            res.errors.push_back(L"恢复被用户取消");
            log.warn(L"RestoreManager", L"恢复被用户取消");
            break;
        }
        if (progress) progress(e.info.relativePath);

        const std::wstring rel = e.info.relativePath;
        const std::wstring dstAbs = config.restorePath + L"\\" + rel;
        const std::wstring srcAbs = dataDir + L"\\" + e.dataPath;

        const size_t pos = dstAbs.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            FileSystem::createDirectories(dstAbs.substr(0, pos));
        }

        // ---- 冲突处理（需求文档 R05）----
        if (FileSystem::exists(dstAbs)) {
            std::string existingHash;
            const bool hashOk = HashCalculator::fileSha256(dstAbs, existingHash);
            if (hashOk && !e.info.hash.empty() && existingHash == e.info.hash) {
                ++res.skipped;  // 已存在且内容一致，跳过
                continue;
            }
            if (!config.overwrite) {
                ++res.skipped;
                // 冲突不覆盖属于非致命警告，不放进 errors（避免成功结果里出现"错误"）
                res.warnings.push_back(std::wstring(L"目标文件已存在且内容不同, 已跳过: ") + rel +
                                       L"（使用覆盖选项可替换）");
                continue;
            }
            // 覆盖模式不再先删后写：FileCopier 内部用临时文件+原子替换，
            // 复制中途失败不会丢失原有目标文件。
        }

        std::string copyErr;
        const bool encrypted = (manifest.meta.encryption == "aes256");
        bool copyOk = false;
        if (encrypted) {
            if (config.password.empty()) {
                ++res.failed;
                res.errors.push_back(std::wstring(L"备份已加密，需要 --password 才能恢复: ") + rel);
                log.error(L"RestoreManager", L"备份已加密，需要密码: " + rel);
                continue;
            }
            copyOk = decryptAndWriteFile(srcAbs, dstAbs, config.password, copyErr);
        } else {
            copyOk = FileCopier::copyFile(srcAbs, dstAbs, &copyErr, cancel);
        }
        if (!copyOk) {
            ++res.failed;
            res.errors.push_back(std::wstring(L"恢复失败: ") + rel + L" - " + utf8ToWide(copyErr));
            log.error(L"RestoreManager", L"恢复失败: " + rel + L" - " + utf8ToWide(copyErr));
            continue;
        }

        // ---- 恢复后 Hash 校验 ----
        std::string dstHash;
        if (HashCalculator::fileSha256(dstAbs, dstHash)) {
            if (!e.info.hash.empty() && dstHash != e.info.hash) {
                ++res.hashMismatch;
                res.errors.push_back(std::wstring(L"恢复后 Hash 校验不一致: ") + rel);
                log.error(L"RestoreManager", L"Hash 校验不一致: " + rel);
            } else {
                ++res.verified;
            }
        }

        // ---- 恢复元数据（修改时间）----
        if (e.info.modifiedTime != 0) {
            FileSystem::setFileTimes(dstAbs, 0, e.info.modifiedTime);
        }

        ++res.restored;
    }

    res.endTime = formatNowWide();
    res.success = (res.failed == 0 && res.hashMismatch == 0);

    log.info(L"RestoreManager",
             (res.success ? std::wstring(L"恢复完成: ") : std::wstring(L"恢复完成(有问题): ")) +
                 L"恢复 " + std::to_wstring(res.restored) + L" 个文件, 跳过 " +
                 std::to_wstring(res.skipped) + L", 失败 " + std::to_wstring(res.failed) +
                 L", 校验 " + std::to_wstring(res.verified) + L", 不一致 " +
                 std::to_wstring(res.hashMismatch));
    return res;
}

}  // namespace backup
