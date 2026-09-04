// FileSystem.h - Windows 文件系统操作封装
// 职责：集中封装 Win32 文件系统 API（CreateFileW/ReadFile/WriteFile/FindFirstFileW 等），
//       为上层提供统一的文件操作能力。业务层不直接调用 Win32 API。
// 对应需求文档 6.7 FileSystem 与 10 Windows 文件系统设计。
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/FileInfo.h"

namespace backup {

// RAII 包装 Windows HANDLE（需求文档 10.2 资源管理）。
// 禁止在裸代码中手动 CloseHandle 的异常路径，统一交给析构释放。
class FileHandle {
public:
    FileHandle() = default;
    explicit FileHandle(HANDLE h) : handle_(h) {}
    ~FileHandle() { close(); }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    FileHandle(FileHandle&& o) noexcept : handle_(o.handle_) { o.handle_ = INVALID_HANDLE_VALUE; }
    FileHandle& operator=(FileHandle&& o) noexcept {
        if (this != &o) {
            close();
            handle_ = o.handle_;
            o.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    void close() {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE get() const { return handle_; }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class FileSystem {
public:
    static bool exists(const std::wstring& path);
    static bool isDirectory(const std::wstring& path);

    // 递归创建目录（含全部父目录）。
    static bool createDirectories(const std::wstring& path);

    // 列出目录下的条目（名称 + 类型），不含 "." 与 ".."。
    static bool listDirectory(const std::wstring& dirPath,
                              std::vector<std::pair<std::wstring, FileType>>& out);

    // 获取文件信息（大小、类型、时间）。路径不存在返回 false。
    static bool getFileInfo(const std::wstring& path, FileInfo& out);

    // 打开文件：读取（共享读/写/删除，允许处理被占用的文件）。
    static FileHandle openRead(const std::wstring& path, std::string* err = nullptr);
    // 打开文件：写入。overwrite=true 覆盖已存在文件，false 仅在不存在时创建。
    static FileHandle openWrite(const std::wstring& path, bool overwrite, std::string* err = nullptr);

    // 分块读写。
    static bool read(HANDLE h, void* buf, DWORD n, DWORD& readCount, std::string* err = nullptr);
    static bool write(HANDLE h, const void* buf, DWORD n, DWORD& writtenCount, std::string* err = nullptr);

    // 恢复修改/创建时间（Unix 秒；0 表示不设置该字段）。
    static bool setFileTimes(const std::wstring& path, uint64_t createdSec, uint64_t modifiedSec);

    static bool deleteFile(const std::wstring& path);

    // 当前 Unix 时间（秒）。
    static uint64_t nowSeconds();

    // 将 Win32 错误码转为可读文本。
    static std::wstring errorString(DWORD err);
    static DWORD lastError() { return ::GetLastError(); }
};

}  // namespace backup
