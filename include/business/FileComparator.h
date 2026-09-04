// FileComparator.h - 文件变更比较器
// 职责：将当前扫描结果与上一次 Manifest 比较，识别新增 / 修改 / 删除 / 未变化文件。
// 核心策略（需求文档 8.2 两级变更检测）：
//   路径不存在历史记录 -> Added
//   存在历史记录：
//     大小变化 -> Modified
//     大小相同且修改时间相同 -> Unchanged
//     大小相同但修改时间变化 -> 计算 Hash 二次确认
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "business/Manifest.h"
#include "core/FileInfo.h"

namespace backup {

enum class FileChangeType : uint8_t {
    Unchanged = 0,
    Added = 1,
    Modified = 2,
    Deleted = 3
};

struct ChangeRecord {
    FileInfo info;                 // 当前状态（Deleted 时仅相对路径/类型有效）
    FileChangeType change = FileChangeType::Unchanged;
};

class FileComparator {
public:
    // Hash 二次确认提供者：由调用方注入（负责拼接绝对路径并计算 Hash），
    // 使比较器本身不依赖具体根目录与读取实现。
    using HashProvider = std::function<bool(const FileInfo& info, std::string& outHash)>;

    // current: 当前扫描并筛选后的文件列表；previous: 上一次 Manifest。
    // 返回的变更记录包含：当前全部文件 + 上次存在但本次已删除的文件。
    // 对"大小相同、时间不同"的文件调用 hashProvider 做二次确认；
    // 若无法计算 Hash，保守视为 Modified。
    static std::vector<ChangeRecord> compare(const std::vector<FileInfo>& current,
                                             const Manifest& previous,
                                             const HashProvider& hashProvider = nullptr);
};

}  // namespace backup
