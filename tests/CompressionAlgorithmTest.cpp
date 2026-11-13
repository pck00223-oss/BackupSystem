// CompressionAlgorithmTest.cpp - 压缩算法接口单元测试（Huffman / RLE）
// 覆盖 ICompressionAlgorithm 两个手写实现：工厂创建、空数据、单字节、单符号、
// 全相同、随机字节、256 符号、各长度往返、包边界、截断/损坏检测、接口多态。
// 两个算法通过统一接口指针验证泛化逻辑（策略模式）。
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/ICompressionAlgorithm.h"

using namespace backup;

namespace {

// 确定性序列（可复现，不依赖随机设备）
std::vector<uint8_t> makeSeq(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    return data;
}

// 简单确定性 PRNG 生成伪随机字节
std::vector<uint8_t> makeRandom(size_t size, uint32_t seed = 12345) {
    std::vector<uint8_t> data(size);
    uint32_t state = seed;
    for (size_t i = 0; i < size; ++i) {
        state = state * 1103515245u + 12345u;
        data[i] = static_cast<uint8_t>((state >> 16) & 0xFF);
    }
    return data;
}

std::vector<uint8_t> makeConstant(size_t size, uint8_t value) {
    return std::vector<uint8_t>(size, value);
}

// 覆盖全部 256 个符号并循环
std::vector<uint8_t> makeAllSymbols(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(i & 0xFF);
    return data;
}

const char* kAlgoNames[] = {"huffman", "rle"};

// 对两个算法执行一次完整往返并断言还原一致。
void expectRoundTrip(const std::vector<uint8_t>& plain, const char* tag) {
    for (const char* algo : kAlgoNames) {
        auto codec = createCompressionAlgorithm(algo);
        ASSERT_NE(codec, nullptr) << algo;
        EXPECT_EQ(codec->name(), algo);
        const std::vector<uint8_t> comp = codec->compress(plain.data(), plain.size());
        bool ok = false;
        const std::vector<uint8_t> out = codec->decompress(comp.data(), comp.size(), &ok);
        EXPECT_TRUE(ok) << algo << " " << tag;
        EXPECT_EQ(out, plain) << algo << " " << tag;
    }
}

}  // namespace

// 工厂：已知算法正确创建，未知算法返回 nullptr
TEST(CompressionAlgorithm, Factory_KnownAndUnknown) {
    for (const char* algo : kAlgoNames) {
        auto codec = createCompressionAlgorithm(algo);
        ASSERT_NE(codec, nullptr);
        EXPECT_EQ(codec->name(), algo);
    }
    EXPECT_EQ(createCompressionAlgorithm("zip"), nullptr);
    EXPECT_EQ(createCompressionAlgorithm(""), nullptr);
}

// 空数据往返
TEST(CompressionAlgorithm, EmptyDataRoundTrip) {
    expectRoundTrip({}, "empty");
}

// 单字节往返
TEST(CompressionAlgorithm, SingleByteRoundTrip) {
    expectRoundTrip(makeConstant(1, 0x42), "single");
}

// 单符号（全相同，不同长度，含 RLE 128 包边界与 Huffman 单符号特例）
TEST(CompressionAlgorithm, SingleSymbolRoundTrip) {
    const size_t lens[] = {2, 3, 127, 128, 129, 256, 300, 1000};
    for (size_t n : lens) {
        expectRoundTrip(makeConstant(n, 0x77), "constant");
    }
}

// 全相同数据应被有效压缩（体积明显小于原始）
TEST(CompressionAlgorithm, ConstantDataCompressesWell) {
    const size_t n = 10000;
    const std::vector<uint8_t> plain = makeConstant(n, 0x5A);
    for (const char* algo : kAlgoNames) {
        auto codec = createCompressionAlgorithm(algo);
        const std::vector<uint8_t> comp = codec->compress(plain.data(), plain.size());
        EXPECT_LT(comp.size(), plain.size()) << algo;
    }
}

// 随机字节往返（重点验证无损，不保证压缩率）
TEST(CompressionAlgorithm, RandomDataRoundTrip) {
    expectRoundTrip(makeRandom(5000), "random");
    expectRoundTrip(makeRandom(50000, 999), "random-large");
}

// 覆盖全部 256 符号
TEST(CompressionAlgorithm, All256SymbolsRoundTrip) {
    expectRoundTrip(makeAllSymbols(256), "256-once");
    expectRoundTrip(makeAllSymbols(4096), "256-many");
}

// 逐长度往返，覆盖各类小尺寸与边界
TEST(CompressionAlgorithm, ManyLengthsRoundTrip) {
    for (size_t n = 0; n <= 300; ++n) {
        expectRoundTrip(makeSeq(n), "seq-lengths");
    }
}

// RLE 抗膨胀：随机数据压缩后体积有界，不会无限膨胀。
// 开销来源：8 字节长度头 + 每个包 1 字节头；包数 ≤ 每 128 字节一个 + 相邻相同次数(n/256)。
TEST(CompressionAlgorithm, RleRandomDataHasBoundedOverhead) {
    const std::vector<uint8_t> plain = makeRandom(20000);
    auto codec = createCompressionAlgorithm("rle");
    const std::vector<uint8_t> comp = codec->compress(plain.data(), plain.size());
    // 上界约为原始的 1/64 头部开销加固定余量，足以覆盖随机数据的 run/literal 切分
    EXPECT_LE(comp.size(), plain.size() + plain.size() / 64 + 32);
}

// 截断的压缩数据必须被识别为失败
TEST(CompressionAlgorithm, TruncatedDataIsRejected) {
    for (const char* algo : kAlgoNames) {
        auto codec = createCompressionAlgorithm(algo);
        const std::vector<uint8_t> plain = makeSeq(500);
        std::vector<uint8_t> comp = codec->compress(plain.data(), plain.size());
        ASSERT_GT(comp.size(), 2u);
        comp.resize(comp.size() - 1);  // 截断末尾
        bool ok = true;
        const std::vector<uint8_t> out = codec->decompress(comp.data(), comp.size(), &ok);
        EXPECT_FALSE(ok) << algo;
        EXPECT_NE(out, plain) << algo;
    }
}

// 空指针/过短输入被拒绝
TEST(CompressionAlgorithm, RejectsMalformedInput) {
    for (const char* algo : kAlgoNames) {
        auto codec = createCompressionAlgorithm(algo);
        bool ok = true;
        EXPECT_TRUE(codec->decompress(nullptr, 0, &ok).empty());
        EXPECT_FALSE(ok);
        const std::vector<uint8_t> tiny(3, 0);  // 短于长度头
        ok = true;
        EXPECT_TRUE(codec->decompress(tiny.data(), tiny.size(), &ok).empty());
        EXPECT_FALSE(ok);
    }
}

// 接口多态：通过 ICompressionAlgorithm 指针完成完整往返（策略模式验证）
TEST(CompressionAlgorithm, PolymorphicRoundTripViaInterface) {
    const std::vector<uint8_t> plain = makeRandom(1000, 7);
    for (const char* algo : kAlgoNames) {
        std::unique_ptr<ICompressionAlgorithm> codec = createCompressionAlgorithm(algo);
        const std::vector<uint8_t> comp = codec->compress(plain.data(), plain.size());
        bool ok = false;
        const std::vector<uint8_t> out = codec->decompress(comp.data(), comp.size(), &ok);
        EXPECT_TRUE(ok) << algo;
        EXPECT_EQ(out, plain) << algo;
    }
}
