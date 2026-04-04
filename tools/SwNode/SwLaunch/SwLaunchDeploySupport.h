#pragma once

#include "SwCrypto.h"
#include "SwDir.h"
#include "SwFile.h"
#include "SwJsonArray.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonValue.h"
#include "SwString.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#endif

#include <cstdio>

inline SwString swLaunchNormalizeRelativePath_(SwString path) {
    path.replace("\\", "/");
    while (path.startsWith("/")) {
        path.remove(0, 1);
    }
    while (path.contains("//")) {
        path.replace("//", "/");
    }
    return path;
}

inline bool swLaunchIsSafeRelativePath_(const SwString& path) {
    const SwString normalized = swLaunchNormalizeRelativePath_(path);
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

inline SwString swLaunchJoinRootAndRelative_(const SwString& root, const SwString& relativePath) {
    SwString base = SwDir::normalizePath(root);
    base.replace("\\", "/");
    if (base.endsWith("/") || base.endsWith("\\")) {
        base.chop(1);
    }
    const SwString normalizedRelative = swLaunchNormalizeRelativePath_(relativePath);
    if (base.isEmpty()) {
        return normalizedRelative;
    }
    return swDirPlatform().absolutePath(base + "/" + normalizedRelative);
}

inline SwString swLaunchChecksumForFile_(const SwString& path) {
    try {
        return SwCrypto::calculateFileChecksum(path.toStdString());
    } catch (...) {
        return SwString();
    }
}

inline bool swLaunchRemoveFileQuiet_(const SwString& path) {
    if (path.isEmpty()) {
        return true;
    }
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

inline bool swLaunchCopyFile_(const SwString& sourcePath,
                              const SwString& destinationPath,
                              SwString* errOut = nullptr) {
    const SwFile destination(destinationPath);
    const SwString destinationDir = destination.getDirectory();
    if (!destinationDir.isEmpty() && !SwDir::mkpathAbsolute(destinationDir, false)) {
        if (errOut) *errOut = SwString("failed to create destination directory: ") + destinationDir;
        return false;
    }
    if (SwFile::copy(sourcePath, destinationPath, true)) {
        return true;
    }
    if (SwFile::copyByChunk(sourcePath, destinationPath, false, 64)) {
        return true;
    }
    if (errOut) *errOut = SwString("failed to copy file: ") + sourcePath + " -> " + destinationPath;
    return false;
}

inline bool swLaunchWriteBytesFile_(const SwString& destinationPath,
                                    const SwByteArray& data,
                                    SwString* errOut = nullptr) {
    SwFile destination(destinationPath);
    const SwString destinationDir = destination.getDirectory();
    if (!destinationDir.isEmpty() && !SwDir::mkpathAbsolute(destinationDir, false)) {
        if (errOut) *errOut = SwString("failed to create destination directory: ") + destinationDir;
        return false;
    }
    if (!destination.openBinary(SwFile::Write)) {
        if (errOut) *errOut = SwString("failed to open destination file: ") + destinationPath;
        return false;
    }
    if (!destination.write(data)) {
        destination.close();
        if (errOut) *errOut = SwString("failed to write destination file: ") + destinationPath;
        return false;
    }
    destination.close();
    return true;
}

inline bool swLaunchWriteTextFile_(const SwString& destinationPath,
                                   const SwString& text,
                                   SwString* errOut = nullptr) {
    return swLaunchWriteBytesFile_(destinationPath, SwByteArray(text.toStdString()), errOut);
}

inline bool swLaunchReplaceFile_(const SwString& tempPath,
                                 const SwString& destinationPath,
                                 SwString* errOut = nullptr) {
#if defined(_WIN32)
    if (::MoveFileExW(tempPath.toStdWString().c_str(),
                      destinationPath.toStdWString().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return true;
    }
    if (errOut) *errOut = SwString("failed to replace file: ") + destinationPath;
    return false;
#else
    if (::rename(tempPath.toStdString().c_str(), destinationPath.toStdString().c_str()) == 0) {
        return true;
    }
    if (errOut) *errOut = SwString("failed to replace file: ") + destinationPath;
    return false;
#endif
}

inline bool swLaunchWriteJsonFileAtomic_(const SwString& path,
                                         const SwJsonObject& object,
                                         SwString* errOut = nullptr) {
    SwFile file(path);
    const SwString dir = file.getDirectory();
    if (!dir.isEmpty() && !SwDir::mkpathAbsolute(dir, false)) {
        if (errOut) *errOut = SwString("failed to create json directory: ") + dir;
        return false;
    }

    const SwString tempPath = path + ".swlaunch.tmp";
    SwJsonDocument document(object);
    if (!swLaunchWriteTextFile_(tempPath, document.toJson(SwJsonDocument::JsonFormat::Pretty), errOut)) {
        (void)swLaunchRemoveFileQuiet_(tempPath);
        return false;
    }
    if (!swLaunchReplaceFile_(tempPath, path, errOut)) {
        (void)swLaunchRemoveFileQuiet_(tempPath);
        return false;
    }
    return true;
}

inline bool swLaunchIsSha256Hex_(const SwString& value) {
    if (value.size() != 64) {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        const bool isDigit = (ch >= '0' && ch <= '9');
        const bool isLowerHex = (ch >= 'a' && ch <= 'f');
        const bool isUpperHex = (ch >= 'A' && ch <= 'F');
        if (!isDigit && !isLowerHex && !isUpperHex) {
            return false;
        }
    }
    return true;
}

inline SwString swLaunchJsonCompact_(const SwJsonValue& value) {
    if (value.isObject()) {
        return SwJsonDocument(value.toObject()).toJson(SwJsonDocument::JsonFormat::Compact);
    }
    if (value.isArray()) {
        return SwJsonDocument(value.toArray()).toJson(SwJsonDocument::JsonFormat::Compact);
    }
    return SwString(value.toString());
}
