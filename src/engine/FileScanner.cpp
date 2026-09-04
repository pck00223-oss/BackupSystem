// FileScanner.cpp - 目录扫描实现
#include "engine/FileScanner.h"

#include <algorithm>

#include "core/Utf.h"
#include "engine/FileSystem.h"

namespace backup {

bool FileScanner::scan(const std::wstring& root,
                       std::vector<FileInfo>& outFiles,
                       std::vector<std::wstring>& outErrors,
                       const CancelCheck& cancel,
                       const ProgressCallback& progress) {
    outFiles.clear();
    outErrors.clear();

    if (!FileSystem::exists(root) || !FileSystem::isDirectory(root)) {
        outErrors.push_back(std::wstring(L"源路径不存在或不是目录: ") + root);
        return false;
    }

    scanRecursive(root, L"", outFiles, outErrors, cancel, progress);

    // 按相对路径排序（大小写不敏感，与 Windows 文件系统行为一致）。
    // 注：若课程要求自行实现排序算法，可替换为归并/快排实现（见需求文档 11 节）。
    std::sort(outFiles.begin(), outFiles.end(),
              [](const FileInfo& a, const FileInfo& b) {
                  return wcsicmpSafe(a.relativePath, b.relativePath) < 0;
              });
    return true;
}

void FileScanner::scanRecursive(const std::wstring& absRoot,
                                const std::wstring& relDir,
                                std::vector<FileInfo>& out,
                                std::vector<std::wstring>& errors,
                                const CancelCheck& cancel,
                                const ProgressCallback& progress) {
    const std::wstring absDir = relDir.empty() ? absRoot : absRoot + L"\\" + relDir;

    std::vector<std::pair<std::wstring, FileType>> entries;
    if (!FileSystem::listDirectory(absDir, entries)) {
        errors.push_back(std::wstring(L"无法枚举目录: ") + absDir);
        return;
    }

    for (const auto& entry : entries) {
        if (cancel && cancel()) return;
        const std::wstring rel = relDir.empty() ? entry.first : relDir + L"\\" + entry.first;
        const std::wstring abs = absRoot + L"\\" + rel;
        if (progress) progress(rel);

        FileInfo info;
        info.relativePath = rel;
        info.name = entry.first;
        info.type = entry.second;
        if (!FileSystem::getFileInfo(abs, info)) {
            errors.push_back(std::wstring(L"无法获取文件信息: ") + abs);
            continue;
        }
        out.push_back(info);

        if (info.type == FileType::Directory) {
            scanRecursive(absRoot, rel, out, errors, cancel, progress);
        }
    }
}

}  // namespace backup
