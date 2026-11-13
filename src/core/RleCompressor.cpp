// RleCompressor.cpp - Packet RLE 行程编码实现（无损）
#include "core/RleCompressor.h"

#include <cstdint>

namespace backup {

namespace {

constexpr uint8_t kRunFlag = 0x80;       // 头部最高位：重复包标记
constexpr size_t kMaxPacket = 128;       // 每个包最多承载 128 字节

void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}
uint64_t readU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

}  // namespace

std::vector<uint8_t> RleCompressor::compress(const uint8_t* data, size_t len) const {
    std::vector<uint8_t> out;
    appendU64(out, static_cast<uint64_t>(len));

    size_t i = 0;
    while (i < len) {
        // 计算从 i 开始的连续相同字节长度（上限 128）。
        size_t run = 1;
        while (i + run < len && data[i + run] == data[i] && run < kMaxPacket)
            ++run;

        if (run >= 2) {
            // 重复包：bit7=1，低 7 位为次数-1，后跟 1 字节。
            out.push_back(static_cast<uint8_t>(kRunFlag | (run - 1)));
            out.push_back(data[i]);
            i += run;
        } else {
            // 字面量包：收集到出现可编码 run（连续两个相同）或满 128 个为止。
            const size_t start = i;
            while (i < len && (i - start) < kMaxPacket) {
                if (i + 1 < len && data[i] == data[i + 1]) break;  // 前方形成 run
                ++i;
            }
            const size_t count = i - start;  // 至少为 1
            out.push_back(static_cast<uint8_t>(count - 1));  // bit7=0
            out.insert(out.end(), data + start, data + start + count);
        }
    }
    return out;
}

std::vector<uint8_t> RleCompressor::decompress(const uint8_t* data, size_t len,
                                                bool* success) const {
    if (success) *success = false;
    if (!data || len < 8) return {};

    const uint64_t originalLen = readU64(data);
    size_t pos = 8;

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(originalLen));

    while (out.size() < originalLen) {
        if (pos >= len) return {};  // 头部截断
        const uint8_t header = data[pos++];
        const size_t count = (header & 0x7F) + 1;

        if (header & kRunFlag) {
            if (pos >= len) return {};  // 缺重复字节
            const uint8_t value = data[pos++];
            out.insert(out.end(), count, value);
        } else {
            if (pos + count > len) return {};  // 字面量截断
            out.insert(out.end(), data + pos, data + pos + count);
            pos += count;
        }
    }

    if (out.size() != originalLen) return {};
    if (success) *success = true;
    return out;
}

}  // namespace backup
