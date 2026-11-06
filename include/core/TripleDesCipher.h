// TripleDesCipher.h - 3DES-CBC 具体实现（BCrypt 算法 ID: 3DES，块8，密钥24）
// 职责：IEncryptionAlgorithm 的 Triple DES 具体策略，与 AES 形成"多种算法"。
// 说明：3DES 块大小 8 字节、密钥 24 字节（192 位），用于体现不同分组密码参数。
#pragma once

#include <cstdint>
#include <string>

#include "core/BcryptBlockCipher.h"

namespace backup {

class TripleDesCipher : public BcryptBlockCipher {
public:
    explicit TripleDesCipher(const std::string& password)
        : BcryptBlockCipher(BCRYPT_3DES_ALGORITHM, 8, 24, password) {}

    // 测试用：原始 24 字节密钥
    TripleDesCipher(const uint8_t* rawKey, size_t rawLen)
        : BcryptBlockCipher(BCRYPT_3DES_ALGORITHM, 8, 24, rawKey, rawLen) {}

    std::string name() const override { return "3des"; }
};

}  // namespace backup
