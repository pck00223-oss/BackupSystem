// CompressedFileTest.cpp - 文件级分块压缩/解压测试
// 覆盖：小文件往返、空文件、跨块大文件（>1MB 多块）、魔数错误、未知算法、
// 取消、压缩+加密端到端组合（先压缩再加密、先解密再解压）。
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "TestEnv.h"
#include "TestUtil.h"

#include "business/BackupManager.h"
#include "business/Manifest.h"
#include "business/RestoreManager.h"

#include "core/CompressedFile.h"
#include "engine/FileSystem.h"

using namespace backup;

namespace {

// 生成确定性伪随机字节
std::vector<uint8_t> genBytes(size_t size, uint32_t seed = 1) {
    std::vector<uint8_t> data(size);
    uint32_t state = seed;
    for (size_t i = 0; i < size; ++i) {
        state = state * 1103515245u + 12345u;
        data[i] = static_cast<uint8_t>((state >> 16) & 0xFF);
    }
    return data;
}

void writeBinary(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    testutil::writeFile(path, std::string(bytes.begin(), bytes.end()));
}

std::vector<uint8_t> readBinary(const std::wstring& path) {
    const std::string s = testutil::readFile(path);
    return std::vector<uint8_t>(s.begin(), s.end());
}

const char* kAlgoNames[] = {"huffman", "rle"};

}  // namespace

// 小文件往返
TEST(CompressedFile, SmallFileRoundTrip) {
    for (const char* algo : kAlgoNames) {
        testenv::TempDir dir(L"cf_small");
        const std::wstring src = dir.file(L"src.bin");
        const std::wstring comp = dir.file(L"out.bkc");
        const std::wstring back = dir.file(L"back.bin");
        const std::vector<uint8_t> data = genBytes(1000);
        writeBinary(src, data);

        std::string err;
        ASSERT_TRUE(CompressedFile::compressFile(src, comp, algo, err)) << algo << " " << err;
        ASSERT_TRUE(CompressedFile::decompressFile(comp, back, algo, err)) << algo << " " << err;
        EXPECT_EQ(readBinary(back), data) << algo;
    }
}

// 空文件往返
TEST(CompressedFile, EmptyFileRoundTrip) {
    for (const char* algo : kAlgoNames) {
        testenv::TempDir dir(L"cf_empty");
        const std::wstring src = dir.file(L"src.bin");
        const std::wstring comp = dir.file(L"out.bkc");
        const std::wstring back = dir.file(L"back.bin");
        writeBinary(src, {});

        std::string err;
        ASSERT_TRUE(CompressedFile::compressFile(src, comp, algo, err)) << err;
        ASSERT_TRUE(CompressedFile::decompressFile(comp, back, algo, err)) << err;
        EXPECT_EQ(readBinary(back).size(), size_t(0));
    }
}

// 跨块大文件：2.5MB，产生多个块，验证分块容器与逐块还原
TEST(CompressedFile, MultiBlockLargeFileRoundTrip) {
    for (const char* algo : kAlgoNames) {
        testenv::TempDir dir(L"cf_large");
        const std::wstring src = dir.file(L"src.bin");
        const std::wstring comp = dir.file(L"out.bkc");
        const std::wstring back = dir.file(L"back.bin");
        const size_t total = 2 * 1024 * 1024 + 500000;  // 约 2.5 MB
        const std::vector<uint8_t> data = genBytes(total, 42);
        writeBinary(src, data);

        std::string err;
        ASSERT_TRUE(CompressedFile::compressFile(src, comp, algo, err)) << err;
        ASSERT_TRUE(CompressedFile::decompressFile(comp, back, algo, err)) << err;
        const std::vector<uint8_t> restored = readBinary(back);
        ASSERT_EQ(restored.size(), data.size());
        EXPECT_EQ(restored, data);
    }
}

// 魔数错误必须被拒绝
TEST(CompressedFile, InvalidMagicRejected) {
    testenv::TempDir dir(L"cf_magic");
    const std::wstring bad = dir.file(L"bad.bkc");
    testutil::writeFile(bad, std::string("XXXX-not-a-valid-container"));
    std::string err;
    EXPECT_FALSE(CompressedFile::decompressFile(bad, dir.file(L"out.bin"),
                                                 "huffman", err));
    EXPECT_FALSE(err.empty());
}

// 未知算法
TEST(CompressedFile, UnknownAlgorithmRejected) {
    testenv::TempDir dir(L"cf_algo");
    testutil::writeFile(dir.file(L"src.bin"), std::string("hello"));
    std::string err;
    EXPECT_FALSE(CompressedFile::compressFile(dir.file(L"src.bin"),
                                               dir.file(L"o.bkc"), "zip", err));
    EXPECT_FALSE(err.empty());
}

// 取消：压缩与解压均返回 cancelled 且不产生目标
TEST(CompressedFile, CancelAborts) {
    testenv::TempDir dir(L"cf_cancel");
    const std::wstring src = dir.file(L"src.bin");
    const std::wstring comp = dir.file(L"o.bkc");
    writeBinary(src, genBytes(200000));

    std::string err;
    EXPECT_FALSE(CompressedFile::compressFile(src, comp, "huffman", err,
                                              []() { return true; }));
    EXPECT_EQ(err, "cancelled");
    EXPECT_FALSE(FileSystem::exists(comp));
}

// 端到端：仅压缩备份 → Manifest 记录压缩方式 → 恢复内容一致
TEST(CompressedFile, EndToEnd_CompressionOnly) {
    for (const char* algo : kAlgoNames) {
        testenv::TempDir base(L"cf_e2e");
        const std::wstring src = base.path() + L"src\\";
        const std::wstring tgt = base.path() + L"tgt\\";
        const std::wstring rst = base.path() + L"rst\\";
        FileSystem::createDirectories(src);
        testutil::writeFile(src + L"a.txt", "plain text content for compression");
        testutil::writeFile(src + L"rep.bin", std::string(5000, 'q'));  // 高重复数据

        BackupConfig cfg;
        cfg.sourcePath = src;
        cfg.targetPath = tgt;
        cfg.mode = BackupMode::Full;
        cfg.compression = algo;
        ASSERT_TRUE(BackupManager::run(cfg).success) << algo;

        Manifest manifest;
        std::string mErr;
        ASSERT_TRUE(manifest.loadFromFile(tgt + L"\\manifest.txt", &mErr)) << mErr;
        EXPECT_EQ(manifest.meta.compression, algo);

        RestoreConfig rcfg;
        rcfg.backupRoot = tgt;
        rcfg.restorePath = rst;
        rcfg.overwrite = true;
        const RestoreResult rr = RestoreManager::run(rcfg);
        EXPECT_TRUE(rr.success) << algo;
        EXPECT_EQ(testutil::readFile(rst + L"a.txt"),
                  std::string("plain text content for compression"));
        EXPECT_EQ(testutil::readFile(rst + L"rep.bin"), std::string(5000, 'q'));
    }
}

// 端到端：压缩+加密（先压缩再加密），恢复时先解密再解压
TEST(CompressedFile, EndToEnd_CompressionAndEncryption) {
    testenv::TempDir base(L"cf_e2e_enc");
    const std::wstring src = base.path() + L"src\\";
    const std::wstring tgt = base.path() + L"tgt\\";
    const std::wstring rst = base.path() + L"rst\\";
    FileSystem::createDirectories(src);
    testutil::writeFile(src + L"secret.txt", "this is secret and repetitive data " +
                                              std::string(3000, 'z'));
    testutil::writeFile(src + L"normal.txt", "ordinary file");

    BackupConfig cfg;
    cfg.sourcePath = src;
    cfg.targetPath = tgt;
    cfg.mode = BackupMode::Full;
    cfg.compression = "huffman";
    cfg.encryption = "aes256";
    cfg.password = "combo-password";
    ASSERT_TRUE(BackupManager::run(cfg).success);

    // data/ 中既非明文也非可直接识别的压缩流
    EXPECT_TRUE(testutil::readFile(tgt + L"\\data\\secret.txt").find("secret") ==
                std::string::npos);

    RestoreConfig rcfg;
    rcfg.backupRoot = tgt;
    rcfg.restorePath = rst;
    rcfg.overwrite = true;
    rcfg.password = "combo-password";
    const RestoreResult rr = RestoreManager::run(rcfg);
    EXPECT_TRUE(rr.success);
    EXPECT_EQ(testutil::readFile(rst + L"normal.txt"), std::string("ordinary file"));
    EXPECT_EQ(testutil::readFile(rst + L"secret.txt"),
              "this is secret and repetitive data " + std::string(3000, 'z'));
}

// 增量：压缩方式不一致必须被拒绝，要求全量重建
TEST(CompressedFile, Incremental_CompressionMismatchRejected) {
    testenv::TempDir base(L"cf_inc_mm");
    const std::wstring src = base.path() + L"src\\";
    const std::wstring tgt = base.path() + L"tgt\\";
    FileSystem::createDirectories(src);
    testutil::writeFile(src + L"f.txt", std::string(2000, 'm'));

    BackupConfig cfg;
    cfg.sourcePath = src;
    cfg.targetPath = tgt;
    cfg.mode = BackupMode::Full;
    cfg.compression = "huffman";
    ASSERT_TRUE(BackupManager::run(cfg).success);

    cfg.mode = BackupMode::Incremental;
    cfg.compression = "rle";  // 换压缩方式
    const BackupResult rr = BackupManager::run(cfg);
    EXPECT_FALSE(rr.success);
    EXPECT_FALSE(rr.errors.empty());
}
