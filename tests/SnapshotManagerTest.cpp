// SnapshotManagerTest.cpp - 快照管理接口测试
#include <gtest/gtest.h>

#include "TestEnv.h"

#include "business/SnapshotManager.h"
#include "engine/FileSystem.h"

using namespace backup;

namespace {

void makeSnapshotDir(const std::wstring& target, const std::wstring& name) {
    FileSystem::createDirectories(target + L"\\snapshots\\" + name + L"\\data");
}

}  // namespace

TEST(SnapshotManager, ListParsesAndSortsNumericSuffix) {
    testenv::TempDir dir(L"snapshot_list");
    makeSnapshotDir(dir.path(), L"20260905-000000");
    makeSnapshotDir(dir.path(), L"20260905-000000-2");
    makeSnapshotDir(dir.path(), L"20260905-000000-10");
    makeSnapshotDir(dir.path(), L"20260905-000000-1");
    makeSnapshotDir(dir.path(), L"not-a-snapshot");

    const auto snaps = SnapshotManager::listSnapshots(dir.path());
    ASSERT_EQ(snaps.size(), size_t(4));
    EXPECT_EQ(snaps[0].timestamp, std::wstring(L"20260905-000000"));
    EXPECT_EQ(snaps[1].timestamp, std::wstring(L"20260905-000000-1"));
    EXPECT_EQ(snaps[2].timestamp, std::wstring(L"20260905-000000-2"));
    EXPECT_EQ(snaps[3].timestamp, std::wstring(L"20260905-000000-10"));
    EXPECT_EQ(SnapshotManager::latestSnapshot(dir.path()).timestamp,
              std::wstring(L"20260905-000000-10"));
}

TEST(SnapshotManager, CreateIsUniqueAndCreatesDataDir) {
    testenv::TempDir dir(L"snapshot_create");
    const SnapshotInfo first = SnapshotManager::createSnapshot(dir.path());
    const SnapshotInfo second = SnapshotManager::createSnapshot(dir.path());

    EXPECT_FALSE(first.timestamp.empty());
    EXPECT_NE(first.timestamp, second.timestamp);
    EXPECT_TRUE(FileSystem::isDirectory(SnapshotManager::snapshotDataDir(dir.path(), first.timestamp)));
    EXPECT_TRUE(FileSystem::isDirectory(SnapshotManager::snapshotDataDir(dir.path(), second.timestamp)));
}

TEST(SnapshotManager, CleanupKeepsNewestAndDeleteIsIdempotent) {
    testenv::TempDir dir(L"snapshot_cleanup");
    makeSnapshotDir(dir.path(), L"20260905-000000");
    makeSnapshotDir(dir.path(), L"20260905-000001");
    makeSnapshotDir(dir.path(), L"20260905-000002");

    EXPECT_EQ(SnapshotManager::cleanupOldSnapshots(dir.path(), 2), 1);
    const auto snaps = SnapshotManager::listSnapshots(dir.path());
    ASSERT_EQ(snaps.size(), size_t(2));
    EXPECT_EQ(snaps[0].timestamp, std::wstring(L"20260905-000001"));
    EXPECT_EQ(snaps[1].timestamp, std::wstring(L"20260905-000002"));

    EXPECT_TRUE(SnapshotManager::deleteSnapshot(dir.path(), L"20260905-000001"));
    EXPECT_TRUE(SnapshotManager::deleteSnapshot(dir.path(), L"20260905-000001"));  // 幂等
    EXPECT_EQ(SnapshotManager::listSnapshots(dir.path()).size(), size_t(1));

    // keep <= 0 时不做清理
    EXPECT_EQ(SnapshotManager::cleanupOldSnapshots(dir.path(), 0), 0);
}
