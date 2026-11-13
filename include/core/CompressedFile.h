// CompressedFile.h - 文件级分块压缩/解压工具（无损）
// 职责：把一个文件按固定块大小切分，每块通过 ICompressionAlgorithm 独立压缩，
//       写入自描述容器；解压时逐块读取并还原。内存只占用一个块，支持大文件与取消。
// 容器格式：
//   魔数 "BKCP"(4) + 块数 uint32(4)；
//   每块：原始大小 uint32(4) + 压缩大小 uint32(4) + 压缩数据。
// 算法名不写入容器，由 Manifest 记录、解压时从工厂获取。
#pragma once

#include <functional>
#include <string>

namespace backup {

class CompressedFile {
public:
    using CancelCheck = std::function<bool()>;

    // 压缩 src 到 dst（临时文件 + 原子替换）。
    static bool compressFile(const std::wstring& src, const std::wstring& dst,
                             const std::string& algorithm, std::string& err,
                             const CancelCheck& cancel = nullptr);

    // 解压 src 到 dst（临时文件 + 原子替换）。
    static bool decompressFile(const std::wstring& src, const std::wstring& dst,
                               const std::string& algorithm, std::string& err,
                               const CancelCheck& cancel = nullptr);
};

}  // namespace backup
