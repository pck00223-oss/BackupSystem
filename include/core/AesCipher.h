// AesCipher.h - AES-256-CBC 具体实现（BCrypt 算法 ID: AES，块16，密钥32）
// 职责：IEncryptionAlgorithm 的 AES 具体策略，全部加解密由 BCrypt 完成。
#pragma once

#include <cstdint>
#include <string>

#include "core/BcryptBlockCipher.h"

namespace backup {

class AesCipher : public BcryptBlockCipher {
public:
    explicit AesCipher(const std::string& password)
        : BcryptBlockCipher(BCRYPT_AES_ALGORITHM, 16, 32, password) {}

    // 测试用：原始 32 字节密钥（NIST 向量）
    AesCipher(const uint8_t* rawKey, size_t rawLen)
        : BcryptBlockCipher(BCRYPT_AES_ALGORITHM, 16, 32, rawKey, rawLen) {}

    std::string name() const override { return "aes256"; }
};

}  // namespace backup
