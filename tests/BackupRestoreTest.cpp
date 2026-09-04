// BackupRestoreTest.cpp - 端到端集成测试
// 覆盖需求文档 17.3/17.4/17.5/17.6 的核心场景：
//   全量备份、增量备份（新增/修改/删除/未变化）、恢复与 Hash 校验、取消、异常。
#include "TestFramework.h"
#include "TestUtil.h"

#include "business/BackupManager.h"
#include "business/Manifest.h"
#include "business/RestoreManager.h"
#include "business/VerifyManager.h"
#include "app/TaskScheduler.h"
#include "core/HashCalculator.h"
#include "core/TimeUtil.h"
#include "engine/FileScanner.h"
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

TEST(Verify_HealthyBackup) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    CHECK(BackupManager::run(cfg).success);

    VerifyResult res = VerifyManager::run(env.target);
    CHECK(res.success);
    CHECK_EQ(res.total, uint64_t(4));  // a.txt / b.cpp / docs\sub\c.txt / empty.txt
    CHECK_EQ(res.passed, uint64_t(4));
    CHECK_EQ(res.missing, uint64_t(0));
    CHECK_EQ(res.corrupted, uint64_t(0));
}

TEST(Verify_MissingFile) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    CHECK(BackupManager::run(cfg).success);

    CHECK(FileSystem::deleteFile(env.target + L"\\data\\a.txt"));
    VerifyResult res = VerifyManager::run(env.target);
    CHECK(!res.success);
    CHECK_EQ(res.missing, uint64_t(1));
    CHECK_EQ(res.passed, uint64_t(3));
    CHECK_EQ(res.corrupted, uint64_t(0));
}

TEST(Verify_CorruptedFile) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    CHECK(BackupManager::run(cfg).success);

    // 同长度改写内容，确保走 Hash 校验而不是大小检查
    testutil::writeFile(env.target + L"\\data\\a.txt", "HELLO WORLD");
    VerifyResult res = VerifyManager::run(env.target);
    CHECK(!res.success);
    CHECK_EQ(res.corrupted, uint64_t(1));
    CHECK_EQ(res.passed, uint64_t(3));
    CHECK_EQ(res.missing, uint64_t(0));
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

// 回归测试：目标路径用 .. 语法“看起来在源之外、实际解析到源内部”也必须被拒绝
TEST(Backup_SourceDotDotBypass_Rejected) {
    TestEnv env;
    // 源为 docs\sub；构造 docs\subx\..\sub\tgt —— 字面上不在源前缀下，
    // 但 GetFullPathNameW 解析后落在源目录内部。
    const std::wstring src = env.src + L"docs\\sub";
    CHECK(FileSystem::createDirectories(env.src + L"docs\\subx"));

    BackupConfig cfg;
    cfg.sourcePath = src;
    cfg.targetPath = env.src + L"docs\\subx\\..\\sub\\tgt";
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

    // 修改源文件并新增两个文件，增量备份时在复制阶段取消
    testutil::writeFile(env.src + L"a.txt", "modified content");
    testutil::writeFile(env.src + L"new1.txt", "new file 1");
    testutil::writeFile(env.src + L"new2.txt", "new file 2");
    cfg.mode = BackupMode::Incremental;

    // progress 在扫描阶段也会被调用（每个扫描条目一次），因此先统计扫描条目数，
    // 只在“复制阶段第 2 个文件”开始前取消：保证至少 1 个文件已复制成功，
    // 且取消发生在复制循环内（而非扫描后立即返回）。
    std::vector<FileInfo> preview;
    std::vector<std::wstring> scanErr;
    CHECK(FileScanner::scan(env.src, preview, scanErr));
    const size_t scanCallCount = preview.size();

    bool cancelNow = false;
    int progressCalls = 0;
    BackupManager::Options opts;
    opts.progress = [&](const std::wstring&) {
        ++progressCalls;
        if (static_cast<size_t>(progressCalls) == scanCallCount + 2) cancelNow = true;
    };
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

// 回归测试：文件与同名目录互换（file <-> directory）时，
// 增量备份应更新 Manifest 类型并在 data/ 中完成物理替换。
TEST(Backup_Incremental_TypeChangeFileDir) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;
    CHECK(BackupManager::run(cfg).success);

    // a.txt 原本是文件 -> 改为同名目录并放入子文件
    testutil::removeAll(env.src + L"a.txt");
    CHECK(FileSystem::createDirectories(env.src + L"a.txt"));
    testutil::writeFile(env.src + L"a.txt\\inner.txt", "inner content");

    // emptydir 原本是目录 -> 改为同名文件
    testutil::removeAll(env.src + L"emptydir");
    testutil::writeFile(env.src + L"emptydir", "now a file");

    cfg.mode = BackupMode::Incremental;
    BackupResult res = BackupManager::run(cfg);
    CHECK(res.success);
    CHECK(res.failed == 0);

    // Manifest 中的类型已更新
    Manifest m;
    CHECK(m.loadFromFile(env.target + L"\\manifest.txt"));
    const Manifest::Entry* eDir = m.find(L"a.txt");
    CHECK(eDir != nullptr);
    CHECK(eDir->info.type == FileType::Directory);
    const Manifest::Entry* eFile = m.find(L"emptydir");
    CHECK(eFile != nullptr);
    CHECK(eFile->info.type == FileType::File);

    // data/ 中物理形态正确
    CHECK(FileSystem::isDirectory(env.target + L"\\data\\a.txt"));
    CHECK(FileSystem::exists(env.target + L"\\data\\emptydir"));
    CHECK(!FileSystem::isDirectory(env.target + L"\\data\\emptydir"));
    CHECK_EQ(hashOf(env.target + L"\\data\\emptydir"), hashOf(env.src + L"emptydir"));

    // data/ 根下不应遗留任何 .baktmp.old 旁路数据
    std::vector<std::pair<std::wstring, FileType>> dataRoot;
    CHECK(FileSystem::listDirectory(env.target + L"\\data", dataRoot));
    for (const auto& entry : dataRoot) {
        CHECK(entry.first.find(L".baktmp.old") == std::wstring::npos);
    }

    // 恢复后结构一致
    RestoreConfig rcfg;
    rcfg.backupRoot = env.target;
    rcfg.restorePath = env.restore;
    RestoreResult rres = RestoreManager::run(rcfg);
    CHECK(rres.success);
    CHECK(rres.failed == 0);
    CHECK(FileSystem::exists(env.restore + L"\\a.txt\\inner.txt"));
    CHECK_EQ(hashOf(env.restore + L"\\a.txt\\inner.txt"), hashOf(env.src + L"a.txt\\inner.txt"));
    CHECK(FileSystem::exists(env.restore + L"\\emptydir"));
    CHECK(!FileSystem::isDirectory(env.restore + L"\\emptydir"));
    CHECK_EQ(hashOf(env.restore + L"\\emptydir"), hashOf(env.src + L"emptydir"));
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
    // 使用当前时刻 HH:MM 作为计划时间：启动时必然“已错过”（或正好到点），
    // 补跑一次后当天不会再触发，且不受跨午夜/固定时间点影响。
    task.scheduleTime = currentHHMM();
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

// 回归测试：用户的正常 .baktmp 文件不被误判为残留、不被误删
TEST(CrashRecovery_LegalBaktmpFile_NotDeleted) {
    TestEnv env;
    // 源目录放一个以 .baktmp 结尾的正常文件
    testutil::writeFile(env.src + L"normal.baktmp", "this is a normal file");

    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    // 第一次全量备份
    BackupResult r1 = BackupManager::run(cfg);
    CHECK(r1.success);
    CHECK(FileSystem::exists(env.target + L"\\data\\normal.baktmp"));

    // verify 不应把合法文件报为残留
    VerifyResult vr = VerifyManager::run(env.target);
    CHECK(vr.residual == 0);
    CHECK(vr.success);

    // 第二次增量备份（触发 recoverResidualData），合法文件不应被删
    cfg.mode = BackupMode::Incremental;
    BackupResult r2 = BackupManager::run(cfg);
    CHECK(r2.success);
    CHECK(FileSystem::exists(env.target + L"\\data\\normal.baktmp"));
}

// 回归测试：.baktmp.old 残留且原路径不存在 → 崩溃恢复应还原旧数据
TEST(CrashRecovery_OldResidual_Restored) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    // 全量备份 a.txt
    BackupResult r1 = BackupManager::run(cfg);
    CHECK(r1.success);
    CHECK(FileSystem::exists(env.target + L"\\data\\a.txt"));

    // 模拟崩溃：把 data\a.txt 移到 data\a.txt.baktmp.old（原路径不存在）
    const std::wstring dataFile = env.target + L"\\data\\a.txt";
    const std::wstring oldFile = env.target + L"\\data\\a.txt.baktmp.old";
    CHECK(FileSystem::movePath(dataFile, oldFile));
    CHECK(!FileSystem::exists(dataFile));
    CHECK(FileSystem::exists(oldFile));

    // 运行增量备份（触发崩溃恢复）
    cfg.mode = BackupMode::Incremental;
    BackupResult r2 = BackupManager::run(cfg);
    CHECK(r2.success);

    // 旧数据应被还原，残留应被清理
    CHECK(FileSystem::exists(dataFile));
    CHECK(!FileSystem::exists(oldFile));
}

// 回归测试：.baktmp.old 残留且原路径已存在（类型一致）→ 应删除旧数据
TEST(CrashRecovery_OldResidual_Committed_Cleaned) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;

    // 全量备份
    BackupResult r1 = BackupManager::run(cfg);
    CHECK(r1.success);

    // 模拟已提交状态：data\a.txt 存在，同时创建 a.txt.baktmp.old（未清理的旧数据）
    const std::wstring oldFile = env.target + L"\\data\\a.txt.baktmp.old";
    testutil::writeFile(oldFile, "old stale data");
    CHECK(FileSystem::exists(oldFile));

    // 运行增量备份（触发崩溃恢复）
    cfg.mode = BackupMode::Incremental;
    BackupResult r2 = BackupManager::run(cfg);
    CHECK(r2.success);

    // 原路径存在且类型与 Manifest 一致 → 旧数据应被清理
    CHECK(!FileSystem::exists(oldFile));
    CHECK(FileSystem::exists(env.target + L"\\data\\a.txt"));
}

// 回归测试：verify 取消回调能中止校验
TEST(Verify_CancelCheck_Aborts) {
    TestEnv env;
    BackupConfig cfg;
    cfg.sourcePath = env.src;
    cfg.targetPath = env.target;
    cfg.mode = BackupMode::Full;
    BackupResult r1 = BackupManager::run(cfg);
    CHECK(r1.success);

    // verify 时 cancelCheck 始终返回 true
    VerifyManager::Options vopts;
    vopts.cancelCheck = []() { return true; };
    VerifyResult vr = VerifyManager::run(env.target, vopts);
    CHECK(!vr.success);
    bool foundCancel = false;
    for (const auto& e : vr.errors) {
        if (e.find(L"取消") != std::wstring::npos) foundCancel = true;
    }
    CHECK(foundCancel);
}
