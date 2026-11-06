// BcryptBlockCipher.cpp - BCrypt 分组密码通用实现
// 全部对称加解密调用系统库：
//   BCryptOpenAlgorithmProvider/BCryptSetProperty 选定算法+CBC
//   BCryptGenerateSymmetricKey 导入密钥
//   BCryptEncrypt/BCryptDecrypt（BCRYPT_BLOCK_PADDING 自动处理 PKCS7）
//   BCryptGenRandom 密码学安全随机 IV
#include "core/BcryptBlockCipher.h"

#include <cstring>

#include "core/HashCalculator.h"

namespace backup {

namespace {

// 执行一次 CBC 转换（输入块对齐，flags=0 不填充）。
// 每次传入 IV 副本（BCrypt 会就地改写 IV 缓冲区），保证纯函数语义。
NTSTATUS cbcTransform(bool encrypt, BCRYPT_KEY_HANDLE hKey,
                      const uint8_t* in, ULONG inLen,
                      const uint8_t* iv, size_t blockSize,
                      uint8_t* out, ULONG outCap, ULONG* outLen, ULONG flags) {
    std::vector<uint8_t> ivCopy(iv, iv + blockSize);
    if (encrypt) {
        return BCryptEncrypt(hKey, const_cast<PUCHAR>(in), inLen, nullptr,
                             ivCopy.data(), static_cast<ULONG>(blockSize),
                             out, outCap, outLen, flags);
    }
    return BCryptDecrypt(hKey, const_cast<PUCHAR>(in), inLen, nullptr,
                         ivCopy.data(), static_cast<ULONG>(blockSize),
                         out, outCap, outLen, flags);
}

}  // namespace

void BcryptBlockCipher::init(LPCWSTR algId, const uint8_t* key, size_t keyLen) {
    NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg_, algId, nullptr, 0);
    if (!BCRYPT_SUCCESS(st)) {
        hAlg_ = nullptr;
        return;
    }

    st = BCryptSetProperty(hAlg_, BCRYPT_CHAINING_MODE,
                           reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                           sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!BCRYPT_SUCCESS(st)) {
        return;
    }

    st = BCryptGenerateSymmetricKey(hAlg_, &hKey_, nullptr, 0,
                                    const_cast<PUCHAR>(key), static_cast<ULONG>(keyLen), 0);
    if (!BCRYPT_SUCCESS(st)) {
        hKey_ = nullptr;
    }
}

BcryptBlockCipher::BcryptBlockCipher(LPCWSTR algId, size_t blockSize, size_t keySize,
                                     const std::string& password)
    : blockSize_(blockSize), keySize_(keySize) {
    // 统一用 SHA-256(password) 得到 32 字节，按算法 keySize 截取（AES 32 / 3DES 24）。
    const std::string derived = HashCalculator::bufferSha256(password.data(), password.size());
    std::vector<uint8_t> key(keySize_, 0);
    const size_t n = keySize_ < derived.size() ? keySize_ : derived.size();
    std::memcpy(key.data(), derived.data(), n);
    init(algId, key.data(), key.size());
}

BcryptBlockCipher::BcryptBlockCipher(LPCWSTR algId, size_t blockSize, size_t keySize,
                                     const uint8_t* rawKey, size_t rawLen)
    : blockSize_(blockSize), keySize_(keySize) {
    std::vector<uint8_t> key(keySize_, 0);
    const size_t n = rawLen < keySize_ ? rawLen : keySize_;
    if (rawKey && n > 0) std::memcpy(key.data(), rawKey, n);
    init(algId, key.data(), key.size());
}

BcryptBlockCipher::~BcryptBlockCipher() {
    if (hKey_) BCryptDestroyKey(hKey_);
    if (hAlg_) BCryptCloseAlgorithmProvider(hAlg_, 0);
}

void BcryptBlockCipher::generateRandomIv(uint8_t* iv) const {
    const NTSTATUS st = BCryptGenRandom(
        nullptr, iv, static_cast<ULONG>(blockSize_), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(st)) {
        // 极端兜底（不应发生）
        const uint64_t t = static_cast<uint64_t>(::GetTickCount64());
        const uint32_t pid = ::GetCurrentProcessId();
        std::memset(iv, 0, blockSize_);
        std::memcpy(iv, &t, sizeof(t) < blockSize_ ? sizeof(t) : blockSize_);
        if (blockSize_ > 8) std::memcpy(iv + 8, &pid, sizeof(pid));
    }
}

void BcryptBlockCipher::encryptCbcBlocks(const uint8_t* in, size_t blocks,
                                         uint8_t* out, uint8_t* prev) const {
    if (!hKey_ || blocks == 0) return;
    const ULONG cb = static_cast<ULONG>(blocks * blockSize_);
    ULONG written = 0;
    const NTSTATUS st = cbcTransform(true, hKey_, in, cb, prev, blockSize_,
                                     out, cb, &written, 0);
    if (BCRYPT_SUCCESS(st) && written == cb) {
        std::memcpy(prev, out + (blocks - 1) * blockSize_, blockSize_);
    }
}

void BcryptBlockCipher::decryptCbcBlocks(const uint8_t* in, size_t blocks,
                                         uint8_t* out, uint8_t* prev) const {
    if (!hKey_ || blocks == 0) return;
    const ULONG cb = static_cast<ULONG>(blocks * blockSize_);
    ULONG written = 0;
    const NTSTATUS st = cbcTransform(false, hKey_, in, cb, prev, blockSize_,
                                     out, cb, &written, 0);
    if (BCRYPT_SUCCESS(st) && written == cb) {
        // CBC 解密链式状态是最后一个密文块（输入的最后一块）
        std::memcpy(prev, in + (blocks - 1) * blockSize_, blockSize_);
    }
}

std::vector<uint8_t> BcryptBlockCipher::encrypt(const uint8_t* data, size_t len) const {
    if ((!data && len > 0) || !hKey_) return {};
    std::vector<uint8_t> iv(blockSize_);
    generateRandomIv(iv.data());
    return encryptWithIV(data, len, iv.data());
}

std::vector<uint8_t> BcryptBlockCipher::encryptWithIV(const uint8_t* data, size_t len,
                                                      const uint8_t* iv) const {
    if (!hKey_) return {};
    static const uint8_t kEmpty = 0;
    PUCHAR pIn = data ? const_cast<PUCHAR>(data) : const_cast<PUCHAR>(&kEmpty);
    const ULONG cb = static_cast<ULONG>(len);

    // 查询带填充后的输出长度
    ULONG needed = 0;
    NTSTATUS st = cbcTransform(true, hKey_, pIn, cb, iv, blockSize_,
                               nullptr, 0, &needed, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(st)) return {};

    std::vector<uint8_t> result(blockSize_ + needed);
    std::memcpy(result.data(), iv, blockSize_);

    ULONG written = 0;
    st = cbcTransform(true, hKey_, pIn, cb, iv, blockSize_,
                      result.data() + blockSize_, needed, &written, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(st)) return {};

    result.resize(blockSize_ + written);
    return result;
}

std::vector<uint8_t> BcryptBlockCipher::decrypt(const uint8_t* data, size_t len,
                                                bool* success) const {
    if (success) *success = false;
    if (!hKey_ || !data || len < blockSize_ * 2) return {};
    if (len % blockSize_ != 0) return {};

    const ULONG cb = static_cast<ULONG>(len - blockSize_);
    PUCHAR pCipher = const_cast<PUCHAR>(data + blockSize_);

    // 查询去填充后的明文长度；padding 非法时直接失败
    ULONG needed = 0;
    NTSTATUS st = cbcTransform(false, hKey_, pCipher, cb, data, blockSize_,
                               nullptr, 0, &needed, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(st)) return {};

    std::vector<uint8_t> out(needed);
    ULONG written = 0;
    st = cbcTransform(false, hKey_, pCipher, cb, data, blockSize_,
                      out.data(), needed, &written, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(st)) return {};

    out.resize(written);
    if (success) *success = true;
    return out;
}

}  // namespace backup
