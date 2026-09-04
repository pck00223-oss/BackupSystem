// FileInfo.h - 文件基本信息的统一数据对象
// 职责：作为扫描、比较、Manifest、筛选共用的文件信息载体。
// 对应需求文档 3.5 文件元数据 与 6.1 FileInfo。
#pragma once

#include <cstdint>
#include <string>

namespace backup {

enum class FileType : uint8_t {
    File = 0,       // 普通文件
    Directory = 1,  // 目录
    Symlink = 2,    // 符号链接/重解析点（识别并记录，恢复时按策略处理）
    Unknown = 3     // 未知/特殊对象
};

// 一个文件/目录的基本信息。
// 第一阶段仅保存：相对路径、大小、修改时间、文件类型（需求文档 3.5 第一阶段范围）。
// 创建时间、访问时间、权限/所有者作为高级元数据，字段已预留。
struct FileInfo {
    std::wstring relativePath;  // 相对源目录的路径（反斜杠分隔，不含源目录前缀）
    std::wstring name;          // 文件名或目录名
    uint64_t size = 0;          // 字节数（目录为 0）
    uint64_t createdTime = 0;   // 创建时间（Unix 秒，扩展元数据）
    uint64_t modifiedTime = 0;  // 修改时间（Unix 秒）
    FileType type = FileType::Unknown;
    std::string hash;           // 内容 SHA-256 十六进制（小写），未计算则为空
    bool hashed = false;        // 是否已计算 hash
};

}  // namespace backup
