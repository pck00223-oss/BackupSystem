// BackupRestoreTest.cpp - 端到端集成测试
// 覆盖需求文档 17.3/17.4/17.5/17.6 的核心场景：
//   全量备份、增量备份（新增/修改/删除/未变化）、恢复与 Hash 校验、取消、异常。
#include "TestFramework.h"
#include "TestUtil.h"

#include "business/BackupManager.h"
#include "business/Manifest.h"
#include "business/RestoreManager.h"
#include "app/TaskScheduler.h"
#include "core/HashCalculator.h"
#include "engine/FileSystem.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>

namespace {

using namespace backup;

struct TestEnv {
    std::wstring src;
    std::wstring target;
    std::wstring restore;

    TestEnv() : src(testutil::makeTempDir(L"src")),
                target(testutil::makeTempDir(L"tgt")),
                restore(testutil::makeTempDir(L"rst")) {
        // 准备源目录树：多级目录 + 空目录 + 若干文件
        testutil::writeFile(src + L"a.txt", "hello world");
        testutil::writeFile(src + L"b.cpp", "int main(){return 0;}\n");
        testutil::writeFile(src + L"docs\\sub\\c.txt", std::string(1000, 'x'));
        testutil::writeFile(src + L"empty.txt", "");
        FileSystem::createDirectories(src + L"emptydir");
    }
    ~TestEnv() {
        testutil::removeAll(src);
        testutil::removeAll(target);
        testutil::removeAll(restore);
    }
};

std::string hashOf(const std::wstring& p) {
    std::string h;
    HashCalculator::fileSha256(p, h);
    return h;
}

}  // namespace

TEST(Backup_Full) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    BackupResult res = BackupManager::run(cfg);
    CHECK(res.success);
    CHECK(res.backedUp == 4);  // 4 个文件（空目录不复制内容）
    CHECK(res.added == 4);

    // Manifest 与 data 结构
    CHECK(FileSystem::exists(env.target + L"\\manifest.txt"));
    CHECK(FileSystem::exists(env.target + L"\\data\\a.txt"));
    CHECK(FileSystem::exists(env.target + L"\\data\\docs\\sub\\c.txt"));

    // 数据一致性：Hash 一致
    CHECK_EQ(hashOf(env.target + L"\\data\\a.txt"), hashOf(env.src + L"a.txt"));
    CHECK_EQ(hashOf(env.target + L"\\data\\docs\\sub\\c.txt"), hashOf(env.src + L"docs\\sub\\c.txt"));

    // 历史记录
    CHECK(FileSystem::exists(env.target + L"\\history.log"));
}

TEST(Backup_Incremental_NoChange) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;
    CHECK(BackupManager::run(cfg).success);

    // 第二次：增量，无变化
    cfg.mode = BackupMode::Incremental;
    BackupResult res2 = BackupManager::run(cfg);
    CHECK(res2.success);
    CHECK(res2.backedUp == 0);  // 无文件被写入
    CHECK(res2.unchanged == 4);
}

TEST(Backup_Incremental_AddModifyDelete) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;
    CHECK(BackupManager::run(cfg).success);

    // 修改 a.txt、新增 d.txt、删除 empty.txt
    testutil::writeFile(env.src + L"a.txt", "modified content");
    testutil::writeFile(env.src + L"d.txt", "new file");
    FileSystem::deleteFile(env.src + L"empty.txt");

    cfg.mode = BackupMode::Incremental;
    BackupResult res = BackupManager::run(cfg);
    CHECK(res.success);
    CHECK(res.backedUp == 2);    // 只处理修改 1 + 新增 1
    CHECK(res.added == 1);
    CHECK(res.modified == 1);
    CHECK(res.deleted == 1);
    CHECK(res.unchanged == 2);

    // data 中内容更新
    CHECK_EQ(hashOf(env.target + L"\\data\\a.txt"), hashOf(env.src + L"a.txt"));
    CHECK(FileSystem::exists(env.target + L"\\data\\d.txt"));
}

TEST(Backup_Incremental_DeleteOnly) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    CHECK(BackupManager::run(cfg).success);

    FileSystem::deleteFile(env.src + L"b.cpp");
    cfg.mode = BackupMode::Incremental;
    BackupResult res = BackupManager::run(cfg);
    CHECK(res.success);
    CHECK(res.deleted == 1);
    // 新 Manifest 不再包含 b.cpp
    Manifest m;
    CHECK(m.loadFromFile(env.target + L"\\manifest.txt"));
    CHECK(m.find(L"b.cpp") == nullptr);
    CHECK(m.find(L"a.txt") != nullptr);
}

TEST(Restore_FullRoundTrip) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    CHECK(BackupManager::run(cfg).success);

    RestoreConfig rcfg;
    rcfg.backupRoot = env.target;
    rcfg.restorePath = env.restore;
    RestoreResult rres = RestoreManager::run(rcfg);
    CHECK(rres.success);
    CHECK(rres.restored == 4);
    CHECK(rres.verified == 4);
    CHECK(rres.failed == 0);

    // 路径、大小、内容、Hash 全部一致
    CHECK(FileSystem::exists(env.restore + L"\\a.txt"));
    CHECK(FileSystem::exists(env.restore + L"\\docs\\sub\\c.txt"));
    CHECK_EQ(hashOf(env.restore + L"\\a.txt"), hashOf(env.src + L"a.txt"));
    CHECK_EQ(hashOf(env.restore + L"\\docs\\sub\\c.txt"), hashOf(env.src + L"docs\\sub\\c.txt"));
}

TEST(Restore_ConflictPolicy) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    CHECK(BackupManager::run(cfg).success);

    // 目标目录预置一个内容不同的 a.txt
    testutil::writeFile(env.restore + L"a.txt", "conflicting");

    RestoreConfig rcfg;
    rcfg.backupRoot = env.target;
    rcfg.restorePath = env.restore;

    // 默认：不覆盖，跳过
    RestoreResult r1 = RestoreManager::run(rcfg);
    CHECK(r1.success);
    CHECK(r1.skipped >= 1);

    // 覆盖模式：内容被替换为备份内容
    rcfg.overwrite = true;
    RestoreResult r2 = RestoreManager::run(rcfg);
    CHECK(r2.success);
    CHECK_EQ(hashOf(env.restore + L"\\a.txt"), hashOf(env.src + L"a.txt"));
}

TEST(Backup_NonExistentSource) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src + L"\\does_not_exist";
    cfg.targetPath = env.target;
    BackupResult res = BackupManager::run(cfg);
    CHECK(!res.success);
    CHECK(!res.errors.empty());
}

TEST(Backup_Cancel) {
    TestEnv env;
    // 制造较大文件，取消后应停止处理
    testutil::writeFile(env.src + L"big.bin", std::string(8 * 1024 * 1024, 'z'));

    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    BackupManager::Options opts;
    opts.cancelCheck = []() { return true; };  // 一开始就取消
    BackupResult res = BackupManager::run(cfg, opts);
    CHECK(res.cancelled);
    CHECK(!res.success);
}

TEST(Backup_Filtered) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.filter.includeExtensions = {L".txt"};
    cfg.filter.maxSize = 500;  // 排除 1000 字节的 c.txt

    BackupResult res = BackupManager::run(cfg);
    CHECK(res.success);
    CHECK(res.backedUp == 2);  // a.txt + empty.txt（c.txt 超大小、b.cpp 非 .txt）
    CHECK(!FileSystem::exists(env.target + L"\\data\\b.cpp"));
}

// 回归测试：源目录 == 目标目录必须被拒绝，否则自我膨胀
TEST(Backup_SourceEqualsTarget_Rejected) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.src;  // 目标等于源
    cfg.mode = BackupMode::Full;
    BackupResult res = BackupManager::run(cfg);
    CHECK(!res.success);
    bool found = false;
    for (const auto& e : res.errors) {
        if (e.find(L"目标路径不能等于或位于源目录内") != std::wstring::npos) found = true;
    }
    CHECK(found);
}

// 回归测试：取消备份后 Manifest 必须保持旧版，不能保存部分快照
TEST(Backup_CancelPreservesOldManifest) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    // 第一次全量备份成功
    BackupResult r1 = BackupManager::run(cfg);
    CHECK(r1.success);
    const std::wstring manifestPath = env.target + L"\\manifest.txt";
    CHECK(FileSystem::exists(manifestPath));

    // 读取旧 Manifest 内容
    std::ifstream ifs(manifestPath.c_str(), std::ios::binary);
    std::string oldContent((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    // 修改源文件，增量备份时在复制阶段取消
    testutil::writeFile(env.src + L"a.txt", "modified content");
    testutil::writeFile(env.src + L"new.txt", "new file");
    cfg.mode = BackupMode::Incremental;

    // progress 在每个文件复制前调用，第一次调用后置取消标志，
    // 确保至少进入复制阶段再取消（而非扫描阶段取消）
    bool cancelNow = false;
    BackupManager::Options opts;
    opts.progress = [&](const std::wstring&) { cancelNow = true; };
    opts.cancelCheck = [&]() { return cancelNow; };
    BackupResult r2 = BackupManager::run(cfg, opts);
    CHECK(r2.cancelled);
    CHECK(!r2.success);

    // Manifest 必须保持旧版（未被覆盖）
    std::ifstream ifs2(manifestPath.c_str(), std::ios::binary);
    std::string newContent((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
    ifs2.close();
    CHECK(oldContent == newContent);
}

// 回归测试：调度器 stop() 后重新 start()，取消标志必须复位，runNow 能正常执行
TEST(Scheduler_StopStart_RunNowSucceeds) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    TaskScheduler scheduler;
    ScheduledTask task;
    task.name = L"test";
    task.config = cfg;
    task.scheduleTime = "23:59";  // 不会自动触发
    scheduler.addTask(task);

    // start -> stop -> start（模拟 stop 后重新使用调度器）
    scheduler.start();
    scheduler.stop();
    scheduler.start();

    // runNow 应能执行备份（若 cancelFlag 未复位，备份会被立刻取消，manifest 不会生成）
    scheduler.runNow(L"test");
    scheduler.stop();

    CHECK(FileSystem::exists(env.target + L"\\manifest.txt"));
    CHECK(FileSystem::exists(env.target + L"\\data\\a.txt"));
}

// 回归测试：错过计划时间只补跑一次，不会重复执行
TEST(Scheduler_MissedTime_RunsOnce) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    TaskScheduler scheduler;
    std::atomic<int> runCount(0);
    scheduler.setCallback([&](const std::wstring&, const BackupResult&) {
        runCount.fetch_add(1);
    });

    ScheduledTask task;
    task.name = L"test";
    task.config = cfg;
    task.scheduleTime = "00:00";  // 肯定已错过，启动后补跑一次
    scheduler.addTask(task);

    scheduler.start();
    // 等待补跑完成（最多 5 秒）
    for (int i = 0; i < 50 && runCount.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(runCount.load() >= 1);
    // 再等 2 秒，确认不会重复执行
    std::this_thread::sleep_for(std::chrono::seconds(2));
    scheduler.stop();

    CHECK(runCount.load() == 1);
}
