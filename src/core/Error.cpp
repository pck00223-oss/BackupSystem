// Error.cpp - 错误码名称
#include "core/Error.h"

namespace backup {

const char* errorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::None:          return "None";
        case ErrorCode::FileNotFound:  return "FileNotFound";
        case ErrorCode::AccessDenied:  return "AccessDenied";
        case ErrorCode::FileLocked:    return "FileLocked";
        case ErrorCode::DiskFull:      return "DiskFull";
        case ErrorCode::InvalidPath:   return "InvalidPath";
        case ErrorCode::InvalidBackup: return "InvalidBackup";
        case ErrorCode::HashMismatch:  return "HashMismatch";
        case ErrorCode::WrongPassword: return "WrongPassword";
        case ErrorCode::Cancelled:     return "Cancelled";
        case ErrorCode::ReadError:     return "ReadError";
        case ErrorCode::WriteError:    return "WriteError";
        case ErrorCode::InvalidConfig: return "InvalidConfig";
        case ErrorCode::UnknownError:  return "UnknownError";
    }
    return "UnknownError";
}

}  // namespace backup
