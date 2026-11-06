// EncryptionFactory.cpp - 加密算法工厂实现
// 按算法名创建对应具体策略，调用方只拿到 IEncryptionAlgorithm 接口。
#include <memory>
#include <string>

#include "core/AesCipher.h"
#include "core/IEncryptionAlgorithm.h"
#include "core/TripleDesCipher.h"

namespace backup {

std::unique_ptr<IEncryptionAlgorithm>
createEncryptionAlgorithm(const std::string& name, const std::string& password) {
    if (name == "aes256") {
        return std::make_unique<AesCipher>(password);
    }
    if (name == "3des") {
        return std::make_unique<TripleDesCipher>(password);
    }
    return nullptr;  // 未知算法
}

}  // namespace backup
