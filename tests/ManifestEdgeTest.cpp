// ManifestEdgeTest.cpp - Manifest 损坏输入与字段校验接口测试
#include <gtest/gtest.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "business/Manifest.h"

using namespace backup;

namespace {

const char* kValidManifest =
    "# BackupSystem Manifest\n"
    "version=1\n"
    "backup_id=20260905-000000\n"
    "source=D:\\Data\n"
    "created=2026-09-05 00:00:00\n"
    "type=full\n"
    "file_count=1\n"
    "\n[file]\n"
    "path=a.txt\n"
    "type=0\n"
    "size=1\n"
    "mtime=1\n"
    "mtime_100ns=1\n"
    "hash=aa\n"
    "data=a.txt\n";

}  // namespace

TEST(ManifestEdge, RejectsEmptyFile) {
    testenv::TempDir dir(L"manifest_empty");
    const std::wstring path = dir.file(L"manifest.txt");
    testutil::writeFile(path, "");

    Manifest m;
    std::string err;
    EXPECT_FALSE(m.loadFromFile(path, &err));
    EXPECT_NE(err.find("manifest is empty"), std::string::npos);
}

TEST(ManifestEdge, RejectsWrongHeader) {
    testenv::TempDir dir(L"manifest_header");
    const std::wstring path = dir.file(L"manifest.txt");
    testutil::writeFile(path, "not a manifest\nversion=1\n");

    Manifest m;
    std::string err;
    EXPECT_FALSE(m.loadFromFile(path, &err));
    EXPECT_NE(err.find("invalid manifest header"), std::string::npos);
}

TEST(ManifestEdge, RejectsInvalidNumber) {
    testenv::TempDir dir(L"manifest_number");
    const std::wstring path = dir.file(L"manifest.txt");
    testutil::writeFile(path,
        "# BackupSystem Manifest\nversion=1\nfile_count=1\n"
        "\n[file]\npath=a.txt\ntype=0\nsize=abc\nmtime=1\n");

    Manifest m;
    std::string err;
    EXPECT_FALSE(m.loadFromFile(path, &err));
    EXPECT_NE(err.find("invalid size"), std::string::npos);
}

TEST(ManifestEdge, RejectsFileCountMismatch) {
    testenv::TempDir dir(L"manifest_count");
    const std::wstring path = dir.file(L"manifest.txt");
    testutil::writeFile(path,
        "# BackupSystem Manifest\nversion=1\nfile_count=5\n"
        "\n[file]\npath=a.txt\ntype=0\nsize=1\nmtime=1\nhash=aa\ndata=a.txt\n");

    Manifest m;
    std::string err;
    EXPECT_FALSE(m.loadFromFile(path, &err));
    EXPECT_NE(err.find("file_count mismatch"), std::string::npos);
}

TEST(ManifestEdge, UnknownFieldsAreIgnored) {
    testenv::TempDir dir(L"manifest_unknown");
    const std::wstring path = dir.file(L"manifest.txt");
    testutil::writeFile(path,
        "# BackupSystem Manifest\nversion=1\nbackup_id=x\ncreated=t\ntype=full\n"
        "file_count=1\nfuture_field=whatever\n"
        "\n[file]\npath=a.txt\ntype=0\nsize=1\nmtime=1\nhash=aa\ndata=a.txt\n");

    Manifest m;
    std::string err;
    EXPECT_TRUE(m.loadFromFile(path, &err)) << err;
    ASSERT_EQ(m.entries.size(), size_t(1));
    EXPECT_EQ(m.entries[0].info.relativePath, std::wstring(L"a.txt"));
}

TEST(ManifestEdge, ValidManifestLoads) {
    testenv::TempDir dir(L"manifest_valid");
    const std::wstring path = dir.file(L"manifest.txt");
    testutil::writeFile(path, kValidManifest);

    Manifest m;
    std::string err;
    EXPECT_TRUE(m.loadFromFile(path, &err)) << err;
    EXPECT_EQ(m.meta.encryption, std::string(""));
}
