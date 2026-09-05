// GuiTaskStoreTest.cpp - GUI 任务持久化往返测试
#include "TestFramework.h"
#include "TestUtil.h"

#include "gui/TaskStore.h"

using namespace backup::gui;

TEST(TaskStore_RoundTrip_ChinesePathsAndSpecialPassword) {
    const std::wstring dir = testutil::makeTempDir(L"taskstore");
    const std::wstring file = dir + L"tasks.dat";

    BackupTask task;
    task.name = L"中文任务 / 每日备份";
    task.sourcePath = L"D:\\资料\\我的文件夹\\源目录";
    task.targetPath = L"D:\\资料\\我的文件夹\\备份目标";
    task.mode = backup::BackupMode::Incremental;
    task.encryption = "aes256";
    task.password = std::string("P@ss 中#$%&+()[]") ;
    task.keepSnapshots = 7;

    CHECK(TaskStore::saveTo(file, {task}));
    std::vector<BackupTask> loaded = TaskStore::loadFrom(file);
    CHECK_EQ(loaded.size(), size_t(1));

    const BackupTask& t = loaded[0];
    CHECK_EQ(t.name, task.name);
    CHECK_EQ(t.sourcePath, task.sourcePath);
    CHECK_EQ(t.targetPath, task.targetPath);
    CHECK(t.mode == backup::BackupMode::Incremental);
    CHECK_EQ(t.encryption, std::string("aes256"));
    CHECK_EQ(t.password, task.password);  // DPAPI 加解密后应与原文一致
    CHECK_EQ(t.keepSnapshots, 7);

    testutil::removeAll(dir);
}

TEST(TaskStore_RoundTrip_NoEncryptionClearsPassword) {
    const std::wstring dir = testutil::makeTempDir(L"taskstore2");
    const std::wstring file = dir + L"tasks.dat";

    BackupTask task;
    task.name = L"plain";
    task.sourcePath = L"C:\\data";
    task.targetPath = L"D:\\bak";
    task.mode = backup::BackupMode::Full;
    task.encryption = "none";
    task.password = "";

    CHECK(TaskStore::saveTo(file, {task}));
    std::vector<BackupTask> loaded = TaskStore::loadFrom(file);
    CHECK_EQ(loaded.size(), size_t(1));
    CHECK(loaded[0].password.empty());

    testutil::removeAll(dir);
}
