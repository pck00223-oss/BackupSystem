// AesEncryptor.cpp - AES-256-CBC 加密/解密实现（自实现，零第三方依赖）
#include "core/AesEncryptor.h"

#include <algorithm>
#include <cstring>

#include <windows.h>

#include "core/HashCalculator.h"

namespace backup {

namespace {

// AES S-box
const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

// AES 逆 S-box
const uint8_t INV_SBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

// 轮常量
const uint8_t RCON[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

// GF(2^8) 乘法
uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        const bool hi = (a & 0x80) != 0;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

}  // namespace

AesEncryptor::AesEncryptor(const std::string& password) {
    // 用 SHA-256(password) 派生 256 位密钥
    std::string hash = HashCalculator::bufferSha256(password.data(), password.size());
    if (hash.size() >= KEY_SIZE) {
        std::memcpy(key_, hash.data(), KEY_SIZE);
    } else {
        // 兜底：用密码直接填充（不应发生）
        std::memset(key_, 0, KEY_SIZE);
        const size_t copyLen = password.size() < KEY_SIZE ? password.size() : KEY_SIZE;
        std::memcpy(key_, password.data(), copyLen);
    }
    keyExpansion();
}

void AesEncryptor::keyExpansion() {
    std::memcpy(roundKeys_, key_, KEY_SIZE);
    size_t bytesGenerated = KEY_SIZE;
    int rconIter = 1;
    uint8_t temp[4];

    while (bytesGenerated < 240) {  // 15轮 × 16字节 = 240
        std::memcpy(temp, roundKeys_ + bytesGenerated - 4, 4);

        if (bytesGenerated % KEY_SIZE == 0) {
            // RotWord
            const uint8_t t = temp[0];
            temp[0] = temp[1]; temp[1] = temp[2]; temp[2] = temp[3]; temp[3] = t;
            // SubWord
            temp[0] = SBOX[temp[0]];
            temp[1] = SBOX[temp[1]];
            temp[2] = SBOX[temp[2]];
            temp[3] = SBOX[temp[3]];
            // Rcon
            temp[0] ^= RCON[rconIter++];
        } else if (bytesGenerated % KEY_SIZE == 16) {
            // AES-256 特有：第 16 字节位置额外 SubWord
            temp[0] = SBOX[temp[0]];
            temp[1] = SBOX[temp[1]];
            temp[2] = SBOX[temp[2]];
            temp[3] = SBOX[temp[3]];
        }

        for (int i = 0; i < 4; ++i) {
            roundKeys_[bytesGenerated] = roundKeys_[bytesGenerated - KEY_SIZE] ^ temp[i];
            ++bytesGenerated;
        }
    }
}

void AesEncryptor::encryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t state[16];
    std::memcpy(state, in, 16);

    // AddRoundKey (第0轮)
    for (int i = 0; i < 16; ++i) state[i] ^= roundKeys_[i];

    for (int round = 1; round < NUM_ROUNDS; ++round) {
        // SubBytes
        for (int i = 0; i < 16; ++i) state[i] = SBOX[state[i]];
        // ShiftRows
        uint8_t tmp;
        tmp = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = tmp;
        tmp = state[2]; state[2] = state[10]; state[10] = tmp;
        tmp = state[6]; state[6] = state[14]; state[14] = tmp;
        tmp = state[3]; state[3] = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = tmp;
        // MixColumns
        for (int c = 0; c < 4; ++c) {
            uint8_t* col = state + c * 4;
            const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            col[0] = gmul(a0,2) ^ gmul(a1,3) ^ a2 ^ a3;
            col[1] = a0 ^ gmul(a1,2) ^ gmul(a2,3) ^ a3;
            col[2] = a0 ^ a1 ^ gmul(a2,2) ^ gmul(a3,3);
            col[3] = gmul(a0,3) ^ a1 ^ a2 ^ gmul(a3,2);
        }
        // AddRoundKey
        const uint8_t* rk = roundKeys_ + round * 16;
        for (int i = 0; i < 16; ++i) state[i] ^= rk[i];
    }

    // 最后一轮（无 MixColumns）
    for (int i = 0; i < 16; ++i) state[i] = SBOX[state[i]];
    uint8_t tmp;
    tmp = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = tmp;
    tmp = state[2]; state[2] = state[10]; state[10] = tmp;
    tmp = state[6]; state[6] = state[14]; state[14] = tmp;
    tmp = state[3]; state[3] = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = tmp;
    const uint8_t* rk = roundKeys_ + NUM_ROUNDS * 16;
    for (int i = 0; i < 16; ++i) state[i] ^= rk[i];

    std::memcpy(out, state, 16);
}

void AesEncryptor::decryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t state[16];
    std::memcpy(state, in, 16);

    // AddRoundKey (最后一轮)
    const uint8_t* rk = roundKeys_ + NUM_ROUNDS * 16;
    for (int i = 0; i < 16; ++i) state[i] ^= rk[i];

    for (int round = NUM_ROUNDS - 1; round >= 1; --round) {
        // InvShiftRows
        uint8_t tmp;
        tmp = state[13]; state[13] = state[9]; state[9] = state[5]; state[5] = state[1]; state[1] = tmp;
        tmp = state[2]; state[2] = state[10]; state[10] = tmp;
        tmp = state[6]; state[6] = state[14]; state[14] = tmp;
        tmp = state[7]; state[7] = state[11]; state[11] = state[15]; state[15] = state[3]; state[3] = tmp;
        // InvSubBytes
        for (int i = 0; i < 16; ++i) state[i] = INV_SBOX[state[i]];
        // AddRoundKey
        rk = roundKeys_ + round * 16;
        for (int i = 0; i < 16; ++i) state[i] ^= rk[i];
        // InvMixColumns
        for (int c = 0; c < 4; ++c) {
            uint8_t* col = state + c * 4;
            const uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            col[0] = gmul(a0,0x0e) ^ gmul(a1,0x0b) ^ gmul(a2,0x0d) ^ gmul(a3,0x09);
            col[1] = gmul(a0,0x09) ^ gmul(a1,0x0e) ^ gmul(a2,0x0b) ^ gmul(a3,0x0d);
            col[2] = gmul(a0,0x0d) ^ gmul(a1,0x09) ^ gmul(a2,0x0e) ^ gmul(a3,0x0b);
            col[3] = gmul(a0,0x0b) ^ gmul(a1,0x0d) ^ gmul(a2,0x09) ^ gmul(a3,0x0e);
        }
    }

    // 第0轮（无 InvMixColumns）
    uint8_t tmp;
    tmp = state[13]; state[13] = state[9]; state[9] = state[5]; state[5] = state[1]; state[1] = tmp;
    tmp = state[2]; state[2] = state[10]; state[10] = tmp;
    tmp = state[6]; state[6] = state[14]; state[14] = tmp;
    tmp = state[7]; state[7] = state[11]; state[11] = state[15]; state[15] = state[3]; state[3] = tmp;
    for (int i = 0; i < 16; ++i) state[i] = INV_SBOX[state[i]];
    rk = roundKeys_;
    for (int i = 0; i < 16; ++i) state[i] ^= rk[i];

    std::memcpy(out, state, 16);
}

void AesEncryptor::generateIV(uint8_t iv[16]) {
    // 用系统时间 + 进程 ID 生成伪随机 IV（够用，非密码学安全）
    const uint64_t t = static_cast<uint64_t>(::GetTickCount64());
    const uint32_t pid = ::GetCurrentProcessId();
    std::memset(iv, 0, 16);
    std::memcpy(iv, &t, sizeof(t));
    std::memcpy(iv + 8, &pid, sizeof(pid));
    // 再混入一些熵
    for (int i = 0; i < 16; ++i) {
        iv[i] ^= static_cast<uint8_t>(::GetTickCount() & 0xFF);
    }
}

std::vector<uint8_t> AesEncryptor::encrypt(const uint8_t* plaintext, size_t len) const {
    if (!plaintext && len > 0) return {};
    // 空文件也允许加密：PKCS7 填充会填充一整个块（16 字节，值为 16）。

    // PKCS7 填充
    const size_t padLen = BLOCK_SIZE - (len % BLOCK_SIZE);
    std::vector<uint8_t> padded(len + padLen);
    if (len > 0 && plaintext) {
        std::copy(plaintext, plaintext + len, padded.begin());
    }
    for (size_t i = len; i < padded.size(); ++i) padded[i] = static_cast<uint8_t>(padLen);

    // 生成 IV
    uint8_t iv[BLOCK_SIZE];
    generateIV(iv);

    // CBC 加密
    std::vector<uint8_t> result(BLOCK_SIZE + padded.size());
    std::memcpy(result.data(), iv, BLOCK_SIZE);

    uint8_t prev[BLOCK_SIZE];
    std::memcpy(prev, iv, BLOCK_SIZE);

    for (size_t offset = 0; offset < padded.size(); offset += BLOCK_SIZE) {
        uint8_t block[BLOCK_SIZE];
        for (size_t i = 0; i < BLOCK_SIZE; ++i) block[i] = padded[offset + i] ^ prev[i];
        uint8_t cipher[BLOCK_SIZE];
        encryptBlock(block, cipher);
        std::memcpy(result.data() + BLOCK_SIZE + offset, cipher, BLOCK_SIZE);
        std::memcpy(prev, cipher, BLOCK_SIZE);
    }

    return result;
}

std::vector<uint8_t> AesEncryptor::decrypt(const uint8_t* data, size_t len, bool* success) const {
    if (success) *success = false;
    if (!data || len < BLOCK_SIZE * 2) return {};  // 至少 IV + 一个密文块

    const uint8_t* iv = data;
    const uint8_t* cipher = data + BLOCK_SIZE;
    const size_t cipherLen = len - BLOCK_SIZE;
    if (cipherLen % BLOCK_SIZE != 0) return {};

    std::vector<uint8_t> padded(cipherLen);
    uint8_t prev[BLOCK_SIZE];
    std::memcpy(prev, iv, BLOCK_SIZE);

    for (size_t offset = 0; offset < cipherLen; offset += BLOCK_SIZE) {
        uint8_t block[BLOCK_SIZE];
        decryptBlock(cipher + offset, block);
        for (size_t i = 0; i < BLOCK_SIZE; ++i) padded[offset + i] = block[i] ^ prev[i];
        std::memcpy(prev, cipher + offset, BLOCK_SIZE);
    }

    // 去除 PKCS7 填充
    const uint8_t padLen = padded.back();
    if (padLen == 0 || padLen > BLOCK_SIZE) return {};
    const bool padValid = std::all_of(padded.end() - padLen, padded.end(),
                                        [padLen](uint8_t b) { return b == padLen; });
    if (!padValid) return {};
    padded.resize(padded.size() - padLen);
    if (success) *success = true;
    return padded;
}

}  // namespace backup
