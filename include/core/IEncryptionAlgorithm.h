// IEncryptionAlgorithm.h - 加密算法抽象接口（策略模式）
// 职责：定义对称分组加密的统一接口，供 BackupManager/RestoreManager 依赖。
// 具体实现：AES-256-CBC、3DES-CBC（均基于 Windows CNG/BCrypt 系统库，不手写算法）。
// 设计意图：管理器只依赖本接口，新增加密算法无需修改管理器（开闭原则），
//           同时这是"封装成库"对外暴露的稳定接口。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace backup {

class IEncryptionAlgorithm {
public:
    virtual ~IEncryptionAlgorithm() = default;

    // 算法标识（写入 Manifest 的 encryption 字段），如 "aes256" / "3des"。
    virtual std::string name() const = 0;

    // 分组块大小（字节）：AES=16，3DES=8。
    virtual size_t blockSize() const = 0;

    // 密钥长度（字节）：AES=32，3DES=24。
    virtual size_t keySize() const = 0;

    // 底层 BCrypt 句柄初始化是否成功（失败时加解密返回空结果）。
    virtual bool valid() const = 0;

    // 一次性加密：返回 IV(blockSize 字节) + 密文（PKCS7 填充）。
    // 空明文也会产生一个完整填充块。失败返回空 vector。
    virtual std::vector<uint8_t> encrypt(const uint8_t* data, size_t len) const = 0;

    // 一次性解密：输入应为 IV + 密文格式，返回去除填充后的明文。
    // success 非空时报告是否成功（长度不足/非块对齐/填充非法均判失败）。
    virtual std::vector<uint8_t> decrypt(const uint8_t* data, size_t len,
                                         bool* success = nullptr) const = 0;

    // 指定 IV 加密（测试用，生产代码应使用 encrypt 生成随机 IV）。
    virtual std::vector<uint8_t> encryptWithIV(const uint8_t* data, size_t len,
                                               const uint8_t* iv) const = 0;

    // 生成密码学安全随机 IV（BCryptGenRandom）。
    virtual void generateRandomIv(uint8_t* iv) const = 0;

    // ---- CBC 流式处理（大文件分块，不做填充，由调用方处理最后一块）----
    // 加密 blocks 个完整块；prev 为前一密文块，调用后更新为本次最后一个密文块。
    virtual void encryptCbcBlocks(const uint8_t* in, size_t blocks,
                                  uint8_t* out, uint8_t* prev) const = 0;

    // 解密 blocks 个完整块；prev 语义同上（CBC 链式状态为密文块）。
    virtual void decryptCbcBlocks(const uint8_t* in, size_t blocks,
                                  uint8_t* out, uint8_t* prev) const = 0;
};

// 工厂：按算法名创建加密器（name 为 "aes256"/"3des"，非法返回 nullptr）。
std::unique_ptr<IEncryptionAlgorithm>
createEncryptionAlgorithm(const std::string& name, const std::string& password);

}  // namespace backup
