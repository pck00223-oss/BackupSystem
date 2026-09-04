// Manifest.h - 备份清单
// 职责：记录一次备份对应的完整文件状态，作为增量检测、恢复、完整性校验的共同数据基础。
// 对应需求文档 6.4 Manifest 与 8 增量备份设计。
// 格式：简单、自有、可解释的文本（UTF-8，key=value），路径用反斜杠相对路径。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/FileInfo.h"

namespace backup {

class Manifest {
public:
    static constexpr const char* kFormatVersion = "1";

    // 一次备份的元信息（Manifest 头部）。
    struct Meta {
        std::string formatVersion = kFormatVersion;
        std::string backupId;       // 备份标识
        std::wstring sourcePath;    // 源目录
        std::string created;        // 创建时间文本
        std::string backupType;     // "full" / "incremental"
        uint64_t fileCount = 0;
        std::string encryption;      // "none" / "aes256"
    };

    // 文件条目 = FileInfo + 备份内的数据存储相对路径。
    struct Entry {
        FileInfo info;
        std::wstring dataPath;  // 数据在备份中的相对存储路径（单镜像方案中默认等于相对路径）
    };

    Manifest() = default;

    // 从备份目录中的 manifest 文件加载。
    bool loadFromFile(const std::wstring& path, std::string* err = nullptr);
    // 保存到文件。
    bool saveToFile(const std::wstring& path, std::string* err = nullptr) const;

    void rebuildIndex();
    const Entry* find(const std::wstring& relativePath) const;

    Meta meta;
    std::vector<Entry> entries;  // 按相对路径排序

private:
    std::map<std::wstring, size_t> index_;  // relativePath -> entries 下标
};

}  // namespace backup
