// FileCopierTest.cpp - 文件复制接口测试
#include <gtest/gtest.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "engine/FileCopier.h"
#include "engine/FileSystem.h"

using namespace backup;

TEST(FileCopier, CopiesAndOverwrites) {
    testenv::TempDir dir(L"copier");
    const std::wstring src = dir.file(L"src.bin");
    const std::wstring dst = dir.file(L"dst.bin");
    testutil::writeFile(src, "first-version");

    std::string err;
    ASSERT_TRUE(FileCopier::copyFile(src, dst, &err, nullptr)) << err;
    EXPECT_EQ(testutil::readFile(dst), std::string("first-version"));

    testutil::writeFile(src, "second-version-longer");
    ASSERT_TRUE(FileCopier::copyFile(src, dst, &err, nullptr)) << err;
    EXPECT_EQ(testutil::readFile(dst), std::string("second-version-longer"));
}

TEST(FileCopier, MissingSourceFails) {
    testenv::TempDir dir(L"copier_missing");
    std::string err;
    EXPECT_FALSE(FileCopier::copyFile(dir.file(L"nope.bin"), dir.file(L"dst.bin"), &err, nullptr));
    EXPECT_FALSE(err.empty());
}

TEST(FileCopier, CancelLeavesNoDestination) {
    testenv::TempDir dir(L"copier_cancel");
    const std::wstring src = dir.file(L"src.bin");
    const std::wstring dst = dir.file(L"dst.bin");
    testutil::writeFile(src, std::string(128 * 1024, 'z'));

    std::string err;
    const bool ok = FileCopier::copyFile(src, dst, &err,
                                         []() { return true; });
    EXPECT_FALSE(ok);
    EXPECT_EQ(err, std::string("cancelled"));
    EXPECT_FALSE(FileSystem::exists(dst));
}
