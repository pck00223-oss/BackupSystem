// Error.h - 统一错误码与业务异常
// 职责：为整个系统定义统一的错误码集合，以及可携带宽字符消息的异常类型。
#pragma once

#include <stdexcept>
#include <string>

#include "core/Utf.h"

namespace backup {

// 对应需求文档 14.6 中要求的错误类型集合。
enum class ErrorCode {
    None = 0,
    FileNotFound,   // 路径不存在
    AccessDenied,   // 权限不足
    FileLocked,     // 文件被其他程序占用
    DiskFull,       // 磁盘空间不足
    InvalidPath,    // 路径非法
    InvalidBackup,  // 备份文件/Manifest 损坏
    HashMismatch,   // 校验不一致
    WrongPassword,  // 密码错误（加密扩展时使用）
    Cancelled,      // 用户取消
    ReadError,      // 读失败
    WriteError,     // 写失败
    InvalidConfig,  // 配置非法
    UnknownError    // 其他错误
};

// 错误码对应的英文名称（用于日志与展示）。
const char* errorCodeName(ErrorCode code);

// 业务异常：code 供程序化判断，wmessage 保存原始宽字符信息。
class BackupError : public std::runtime_error {
public:
    BackupError(ErrorCode code, const std::wstring& message)
        : std::runtime_error(wideToUtf8(message)), code_(code), wmessage_(message) {}

    ErrorCode code() const noexcept { return code_; }
    const std::wstring& wmessage() const noexcept { return wmessage_; }

private:
    ErrorCode code_;
    std::wstring wmessage_;
};

}  // namespace backup
