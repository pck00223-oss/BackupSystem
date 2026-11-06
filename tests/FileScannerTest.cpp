// FileScannerTest.cpp - 目录扫描接口测试
#include <gtest/gtest.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "core/Utf.h"
#include "engine/FileScanner.h"
#include "engine/FileSystem.h"

using namespace backup;

TEST(FileScanner, MissingRootReturnsFalse) {
    testenv::TempDir dir(L"scanner_missing");
    std::vector<FileInfo> files;
    std::vector<std::wstring> errors;
    EXPECT_FALSE(FileScanner::scan(dir.file(L"nope"), files, errors));
    EXPECT_FALSE(errors.empty());
}

TEST(FileScanner, ScansSortedTreeWithDirectories) {
    testenv::TempDir dir(L"scanner");
    testutil::writeFile(dir.file(L"b.txt"), "b");
    testutil::writeFile(dir.file(L"a.txt"), "a");
    testutil::writeFile(dir.file(L"sub\\c.txt"), "c");
    FileSystem::createDirectories(dir.file(L"emptydir"));

    std::vector<FileInfo> files;
    std::vector<std::wstring> errors;
    int progressCalls = 0;
    ASSERT_TRUE(FileScanner::scan(dir.path(), files, errors, nullptr,
                                  [&progressCalls](const std::wstring&) { ++progressCalls; }));
    EXPECT_TRUE(errors.empty());
    EXPECT_GT(progressCalls, 0);

    bool hasA = false, hasB = false, hasC = false, hasSub = false, hasEmpty = false;
    for (const auto& f : files) {
        if (f.relativePath == L"a.txt") hasA = true;
        if (f.relativePath == L"b.txt") hasB = true;
        if (f.relativePath == L"sub\\c.txt") hasC = true;
        if (f.relativePath == L"sub") { hasSub = true; EXPECT_EQ(f.type, FileType::Directory); }
        if (f.relativePath == L"emptydir") { hasEmpty = true; EXPECT_EQ(f.type, FileType::Directory); }
    }
    EXPECT_TRUE(hasA && hasB && hasC && hasSub && hasEmpty);

    // 结果按相对路径大小写不敏感升序
    for (size_t i = 1; i < files.size(); ++i) {
        EXPECT_LE(wcsicmpSafe(files[i - 1].relativePath, files[i].relativePath), 0);
    }
}

TEST(FileScanner, CancelStopsEarly) {
    testenv::TempDir dir(L"scanner_cancel");
    testutil::writeFile(dir.file(L"a.txt"), "a");
    std::vector<FileInfo> files;
    std::vector<std::wstring> errors;
    EXPECT_TRUE(FileScanner::scan(dir.path(), files, errors, []() { return true; }));
    EXPECT_TRUE(files.empty());
}

TEST(FileScanner, SymlinkIsRecordedWithWarning) {
    testenv::TempDir dir(L"scanner_link");
    testutil::writeFile(dir.file(L"target.txt"), "t");
    const std::wstring link = dir.file(L"link.txt");
    if (!::CreateSymbolicLinkW(link.c_str(), L"target.txt", 0)) {
        GTEST_SKIP() << "cannot create symlink (需要开发者模式或管理员权限)";
    }

    std::vector<FileInfo> files;
    std::vector<std::wstring> errors;
    std::vector<std::wstring> warnings;
    ASSERT_TRUE(FileScanner::scan(dir.path(), files, errors, nullptr, nullptr, &warnings));
    // 新行为：符号链接被记录进遍历结果（不再跳过），并有警告提示
    EXPECT_FALSE(warnings.empty());
    bool foundLink = false;
    for (const auto& f : files) {
        if (f.relativePath == L"link.txt") {
            foundLink = true;
            EXPECT_EQ(f.type, FileType::Symlink);
            EXPECT_EQ(f.symlinkTarget, std::wstring(L"target.txt"));
        }
    }
    EXPECT_TRUE(foundLink) << "符号链接应被记录";
}
