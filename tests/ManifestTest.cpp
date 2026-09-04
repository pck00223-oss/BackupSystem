// ManifestTest.cpp - Manifest 序列化与查找测试
#include "TestFramework.h"
#include "TestUtil.h"

#include "business/Manifest.h"
#include "core/Utf.h"

using namespace backup;

TEST(Manifest_RoundTrip) {
    const std::wstring dir = testutil::makeTempDir(L"manifest");
    const std::wstring file = dir + L"manifest.txt";

    Manifest m;
    m.meta.backupId = "20260904-200001";
    m.meta.sourcePath = L"C:\\Data";
    m.meta.created = "2026-09-04 20:00:01";
    m.meta.backupType = "incremental";

    Manifest::Entry e1;
    e1.info.relativePath = L"dir\\a.txt";
    e1.info.name = L"a.txt";
    e1.info.size = 123;
    e1.info.modifiedTime = 1720000000;
    e1.info.type = FileType::File;
    e1.info.hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    e1.info.hashed = true;
    e1.dataPath = L"dir\\a.txt";
    m.entries.push_back(e1);

    Manifest::Entry e2;
    e2.info.relativePath = L"中文目录\\数据.txt";  // 中文路径
    e2.info.name = L"数据.txt";
    e2.info.size = 42;
    e2.info.modifiedTime = 1720000100;
    e2.info.type = FileType::File;
    e2.dataPath = L"中文目录\\数据.txt";
    m.entries.push_back(e2);
    m.meta.fileCount = 2;
    m.rebuildIndex();

    std::string err;
    CHECK(m.saveToFile(file, &err));
    CHECK(err.empty());

    Manifest loaded;
    CHECK(loaded.loadFromFile(file, &err));
    CHECK(err.empty());
    CHECK_EQ(loaded.entries.size(), size_t(2));
    CHECK_EQ(loaded.meta.backupId, "20260904-200001");
    CHECK_EQ(loaded.meta.sourcePath, L"C:\\Data");
    CHECK_EQ(loaded.meta.backupType, "incremental");

    // 顺序与字段一致性
    CHECK_EQ(loaded.entries[0].info.relativePath, L"dir\\a.txt");
    CHECK_EQ(loaded.entries[0].info.size, uint64_t(123));
    CHECK_EQ(loaded.entries[0].info.modifiedTime, uint64_t(1720000000));
    CHECK_EQ(loaded.entries[0].info.hash, e1.info.hash);
    CHECK_EQ(loaded.entries[1].info.relativePath, L"中文目录\\数据.txt");
    CHECK_EQ(loaded.entries[1].dataPath, L"中文目录\\数据.txt");

    // find
    const Manifest::Entry* found = loaded.find(L"中文目录\\数据.txt");
    CHECK(found != nullptr);
    CHECK(found->info.size == 42);
    CHECK(loaded.find(L"不存在.txt") == nullptr);

    testutil::removeAll(dir);
}

TEST(Manifest_FileCount) {
    const std::wstring dir = testutil::makeTempDir(L"manifest2");
    const std::wstring file = dir + L"m.txt";
    Manifest m;
    m.meta.backupId = "x";
    for (int i = 0; i < 100; ++i) {
        Manifest::Entry e;
        e.info.relativePath = L"f" + std::to_wstring(i) + L".txt";
        e.info.type = FileType::File;
        e.dataPath = e.info.relativePath;
        m.entries.push_back(e);
    }
    m.meta.fileCount = 100;
    std::string err;
    CHECK(m.saveToFile(file, &err));
    Manifest loaded;
    CHECK(loaded.loadFromFile(file, &err));
    CHECK_EQ(loaded.entries.size(), size_t(100));
    testutil::removeAll(dir);
}
