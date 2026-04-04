#pragma once

/**
 * @file src/core/installer/SwInstallerPayload.h
 * @brief Header-only embedded payload manifest and extraction helpers for the Sw installer runtime.
 */

#include "SwByteArray.h"
#include "SwCrypto.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwFileInfo.h"
#include "SwList.h"
#include "SwString.h"
#include "third_party/miniz/miniz.h"

#include <cstdio>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

namespace swinstaller {

inline SwString swInstallerNormalizePath(SwString path) {
    if (path.isEmpty()) {
        return SwString();
    }
    path = SwDir::normalizePath(path);
    path.replace("\\", "/");
    while (path.startsWith("///?/")) {
        path.remove(0, 1);
    }
    if (path.startsWith("//?/UNC/")) {
        path.remove(0, 8);
        path = "//" + path;
        return path;
    }
    while (path.startsWith("//?/")) {
        path.remove(0, 4);
    }
    while (path.startsWith("/?/")) {
        path.remove(0, 3);
    }
    while (path.startsWith("/") && path.size() > 2 && path[2] == ':') {
        path.remove(0, 1);
    }
    return path;
}

struct SwInstallerEmbeddedFile {
    SwString relativePath;
    SwString checksumSha256;
    long long originalSize{0};
    long long storedSize{0};
    bool compressed{false};
    const unsigned char* bytes{nullptr};
};

struct SwInstallerEmbeddedPayload {
    SwString payloadId;
    SwString displayName;
    SwList<SwInstallerEmbeddedFile> files;

    const SwInstallerEmbeddedFile* findFile(const SwString& relativePath) const {
        const SwString normalized = normalizeRelativePath_(relativePath);
        for (size_t i = 0; i < files.size(); ++i) {
            if (normalizeRelativePath_(files[i].relativePath) == normalized) {
                return &files[i];
            }
        }
        return nullptr;
    }

private:
    static SwString normalizeRelativePath_(SwString path) {
        path.replace("\\", "/");
        while (path.startsWith("/")) {
            path.remove(0, 1);
        }
        while (path.contains("//")) {
            path.replace("//", "/");
        }
        return path;
    }
};

class SwInstallerPayload {
public:
    static SwString normalizeRelativePath(SwString path) {
        path.replace("\\", "/");
        while (path.startsWith("/")) {
            path.remove(0, 1);
        }
        while (path.contains("//")) {
            path.replace("//", "/");
        }
        return path;
    }

    static bool isSafeRelativePath(const SwString& path) {
        const SwString normalized = normalizeRelativePath(path);
        if (normalized.isEmpty()) {
            return false;
        }
        if (normalized.startsWith("/") || normalized.startsWith("\\")) {
            return false;
        }
        if (normalized.size() > 1 && normalized[1] == ':') {
            return false;
        }
        const SwList<SwString> parts = normalized.split('/');
        for (size_t i = 0; i < parts.size(); ++i) {
            if (parts[i].isEmpty() || parts[i] == "." || parts[i] == "..") {
                return false;
            }
        }
        return true;
    }

    static SwString joinRootAndRelative(const SwString& root, const SwString& relativePath) {
        SwString base = swInstallerNormalizePath(root);
        if (base.endsWith("/") || base.endsWith("\\")) {
            base.chop(1);
        }
        const SwString normalizedRelative = normalizeRelativePath(relativePath);
        if (base.isEmpty()) {
            return normalizedRelative;
        }
        return swInstallerNormalizePath(base + "/" + normalizedRelative);
    }

    static bool extractAll(const SwInstallerEmbeddedPayload& payload,
                           const SwString& targetRoot,
                           SwList<SwString>* writtenFiles = nullptr,
                           SwList<SwString>* writtenDirs = nullptr,
                           SwString* errOut = nullptr) {
        for (size_t i = 0; i < payload.files.size(); ++i) {
            if (!extractFile(payload.files[i], targetRoot, writtenDirs, errOut)) {
                return false;
            }
            if (writtenFiles) {
                appendUnique_(*writtenFiles, joinRootAndRelative(targetRoot, payload.files[i].relativePath));
            }
        }
        return true;
    }

    static bool extractFile(const SwInstallerEmbeddedPayload& payload,
                            const SwString& relativePath,
                            const SwString& targetRoot,
                            SwList<SwString>* writtenDirs = nullptr,
                            SwString* errOut = nullptr) {
        const SwInstallerEmbeddedFile* file = payload.findFile(relativePath);
        if (!file) {
            setErr_(errOut, SwString("payload file not found: ") + relativePath);
            return false;
        }
        return extractFile(*file, targetRoot, writtenDirs, errOut);
    }

    static bool extractFile(const SwInstallerEmbeddedFile& file,
                            const SwString& targetRoot,
                            SwList<SwString>* writtenDirs = nullptr,
                            SwString* errOut = nullptr) {
        const SwString normalizedRelative = normalizeRelativePath(file.relativePath);
        if (!isSafeRelativePath(normalizedRelative)) {
            setErr_(errOut, SwString("unsafe payload path: ") + file.relativePath);
            return false;
        }

        const SwString destinationPath = joinRootAndRelative(targetRoot, normalizedRelative);
        const SwString destinationDir = parentPath_(destinationPath);
        if (!destinationDir.isEmpty() && !SwDir::mkpathAbsolute(destinationDir, false)) {
            setErr_(errOut, SwString("failed to create destination directory: ") + destinationDir);
            return false;
        }
        if (writtenDirs) {
            collectParentDirs_(destinationDir, targetRoot, *writtenDirs);
        }

        SwByteArray decoded;
        if (!decodeFile_(file, decoded, errOut)) {
            return false;
        }

        const SwString tempPath = destinationPath + ".swinstall.tmp";
        SwFile out(tempPath);
        if (!out.openBinary(SwFile::Write)) {
            setErr_(errOut, SwString("failed to open temp file for write: ") + tempPath);
            return false;
        }
        if (!out.write(decoded)) {
            out.close();
            setErr_(errOut, SwString("failed to write temp file: ") + tempPath);
            return false;
        }
        out.close();

        if (!file.checksumSha256.isEmpty()) {
            const SwString actualChecksum = fileChecksum_(tempPath);
            if (actualChecksum != file.checksumSha256) {
                (void)deleteFileQuiet_(tempPath);
                setErr_(errOut,
                        SwString("checksum mismatch for ") + normalizedRelative + SwString(" expected=") +
                            file.checksumSha256 + SwString(" actual=") + actualChecksum);
                return false;
            }
        }

        if (!replaceFile_(tempPath, destinationPath, errOut)) {
            (void)deleteFileQuiet_(tempPath);
            return false;
        }

        return true;
    }

    static SwString fileChecksum(const SwInstallerEmbeddedFile& file) {
        SwByteArray decoded;
        SwString err;
        if (!decodeFile_(file, decoded, &err)) {
            return SwString();
        }
        return sha256_(decoded);
    }

private:
    static void setErr_(SwString* errOut, const SwString& value) {
        if (errOut) {
            *errOut = value;
        }
    }

    static void appendUnique_(SwList<SwString>& list, const SwString& value) {
        if (!list.contains(value)) {
            list.append(value);
        }
    }

    static SwString parentPath_(SwString path) {
        path.replace("\\", "/");
        const size_t slash = path.lastIndexOf('/');
        if (slash == static_cast<size_t>(-1)) {
            return SwString();
        }
        return path.left(static_cast<int>(slash));
    }

    static void collectParentDirs_(SwString directoryPath,
                                   const SwString& stopRoot,
                                   SwList<SwString>& writtenDirs) {
        SwString current = swInstallerNormalizePath(directoryPath);
        SwString stop = swInstallerNormalizePath(stopRoot);
        if (!stop.isEmpty() && (stop.endsWith("/") || stop.endsWith("\\"))) {
            stop.chop(1);
        }
        while (!current.isEmpty()) {
            appendUnique_(writtenDirs, current);
            if (current == stop) {
                break;
            }
            SwString parent = parentPath_(current);
            if (parent == current || parent.isEmpty()) {
                break;
            }
            current = parent;
        }
    }

    static SwString fileChecksum_(const SwString& path) {
        try {
            return SwCrypto::calculateFileChecksum(path.toStdString());
        } catch (...) {
            return SwString();
        }
    }

    static SwString sha256_(const SwByteArray& bytes) {
        const std::vector<unsigned char> digest =
            SwCrypto::generateHashSHA256(std::string(bytes.constData(), bytes.size()));
        static const char* kHex = "0123456789abcdef";
        SwString out;
        out.reserve(digest.size() * 2);
        for (size_t i = 0; i < digest.size(); ++i) {
            out += kHex[(digest[i] >> 4) & 0x0F];
            out += kHex[digest[i] & 0x0F];
        }
        return out;
    }

    static bool decodeFile_(const SwInstallerEmbeddedFile& file, SwByteArray& out, SwString* errOut) {
        out.clear();
        if (!file.bytes || file.storedSize < 0 || file.originalSize < 0) {
            setErr_(errOut, SwString("invalid payload blob for: ") + file.relativePath);
            return false;
        }

        if (!file.compressed) {
            out.append(reinterpret_cast<const char*>(file.bytes), static_cast<size_t>(file.storedSize));
            return true;
        }

        if (file.originalSize == 0) {
            out.clear();
            return true;
        }

        mz_ulong destLen = static_cast<mz_ulong>(file.originalSize);
        std::vector<unsigned char> decoded(static_cast<size_t>(file.originalSize));
        const int rc = mz_uncompress(decoded.data(),
                                     &destLen,
                                     reinterpret_cast<const unsigned char*>(file.bytes),
                                     static_cast<mz_ulong>(file.storedSize));
        if (rc != MZ_OK) {
            setErr_(errOut, SwString("payload decompression failed for: ") + file.relativePath);
            return false;
        }

        out.append(reinterpret_cast<const char*>(decoded.data()), static_cast<size_t>(destLen));
        return true;
    }

    static bool deleteFileQuiet_(const SwString& path) {
#if defined(_WIN32)
        const std::wstring wide = path.toStdWString();
        if (::DeleteFileW(wide.c_str())) {
            return true;
        }
        const DWORD err = ::GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
#else
        return ::remove(path.toStdString().c_str()) == 0;
#endif
    }

    static bool replaceFile_(const SwString& tempPath, const SwString& destinationPath, SwString* errOut) {
#if defined(_WIN32)
        const std::wstring wideTemp = tempPath.toStdWString();
        const std::wstring wideDest = destinationPath.toStdWString();
        if (::MoveFileExW(wideTemp.c_str(), wideDest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            return true;
        }
        setErr_(errOut, SwString("failed to move temp file to destination: ") + destinationPath);
        return false;
#else
        if (::rename(tempPath.toStdString().c_str(), destinationPath.toStdString().c_str()) == 0) {
            return true;
        }
        setErr_(errOut, SwString("failed to rename temp file to destination: ") + destinationPath);
        return false;
#endif
    }
};

} // namespace swinstaller
