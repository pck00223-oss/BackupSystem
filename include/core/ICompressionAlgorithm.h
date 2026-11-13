// ICompressionAlgorithm.h - 压缩算法抽象接口（策略模式）
// 职责：定义无损压缩/解压的统一接口，供 BackupManager/RestoreManager 依赖。
// 具体实现：Huffman（手写哈夫曼编码）、RLE（Packet 行程编码），均为无损。
// 设计意图：管理器只依赖本接口，新增压缩算法无需修改管理器（开闭原则）；
//           算法内部自带解码所需的头信息（词频表/包结构），保证可独立还原。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace backup {

class ICompressionAlgorithm {
public:
    virtual ~ICompressionAlgorithm() = default;

    // 算法标识（写入压缩容器与 Manifest），如 "huffman" / "rle"。
    virtual std::string name() const = 0;

    // 压缩 data，返回自包含的压缩数据（含解码所需头部）。
    virtual std::vector<uint8_t> compress(const uint8_t* data, size_t len) const = 0;

    // 解压 compress 产生的数据，返回还原后的原始字节。
    // success 非空时报告是否成功（格式损坏/截断/校验不符均判失败）。
    virtual std::vector<uint8_t> decompress(const uint8_t* data, size_t len,
                                            bool* success = nullptr) const = 0;
};

// 工厂：按算法名创建压缩器（"huffman"/"rle"，非法返回 nullptr）。
std::unique_ptr<ICompressionAlgorithm>
createCompressionAlgorithm(const std::string& name);

}  // namespace backup
