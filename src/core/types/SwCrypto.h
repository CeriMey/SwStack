/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cstring>   // std::strlen

#include "SwDebug.h"
static constexpr const char* kSwLogCategory_SwCrypto = "sw.core.types.swcrypto";


//==============================//
//            Windows           //
//==============================//
#if defined(_WIN32)
  #include "platform/win/SwWindows.h"
  #include <bcrypt.h>
  #ifndef STATUS_NOT_IMPLEMENTED
    #define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
  #endif
  #pragma comment(lib, "bcrypt.lib")
#endif

//==============================//
//            Linux             //
//==============================//
#if !defined(_WIN32)
  // OpenSSL pour Linux
  #include <openssl/evp.h>
  #include <openssl/hmac.h>
  #include <openssl/err.h>
#endif


//--------------------------------------------------------------------------------------------------
// BcryptWrapper : uniquement côté Windows (inchangé côté API, bug de masquage corrigé)
//--------------------------------------------------------------------------------------------------
#if defined(_WIN32)
class BcryptWrapper {
public:
    static BcryptWrapper& instance() {
        static BcryptWrapper wrapper;
        return wrapper;
    }

    static NTSTATUS OpenAlgorithmProvider(BCRYPT_ALG_HANDLE* phAlgorithm, LPCWSTR pszAlgId, LPCWSTR pszImplementation, ULONG dwFlags) {
        if (!instance().pBCryptOpenAlgorithmProvider) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptOpenAlgorithmProvider is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptOpenAlgorithmProvider(phAlgorithm, pszAlgId, pszImplementation, dwFlags);
    }

    static NTSTATUS CloseAlgorithmProvider(BCRYPT_ALG_HANDLE hAlgorithm, ULONG dwFlags) {
        if (!instance().pBCryptCloseAlgorithmProvider) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptCloseAlgorithmProvider is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptCloseAlgorithmProvider(hAlgorithm, dwFlags);
    }

    static NTSTATUS CreateHash(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_HASH_HANDLE* phHash, PUCHAR pbHashObject, ULONG cbHashObject, PUCHAR pbSecret, ULONG cbSecret, ULONG dwFlags) {
        if (!instance().pBCryptCreateHash) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptCreateHash is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptCreateHash(hAlgorithm, phHash, pbHashObject, cbHashObject, pbSecret, cbSecret, dwFlags);
    }

    static NTSTATUS HashData(BCRYPT_HASH_HANDLE hHash, PUCHAR pbInput, ULONG cbInput, ULONG dwFlags) {
        if (!instance().pBCryptHashData) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptHashData is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptHashData(hHash, pbInput, cbInput, dwFlags);
    }

    static NTSTATUS FinishHash(BCRYPT_HASH_HANDLE hHash, PUCHAR pbOutput, ULONG cbOutput, ULONG dwFlags) {
        if (!instance().pBCryptFinishHash) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptFinishHash is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptFinishHash(hHash, pbOutput, cbOutput, dwFlags);
    }

    static NTSTATUS DestroyHash(BCRYPT_HASH_HANDLE hHash) {
        if (!instance().pBCryptDestroyHash) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptDestroyHash is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptDestroyHash(hHash);
    }

    static NTSTATUS GetProperty(BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
        if (!instance().pBCryptGetProperty) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptGetProperty is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptGetProperty(hObject, pszProperty, pbOutput, cbOutput, pcbResult, dwFlags);
    }

    static NTSTATUS SetProperty(BCRYPT_HANDLE hObject, LPCWSTR pszProperty, PUCHAR pbInput, ULONG cbInput, ULONG dwFlags) {
        if (!instance().pBCryptSetProperty) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptSetProperty is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptSetProperty(hObject, pszProperty, pbInput, cbInput, dwFlags);
    }

    static NTSTATUS GenerateSymmetricKey(BCRYPT_ALG_HANDLE hAlgorithm, BCRYPT_KEY_HANDLE* phKey, PUCHAR pbKeyObject, ULONG cbKeyObject, PUCHAR pbSecret, ULONG cbSecret, ULONG dwFlags) {
        if (!instance().pBCryptGenerateSymmetricKey) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptGenerateSymmetricKey is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptGenerateSymmetricKey(hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);
    }

    static NTSTATUS Encrypt(BCRYPT_KEY_HANDLE hKey, PUCHAR pbInput, ULONG cbInput, VOID* pPaddingInfo, PUCHAR pbIV, ULONG cbIV, PUCHAR pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
        if (!instance().pBCryptEncrypt) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptEncrypt is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptEncrypt(hKey, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult, dwFlags);
    }

    static NTSTATUS Decrypt(BCRYPT_KEY_HANDLE hKey, PUCHAR pbInput, ULONG cbInput, VOID* pPaddingInfo, PUCHAR pbIV, ULONG cbIV, PUCHAR pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
        if (!instance().pBCryptDecrypt) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptDecrypt is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptDecrypt(hKey, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult, dwFlags);
    }

    static NTSTATUS DestroyKey(BCRYPT_KEY_HANDLE hKey) {
        if (!instance().pBCryptDestroyKey) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] BCryptDestroyKey is not initialized.";
            return STATUS_NOT_IMPLEMENTED;
        }
        return instance().pBCryptDestroyKey(hKey);
    }

private:
    // Pointeurs vers fonctions BCrypt
    decltype(&BCryptOpenAlgorithmProvider) pBCryptOpenAlgorithmProvider = nullptr;
    decltype(&BCryptCloseAlgorithmProvider) pBCryptCloseAlgorithmProvider = nullptr;
    decltype(&BCryptCreateHash) pBCryptCreateHash = nullptr;
    decltype(&BCryptHashData) pBCryptHashData = nullptr;
    decltype(&BCryptFinishHash) pBCryptFinishHash = nullptr;
    decltype(&BCryptDestroyHash) pBCryptDestroyHash = nullptr;
    decltype(&BCryptGetProperty) pBCryptGetProperty = nullptr;
    decltype(&BCryptSetProperty) pBCryptSetProperty = nullptr;
    decltype(&BCryptGenerateSymmetricKey) pBCryptGenerateSymmetricKey = nullptr;
    decltype(&BCryptEncrypt) pBCryptEncrypt = nullptr;
    decltype(&BCryptDecrypt) pBCryptDecrypt = nullptr;
    decltype(&BCryptDestroyKey) pBCryptDestroyKey = nullptr;

    HMODULE hBcryptDll = nullptr;

    BcryptWrapper() {
        // ⚠️ Corrigé : on initialise le membre, pas une variable locale.
        hBcryptDll = LoadLibrary(TEXT("bcrypt.dll"));
        if (!hBcryptDll) {
            throw std::runtime_error("Failed to load bcrypt.dll");
        }

        pBCryptOpenAlgorithmProvider = reinterpret_cast<decltype(pBCryptOpenAlgorithmProvider)>(GetProcAddress(hBcryptDll, "BCryptOpenAlgorithmProvider"));
        pBCryptCloseAlgorithmProvider = reinterpret_cast<decltype(pBCryptCloseAlgorithmProvider)>(GetProcAddress(hBcryptDll, "BCryptCloseAlgorithmProvider"));
        pBCryptCreateHash = reinterpret_cast<decltype(pBCryptCreateHash)>(GetProcAddress(hBcryptDll, "BCryptCreateHash"));
        pBCryptHashData = reinterpret_cast<decltype(pBCryptHashData)>(GetProcAddress(hBcryptDll, "BCryptHashData"));
        pBCryptFinishHash = reinterpret_cast<decltype(pBCryptFinishHash)>(GetProcAddress(hBcryptDll, "BCryptFinishHash"));
        pBCryptDestroyHash = reinterpret_cast<decltype(pBCryptDestroyHash)>(GetProcAddress(hBcryptDll, "BCryptDestroyHash"));
        pBCryptGetProperty = reinterpret_cast<decltype(pBCryptGetProperty)>(GetProcAddress(hBcryptDll, "BCryptGetProperty"));
        pBCryptSetProperty = reinterpret_cast<decltype(pBCryptSetProperty)>(GetProcAddress(hBcryptDll, "BCryptSetProperty"));
        pBCryptGenerateSymmetricKey = reinterpret_cast<decltype(pBCryptGenerateSymmetricKey)>(GetProcAddress(hBcryptDll, "BCryptGenerateSymmetricKey"));
        pBCryptEncrypt = reinterpret_cast<decltype(pBCryptEncrypt)>(GetProcAddress(hBcryptDll, "BCryptEncrypt"));
        pBCryptDecrypt = reinterpret_cast<decltype(pBCryptDecrypt)>(GetProcAddress(hBcryptDll, "BCryptDecrypt"));
        pBCryptDestroyKey = reinterpret_cast<decltype(pBCryptDestroyKey)>(GetProcAddress(hBcryptDll, "BCryptDestroyKey"));

        std::vector<std::pair<void*, const char*>> functions = {
            {reinterpret_cast<void*>(pBCryptOpenAlgorithmProvider), "BCryptOpenAlgorithmProvider"},
            {reinterpret_cast<void*>(pBCryptCloseAlgorithmProvider), "BCryptCloseAlgorithmProvider"},
            {reinterpret_cast<void*>(pBCryptCreateHash), "BCryptCreateHash"},
            {reinterpret_cast<void*>(pBCryptHashData), "BCryptHashData"},
            {reinterpret_cast<void*>(pBCryptFinishHash), "BCryptFinishHash"},
            {reinterpret_cast<void*>(pBCryptDestroyHash), "BCryptDestroyHash"},
            {reinterpret_cast<void*>(pBCryptGetProperty), "BCryptGetProperty"},
            {reinterpret_cast<void*>(pBCryptSetProperty), "BCryptSetProperty"},
            {reinterpret_cast<void*>(pBCryptGenerateSymmetricKey), "BCryptGenerateSymmetricKey"},
            {reinterpret_cast<void*>(pBCryptEncrypt), "BCryptEncrypt"},
            {reinterpret_cast<void*>(pBCryptDecrypt), "BCryptDecrypt"},
            {reinterpret_cast<void*>(pBCryptDestroyKey), "BCryptDestroyKey"}
        };

        std::string missingFunctions;
        for (const auto& func : functions) {
            if (!func.first) {
                missingFunctions += std::string(func.second) + "\n";
            }
        }

        if (!missingFunctions.empty()) {
            swCError(kSwLogCategory_SwCrypto) << "[SwCrypto] Failed to retrieve the following required BCrypt functions:";
            swCError(kSwLogCategory_SwCrypto) << missingFunctions;
            FreeLibrary(hBcryptDll);
            hBcryptDll = nullptr;
        }
    }

    ~BcryptWrapper() {
        if (hBcryptDll) {
            FreeLibrary(hBcryptDll);
        }
    }

    BcryptWrapper(const BcryptWrapper&) = delete;
    BcryptWrapper& operator=(const BcryptWrapper&) = delete;
};
#endif // _WIN32


//--------------------------------------------------------------------------------------------------
// SwCrypto : API inchangée, implémentation Windows/Linux sous le capot
//--------------------------------------------------------------------------------------------------
class SwCrypto {
public:
    // Génération de hachage SHA256 / SHA512
    static std::vector<unsigned char> generateHashSHA1(const std::string& input) {
        return computeHash(input, HashAlgo::SHA1);
    }

    static std::vector<unsigned char> generateHashSHA256(const std::string& input) {
        return computeHash(input, HashAlgo::SHA256);
    }
    static std::vector<unsigned char> generateHashSHA512(const std::string& input) {
        return computeHash(input, HashAlgo::SHA512);
    }

    // Hachage -> string hex
    static std::string hashSHA1(const std::string& input) {
        return hexEncode(generateHashSHA1(input));
    }

    static std::string hashSHA256(const std::string& input) {
        return hexEncode(generateHashSHA256(input));
    }
    static std::string hashSHA512(const std::string& input) {
        return hexEncode(generateHashSHA512(input));
    }

    // HMAC-SHA256 (clé)
    static std::vector<unsigned char> generateKeyedHashSHA256(const std::string& data, const std::string& key) {
        return computeHMAC_SHA256(data, key);
    }

    // AES (ECB, padding PKCS#7 manuel pour compatibilité)
    static std::vector<unsigned char> encryptAES(const std::vector<unsigned char>& data, const std::vector<unsigned char>& key) {
        auto validKey = normalizeKey(key);
        auto encrypted = cryptAES_ECB(data, validKey, true);
        return encrypted;
    }
    static std::vector<unsigned char> decryptAES(const std::vector<unsigned char>& data, const std::vector<unsigned char>& key) {
        auto validKey = normalizeKey(key);
        auto decrypted = cryptAES_ECB(data, validKey, false);
        return removePKCS7Padding(decrypted);
    }

    // Surcharges string (AES + base64)
    static std::string encryptAES(const std::string& data, const std::string& key) {
        auto validKey = normalizeKey(std::vector<unsigned char>(key.begin(), key.end()));
        auto encrypted = cryptAES_ECB(std::vector<unsigned char>(data.begin(), data.end()), validKey, true);
        return base64Encode(encrypted);
    }
    static std::string decryptAES(const std::string& data, const std::string& key) {
        auto validKey = normalizeKey(std::vector<unsigned char>(key.begin(), key.end()));
        auto decoded = base64Decode(data);
        auto decrypted = cryptAES_ECB(decoded, validKey, false);
        auto unpadded = removePKCS7Padding(decrypted);
        return std::string(unpadded.begin(), unpadded.end());
    }

    // Base64
    static std::string base64Encode(const std::vector<unsigned char>& data) {
        const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        int val = 0, valb = -6;
        for (unsigned char c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(base64Chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) encoded.push_back(base64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (encoded.size() % 4) encoded.push_back('=');
        return encoded;
    }
    static std::string base64Encode(const char* data) {
        if (!data) throw std::invalid_argument("Null pointer passed to base64Encode.");
        std::vector<unsigned char> vec(data, data + std::strlen(data));
        return base64Encode(vec);
    }
    static std::string base64Encode(char* data) {
        if (!data) throw std::invalid_argument("Null pointer passed to base64Encode.");
        std::vector<unsigned char> vec(data, data + std::strlen(data));
        return base64Encode(vec);
    }
    static std::string base64Encode(const std::string& data) {
        std::vector<unsigned char> vec(data.begin(), data.end());
        return base64Encode(vec);
    }
    static std::vector<unsigned char> base64Decode(const std::string& encoded) {
        const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[base64Chars[i]] = i;

        std::vector<unsigned char> decoded;
        int val = 0, valb = -8;
        for (unsigned char c : encoded) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                decoded.push_back((val >> valb) & 0xFF);
                valb -= 8;
            }
        }
        return decoded;
    }

    // Checksum de fichier (SHA-256)
    static std::string calculateFileChecksum(const std::string& filePath) {
    #if defined(_WIN32)
        BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD hashObjectSize = 0, hashSize = 0, cbData = 0;
        std::vector<unsigned char> hashObject, hashValue;

        if (BcryptWrapper::OpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
            throw std::runtime_error("Failed to open SHA256 algorithm provider.");
        }

        try {
            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectSize, sizeof(DWORD), &cbData, 0) != 0) {
                throw std::runtime_error("Failed to get hash object size.");
            }
            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashSize, sizeof(DWORD), &cbData, 0) != 0) {
                throw std::runtime_error("Failed to get hash size.");
            }

            hashObject.resize(hashObjectSize);
            hashValue.resize(hashSize);

            if (BcryptWrapper::CreateHash(hAlgorithm, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0) != 0) {
                throw std::runtime_error("Failed to create hash.");
            }

            constexpr size_t bufferSize = 1024 * 1024;
            std::vector<unsigned char> buffer(bufferSize);
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open the file: " + filePath);
            }

            while (file.good()) {
                file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
                std::streamsize bytesRead = file.gcount();
                if (bytesRead > 0) {
                    if (BcryptWrapper::HashData(hHash, buffer.data(), static_cast<ULONG>(bytesRead), 0) != 0) {
                        throw std::runtime_error("Failed to hash file data.");
                    }
                }
            }

            if (BcryptWrapper::FinishHash(hHash, hashValue.data(), hashSize, 0) != 0) {
                throw std::runtime_error("Failed to finalize hash.");
            }
        } catch (...) {
            if (hHash) BcryptWrapper::DestroyHash(hHash);
            if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
            throw;
        }

        if (hHash) BcryptWrapper::DestroyHash(hHash);
        if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);

        return hexEncode(hashValue);
    #else
        const EVP_MD* md = EVP_sha256();
        if (!md) throw std::runtime_error("EVP_sha256 unavailable.");
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed.");

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("Failed to open the file: " + filePath);
        }

        if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestInit_ex failed.");
        }

        constexpr size_t bufferSize = 1024 * 1024;
        std::vector<unsigned char> buffer(bufferSize);
        while (file) {
            file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
            std::streamsize bytesRead = file.gcount();
            if (bytesRead > 0) {
                if (EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(bytesRead)) != 1) {
                    EVP_MD_CTX_free(ctx);
                    throw std::runtime_error("EVP_DigestUpdate failed.");
                }
            }
        }

        std::vector<unsigned char> hash(EVP_MD_size(md));
        unsigned int outLen = 0;
        if (EVP_DigestFinal_ex(ctx, hash.data(), &outLen) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestFinal_ex failed.");
        }
        EVP_MD_CTX_free(ctx);
        hash.resize(outLen);
        return hexEncode(hash);
    #endif
    }

private:
    // ---- Helpers cross‑platform (API interne, pas visible à l'extérieur) ----
    enum class HashAlgo { SHA1, SHA256, SHA512 };

    static std::string hexEncode(const std::vector<unsigned char>& bytes) {
        std::ostringstream oss;
        for (auto b : bytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        return oss.str();
    }

    static std::vector<unsigned char> normalizeKey(const std::vector<unsigned char>& key) {
        if (key.size() == 32) return key;
        std::string keyStr(key.begin(), key.end());
        return computeHash(keyStr, HashAlgo::SHA256); // 32 octets
    }

    static std::vector<unsigned char> computeHash(const std::string& input, HashAlgo algo) {
    #if defined(_WIN32)
        BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD hashObjectSize = 0, hashSize = 0, cbData = 0;
        std::vector<unsigned char> hashObject, hashValue;

        LPCWSTR algId = BCRYPT_SHA256_ALGORITHM;
        switch (algo) {
        case HashAlgo::SHA1:   algId = BCRYPT_SHA1_ALGORITHM;   break;
        case HashAlgo::SHA256: algId = BCRYPT_SHA256_ALGORITHM; break;
        case HashAlgo::SHA512: algId = BCRYPT_SHA512_ALGORITHM; break;
        }
        if (BcryptWrapper::OpenAlgorithmProvider(&hAlgorithm, algId, nullptr, 0) != 0) {
            throw std::runtime_error("Failed to open algorithm provider.");
        }

        try {
            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectSize, sizeof(DWORD), &cbData, 0) != 0)
                throw std::runtime_error("Failed to get hash object size.");
            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashSize, sizeof(DWORD), &cbData, 0) != 0)
                throw std::runtime_error("Failed to get hash size.");

            hashObject.resize(hashObjectSize);
            hashValue.resize(hashSize);

            if (BcryptWrapper::CreateHash(hAlgorithm, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0) != 0)
                throw std::runtime_error("Failed to create hash.");
            if (BcryptWrapper::HashData(hHash, (PUCHAR)input.data(), (ULONG)input.size(), 0) != 0)
                throw std::runtime_error("Failed to hash data.");
            if (BcryptWrapper::FinishHash(hHash, hashValue.data(), hashSize, 0) != 0)
                throw std::runtime_error("Failed to finish hash.");
        } catch (...) {
            if (hHash) BcryptWrapper::DestroyHash(hHash);
            if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
            throw;
        }
        if (hHash) BcryptWrapper::DestroyHash(hHash);
        if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
        return hashValue;
    #else
        const EVP_MD* md = nullptr;
        switch (algo) {
        case HashAlgo::SHA1:   md = EVP_sha1();   break;
        case HashAlgo::SHA256: md = EVP_sha256(); break;
        case HashAlgo::SHA512: md = EVP_sha512(); break;
        }
        if (!md) throw std::runtime_error("EVP sha algo unavailable.");
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed.");
        std::vector<unsigned char> out(EVP_MD_size(md));
        unsigned int outLen = 0;

        if (EVP_DigestInit_ex(ctx, md, nullptr) != 1 ||
            EVP_DigestUpdate(ctx, input.data(), input.size()) != 1 ||
            EVP_DigestFinal_ex(ctx, out.data(), &outLen) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP digest operation failed.");
        }
        EVP_MD_CTX_free(ctx);
        out.resize(outLen);
        return out;
    #endif
    }

    static std::vector<unsigned char> computeHMAC_SHA256(const std::string& data, const std::string& key) {
    #if defined(_WIN32)
        BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
        BCRYPT_HASH_HANDLE hHash = nullptr;
        DWORD hashObjectSize = 0, hashSize = 0, cbData = 0;
        std::vector<unsigned char> hashObject, hashValue;

        if (BcryptWrapper::OpenAlgorithmProvider(&hAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
            throw std::runtime_error("Failed to open HMAC algorithm provider.");

        try {
            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectSize, sizeof(DWORD), &cbData, 0) != 0)
                throw std::runtime_error("Failed to get HMAC object size.");
            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_HASH_LENGTH, (PUCHAR)&hashSize, sizeof(DWORD), &cbData, 0) != 0)
                throw std::runtime_error("Failed to get HMAC hash size.");

            hashObject.resize(hashObjectSize);
            hashValue.resize(hashSize);

            if (BcryptWrapper::CreateHash(hAlgorithm, &hHash, hashObject.data(), hashObjectSize,
                                          (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0)
                throw std::runtime_error("Failed to create HMAC hash.");

            if (BcryptWrapper::HashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0) != 0)
                throw std::runtime_error("Failed to hash data.");

            if (BcryptWrapper::FinishHash(hHash, hashValue.data(), hashSize, 0) != 0)
                throw std::runtime_error("Failed to finish HMAC hash.");
        } catch (...) {
            if (hHash) BcryptWrapper::DestroyHash(hHash);
            if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
            throw;
        }
        if (hHash) BcryptWrapper::DestroyHash(hHash);
        if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
        return hashValue;
    #else
        unsigned int outLen = 0;
        std::vector<unsigned char> out(EVP_MD_size(EVP_sha256()));
        unsigned char* rv = HMAC(EVP_sha256(),
                                 key.data(), static_cast<int>(key.size()),
                                 reinterpret_cast<const unsigned char*>(data.data()),
                                 static_cast<int>(data.size()),
                                 out.data(), &outLen);
        if (!rv) throw std::runtime_error("HMAC failed.");
        out.resize(outLen);
        return out;
    #endif
    }

    static std::vector<unsigned char> cryptAES_ECB(const std::vector<unsigned char>& data,
                                                   const std::vector<unsigned char>& key,
                                                   bool encrypt) {
    #if defined(_WIN32)
        BCRYPT_ALG_HANDLE hAlgorithm = nullptr;
        BCRYPT_KEY_HANDLE hKey = nullptr;
        DWORD keyObjectSize = 0, blockSize = 0, cbData = 0;
        std::vector<unsigned char> keyObject, output;

        try {
            if (BcryptWrapper::OpenAlgorithmProvider(&hAlgorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
                throw std::runtime_error("Failed to open AES algorithm provider.");

            if (BcryptWrapper::SetProperty(hAlgorithm, BCRYPT_CHAINING_MODE,
                                           (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                                           sizeof(BCRYPT_CHAIN_MODE_ECB), 0) != 0)
                throw std::runtime_error("Failed to set AES chaining mode to ECB.");

            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&keyObjectSize, sizeof(DWORD), &cbData, 0) != 0)
                throw std::runtime_error("Failed to get AES key object size.");

            if (BcryptWrapper::GetProperty(hAlgorithm, BCRYPT_BLOCK_LENGTH, (PUCHAR)&blockSize, sizeof(DWORD), &cbData, 0) != 0)
                throw std::runtime_error("Failed to get AES block size.");

            if (key.size() != 16 && key.size() != 24 && key.size() != 32)
                throw std::invalid_argument("Invalid AES key size. Key must be 16, 24, or 32 bytes.");

            keyObject.resize(keyObjectSize);

            std::vector<unsigned char> input = data;
            if (encrypt) {
                size_t paddingSize = blockSize - (input.size() % blockSize);
                if (paddingSize == 0) paddingSize = blockSize;
                input.insert(input.end(), paddingSize, static_cast<unsigned char>(paddingSize));
            }

            output.resize(input.size() + blockSize);

            if (BcryptWrapper::GenerateSymmetricKey(hAlgorithm, &hKey, keyObject.data(), keyObjectSize,
                                                    (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0)
                throw std::runtime_error("Failed to generate AES key.");

            if (encrypt) {
                if (BcryptWrapper::Encrypt(hKey, (PUCHAR)input.data(), (ULONG)input.size(),
                                           nullptr, nullptr, 0, output.data(),
                                           (ULONG)output.size(), &cbData, 0) != 0)
                    throw std::runtime_error("Failed to encrypt data.");
            } else {
                if (BcryptWrapper::Decrypt(hKey, (PUCHAR)input.data(), (ULONG)input.size(),
                                           nullptr, nullptr, 0, output.data(),
                                           (ULONG)output.size(), &cbData, 0) != 0)
                    throw std::runtime_error("Failed to decrypt data.");
            }

            output.resize(cbData);
        } catch (...) {
            if (hKey) BcryptWrapper::DestroyKey(hKey);
            if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
            throw;
        }

        if (hKey) BcryptWrapper::DestroyKey(hKey);
        if (hAlgorithm) BcryptWrapper::CloseAlgorithmProvider(hAlgorithm, 0);
        return output;

    #else
        const size_t blockSize = 16; // AES
        if (key.size() != 16 && key.size() != 24 && key.size() != 32)
            throw std::invalid_argument("Invalid AES key size. Key must be 16, 24, or 32 bytes.");

        std::vector<unsigned char> input = data;
        if (encrypt) {
            size_t paddingSize = blockSize - (input.size() % blockSize);
            if (paddingSize == 0) paddingSize = blockSize;
            input.insert(input.end(), paddingSize, static_cast<unsigned char>(paddingSize));
        } else {
            if (input.size() % blockSize != 0)
                throw std::invalid_argument("Invalid data size for AES-ECB (must be multiple of 16).");
        }

        const EVP_CIPHER* cipher = nullptr;
        switch (key.size()) {
            case 16: cipher = EVP_aes_128_ecb(); break;
            case 24: cipher = EVP_aes_192_ecb(); break;
            case 32: cipher = EVP_aes_256_ecb(); break;
            default: throw std::invalid_argument("Invalid AES key size.");
        }
        if (!cipher) throw std::runtime_error("EVP AES cipher unavailable.");

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed.");

        std::vector<unsigned char> output(input.size() + blockSize);
        int outLen1 = 0, outLen2 = 0;

        if (encrypt) {
            if (EVP_EncryptInit_ex(ctx, cipher, nullptr, key.data(), nullptr) != 1 ||
                EVP_CIPHER_CTX_set_padding(ctx, 0) != 1 ||
                EVP_EncryptUpdate(ctx, output.data(), &outLen1, input.data(), static_cast<int>(input.size())) != 1 ||
                EVP_EncryptFinal_ex(ctx, output.data() + outLen1, &outLen2) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                throw std::runtime_error("EVP AES encrypt failed.");
            }
        } else {
            if (EVP_DecryptInit_ex(ctx, cipher, nullptr, key.data(), nullptr) != 1 ||
                EVP_CIPHER_CTX_set_padding(ctx, 0) != 1 ||
                EVP_DecryptUpdate(ctx, output.data(), &outLen1, input.data(), static_cast<int>(input.size())) != 1 ||
                EVP_DecryptFinal_ex(ctx, output.data() + outLen1, &outLen2) != 1) {
                EVP_CIPHER_CTX_free(ctx);
                throw std::runtime_error("EVP AES decrypt failed.");
            }
        }

        EVP_CIPHER_CTX_free(ctx);
        output.resize(outLen1 + outLen2);
        return output;
    #endif
    }

    static std::vector<unsigned char> removePKCS7Padding(const std::vector<unsigned char>& data) {
        if (data.empty()) {
            throw std::invalid_argument("Invalid data: empty.");
        }
        const size_t blockSize = 16; // AES
        unsigned char paddingSize = data.back();
        // Si ce n'est manifestement pas un padding PKCS#7 valide, renvoyer tel quel.
        if (paddingSize == 0 || paddingSize > blockSize || paddingSize > data.size())
            return data;

        size_t start = data.size() - paddingSize;
        for (size_t i = start; i < data.size(); ++i) {
            if (data[i] != paddingSize) {
                return data; // pas du PKCS#7 propre → rendre tel quel
            }
        }
        return std::vector<unsigned char>(data.begin(), data.end() - paddingSize);
    }
};
