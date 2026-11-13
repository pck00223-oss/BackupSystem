// HuffmanCompressor.h - 手写哈夫曼压缩/解压（无损）
// 职责：实现 ICompressionAlgorithm。按 6.pptx 流程：
//   统计词频 -> 最小堆构建哈夫曼树 -> 生成前缀码 -> 按位编码（记录末尾有效位）；
//   解压时依据词频表重建同一棵树，逐位走树还原。
// 单块格式：原始长度(8) + 符号数(2) + 稀疏词频表(符号1+频率4)*N + 末尾有效位(1) + 比特流。
#pragma once

#include "core/ICompressionAlgorithm.h"

namespace backup {

class HuffmanCompressor : public ICompressionAlgorithm {
public:
    std::string name() const override { return "huffman"; }
    std::vector<uint8_t> compress(const uint8_t* data, size_t len) const override;
    std::vector<uint8_t> decompress(const uint8_t* data, size_t len,
                                    bool* success = nullptr) const override;
};

}  // namespace backup
