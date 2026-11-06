// TarPacker.h - TAR 打包/解包接口（ustar / POSIX IEEE P1003.1）
// 职责：将一个目录树打包为单个 TAR 文件，或从 TAR 文件还原目录树。
//       正确处理普通文件、目录、硬链接（HARDLINK）、符号链接（SYMLINK）。
// 对应课程《打包解包》：512 字节块，Header + File data + Padding，ustar 头。
// 设计：零第三方依赖，仅用标准库 + 已有 FileSystem 封装。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/FileInfo.h"

namespace backup {

// 进度回调：每处理一个归档条目触发一次，参数为归档内相对路径。
using TarProgressCallback = std::function<void(const std::wstring& relativePath)>;

// TAR 类型标志（typeflag），对应课程 PPT 中的类型常量。
constexpr char kTarRegular = '0';     // 普通文件（REGULAR/NORMAL）
constexpr char kTarHardLink = '1';    // 硬链接（HARDLINK）
constexpr char kTarSymlink = '2';     // 符号链接（SYMLINK）
constexpr char kTarChar = '3';        // 字符设备（CHAR）
constexpr char kTarBlock = '4';       // 块设备（BLOCK）
constexpr char kTarDirectory = '5';   // 目录（DIRECTORY）
constexpr char kTarFifo = '6';        // 管道（FIFO）

class TarPacker {
public:
    // 打包：以先序遍历扫描 srcDir，写入 tarFile。
    // 硬链接：同一文件 ID（卷+inode）的多个硬链接，首个写为普通文件，后续写为
    //        HARDLINK（typeflag='1'，linkname 指向首个归档路径）。
    // 符号链接：写为 SYMLINK（typeflag='2'，linkname 记录目标），不跟随链接内容。
    // 目录：写为 DIRECTORY（typeflag='5'）。
    // 进度回调可选（每写入一个归档条目触发一次）。
    static bool packDirectory(const std::wstring& srcDir,
                              const std::wstring& tarFile,
                              std::string* err = nullptr,
                              const TarProgressCallback& progress = nullptr);

    // 解包：读取 tarFile，还原到 destDir。
    // HARDLINK -> CreateHardLinkW；SYMLINK -> CreateSymbolicLinkW；
    // REGULAR -> 写文件；DIRECTORY -> 建目录。
    static bool extractTo(const std::wstring& tarFile,
                          const std::wstring& destDir,
                          std::string* err = nullptr,
                          const TarProgressCallback& progress = nullptr);

    // 仅列出 TAR 中包含的条目（不解包），用于校验/预览。
    static bool listEntries(const std::wstring& tarFile,
                            std::vector<std::wstring>& outPaths,
                            std::string* err = nullptr);
};

}  // namespace backup
