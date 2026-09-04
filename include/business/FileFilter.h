// FileFilter.h - 自定义备份筛选器
// 职责：根据用户配置的筛选规则过滤扫描结果，只保留符合条件文件。
// 对应需求文档 3.3 自定义备份：筛选逻辑由 C++ 实现，GUI 只负责收集条件。
// 自定义备份不重复实现备份过程，仅负责筛选后交给统一备份流程。
#pragma once

#include <string>
#include <vector>

#include "core/BackupConfig.h"
#include "core/FileInfo.h"

namespace backup {

class FileFilter {
public:
    void setRule(const FilterRule& rule) { rule_ = rule; }
    const FilterRule& rule() const { return rule_; }

    // 过滤 in，仅保留匹配的文件；目录是否保留由 keepDirectories 决定。
    std::vector<FileInfo> filter(const std::vector<FileInfo>& in, bool keepDirectories) const;

private:
    FilterRule rule_;
};

}  // namespace backup
