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
                       const ProgressCallback& progress,
                       std::vector<std::wstring>* outWarnings) {
    outFiles.clear();
    outErrors.clear();
    if (outWarnings) outWarnings->clear();

    if (!FileSystem::exists(root) || !FileSystem::isDirectory(root)) {
        outErrors.push_back(std::wstring(L"源路径不存在或不是目录: ") + root);
        return false;
    }

    // 先序遍历（preorder）：先访问当前目录的条目，再逐级深入子目录。
    // 对应课程《文件系统》路径遍历图：从根 inode 的目录块出发，
    // 按 Name->inode 映射逐级定位（/ -> a -> b -> c.c）。
    scanRecursive(root, L"", outFiles, outErrors, cancel, progress, outWarnings);

    // 输出保持先序遍历结构（父条目必在子目录树之前）。
    // 每个目录内的兄弟条目已在 scanRecursive 中按名称排序，保证确定性。
    return true;
}

void FileScanner::scanRecursive(const std::wstring& absRoot,
                                const std::wstring& relDir,
                                std::vector<FileInfo>& out,
                                std::vector<std::wstring>& errors,
                                const CancelCheck& cancel,
                                const ProgressCallback& progress,
                                std::vector<std::wstring>* warnings) {
    const std::wstring absDir = relDir.empty() ? absRoot : absRoot + L"\\" + relDir;

    std::vector<std::pair<std::wstring, FileType>> entries;
    if (!FileSystem::listDirectory(absDir, entries)) {
        errors.push_back(std::wstring(L"无法枚举目录: ") + absDir);
        return;
    }

    // 目录内条目按名称排序（大小写不敏感），保证先序输出确定性。
    std::sort(entries.begin(), entries.end(),
              [](const std::pair<std::wstring, FileType>& a,
                 const std::pair<std::wstring, FileType>& b) {
                  return wcsicmpSafe(a.first, b.first) < 0;
              });

    // ---- 先序遍历核心：当前条目先入队，目录条目立即递归子树 ----
    for (const auto& entry : entries) {
        if (cancel && cancel()) return;
        const std::wstring rel = relDir.empty() ? entry.first : relDir + L"\\" + entry.first;
        const std::wstring abs = absRoot + L"\\" + rel;
        if (progress) progress(rel);

        // 读取该条目的"文件头"（对应 inode：类型/大小/时间/inode号/硬链接计数）
        FileInfo info;
        info.relativePath = rel;
        info.name = entry.first;
        info.type = entry.second;
        if (!FileSystem::getFileInfo(abs, info)) {
            errors.push_back(std::wstring(L"无法获取文件信息: ") + abs);
            continue;
        }
        if (info.type == FileType::Symlink) {
            // 符号链接：备份链接本身（读取目标路径），不跟随、不递归，防止成环或越界。
            // 对应 Unix symlink（TAR typeflag='2'）。读取失败也记录该链接，目标留空。
            std::wstring target;
            if (FileSystem::readSymlinkTarget(abs, target)) {
                info.symlinkTarget = target;
            }
            if (warnings) warnings->push_back(std::wstring(L"包含符号链接: ") + rel);
            out.push_back(info);
            continue;
        }
        // 先序：父条目先于子目录树
        out.push_back(info);

        if (info.type == FileType::Directory) {
            scanRecursive(absRoot, rel, out, errors, cancel, progress, warnings);
        }
    }
}

}  // namespace backup
