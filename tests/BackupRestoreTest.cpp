// BackupRestoreTest.cpp - 端到端集成测试
// 覆盖需求文档 17.3/17.4/17.5/17.6 的核心场景：
//   全量备份、增量备份（新增/修改/删除/未变化）、恢复与 Hash 校验、取消、异常。
#include "TestFramework.h"
#include "TestUtil.h"

#include "business/BackupManager.h"
#include "business/Manifest.h"
#include "business/RestoreManager.h"
#include "core/HashCalculator.h"
#include "engine/FileSystem.h"

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
