// VerifySnapshotTest.cpp - verify 对快照的逐份校验（含加密快照）
#include <gtest/gtest.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "business/BackupManager.h"
#include "business/SnapshotManager.h"
#include "business/VerifyManager.h"
#include "engine/FileSystem.h"

using namespace backup;

TEST(VerifySnapshot, EncryptedSnapshotCorruptionDetectedPerSnapshot) {
    testenv::TempDir root(L"verify_snapshot");
    const std::wstring src = root.file(L"src\\");
    const std::wstring tgt = root.file(L"tgt\\");
    FileSystem::createDirectories(src);
    FileSystem::createDirectories(tgt);

    testutil::writeFile(src + L"a.txt", "version-one");

    BackupConfig cfg;
    cfg.sourcePath = src;
    cfg.targetPath = tgt;
    cfg.mode = BackupMode::Full;
    cfg.keepSnapshots = 2;
    cfg.encryption = "aes256";
    cfg.password = "snapshot-password";
    ASSERT_TRUE(BackupManager::run(cfg).success);

    testutil::writeFile(src + L"a.txt", "version-two");
    cfg.mode = BackupMode::Incremental;
    ASSERT_TRUE(BackupManager::run(cfg).success);

    const std::vector<SnapshotInfo> snaps = SnapshotManager::listSnapshots(tgt);
    ASSERT_EQ(snaps.size(), size_t(2));

    // 两份快照默认都完整（加密条目用密文 Hash 校验）
    EXPECT_TRUE(VerifyManager::run(snaps[0].path).success);
    EXPECT_TRUE(VerifyManager::run(snaps[1].path).success);

    // 破坏最新快照中的密文（先删除以断开硬链接，避免连带污染根目录/旧快照）
    const std::wstring newestData = snaps[1].path + L"\\data\\a.txt";
    ASSERT_TRUE(FileSystem::deleteFile(newestData));
    testutil::writeFile(newestData, "tampered-ciphertext");

    const VerifyResult bad = VerifyManager::run(snaps[1].path);
    EXPECT_FALSE(bad.success);
    EXPECT_GE(bad.corrupted, uint64_t(1));
    EXPECT_EQ(bad.missing, uint64_t(0));

    // 旧快照不受影响
    EXPECT_TRUE(VerifyManager::run(snaps[0].path).success);
}
