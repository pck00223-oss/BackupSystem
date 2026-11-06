// TarPackerTest.cpp - TAR 打包/解包 + 硬链接 + 符号链接测试
#include <gtest/gtest.h>

#include <windows.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "engine/FileSystem.h"
#include "engine/TarPacker.h"

using namespace backup;

namespace {

// 判断文件是否符号链接
bool isSymlink(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

}  // namespace

// 打包 -> 解包往返：普通文件 + 目录 + 子目录内容一致
TEST(TarPacker, RoundTripPreservesFilesAndDirs) {
    testenv::TempDir src(L"tar_src");
    testenv::TempDir tar(L"tar_file");
    testenv::TempDir dst(L"tar_dst");

    testutil::writeFile(src.file(L"a.txt"), "hello tar");
    testutil::writeFile(src.file(L"sub\\b.bin"), std::string(5000, 'B'));  // 跨多个 512 块
    testutil::writeFile(src.file(L"sub\\deep\\c.txt"), "deep file");

    const std::wstring tarPath = tar.file(L"backup.tar");
    std::string err;
    ASSERT_TRUE(TarPacker::packDirectory(src.path(), tarPath, &err)) << err;

    // listEntries 应包含全部条目（目录 + 文件）
    std::vector<std::wstring> entries;
    ASSERT_TRUE(TarPacker::listEntries(tarPath, entries, &err)) << err;
    bool hasA = false, hasSub = false, hasSubB = false, hasDeep = false, hasC = false;
    for (const auto& p : entries) {
        if (p == L"a.txt") hasA = true;
        if (p == L"sub") hasSub = true;
        if (p == L"sub/b.bin") hasSubB = true;
        if (p == L"sub/deep") hasDeep = true;
        if (p == L"sub/deep/c.txt") hasC = true;
    }
    EXPECT_TRUE(hasA) << "a.txt missing";
    EXPECT_TRUE(hasSub) << "sub dir missing";
    EXPECT_TRUE(hasSubB) << "sub/b.bin missing";
    EXPECT_TRUE(hasDeep) << "sub/deep missing";
    EXPECT_TRUE(hasC) << "sub/deep/c.txt missing";

    ASSERT_TRUE(TarPacker::extractTo(tarPath, dst.path(), &err)) << err;
    EXPECT_EQ(testutil::readFile(dst.file(L"a.txt")), std::string("hello tar"));
    EXPECT_EQ(testutil::readFile(dst.file(L"sub\\b.bin")), std::string(5000, 'B'));
    EXPECT_EQ(testutil::readFile(dst.file(L"sub\\deep\\c.txt")), std::string("deep file"));
    EXPECT_TRUE(FileSystem::isDirectory(dst.file(L"sub\\deep")));
}

// 硬链接：打包后解包，链接组内两个文件共享同一物理文件（同一 fileIndex）
TEST(TarPacker, HardLinksPreserved) {
    testenv::TempDir src(L"tar_hl_src");
    testenv::TempDir tar(L"tar_hl_file");
    testenv::TempDir dst(L"tar_hl_dst");

    testutil::writeFile(src.file(L"f1.txt"), "hard link content");
    // 创建硬链接 f2.txt -> f1.txt（同一卷同一文件索引）
    std::string hlErr;
    ASSERT_TRUE(FileSystem::createHardLink(src.file(L"f2.txt"), src.file(L"f1.txt"), &hlErr))
        << hlErr;

    // 源端验证：两个文件文件索引相同
    FileInfo i1, i2;
    ASSERT_TRUE(FileSystem::getFileInfo(src.file(L"f1.txt"), i1));
    ASSERT_TRUE(FileSystem::getFileInfo(src.file(L"f2.txt"), i2));
    EXPECT_EQ(i1.fileIndex, i2.fileIndex);
    EXPECT_GT(i1.linkCount, 1u);

    const std::wstring tarPath = tar.file(L"backup.tar");
    std::string err;
    ASSERT_TRUE(TarPacker::packDirectory(src.path(), tarPath, &err)) << err;

    ASSERT_TRUE(TarPacker::extractTo(tarPath, dst.path(), &err)) << err;

    // 解包后内容一致
    EXPECT_EQ(testutil::readFile(dst.file(L"f1.txt")), std::string("hard link content"));
    EXPECT_EQ(testutil::readFile(dst.file(L"f2.txt")), std::string("hard link content"));

    // 解包后仍是硬链接（同一文件索引）
    FileInfo d1, d2;
    ASSERT_TRUE(FileSystem::getFileInfo(dst.file(L"f1.txt"), d1));
    ASSERT_TRUE(FileSystem::getFileInfo(dst.file(L"f2.txt"), d2));
    EXPECT_EQ(d1.fileIndex, d2.fileIndex);
}

// 符号链接：打包记录目标（SYMLINK），解包重建符号链接
TEST(TarPacker, SymlinkPreserved) {
    testenv::TempDir src(L"tar_sl_src");
    testenv::TempDir tar(L"tar_sl_file");
    testenv::TempDir dst(L"tar_sl_dst");

    testutil::writeFile(src.file(L"target.txt"), "link target content");

    // 创建文件符号链接 link.txt -> target.txt（开发者模式或管理员权限）
    std::string slErr;
    const bool created = FileSystem::createSymlink(src.file(L"link.txt"),
                                                   L"target.txt", false, &slErr);
    if (!created) {
        GTEST_SKIP() << "创建符号链接需要开发者模式或管理员权限: " << slErr;
    }
    ASSERT_TRUE(isSymlink(src.file(L"link.txt")));

    std::wstring target;
    ASSERT_TRUE(FileSystem::readSymlinkTarget(src.file(L"link.txt"), target));
    EXPECT_EQ(target, L"target.txt");

    const std::wstring tarPath = tar.file(L"backup.tar");
    std::string err;
    ASSERT_TRUE(TarPacker::packDirectory(src.path(), tarPath, &err)) << err;

    ASSERT_TRUE(TarPacker::extractTo(tarPath, dst.path(), &err)) << err;

    // 解包后 link.txt 仍是符号链接，且指向相同目标
    EXPECT_TRUE(isSymlink(dst.file(L"link.txt"))) << "符号链接未被重建";
    std::wstring restoredTarget;
    if (FileSystem::readSymlinkTarget(dst.file(L"link.txt"), restoredTarget)) {
        EXPECT_EQ(restoredTarget, L"target.txt");
    }
    // 链接目标内容可通过链接读取
    EXPECT_EQ(testutil::readFile(dst.file(L"link.txt")), std::string("link target content"));
}

// 大文件（多块）+ 空文件
TEST(TarPacker, LargeAndEmptyFiles) {
    testenv::TempDir src(L"tar_lg_src");
    testenv::TempDir tar(L"tar_lg_file");
    testenv::TempDir dst(L"tar_lg_dst");

    testutil::writeFile(src.file(L"empty.txt"), "");
    const std::string big(1 * 1024 * 1024 + 17, 'X');
    testutil::writeFile(src.file(L"big.bin"), big);

    const std::wstring tarPath = tar.file(L"backup.tar");
    std::string err;
    ASSERT_TRUE(TarPacker::packDirectory(src.path(), tarPath, &err)) << err;
    ASSERT_TRUE(TarPacker::extractTo(tarPath, dst.path(), &err)) << err;

    EXPECT_EQ(testutil::readFile(dst.file(L"empty.txt")), std::string());
    EXPECT_EQ(testutil::readFile(dst.file(L"big.bin")), big);
}

// 先序遍历顺序：父目录条目先于其子树
TEST(TarPacker, PreorderTraversalOrder) {
    testenv::TempDir src(L"tar_pre_src");
    testenv::TempDir tar(L"tar_pre_file");
    testenv::TempDir dst(L"tar_pre_dst");

    testutil::writeFile(src.file(L"z_top.txt"), "top");
    testutil::writeFile(src.file(L"a_dir\\x.txt"), "x");
    testutil::writeFile(src.file(L"a_dir\\y.txt"), "y");
    testutil::writeFile(src.file(L"b_dir\\deep\\z.txt"), "z");

    const std::wstring tarPath = tar.file(L"backup.tar");
    std::string err;
    ASSERT_TRUE(TarPacker::packDirectory(src.path(), tarPath, &err)) << err;

    std::vector<std::wstring> entries;
    ASSERT_TRUE(TarPacker::listEntries(tarPath, entries, &err)) << err;

    // 父目录条目应出现在其子文件之前
    auto posOf = [&entries](const std::wstring& p) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i] == p) return i;
        }
        return static_cast<size_t>(-1);
    };
    const size_t posADir = posOf(L"a_dir");
    const size_t posAX = posOf(L"a_dir/x.txt");
    const size_t posBDir = posOf(L"b_dir");
    const size_t posBDeep = posOf(L"b_dir/deep");
    const size_t posBDeepZ = posOf(L"b_dir/deep/z.txt");
    EXPECT_NE(posADir, static_cast<size_t>(-1));
    EXPECT_LT(posADir, posAX);
    EXPECT_LT(posBDir, posBDeep);
    EXPECT_LT(posBDeep, posBDeepZ);
}
