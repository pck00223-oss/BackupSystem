// FileComparatorTest.cpp - 增量变更检测测试
// 覆盖需求文档 17.4 增量备份测试表 I01-I09 的核心判定逻辑。
#include "TestFramework.h"

#include <algorithm>

#include "business/FileComparator.h"
#include "business/Manifest.h"

namespace {

using namespace backup;

FileInfo makeFile(const std::wstring& path, uint64_t size, uint64_t mtime,
                  const std::string& hash = "") {
    FileInfo f;
    f.relativePath = path;
    f.name = path.substr(path.find_last_of(L"\\/") + 1);
    f.size = size;
    f.modifiedTime = mtime;
    f.type = FileType::File;
    f.hash = hash;
    f.hashed = !hash.empty();
    return f;
}

FileInfo makeDir(const std::wstring& path, uint64_t mtime) {
    FileInfo f;
    f.relativePath = path;
    f.name = path.substr(path.find_last_of(L"\\/") + 1);
    f.size = 0;
    f.modifiedTime = mtime;
    f.type = FileType::Directory;
    return f;
}

Manifest buildPrevious(const std::vector<FileInfo>& files) {
    Manifest m;
    m.meta.backupId = "prev";
    for (const auto& f : files) {
        Manifest::Entry e;
        e.info = f;
        e.dataPath = f.relativePath;
        m.entries.push_back(e);
    }
    m.rebuildIndex();
    return m;
}

// HashProvider 假实现：hash 字段以 "hash:" 开头时，直接使用完整假 hash 值
// （与 Manifest 中保存的 hash 字符串保持一致）。
bool fakeHash(const FileInfo& info, std::string& outHash) {
    if (info.hash.size() > 5 && info.hash.substr(0, 5) == "hash:") {
        outHash = info.hash;
        return true;
    }
    return false;
}

FileChangeType changeOf(const std::vector<ChangeRecord>& records, const std::wstring& path) {
    const auto it = std::find_if(records.begin(), records.end(),
                                 [&path](const ChangeRecord& r) { return r.info.relativePath == path; });
    return (it != records.end()) ? it->change : FileChangeType::Unchanged;
}

}  // namespace

TEST(Comparator_Unchanged) {
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:same")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 100, 1000, "hash:same")}, prev, fakeHash);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Unchanged);
}

TEST(Comparator_Added) {
    auto prev = buildPrevious({});
    auto r = FileComparator::compare({makeFile(L"new.txt", 10, 1)}, prev, fakeHash);
    CHECK_EQ(changeOf(r, L"new.txt"), FileChangeType::Added);
}

TEST(Comparator_ModifiedBySize) {
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:old")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 200, 1000, "hash:new")}, prev, fakeHash);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Modified);
}

TEST(Comparator_ModifiedByHash) {
    // 大小相同、时间变化、Hash 不同 -> Modified
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:old")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 100, 2000, "hash:new")}, prev, fakeHash);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Modified);
}

TEST(Comparator_HashSameMeansUnchanged) {
    // 大小相同、时间变化、Hash 相同 -> Unchanged（避免误报）
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:same")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 100, 2000, "hash:same")}, prev, fakeHash);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Unchanged);
}

TEST(Comparator_Deleted) {
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:x"),
                               makeFile(L"b.txt", 50, 900, "hash:y")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 100, 1000, "hash:x")}, prev, fakeHash);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Unchanged);
    CHECK_EQ(changeOf(r, L"b.txt"), FileChangeType::Deleted);
}

TEST(Comparator_Mixed) {
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:x"),
                               makeFile(L"b.txt", 100, 1000, "hash:y"),
                               makeFile(L"c.txt", 100, 1000, "hash:z")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 100, 1000, "hash:x"),        // 未变化
                        makeFile(L"b.txt", 100, 2000, "hash:changed"),  // 修改
                        makeFile(L"d.txt", 1, 1)},                      // 新增
                       prev, fakeHash);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Unchanged);
    CHECK_EQ(changeOf(r, L"b.txt"), FileChangeType::Modified);
    CHECK_EQ(changeOf(r, L"c.txt"), FileChangeType::Deleted);
    CHECK_EQ(changeOf(r, L"d.txt"), FileChangeType::Added);
}

TEST(Comparator_NoHashProviderConservative) {
    // 无法计算 Hash 时，时间变化的文件保守视为 Modified
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "")});
    auto r = FileComparator::compare({makeFile(L"a.txt", 100, 2000, "")}, prev, nullptr);
    CHECK_EQ(changeOf(r, L"a.txt"), FileChangeType::Modified);
}

TEST(Comparator_TypeChangeFileToDirectory) {
    // 文件 -> 同名目录：当前目录 Added，旧文件 Deleted
    auto prev = buildPrevious({makeFile(L"a.txt", 100, 1000, "hash:old")});
    auto r = FileComparator::compare({makeDir(L"a.txt", 2000)}, prev, fakeHash);

    int addedDir = 0;
    int deletedFile = 0;
    for (const auto& c : r) {
        if (c.info.relativePath != L"a.txt") continue;
        if (c.info.type == FileType::Directory && c.change == FileChangeType::Added) ++addedDir;
        if (c.info.type == FileType::File && c.change == FileChangeType::Deleted) ++deletedFile;
    }
    CHECK_EQ(addedDir, 1);
    CHECK_EQ(deletedFile, 1);
}

TEST(Comparator_TypeChangeDirectoryToFile) {
    // 目录 -> 同名文件：当前文件 Added，旧目录 Deleted
    auto prev = buildPrevious({makeDir(L"a.txt", 1000)});
    auto r = FileComparator::compare({makeFile(L"a.txt", 42, 2000, "hash:new")}, prev, fakeHash);

    int addedFile = 0;
    int deletedDir = 0;
    for (const auto& c : r) {
        if (c.info.relativePath != L"a.txt") continue;
        if (c.info.type == FileType::File && c.change == FileChangeType::Added) ++addedFile;
        if (c.info.type == FileType::Directory && c.change == FileChangeType::Deleted) ++deletedDir;
    }
    CHECK_EQ(addedFile, 1);
    CHECK_EQ(deletedDir, 1);
}
