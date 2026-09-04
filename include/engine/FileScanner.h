// FileScanner.h - 目录扫描器
// 职责：递归遍历目录树，为每个文件/目录生成 FileInfo（相对路径、大小、类型、时间）。
//       单个条目失败时记录错误并继续，不中断整个扫描。
// 不负责：备份、压缩、加密、GUI（需求文档 6.2 职责边界）。
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/FileInfo.h"

namespace backup {

class FileScanner {
public:
    // 扫描过程中的回调。
    using ProgressCallback = std::function<void(const std::wstring& relativePath)>;
    // 取消检查：返回 true 表示应中止扫描。
    using CancelCheck = std::function<bool()>;

    // 扫描 root，输出全部条目（含目录）并按相对路径排序。
    // 根目录不存在/不是目录时返回 false 并写入 outErrors。
    static bool scan(const std::wstring& root,
                     std::vector<FileInfo>& outFiles,
                     std::vector<std::wstring>& outErrors,
                     const CancelCheck& cancel = nullptr,
                     const ProgressCallback& progress = nullptr);

private:
    static void scanRecursive(const std::wstring& absRoot,
                              const std::wstring& relDir,
                              std::vector<FileInfo>& out,
                              std::vector<std::wstring>& errors,
                              const CancelCheck& cancel,
                              const ProgressCallback& progress);
};

}  // namespace backup
