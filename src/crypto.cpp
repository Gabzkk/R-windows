// crypto.cpp - Windows CNG (BCrypt) AES-256-CBC & Dynamic XOR Engine
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

extern "C" {

void EncryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    NTSTATUS status;
    
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status)) {
        // High-speed fallback: dynamic rolling XOR
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen] ^ (unsigned char)(i & 0xFF);
        }
        return;
    }
    
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen] ^ (unsigned char)(i & 0xFF);
        }
        return;
    }
    
    // Hash key with SHA-256 to ensure exact 256-bit key
    BCRYPT_ALG_HANDLE hHashAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    unsigned char keyHash[32];
    
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&hHashAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0))) {
        if (NT_SUCCESS(BCryptCreateHash(hHashAlg, &hHash, NULL, 0, NULL, 0, 0))) {
            BCryptHashData(hHash, key, keyLen, 0);
            BCryptFinishHash(hHash, keyHash, sizeof(keyHash), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hHashAlg, 0);
    } else {
        memcpy(keyHash, key, keyLen > 32 ? 32 : keyLen);
    }
    
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, keyHash, sizeof(keyHash), 0);
    if (NT_SUCCESS(status)) {
        unsigned char iv[16] = {0}; // Standard CBC zero-IV initialization
        ULONG resultSize = 0;
        BCryptEncrypt(hKey, data, len, NULL, iv, sizeof(iv), data, len, &resultSize, 0);
        BCryptDestroyKey(hKey);
    } else {
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen] ^ (unsigned char)(i & 0xFF);
        }
    }
    
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
}

void DecryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    NTSTATUS status;
    
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!NT_SUCCESS(status)) {
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen] ^ (unsigned char)(i & 0xFF);
        }
        return;
    }
    
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen] ^ (unsigned char)(i & 0xFF);
        }
        return;
    }
    
    BCRYPT_ALG_HANDLE hHashAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    unsigned char keyHash[32];
    
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&hHashAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0))) {
        if (NT_SUCCESS(BCryptCreateHash(hHashAlg, &hHash, NULL, 0, NULL, 0, 0))) {
            BCryptHashData(hHash, key, keyLen, 0);
            BCryptFinishHash(hHash, keyHash, sizeof(keyHash), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hHashAlg, 0);
    } else {
        memcpy(keyHash, key, keyLen > 32 ? 32 : keyLen);
    }
    
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, keyHash, sizeof(keyHash), 0);
    if (NT_SUCCESS(status)) {
        unsigned char iv[16] = {0};
        ULONG resultSize = 0;
        BCryptDecrypt(hKey, data, len, NULL, iv, sizeof(iv), data, len, &resultSize, 0);
        BCryptDestroyKey(hKey);
    } else {
        for (int i = 0; i < len; i++) {
            data[i] ^= key[i % keyLen] ^ (unsigned char)(i & 0xFF);
        }
    }
    
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
}

}
