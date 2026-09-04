// HashCalculator.h - 文件内容哈希
// 职责：计算文件/内存块的 SHA-256，用于增量检测二次确认、恢复校验与完整性验证。
// 对应需求文档 8.2 两级变更检测（Hash 二次确认）与 Phase 6 Hash 校验。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace backup {

class HashCalculator {
public:
    // 计算整个文件的 SHA-256（分块读取，避免大文件占用内存）。
    // 成功返回 true 并写入 outHex（64 位小写十六进制）；失败返回 false 并填充 errMsg。
    static bool fileSha256(const std::wstring& path, std::string& outHex, std::string* errMsg = nullptr);

    // 计算内存块的 SHA-256。
    static std::string bufferSha256(const void* data, size_t len);
};

}  // namespace backup
