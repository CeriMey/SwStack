#pragma once

#include "SwDir.h"
#include "SwStandardPaths.h"
#include "SwString.h"

#include <cstdint>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include "platform/win/SwWindows.h"
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace swVoltDetail {

inline SwString sanitizeComponent(const SwString& value) {
    const std::string input = value.trimmed().toStdString();
    std::string output;
    output.reserve(input.size());
    for (char ch : input) {
        const bool alphaNum =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9');
        output.push_back(alphaNum || ch == '-' || ch == '_' || ch == '.' ? ch : '_');
    }
    while (!output.empty() && output.front() == '.') {
        output.erase(output.begin());
    }
    return output.empty() ? SwString("default") : SwString(output);
}

inline std::uint64_t fnv1a64(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < value.size(); ++i) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(value[i]));
        hash *= 1099511628211ull;
    }
    return hash;
}

inline SwString hex64(std::uint64_t value) {
    static const char* digits = "0123456789abcdef";
    char buffer[17] = {0};
    for (int i = 15; i >= 0; --i) {
        buffer[i] = digits[value & 0x0f];
        value >>= 4;
    }
    return SwString(buffer);
}

inline SwString fileNameForKey(const SwString& key) {
    return sanitizeComponent(key).left(48) + "-" + hex64(fnv1a64(key.toStdString())) + ".volt";
}

inline SwString joinPath(const SwString& left, const SwString& right) {
    if (left.trimmed().isEmpty()) {
        return right;
    }
    if (right.trimmed().isEmpty()) {
        return left;
    }
    SwString combined = left;
    if (!combined.endsWith("/") && !combined.endsWith("\\")) {
        combined += "/";
    }
    combined += right;
    return SwDir::normalizePath(combined);
}

inline void secureClear(std::string& value) {
    if (value.empty()) {
        return;
    }
#ifdef _WIN32
    SecureZeroMemory(&value[0], value.size());
#else
    volatile char* ptr = &value[0];
    for (size_t i = 0; i < value.size(); ++i) {
        ptr[i] = 0;
    }
#endif
    value.clear();
}

inline bool ensureDirectory(const SwString& path, SwString* errorOut) {
    if (!SwDir::mkpathAbsolute(path)) {
        if (errorOut) {
            *errorOut = "Unable to create SwVolt directory";
        }
        return false;
    }
#ifndef _WIN32
    ::chmod(path.toStdString().c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
#endif
    if (errorOut) {
        errorOut->clear();
    }
    return true;
}

inline bool fileExists(const SwString& path) {
#ifdef _WIN32
    const std::wstring wide = path.toStdWString();
    const DWORD attrs = GetFileAttributesW(wide.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info;
    return ::stat(path.toStdString().c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

#ifdef _WIN32
inline bool writeBinaryFile(const SwString& path, const std::string& data, SwString* errorOut) {
    const std::wstring wide = path.toStdWString();
    HANDLE handle = CreateFileW(wide.c_str(),
                                GENERIC_WRITE,
                                0,
                                nullptr,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errorOut) {
            *errorOut = "Unable to open Windows secret file";
        }
        return false;
    }

    const char* cursor = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        const DWORD chunk = remaining > static_cast<size_t>(0xffffffffu)
                                ? 0xffffffffu
                                : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(handle, cursor, chunk, &written, nullptr)) {
            CloseHandle(handle);
            if (errorOut) {
                *errorOut = "Unable to write Windows secret file";
            }
            return false;
        }
        cursor += written;
        remaining -= written;
    }

    CloseHandle(handle);
    if (errorOut) {
        errorOut->clear();
    }
    return true;
}

inline bool readBinaryFile(const SwString& path, std::string& dataOut, SwString* errorOut) {
    const std::wstring wide = path.toStdWString();
    HANDLE handle = CreateFileW(wide.c_str(),
                                GENERIC_READ,
                                FILE_SHARE_READ,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (errorOut) {
            *errorOut = "Unable to open Windows secret file";
        }
        return false;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
        CloseHandle(handle);
        if (errorOut) {
            *errorOut = "Unable to read Windows secret file size";
        }
        return false;
    }

    dataOut.assign(static_cast<size_t>(size.QuadPart), '\0');
    char* cursor = dataOut.empty() ? nullptr : &dataOut[0];
    size_t remaining = dataOut.size();
    while (remaining > 0) {
        const DWORD chunk = remaining > static_cast<size_t>(0xffffffffu)
                                ? 0xffffffffu
                                : static_cast<DWORD>(remaining);
        DWORD read = 0;
        if (!ReadFile(handle, cursor, chunk, &read, nullptr)) {
            CloseHandle(handle);
            secureClear(dataOut);
            if (errorOut) {
                *errorOut = "Unable to read Windows secret file";
            }
            return false;
        }
        if (read == 0) {
            break;
        }
        cursor += read;
        remaining -= read;
    }

    CloseHandle(handle);
    if (errorOut) {
        errorOut->clear();
    }
    return true;
}
#else
inline bool writeBinaryFile(const SwString& path, const std::string& data, SwString* errorOut) {
    const int fd = ::open(path.toStdString().c_str(), O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (errorOut) {
            *errorOut = "Unable to open Linux secret file";
        }
        return false;
    }

    ::fchmod(fd, S_IRUSR | S_IWUSR);

    const char* cursor = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, remaining);
        if (written <= 0) {
            ::close(fd);
            if (errorOut) {
                *errorOut = "Unable to write Linux secret file";
            }
            return false;
        }
        cursor += static_cast<size_t>(written);
        remaining -= static_cast<size_t>(written);
    }

    ::fsync(fd);
    ::close(fd);
    if (errorOut) {
        errorOut->clear();
    }
    return true;
}

inline bool readBinaryFile(const SwString& path, std::string& dataOut, SwString* errorOut) {
    struct stat info;
    if (::stat(path.toStdString().c_str(), &info) != 0) {
        if (errorOut) {
            *errorOut = "Unable to open Linux secret file";
        }
        return false;
    }
    if ((info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        if (errorOut) {
            *errorOut = "Linux secret file permissions are too open";
        }
        return false;
    }
    if (info.st_uid != ::geteuid()) {
        if (errorOut) {
            *errorOut = "Linux secret file is not owned by the current user";
        }
        return false;
    }

    const int fd = ::open(path.toStdString().c_str(), O_RDONLY);
    if (fd < 0) {
        if (errorOut) {
            *errorOut = "Unable to read Linux secret file";
        }
        return false;
    }

    dataOut.assign(static_cast<size_t>(info.st_size), '\0');
    char* cursor = dataOut.empty() ? nullptr : &dataOut[0];
    size_t remaining = dataOut.size();
    while (remaining > 0) {
        const ssize_t readBytes = ::read(fd, cursor, remaining);
        if (readBytes < 0) {
            ::close(fd);
            secureClear(dataOut);
            if (errorOut) {
                *errorOut = "Unable to read Linux secret file";
            }
            return false;
        }
        if (readBytes == 0) {
            break;
        }
        cursor += static_cast<size_t>(readBytes);
        remaining -= static_cast<size_t>(readBytes);
    }

    ::close(fd);
    if (errorOut) {
        errorOut->clear();
    }
    return true;
}
#endif

inline bool removeFile(const SwString& path, SwString* errorOut) {
#ifdef _WIN32
    const std::wstring wide = path.toStdWString();
    if (!DeleteFileW(wide.c_str())) {
        const DWORD lastError = GetLastError();
        if (lastError != ERROR_FILE_NOT_FOUND) {
            if (errorOut) {
                *errorOut = "Unable to remove Windows secret file";
            }
            return false;
        }
    }
#else
    if (::remove(path.toStdString().c_str()) != 0 && errno != ENOENT) {
        if (errorOut) {
            *errorOut = "Unable to remove Linux secret file";
        }
        return false;
    }
#endif
    if (errorOut) {
        errorOut->clear();
    }
    return true;
}

} // namespace swVoltDetail

class SwVolt {
public:
    enum BackendKind {
        UnsupportedBackend,
        WindowsDpapiBackend,
        LinuxPrivateFileBackend
    };

    enum ProtectionLevel {
        UnsupportedProtection,
        PermissionsOnlyProtection,
        UserBoundEncryptionProtection
    };

    enum Policy {
        BestEffortPolicy,
        RequireUserBoundEncryptionPolicy
    };

    SwVolt(const SwString& organization = "SwCore",
           const SwString& application = "Application",
           const SwString& scope = "default",
           Policy policy = BestEffortPolicy)
        : organization_(swVoltDetail::sanitizeComponent(organization)),
          application_(swVoltDetail::sanitizeComponent(application)),
          scope_(swVoltDetail::sanitizeComponent(scope)),
          policy_(policy) {
    }

    BackendKind backend() const {
#ifdef _WIN32
        return WindowsDpapiBackend;
#elif defined(__linux__)
        return LinuxPrivateFileBackend;
#else
        return UnsupportedBackend;
#endif
    }

    ProtectionLevel protectionLevel() const {
#ifdef _WIN32
        return UserBoundEncryptionProtection;
#elif defined(__linux__)
        return PermissionsOnlyProtection;
#else
        return UnsupportedProtection;
#endif
    }

    Policy policy() const {
        return policy_;
    }

    bool meetsPolicy() const {
        return !(policy_ == RequireUserBoundEncryptionPolicy &&
                 protectionLevel() != UserBoundEncryptionProtection);
    }

    SwString backendName() const {
        switch (backend()) {
        case WindowsDpapiBackend:
            return "windows-dpapi";
        case LinuxPrivateFileBackend:
            return "linux-private-file";
        default:
            return "unsupported";
        }
    }

    SwString protectionName() const {
        switch (protectionLevel()) {
        case UserBoundEncryptionProtection:
            return "user-bound-encryption";
        case PermissionsOnlyProtection:
            return "permissions-only";
        default:
            return "unsupported";
        }
    }

    SwString rootPath() const {
        return rootPath_();
    }

    bool contains(const SwString& key) const {
        if (!ensureReady_(nullptr)) {
            return false;
        }
        return swVoltDetail::fileExists(secretPath_(key));
    }

    bool writeSecret(const SwString& key,
                     const SwString& value,
                     SwString* errorOut = nullptr) const {
        if (!ensureReady_(errorOut)) {
            return false;
        }

        std::string payload = value.toStdString();
#ifdef _WIN32
        std::string encrypted;
        if (!protectWindows_(payload, encrypted, errorOut)) {
            swVoltDetail::secureClear(payload);
            return false;
        }
        swVoltDetail::secureClear(payload);
        const bool ok = swVoltDetail::writeBinaryFile(secretPath_(key), encrypted, errorOut);
        swVoltDetail::secureClear(encrypted);
        return ok;
#else
        const bool ok = swVoltDetail::writeBinaryFile(secretPath_(key), payload, errorOut);
        swVoltDetail::secureClear(payload);
        return ok;
#endif
    }

    bool readSecret(const SwString& key,
                    SwString& valueOut,
                    SwString* errorOut = nullptr) const {
        valueOut.clear();
        if (!ensureReady_(errorOut)) {
            return false;
        }

        std::string payload;
        if (!swVoltDetail::readBinaryFile(secretPath_(key), payload, errorOut)) {
            return false;
        }

#ifdef _WIN32
        std::string plain;
        const bool ok = unprotectWindows_(payload, plain, errorOut);
        swVoltDetail::secureClear(payload);
        if (!ok) {
            swVoltDetail::secureClear(plain);
            return false;
        }
        valueOut = SwString(plain);
        swVoltDetail::secureClear(plain);
#else
        valueOut = SwString(payload);
        swVoltDetail::secureClear(payload);
#endif

        if (errorOut) {
            errorOut->clear();
        }
        return true;
    }

    bool removeSecret(const SwString& key, SwString* errorOut = nullptr) const {
        if (!ensureReady_(errorOut)) {
            return false;
        }
        return swVoltDetail::removeFile(secretPath_(key), errorOut);
    }

    static SwString safeKeyName(const SwString& key) {
        return swVoltDetail::fileNameForKey(key);
    }

private:
    SwString rootPath_() const {
        SwString base = SwStandardPaths::writableLocation(SwStandardPaths::AppDataLocation);
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::AppConfigLocation);
        }
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::HomeLocation);
        }

        return swVoltDetail::joinPath(
            swVoltDetail::joinPath(
                swVoltDetail::joinPath(
                    swVoltDetail::joinPath(base, organization_),
                    application_),
                "volt"),
            scope_);
    }

    SwString secretPath_(const SwString& key) const {
        return swVoltDetail::joinPath(rootPath_(), swVoltDetail::fileNameForKey(key));
    }

    bool ensureReady_(SwString* errorOut) const {
        if (!meetsPolicy()) {
            if (errorOut) {
                *errorOut = "SwVolt policy requires user-bound encryption on this platform";
            }
            return false;
        }
        if (backend() == UnsupportedBackend) {
            if (errorOut) {
                *errorOut = "SwVolt backend unsupported on this platform";
            }
            return false;
        }
        return swVoltDetail::ensureDirectory(rootPath_(), errorOut);
    }

#ifdef _WIN32
    bool protectWindows_(const std::string& plain,
                         std::string& encryptedOut,
                         SwString* errorOut) const {
        HMODULE crypt32 = LoadLibraryW(L"Crypt32.dll");
        if (!crypt32) {
            if (errorOut) {
                *errorOut = "Unable to load Crypt32.dll";
            }
            return false;
        }

        typedef BOOL (WINAPI* CryptProtectDataFn)(
            DATA_BLOB*, LPCWSTR, DATA_BLOB*, PVOID, CRYPTPROTECT_PROMPTSTRUCT*, DWORD, DATA_BLOB*);
        CryptProtectDataFn cryptProtectData =
            reinterpret_cast<CryptProtectDataFn>(GetProcAddress(crypt32, "CryptProtectData"));
        if (!cryptProtectData) {
            FreeLibrary(crypt32);
            if (errorOut) {
                *errorOut = "CryptProtectData unavailable";
            }
            return false;
        }

        const std::string entropyText =
            organization_.toStdString() + "|" + application_.toStdString() + "|" + scope_.toStdString();
        DATA_BLOB plainBlob;
        plainBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
        plainBlob.cbData = static_cast<DWORD>(plain.size());
        DATA_BLOB entropyBlob;
        entropyBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(entropyText.data()));
        entropyBlob.cbData = static_cast<DWORD>(entropyText.size());
        DATA_BLOB encryptedBlob = {};

        const BOOL ok = cryptProtectData(&plainBlob,
                                         L"SwVolt",
                                         &entropyBlob,
                                         nullptr,
                                         nullptr,
                                         CRYPTPROTECT_UI_FORBIDDEN,
                                         &encryptedBlob);
        FreeLibrary(crypt32);
        if (!ok) {
            if (errorOut) {
                *errorOut = "CryptProtectData failed";
            }
            return false;
        }

        encryptedOut.assign(reinterpret_cast<const char*>(encryptedBlob.pbData),
                            reinterpret_cast<const char*>(encryptedBlob.pbData) + encryptedBlob.cbData);
        LocalFree(encryptedBlob.pbData);
        if (errorOut) {
            errorOut->clear();
        }
        return true;
    }

    bool unprotectWindows_(const std::string& encrypted,
                           std::string& plainOut,
                           SwString* errorOut) const {
        HMODULE crypt32 = LoadLibraryW(L"Crypt32.dll");
        if (!crypt32) {
            if (errorOut) {
                *errorOut = "Unable to load Crypt32.dll";
            }
            return false;
        }

        typedef BOOL (WINAPI* CryptUnprotectDataFn)(
            DATA_BLOB*, LPWSTR*, DATA_BLOB*, PVOID, CRYPTPROTECT_PROMPTSTRUCT*, DWORD, DATA_BLOB*);
        CryptUnprotectDataFn cryptUnprotectData =
            reinterpret_cast<CryptUnprotectDataFn>(GetProcAddress(crypt32, "CryptUnprotectData"));
        if (!cryptUnprotectData) {
            FreeLibrary(crypt32);
            if (errorOut) {
                *errorOut = "CryptUnprotectData unavailable";
            }
            return false;
        }

        const std::string entropyText =
            organization_.toStdString() + "|" + application_.toStdString() + "|" + scope_.toStdString();
        DATA_BLOB encryptedBlob;
        encryptedBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.data()));
        encryptedBlob.cbData = static_cast<DWORD>(encrypted.size());
        DATA_BLOB entropyBlob;
        entropyBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(entropyText.data()));
        entropyBlob.cbData = static_cast<DWORD>(entropyText.size());
        DATA_BLOB plainBlob = {};

        const BOOL ok = cryptUnprotectData(&encryptedBlob,
                                           nullptr,
                                           &entropyBlob,
                                           nullptr,
                                           nullptr,
                                           CRYPTPROTECT_UI_FORBIDDEN,
                                           &plainBlob);
        FreeLibrary(crypt32);
        if (!ok) {
            if (errorOut) {
                *errorOut = "CryptUnprotectData failed";
            }
            return false;
        }

        plainOut.assign(reinterpret_cast<const char*>(plainBlob.pbData),
                        reinterpret_cast<const char*>(plainBlob.pbData) + plainBlob.cbData);
        LocalFree(plainBlob.pbData);
        if (errorOut) {
            errorOut->clear();
        }
        return true;
    }
#endif

    SwString organization_;
    SwString application_;
    SwString scope_;
    Policy policy_;
};
