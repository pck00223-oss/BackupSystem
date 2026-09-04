// ResidualUtil.h - 崩溃残留识别共享工具
// 职责：统一管理临时文件/旧数据的后缀常量与残留识别逻辑，
//       避免 BackupManager / VerifyManager / FileCopier 各自硬编码导致规则不一致。
#pragma once

#include <algorithm>
#include <string>

namespace backup {

// 临时文件基础后缀（FileCopier 原子写入用，实际文件名会追加 pid.counter 保证唯一）
inline const std::wstring& tempSuffix() {
    static const std::wstring s = L".baktmp";
    return s;
}

// 旧数据后缀（类型互换时被移走的旧文件/目录）
inline const std::wstring& oldSuffix() {
    static const std::wstring s = L".baktmp.old";
    return s;
}

// 判断文件名是否是旧数据残留（.baktmp.old 或 .baktmp.old<数字>）。
// 是则返回去掉后缀后的原文件名，否则返回空字符串。
inline std::wstring parseOldResidual(const std::wstring& name) {
    const std::wstring& suffix = oldSuffix();
    if (name.size() <= suffix.size()) return L"";
    const size_t pos = name.rfind(suffix);
    if (pos == std::wstring::npos) return L"";
    const std::wstring after = name.substr(pos + suffix.size());
    if (std::any_of(after.begin(), after.end(),
                     [](wchar_t c) { return c < L'0' || c > L'9'; })) {
        return L"";
    }
    return name.substr(0, pos);
}

// 判断文件名是否是临时文件残留。
// 匹配两种格式：
//   1. 精确的 .baktmp（旧格式，兼容历史残留）
//   2. .baktmp.<pid>.<counter>（新格式，FileCopier 唯一临时文件名）
inline bool isTempResidual(const std::wstring& name) {
    const std::wstring& suffix = tempSuffix();
    if (name.size() < suffix.size()) return false;
    // 格式1：精确 .baktmp
    if (name == suffix) return true;
    // 格式2：.baktmp.<数字> 或 .baktmp.<数字>.<数字>
    if (name.size() > suffix.size() + 1 && name[suffix.size()] == L'.') {
        const std::wstring after = name.substr(suffix.size() + 1);
        if (after.empty()) return false;
        int dotCount = 0;
        const bool valid = std::all_of(after.begin(), after.end(), [&dotCount](wchar_t c) {
            if (c == L'.') {
                ++dotCount;
                return dotCount <= 1;
            }
            return c >= L'0' && c <= L'9';
        });
        return valid;
    }
    return false;
}

// 判断文件名是否是任何崩溃残留（临时文件或旧数据）。
inline bool isResidualName(const std::wstring& name) {
    return !parseOldResidual(name).empty() || isTempResidual(name);
}

}  // namespace backup
