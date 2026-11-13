// CompressionFactory.cpp - 压缩算法工厂实现
// 按算法名创建对应具体策略，调用方只拿到 ICompressionAlgorithm 接口。
#include <memory>
#include <string>

#include "core/HuffmanCompressor.h"
#include "core/ICompressionAlgorithm.h"
#include "core/RleCompressor.h"

namespace backup {

std::unique_ptr<ICompressionAlgorithm>
createCompressionAlgorithm(const std::string& name) {
    if (name == "huffman") {
        return std::make_unique<HuffmanCompressor>();
    }
    if (name == "rle") {
        return std::make_unique<RleCompressor>();
    }
    return nullptr;  // 未知算法
}

}  // namespace backup
