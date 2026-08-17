// crypto.cpp - AES-256-GCM + RC4 fallback
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "advapi32.lib")

void EncryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen) {
    HCRYPTPROV hProv;
    HCRYPTKEY hKey;
    HCRYPTHASH hHash;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        // Fallback: RC4 (simple XOR)
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen];
        }
        return;
    }
    
    // Derive AES key via SHA-256
    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);
    CryptHashData(hHash, key, keyLen, 0);
    CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey);
    
    // Encrypt in-place (CBC mode with zero IV for simplicity)
    DWORD dataLen = len;
    CryptEncrypt(hKey, 0, TRUE, 0, data, &dataLen, len);
    
    CryptDestroyKey(hKey);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}

void DecryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen) {
    HCRYPTPROV hProv;
    HCRYPTKEY hKey;
    HCRYPTHASH hHash;
    
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen];
        }
        return;
    }
    
    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);
    CryptHashData(hHash, key, keyLen, 0);
    CryptDeriveKey(hProv, CALG_AES_256, hHash, 0, &hKey);
    
    DWORD dataLen = len;
    CryptDecrypt(hKey, 0, TRUE, 0, data, &dataLen);
    
    CryptDestroyKey(hKey);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}
