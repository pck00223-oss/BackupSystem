// GuiTaskStoreTest.cpp - GUI 任务持久化往返测试（GoogleTest）
#include <gtest/gtest.h>

#include "TestEnv.h"

#include "gui/TaskStore.h"

using namespace backup::gui;

// 中文任务名/路径 + 特殊字符密码：保存后加载应完全一致（密码经 DPAPI 加解密）
TEST(TaskStore, RoundTrip_ChinesePathsAndSpecialPassword) {
    testenv::LoggerGuard loggerGuard;
    if (!testenv::dpapiAvailable()) {
        GTEST_SKIP() << "DPAPI unavailable in this environment (e.g. sandbox/service account)";
    }

    testenv::TempDir dir(L"taskstore");
    const std::wstring file = dir.file(L"tasks.dat");

    BackupTask task;
    task.name = L"中文任务 / 每日备份";
    task.sourcePath = L"D:\\资料\\我的文件夹\\源目录";
    task.targetPath = L"D:\\资料\\我的文件夹\\备份目标";
    task.mode = backup::BackupMode::Incremental;
    task.encryption = "aes256";
    task.password = "P@ss 中#$%&+()[]";
    task.keepSnapshots = 7;

    ASSERT_TRUE(TaskStore::saveTo(file, {task}));
    std::vector<BackupTask> loaded = TaskStore::loadFrom(file);
    ASSERT_EQ(loaded.size(), size_t(1));

    const BackupTask& t = loaded[0];
    EXPECT_EQ(t.name, task.name);
    EXPECT_EQ(t.sourcePath, task.sourcePath);
    EXPECT_EQ(t.targetPath, task.targetPath);
    EXPECT_EQ(t.mode, backup::BackupMode::Incremental);
    EXPECT_EQ(t.encryption, std::string("aes256"));
    EXPECT_EQ(t.password, task.password);
    EXPECT_EQ(t.keepSnapshots, 7);
}

// 未加密任务：密码字段应为空，且不残留污染
TEST(TaskStore, RoundTrip_NoEncryptionClearsPassword) {
    testenv::LoggerGuard loggerGuard;
    testenv::TempDir dir(L"taskstore2");
    const std::wstring file = dir.file(L"tasks.dat");

    BackupTask task;
    task.name = L"plain";
    task.sourcePath = L"C:\\data";
    task.targetPath = L"D:\\bak";
    task.mode = backup::BackupMode::Full;
    task.encryption = "none";
    task.password = "";

    ASSERT_TRUE(TaskStore::saveTo(file, {task}));
    std::vector<BackupTask> loaded = TaskStore::loadFrom(file);
    ASSERT_EQ(loaded.size(), size_t(1));
    EXPECT_TRUE(loaded[0].password.empty());
}
