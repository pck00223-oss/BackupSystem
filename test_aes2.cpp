#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "core/AesEncryptor.h"
#pragma comment(lib, "bcrypt.lib")
using namespace backup;
int main() {
    const uint8_t key[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    const uint8_t pt[16] = {0,17,34,51,68,85,102,119,136,153,170,187,204,221,238,255};
    const uint8_t zeroIv[16] = {0};
    
    // ????
    AesEncryptor aes(key, 32);
    std::vector<uint8_t> result = aes.encryptWithIV(pt, 16, zeroIv);
    printf("My impl (first cipher block): ");
    for (int i = 16; i < 32; i++) printf("%02x", result[i]);
    printf("\n");
    
    // BCrypt
    uint8_t ct[16];
    BCRYPT_ALG_HANDLE hAlg;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    BCRYPT_KEY_HANDLE hKey;
    BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)key, 32, 0);
    ULONG cbResult;
    BCryptEncrypt(hKey, (PUCHAR)pt, 16, nullptr, nullptr, 0, ct, 16, &cbResult, 0);
    printf("BCrypt ECB:                    ");
    for (int i = 0; i < 16; i++) printf("%02x", ct[i]);
    printf("\n");
    printf("NIST expected:                 8ea2b7ca516745bfeafc49904b49608\n");
    
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return 0;
}
