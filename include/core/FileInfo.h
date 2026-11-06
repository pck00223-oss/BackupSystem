// FileInfo.h - 文件头结构（FileHeader，对应 Unix inode / TAR 头部记录）
// 职责：文件遍历接口的数据载体。目录树中的每个文件/目录对应一个"文件头"，
//       记录遍历所需的全部元数据：类型、权限(mode)、属主、大小、时间、
//       inode 号（volumeSerial+fileIndex）、硬链接计数、链接目标、数据位置。
// 对应课程《文件系统》：inode 是文件的元数据集合（Mode/Owner/Size/Timestamp/
//       直接/间接指针），目录项通过 Name->inode 映射定位文件（见路径遍历图）。
// 本结构同时被扫描、比较、Manifest、筛选、打包共用的文件信息载体。
#pragma once

#include <cstdint>
#include <string>

namespace backup {

enum class FileType : uint8_t {
    File = 0,       // 普通文件（TAR typeflag='0' REGULAR）
    Directory = 1,  // 目录（TAR typeflag='5' DIRECTORY）
    Symlink = 2,    // 符号链接/重解析点（TAR typeflag='2' SYMLINK，识别并记录目标）
    Unknown = 3     // 未知/特殊对象
};

// 文件头 struct：一次遍历/扫描中单个文件或目录的全部元数据。
struct FileInfo {
    // ---- 名称与类型（对应 TAR union Record 的 name / typeflag）----
    std::wstring relativePath;  // 相对源目录的路径（反斜杠分隔，不含源目录前缀）
    std::wstring name;          // 文件名或目录名
    FileType type = FileType::Unknown;

    // ---- 元数据（对应 inode 的 Mode/Owner/Size/Timestamp）----
    uint32_t mode = 0;              // 权限位（八进制，Windows 简化映射：目录 0755/文件 0644）
    uint64_t size = 0;              // 字节数（目录为 0）
    uint64_t createdTime = 0;       // 创建时间（Unix 秒）
    uint64_t modifiedTime = 0;      // 修改时间（Unix 秒，兼容字段）
    uint64_t modifiedTime100ns = 0; // 修改时间（Windows FILETIME，100ns 单位，从 1601 年起）
                                    // 用于同秒内内容变化的精确检测（增量备份核心）

    // ---- inode 标识（对应 Unix inode 号 + 设备号 + 链接计数）----
    uint32_t volumeSerial = 0;   // 卷序列号（对应 Unix 设备号）
    uint64_t fileIndex = 0;      // 文件索引（对应 Unix inode 号）
    uint32_t linkCount = 0;      // 硬链接计数（ls -li 第三列；>1 说明存在多个目录项指向同一 inode）
                                   // 同一卷+同一 fileIndex 且 linkCount>1 => 硬链接组

    // ---- 链接信息（对应 TAR typeflag='1'/'2' 与 linkname 字段）----
    // 若该文件是硬链接组中的非首个条目，指向首个条目的相对路径（TAR HARDLINK）。
    // 空表示该条目是硬链接组首个，按普通文件（REGULAR）打包。
    std::wstring hardLinkTo;
    // type==Symlink 时，记录符号链接指向的目标路径（TAR SYMLINK，linkname）。
    // 备份符号链接本身而非目标内容，恢复时重建符号链接。
    std::wstring symlinkTarget;

    // ---- 数据位置与校验（对应 inode 数据块指针；备份内为数据存储路径）----
    std::wstring dataPath;      // 数据在备份中的相对存储路径
    std::string hash;           // 内容 SHA-256 十六进制（小写），未计算则为空
    bool hashed = false;        // 是否已计算 hash
};

}  // namespace backup
