// HashCalculatorTest.cpp - SHA-256 测试
// 使用 FIPS 180-4 标准测试向量。
#include "TestFramework.h"
#include "TestUtil.h"

#include "core/HashCalculator.h"

using namespace backup;

TEST(Sha256_EmptyString) {
    CHECK_EQ(HashCalculator::bufferSha256("", 0),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256_Abc) {
    CHECK_EQ(HashCalculator::bufferSha256("abc", 3),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256_Hello) {
    CHECK_EQ(HashCalculator::bufferSha256("hello", 5),
             "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST(Sha256_OneMillionA) {
    // 1000000 个 'a'
    std::string s(1000000, 'a');
    CHECK_EQ(HashCalculator::bufferSha256(s.data(), s.size()),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256_File) {
    const std::wstring dir = testutil::makeTempDir(L"hash");
    const std::wstring f = dir + L"abc.txt";
    testutil::writeFile(f, "abc");
    std::string hex;
    CHECK(HashCalculator::fileSha256(f, hex));
    CHECK_EQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::string err;
    CHECK(!HashCalculator::fileSha256(dir + L"not_exist.txt", hex, &err));
    CHECK(!err.empty());
    testutil::removeAll(dir);
}
