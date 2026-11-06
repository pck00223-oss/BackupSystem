// RestoreManager.cpp - 数据恢复实现
// 流程（需求文档 7.2）：读取 Manifest -> 创建目录 -> 恢复文件 -> 恢复元数据 -> Hash 校验
#include "business/RestoreManager.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include "business/Manifest.h"
#include "business/SnapshotManager.h"
#include "core/IEncryptionAlgorithm.h"
#include "core/PasswordUtil.h"
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
                          const std::string& algorithm, const std::string& password,
                          std::string& err) {
    std::ifstream ifs(src.c_str(), std::ios::binary);
    if (!ifs) { err = "cannot open encrypted file"; return false; }

    // 面向接口创建具体解密器（AES/3DES），块大小由算法决定。
    auto cipher = createEncryptionAlgorithm(algorithm, password);
    if (!cipher) { err = "unknown encryption algorithm: " + algorithm; return false; }
    const size_t bs = cipher->blockSize();

    std::vector<uint8_t> iv(bs);
    ifs.read(reinterpret_cast<char*>(iv.data()), static_cast<std::streamsize>(bs));
    if (ifs.gcount() != static_cast<std::streamsize>(bs)) {
        err = "encrypted file is too short";
        return false;
    }

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

    std::vector<uint8_t> prev(bs);
    std::memcpy(prev.data(), iv.data(), bs);

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
        while (pending.size() >= bs * 2) {
            const size_t processable = pending.size() - bs;
            plain.resize(processable);
            cipher->decryptCbcBlocks(pending.data(), processable / bs,
                                     plain.data(), prev.data());
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
    if (pending.size() != bs) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "encrypted data length is invalid";
        return false;
    }
    plain.resize(bs);
    cipher->decryptCbcBlocks(pending.data(), 1, plain.data(), prev.data());
    const uint8_t pad = plain[bs - 1];
    if (pad == 0 || pad > bs) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "decryption failed (wrong password or corrupted data)";
        return false;
    }
    if (!std::all_of(plain.begin() + (bs - pad), plain.end(),
                     [pad](uint8_t b) { return b == pad; })) {
        ofs.close();
        ::DeleteFileW(tmp.c_str());
        err = "decryption failed (wrong password or corrupted data)";
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(plain.data()),
              static_cast<std::streamsize>(bs - pad));
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

    // ---- 密码快速前置校验（在解密任何文件之前，加快校验速度）----
    // 加密仓库：先核对密码验证标记，密码缺失/错误立即返回，不逐个文件解密，避免浪费时间。
    const bool repoEncrypted =
        !manifest.meta.encryption.empty() && manifest.meta.encryption != "none";
    if (repoEncrypted) {
        if (config.password.empty()) {
            res.success = false;
            res.errors.push_back(L"备份已加密，需要 --password 才能恢复（密码前置校验未通过，未解密任何文件）");
            log.error(L"RestoreManager", L"密码缺失，前置校验失败");
            res.endTime = formatNowWide();
            return res;
        }
        if (!manifest.meta.passwordVerifier.empty() &&
            manifest.meta.passwordVerifier != computePasswordVerifier(config.password)) {
            res.success = false;
            res.errors.push_back(L"密码错误：密码验证标记不匹配（前置校验未通过，未解密任何文件）");
            log.error(L"RestoreManager", L"密码错误，前置校验失败");
            res.endTime = formatNowWide();
            return res;
        }
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

    // 符号链接目标是否为目录：尽量解析，无法判断时默认文件。
    const auto targetIsDirectory = [&](const std::wstring& linkAbs,
                                       const std::wstring& target) -> bool {
        if (target.empty()) return false;
        if (target.back() == L'\\' || target.back() == L'/') return true;
        std::wstring resolved = target;
        if (target.size() < 2 || target[1] != L':') {
            // 相对路径：相对于链接所在目录
            const size_t pos = linkAbs.find_last_of(L"\\/");
            const std::wstring base = (pos == std::wstring::npos)
                                          ? config.restorePath
                                          : linkAbs.substr(0, pos);
            resolved = base + L"\\" + target;
        }
        if (FileSystem::exists(resolved)) return FileSystem::isDirectory(resolved);
        return false;
    };

    // 硬链接条目延后到第二遍处理：等组内首个文件恢复后再 CreateHardLink。
    std::vector<const Manifest::Entry*> pendingHardLinks;

    for (const auto& e : manifest.entries) {
        // 硬链接非首个：延后到第二遍处理
        if (!e.info.hardLinkTo.empty()) {
            pendingHardLinks.push_back(&e);
            continue;
        }
        // 目录条目：重建空目录结构
        if (e.info.type == FileType::Directory) {
            FileSystem::createDirectories(config.restorePath + L"\\" + e.info.relativePath);
            continue;
        }
        // 符号链接：重建链接本身（不复制内容）
        if (e.info.type == FileType::Symlink) {
            const std::wstring dstAbs = config.restorePath + L"\\" + e.info.relativePath;
            const size_t pos = dstAbs.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                FileSystem::createDirectories(dstAbs.substr(0, pos));
            }
            bool isDir = targetIsDirectory(dstAbs, e.info.symlinkTarget);
            std::string linkErr;
            if (!FileSystem::createSymlink(dstAbs, e.info.symlinkTarget, isDir, &linkErr)) {
                // 目标类型判断可能错误，尝试另一种类型
                if (!FileSystem::createSymlink(dstAbs, e.info.symlinkTarget, !isDir, &linkErr)) {
                    ++res.failed;
                    res.errors.push_back(std::wstring(L"重建符号链接失败: ") + e.info.relativePath +
                                         L"（可能需要开发者模式/管理员）");
                    log.error(L"RestoreManager", L"重建符号链接失败: " + e.info.relativePath);
                }
            }
            continue;
        }
        if (e.info.type != FileType::File) continue;  // 跳过未知特殊类型
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
        const bool encrypted =
            manifest.meta.encryption == "aes256" || manifest.meta.encryption == "3des";
        bool copyOk = false;
        if (encrypted) {
            if (config.password.empty()) {
                ++res.failed;
                res.errors.push_back(std::wstring(L"备份已加密，需要 --password 才能恢复: ") + rel);
                log.error(L"RestoreManager", L"备份已加密，需要密码: " + rel);
                continue;
            }
            copyOk = decryptAndWriteFile(srcAbs, dstAbs, manifest.meta.encryption,
                                         config.password, copyErr);
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

    // ---- 第二遍：建立硬链接 ----
    for (const auto* e : pendingHardLinks) {
        const std::wstring linkAbs = config.restorePath + L"\\" + e->info.relativePath;
        std::wstring targetRel = e->info.hardLinkTo;
        const std::wstring targetAbs = config.restorePath + L"\\" + targetRel;

        // 目标尚未恢复（失败/跳过）时，回退为复制，保证文件存在
        if (!FileSystem::exists(targetAbs)) {
            const std::wstring srcAbs = dataDir + L"\\" + e->dataPath;
            const size_t pos = linkAbs.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                FileSystem::createDirectories(linkAbs.substr(0, pos));
            }
            std::string copyErr;
            if (!FileCopier::copyFile(srcAbs, linkAbs, &copyErr, cancel)) {
                ++res.failed;
                res.errors.push_back(std::wstring(L"硬链接目标缺失且复制失败: ") + e->info.relativePath);
                log.error(L"RestoreManager", L"硬链接目标缺失且复制失败: " + e->info.relativePath);
            }
            continue;
        }

        std::string hlErr;
        if (!FileSystem::createHardLink(linkAbs, targetAbs, &hlErr)) {
            // 硬链接失败（跨卷/FAT/权限）：回退复制目标文件
            std::string copyErr;
            if (!FileCopier::copyFile(targetAbs, linkAbs, &copyErr, cancel)) {
                ++res.failed;
                res.errors.push_back(std::wstring(L"重建硬链接失败: ") + e->info.relativePath);
                log.error(L"RestoreManager", L"重建硬链接失败: " + e->info.relativePath);
            }
        }
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
