// TimeUtil.h - 时间格式化工具
// 职责：基于 Windows 本地时间的格式化，供日志、历史记录、备份 ID 使用。
#pragma once

#include <string>

namespace backup {

// "2026-09-04 20:00:01"
std::string formatNowUtf8();
std::wstring formatNowWide();

// 备份 ID："20260904-200001"
std::string makeBackupId();

// 当天日期："2026-09-04"（调度去重用）
std::string dateToday();

// 当前 "HH:MM"（调度比对用）
std::string currentHHMM();

// 校验并解析 "HH:MM" 格式（00:00-23:59）。
// 成功返回 true，hour/minute 输出解析结果；格式非法或范围越界返回 false。
bool parseHHMM(const std::wstring& s, int& hour, int& minute);

}  // namespace backup
