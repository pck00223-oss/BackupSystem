// FileFilter.cpp - 自定义备份筛选实现
#include "business/FileFilter.h"

#include <algorithm>
#include <cwctype>

#include "core/Utf.h"

namespace backup {

namespace {

// 取文件名的小写扩展名（含点，如 ".cpp"）；无扩展名返回空。
std::wstring lowerExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == name.size() - 1) return {};
    std::wstring ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return ext;
}

bool listContains(const std::vector<std::wstring>& list, const std::wstring& value) {
    return std::any_of(list.begin(), list.end(),
                       [&value](const std::wstring& item) { return wcsicmpSafe(item, value) == 0; });
}

// 目录段前缀匹配：prefix 匹配 relativePath 的前缀，且 prefix 后是路径分隔符或完全相等。
// 防止 exclude_path=doc 误排除 documents\重要文件。
// 自动去除 prefix 尾部的 \ 或 /，兼容配置文件里写 temp\ 的情况。
bool pathStartsWithSegment(const std::wstring& relativePath, const std::wstring& prefix) {
    std::wstring p = prefix;
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    if (p.empty()) return false;
    if (!startsWithNoCase(relativePath, p)) return false;
    if (relativePath.size() == p.size()) return true;
    const wchar_t next = relativePath[p.size()];
    return next == L'\\' || next == L'/';
}

}  // namespace

bool FilterRule::isMatch(const FileInfo& info) const {
    // 扩展名
    if (!includeExtensions.empty() || !excludeExtensions.empty()) {
        const std::wstring ext = lowerExtension(info.name);
        if (!includeExtensions.empty() && !listContains(includeExtensions, ext)) return false;
        if (listContains(excludeExtensions, ext)) return false;
    }
    // 相对路径前缀（include 为空则不限）
    if (!includeSubPaths.empty()) {
        const bool matched = std::any_of(
            includeSubPaths.begin(), includeSubPaths.end(),
            [&info](const std::wstring& p) { return pathStartsWithSegment(info.relativePath, p); });
        if (!matched) return false;
    }
    if (std::any_of(excludeSubPaths.begin(), excludeSubPaths.end(),
                    [&info](const std::wstring& p) { return pathStartsWithSegment(info.relativePath, p); })) {
        return false;
    }
    // 大小
    if (minSize != 0 && info.size < minSize) return false;
    if (maxSize != 0 && info.size > maxSize) return false;
    // 修改时间
    if (modifiedAfterEnabled && info.modifiedTime < modifiedAfterUnix) return false;
    // 空文件
    if (skipEmptyFiles && info.size == 0) return false;
    return true;
}

std::vector<FileInfo> FileFilter::filter(const std::vector<FileInfo>& in, bool keepDirectories) const {
    std::vector<FileInfo> out;
    out.reserve(in.size());
    for (const auto& f : in) {
        if (f.type == FileType::Directory) {
            if (keepDirectories) out.push_back(f);
            continue;
        }
        if (rule_.isMatch(f)) out.push_back(f);
    }
    return out;
}

}  // namespace backup
