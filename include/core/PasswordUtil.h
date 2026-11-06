// PasswordUtil.h - 密码验证标记工具（共享）
// 职责：计算密码验证标记，供备份写入、恢复前置校验、增量密码比对共用，避免逻辑复制。
// 说明：这不是密码安全存储，仅用 SHA256(password+固定盐) 快速判断密码是否正确；
//       固定盐必须保持不变，以兼容历史 Manifest。
#pragma once

#include <string>

#include "core/HashCalculator.h"

namespace backup {

// 计算密码验证标记：SHA256(password + 固定盐)。
inline std::string computePasswordVerifier(const std::string& password) {
    static const char kSalt[] = "|BackupSystem_AES_Password_Verifier_v1";
    const std::string salted = password + kSalt;
    return HashCalculator::bufferSha256(salted.data(), salted.size());
}

}  // namespace backup
