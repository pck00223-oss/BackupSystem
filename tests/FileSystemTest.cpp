// FileSystemTest.cpp - Win32 文件操作封装接口测试
#include <gtest/gtest.h>

#include <windows.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "engine/FileSystem.h"

using namespace backup;

TEST(FileSystem, CreateListAndQuery) {
    testenv::TempDir dir(L"fs");
    const std::wstring nested = dir.path() + L"a\\b\\c";
    ASSERT_TRUE(FileSystem::createDirectories(nested));
    EXPECT_TRUE(FileSystem::exists(nested));
    EXPECT_TRUE(FileSystem::isDirectory(nested));

    // 写一个文件并校验元数据
    const std::wstring file = dir.path() + L"a\\data.bin";
    {
        std::string err;
        FileHandle h = FileSystem::openWrite(file, /*overwrite=*/true, &err);
        ASSERT_TRUE(h.valid()) << err;
        const char payload[] = "hello";
        DWORD written = 0;
        ASSERT_TRUE(FileSystem::write(h.get(), payload, 5, written, &err)) << err;
        EXPECT_EQ(written, DWORD(5));
    }

    FileInfo info;
    ASSERT_TRUE(FileSystem::getFileInfo(file, info));
    EXPECT_EQ(info.size, uint64_t(5));
    EXPECT_EQ(info.type, FileType::File);

    std::vector<std::pair<std::wstring, FileType>> entries;
    ASSERT_TRUE(FileSystem::listDirectory(dir.path() + L"a", entries));
    bool foundFile = false, foundDir = false;
    for (const auto& e : entries) {
        if (e.first == L"data.bin") { foundFile = true; EXPECT_EQ(e.second, FileType::File); }
        if (e.first == L"b") { foundDir = true; EXPECT_EQ(e.second, FileType::Directory); }
    }
    EXPECT_TRUE(foundFile);
    EXPECT_TRUE(foundDir);
}

TEST(FileSystem, FullPathNormalizesDotDot) {
    testenv::TempDir dir(L"fs_path");
    const std::wstring base = FileSystem::fullPath(dir.path());
    const std::wstring messy = FileSystem::fullPath(dir.path() + L"sub\\..\\target");
    EXPECT_EQ(messy, base + L"target");
}

TEST(FileSystem, MoveAndRemoveAll) {
    testenv::TempDir dir(L"fs_move");
    const std::wstring src = dir.file(L"src.txt");
    const std::wstring dst = dir.file(L"dst.txt");
    testutil::writeFile(src, "content");

    ASSERT_TRUE(FileSystem::movePath(src, dst));
    EXPECT_FALSE(FileSystem::exists(src));
    ASSERT_TRUE(FileSystem::exists(dst));

    // 只读文件也应能被 removeAll 清掉
    ::SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_READONLY);
    EXPECT_TRUE(FileSystem::removeAll(dst));
    EXPECT_FALSE(FileSystem::exists(dst));

    // 目录树递归删除
    ASSERT_TRUE(FileSystem::createDirectories(dir.path() + L"tree\\sub"));
    testutil::writeFile(dir.file(L"tree\\sub\\x.txt"), "x");
    EXPECT_TRUE(FileSystem::removeAll(dir.path() + L"tree"));
    EXPECT_FALSE(FileSystem::exists(dir.path() + L"tree"));
}

TEST(FileSystem, ErrorStringIsReadable) {
    EXPECT_FALSE(FileSystem::errorString(ERROR_FILE_NOT_FOUND).empty());
    EXPECT_FALSE(FileSystem::errorString(ERROR_ACCESS_DENIED).empty());
}
