// RleCompressor.h - Packet RLE 行程编码压缩/解压（无损）
// 职责：实现 ICompressionAlgorithm。采用带类型标记的 Packet 设计，
//       对随机数据也只产生有限的头部开销（最坏约 1/128），不会无限膨胀。
// 单块格式：原始长度(8) + 若干 packet；每个 packet 为 1 字节头 + 数据：
//   bit7=1 重复包：低7位+1 为重复次数(1..128)，后跟 1 字节；
//   bit7=0 字面量包：低7位+1 为字节数(1..128)，后跟对应字面量。
#pragma once

#include "core/ICompressionAlgorithm.h"

namespace backup {

class RleCompressor : public ICompressionAlgorithm {
public:
    std::string name() const override { return "rle"; }
    std::vector<uint8_t> compress(const uint8_t* data, size_t len) const override;
    std::vector<uint8_t> decompress(const uint8_t* data, size_t len,
                                    bool* success = nullptr) const override;
};

}  // namespace backup
