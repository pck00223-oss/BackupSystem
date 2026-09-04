// ConfigLoader.h - 配置文件解析
// 职责：解析简单 key=value 自定义配置格式（需求文档 12 配置设计，
//       优先使用简单自定义配置格式以减少第三方依赖）。
#pragma once

#include <string>
#include <vector>

#include "core/BackupConfig.h"

namespace backup {

class ConfigLoader {
public:
    // 从文件加载配置。文件不存在返回 false。
    // 未知/非法字段写入 warnings，不中断解析。
    static bool loadFromFile(const std::wstring& path,
                             BackupConfig& out,
                             std::vector<std::wstring>& warnings);

    // 解析若干行文本。
    static bool parseLines(const std::vector<std::string>& lines,
                           BackupConfig& out,
                           std::vector<std::wstring>& warnings);
};

}  // namespace backup
