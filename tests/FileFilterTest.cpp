// FileFilterTest.cpp - 自定义筛选规则测试
#include <gtest/gtest.h>

#include "business/FileFilter.h"

namespace {

using namespace backup;

FileInfo makeFile(const std::wstring& name, const std::wstring& relPath,
                  uint64_t size, uint64_t mtime) {
    FileInfo f;
    f.name = name;
    f.relativePath = relPath;
    f.size = size;
    f.modifiedTime = mtime;
    f.type = FileType::File;
    return f;
}

}  // namespace

TEST(Filter, ByExtension) {
    FilterRule rule;
    rule.includeExtensions = {L".cpp", L".h"};
    EXPECT_TRUE(rule.isMatch(makeFile(L"a.cpp", L"a.cpp", 10, 1)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"B.H", L"B.H", 10, 1)));   // 大小写不敏感
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a.txt", L"a.txt", 10, 1)));
}

TEST(Filter, ExcludeExtension) {
    FilterRule rule;
    rule.excludeExtensions = {L".tmp", L".log"};
    EXPECT_TRUE(rule.isMatch(makeFile(L"a.cpp", L"a.cpp", 10, 1)));
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a.tmp", L"a.tmp", 10, 1)));
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a.LOG", L"a.LOG", 10, 1)));
}

TEST(Filter, BySize) {
    FilterRule rule;
    rule.minSize = 100;
    rule.maxSize = 1000;
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a", L"a", 99, 1)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"a", L"a", 100, 1)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"a", L"a", 1000, 1)));
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a", L"a", 1001, 1)));
}

TEST(Filter, ByTime) {
    FilterRule rule;
    rule.modifiedAfterEnabled = true;
    rule.modifiedAfterUnix = 1000;
    EXPECT_TRUE(rule.isMatch(makeFile(L"a", L"a", 10, 1000)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"a", L"a", 10, 2000)));
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a", L"a", 10, 999)));
}

TEST(Filter, BySubPath) {
    FilterRule rule;
    rule.includeSubPaths = {L"docs"};
    EXPECT_TRUE(rule.isMatch(makeFile(L"a.txt", L"docs\\a.txt", 10, 1)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"a.txt", L"docs\\sub\\a.txt", 10, 1)));
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a.txt", L"other\\a.txt", 10, 1)));

    rule.includeSubPaths.clear();
    rule.excludeSubPaths = {L"temp"};
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a.txt", L"temp\\a.txt", 10, 1)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"a.txt", L"docs\\a.txt", 10, 1)));
}

TEST(Filter, SkipEmpty) {
    FilterRule rule;
    rule.skipEmptyFiles = true;
    EXPECT_TRUE(!rule.isMatch(makeFile(L"a", L"a", 0, 1)));
    EXPECT_TRUE(rule.isMatch(makeFile(L"a", L"a", 1, 1)));
}
