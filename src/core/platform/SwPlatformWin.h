#pragma once

/**
 * @file src/core/platform/SwPlatformWin.h
 * @ingroup core_platform
 * @brief Declares the public interface exposed by SwPlatformWin in the CoreSw core platform
 * abstraction layer.
 *
 * This header belongs to the CoreSw core platform abstraction layer. It encapsulates low-level
 * filesystem and standard-location services that differ across supported operating systems.
 *
 * Within that layer, this file focuses on the platform win interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwWinFilePlatform, SwWinDirPlatform,
 * SwWinFileInfoPlatform, and SwWinStandardLocationProvider.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * The declarations in this area keep higher layers independent from direct POSIX or Win32 usage.
 *
 */

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

static constexpr const char* kSwLogCategory_SwPlatformWin = "sw.core.platform.swplatformwin";

#ifdef _WIN32

#include "SwPlatform.h"
#include "SwString.h"
#include "SwList.h"
#include "SwDateTime.h"
#include "SwDebug.h"
#include "platform/win/SwWindows.h"
#include <Shlwapi.h>
#include <shlobj.h>
#include <sys/stat.h>
#include <direct.h>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace {
inline std::wstring toWide(const SwString& str) {
    return str.toStdWString();
}

inline SwString fromWide(const wchar_t* wstr) {
    return SwString::fromWCharArray(wstr);
}

inline SwString fromWide(const std::wstring& wstr) {
    return SwString::fromWCharArray(wstr.c_str());
}
}

class SwWinFilePlatform : public SwFilePlatform {
public:
    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return `true` when the object reports file; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isFile(const SwString& path) const override {
        struct _stat64 info;
        if (_wstat64(toWide(path).c_str(), &info) != 0) {
            return false;
        }
        return (info.st_mode & _S_IFREG) != 0;
    }

    /**
     * @brief Performs the `copy` operation.
     * @param source Value passed to the method.
     * @param destination Value passed to the method.
     * @param overwrite Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool copy(const SwString& source, const SwString& destination, bool overwrite) override {
        if (!CopyFileW(toWide(source).c_str(), toWide(destination).c_str(), !overwrite)) {
            swCError(kSwLogCategory_SwPlatformWin) << "Failed to copy file. Error: " << GetLastError();
            return false;
        }
        return true;
    }

    /**
     * @brief Performs the `getFileMetadata` operation.
     * @param path Path used by the operation.
     * @param creationTime Value passed to the method.
     * @param lastAccessTime Value passed to the method.
     * @param lastWriteTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool getFileMetadata(const SwString& path, SwDateTime& creationTime, SwDateTime& lastAccessTime, SwDateTime& lastWriteTime) override {
        HANDLE handle = CreateFileW(toWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            swCError(kSwLogCategory_SwPlatformWin) << "Failed to open file for metadata: " << GetLastError();
            return false;
        }
        FILETIME c, a, w;
        if (!GetFileTime(handle, &c, &a, &w)) {
            CloseHandle(handle);
            return false;
        }
        CloseHandle(handle);
        creationTime = fileTimeToDateTime(c);
        lastAccessTime = fileTimeToDateTime(a);
        lastWriteTime = fileTimeToDateTime(w);
        return true;
    }

    /**
     * @brief Sets the creation Time.
     * @param path Path used by the operation.
     * @param creationTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setCreationTime(const SwString& path, SwDateTime creationTime) override {
        HANDLE handle = openForMetadata(path);
        if (handle == INVALID_HANDLE_VALUE) return false;
        FILETIME ft = dateTimeToFileTime(creationTime);
        bool ok = SetFileTime(handle, &ft, nullptr, nullptr) != 0;
        CloseHandle(handle);
        return ok;
    }

    /**
     * @brief Sets the last Write Date.
     * @param path Path used by the operation.
     * @param lastWriteTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setLastWriteDate(const SwString& path, SwDateTime lastWriteTime) override {
        HANDLE handle = openForMetadata(path);
        if (handle == INVALID_HANDLE_VALUE) return false;
        FILETIME ft = dateTimeToFileTime(lastWriteTime);
        bool ok = SetFileTime(handle, nullptr, nullptr, &ft) != 0;
        CloseHandle(handle);
        return ok;
    }

    /**
     * @brief Sets the last Access Date.
     * @param path Path used by the operation.
     * @param lastAccessTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setLastAccessDate(const SwString& path, SwDateTime lastAccessTime) override {
        HANDLE handle = openForMetadata(path);
        if (handle == INVALID_HANDLE_VALUE) return false;
        FILETIME ft = dateTimeToFileTime(lastAccessTime);
        bool ok = SetFileTime(handle, nullptr, &ft, nullptr) != 0;
        CloseHandle(handle);
        return ok;
    }

    /**
     * @brief Sets the all Dates.
     * @param path Path used by the operation.
     * @param creationTime Value passed to the method.
     * @param lastAccessTime Value passed to the method.
     * @param lastWriteTime Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setAllDates(const SwString& path, SwDateTime creationTime, SwDateTime lastAccessTime, SwDateTime lastWriteTime) override {
        HANDLE handle = openForMetadata(path);
        if (handle == INVALID_HANDLE_VALUE) return false;
        FILETIME c = dateTimeToFileTime(creationTime);
        FILETIME a = dateTimeToFileTime(lastAccessTime);
        FILETIME w = dateTimeToFileTime(lastWriteTime);
        bool ok = SetFileTime(handle, &c, &a, &w) != 0;
        CloseHandle(handle);
        return ok;
    }

    /**
     * @brief Performs the `writeMetadata` operation on the associated resource.
     * @param path Path used by the operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool writeMetadata(const SwString& path, const SwString& key, const SwString& value) override {
        if (!isNTFS(path)) {
            swCWarning(kSwLogCategory_SwPlatformWin) << "ADS metadata unsupported on this volume";
            return false;
        }
        std::wstring adsPath = toWide(convertToLongPath(path)) + L":" + toWide(key);
        HANDLE handle = CreateFileW(adsPath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            swCError(kSwLogCategory_SwPlatformWin) << "Failed to open ADS: " << GetLastError();
            return false;
        }
        std::wstring data = toWide(value);
        DWORD written = 0;
        BOOL ok = WriteFile(handle, data.c_str(), static_cast<DWORD>(data.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(handle);
        return ok != 0;
    }

    /**
     * @brief Performs the `readMetadata` operation on the associated resource.
     * @param path Path used by the operation.
     * @param key Value passed to the method.
     * @return The resulting metadata.
     */
    SwString readMetadata(const SwString& path, const SwString& key) override {
        std::wstring adsPath = toWide(convertToLongPath(path)) + L":" + toWide(key);
        HANDLE handle = CreateFileW(adsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return SwString();
        }
        std::wstring buffer(1024, L'\0');
        DWORD read = 0;
        wchar_t* raw = buffer.empty() ? nullptr : &buffer[0];
        if (!raw) {
            CloseHandle(handle);
            return SwString();
        }
        if (!ReadFile(handle, raw, static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), &read, nullptr)) {
            CloseHandle(handle);
            return SwString();
        }
        CloseHandle(handle);
        buffer.resize(read / sizeof(wchar_t));
        return fromWide(buffer);
    }

private:
    static SwDateTime fileTimeToDateTime(const FILETIME& ft) {
        ULONGLONG ull = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        ull -= 116444736000000000ULL;
        ull /= 10000000ULL;
        return SwDateTime(static_cast<std::time_t>(ull));
    }

    static FILETIME dateTimeToFileTime(SwDateTime dt) {
        ULONGLONG ull = static_cast<ULONGLONG>(dt.toTimeT());
        ull = ull * 10000000ULL + 116444736000000000ULL;
        FILETIME ft;
        ft.dwLowDateTime = static_cast<DWORD>(ull);
        ft.dwHighDateTime = static_cast<DWORD>(ull >> 32);
        return ft;
    }

    HANDLE openForMetadata(const SwString& path) const {
        return CreateFileW(toWide(path).c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    bool isNTFS(const SwString& path) const {
        wchar_t volumeRoot[MAX_PATH];
        if (!GetVolumePathNameW(toWide(path).c_str(), volumeRoot, MAX_PATH)) {
            return false;
        }
        wchar_t fsName[MAX_PATH];
        if (!GetVolumeInformationW(volumeRoot, nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH)) {
            return false;
        }
        return std::wstring(fsName) == L"NTFS";
    }

    SwString convertToLongPath(const SwString& path) const {
        SwString normalized = path;
        normalized.replace("/", "\\");
        if (!normalized.startsWith("\\\\?\\")) {
            normalized = SwString("\\\\?\\") + normalized;
        }
        return normalized;
    }
};

class SwWinDirPlatform : public SwDirPlatform {
public:
    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool exists(const SwString& path) const override {
        DWORD attr = GetFileAttributesW(toWide(path).c_str());
        return attr != INVALID_FILE_ATTRIBUTES;
    }

    /**
     * @brief Returns whether the object reports directory.
     * @param path Path used by the operation.
     * @return `true` when the object reports directory; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isDirectory(const SwString& path) const override {
        DWORD attr = GetFileAttributesW(toWide(path).c_str());
        return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     * @return The requested normalize Path.
     */
    SwString normalizePath(const SwString& path) const override {
        SwString result = path.trimmed();
        result.replace("/", "\\");
        if (result.isEmpty()) {
            return result;
        }

        if (result.startsWith("\\\\?\\")) {
            return result;
        }

        const bool isDriveAbsolute = result.size() > 1 && result[1] == ':';
        const bool isUnc = result.startsWith("\\\\");
        if (!isDriveAbsolute && !isUnc) {
            wchar_t buffer[MAX_PATH];
            DWORD len = GetFullPathNameW(toWide(result).c_str(), MAX_PATH, buffer, nullptr);
            if (len != 0 && len < MAX_PATH) {
                result = fromWide(buffer);
                result.replace("/", "\\");
            }
        }

        if (result.startsWith("\\\\")) {
            if (!result.startsWith("\\\\?\\UNC\\")) {
                result = SwString("\\\\?\\UNC\\") + result.mid(2);
            }
            return result;
        }

        return SwString("\\\\?\\") + result;
    }

    /**
     * @brief Performs the `pathSeparator` operation.
     * @return The requested path Separator.
     */
    SwString pathSeparator() const override { return SwString("\\"); }

    /**
     * @brief Performs the `entryList` operation.
     * @param path Path used by the operation.
     * @param flags Flags that refine the operation.
     * @return The requested entry List.
     */
    SwStringList entryList(const SwString& path, EntryTypes flags) const override {
        return listInternal(path, flags, {});
    }

    /**
     * @brief Performs the `entryList` operation.
     * @param path Path used by the operation.
     * @param filters Value passed to the method.
     * @param flags Flags that refine the operation.
     * @return The requested entry List.
     */
    SwStringList entryList(const SwString& path, const SwStringList& filters, EntryTypes flags) const override {
        return listInternal(path, flags, filters);
    }

    /**
     * @brief Performs the `findFiles` operation.
     * @param path Path used by the operation.
     * @param filter Value passed to the method.
     * @return The requested find Files.
     */
    SwStringList findFiles(const SwString& path, const SwString& filter) const override {
        SwStringList found;
        recurseFind(normalizeWithSeparator(path), filter, found);
        return found;
    }

    /**
     * @brief Performs the `absolutePath` operation.
     * @param path Path used by the operation.
     * @return The requested absolute Path.
     */
    SwString absolutePath(const SwString& path) const override {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetFullPathNameW(toWide(path).c_str(), MAX_PATH, buffer, nullptr);
        if (len == 0 || len >= MAX_PATH) {
            return path;
        }
        return fromWide(buffer);
    }

    /**
     * @brief Returns the current current Path.
     * @return The current current Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString currentPath() const override {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetCurrentDirectoryW(MAX_PATH, buffer);
        if (len == 0 || len >= MAX_PATH) {
            return SwString();
        }
        SwString result = fromWide(buffer);
        if (!result.endsWith(pathSeparator())) {
            result += pathSeparator();
        }
        return result;
    }

    /**
     * @brief Performs the `mkdir` operation.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool mkdir(const SwString& path) const override {
        SwString normalized = normalizePath(path);
        std::wstring wide = toWide(normalized);
        if (wide.empty()) {
            return false;
        }

        if (CreateDirectoryW(wide.c_str(), nullptr)) {
            return true;
        }

        DWORD err = GetLastError();
        const DWORD attr = GetFileAttributesW(wide.c_str());
        if (err == ERROR_ALREADY_EXISTS ||
            (err == ERROR_ACCESS_DENIED && attr != INVALID_FILE_ATTRIBUTES &&
             (attr & FILE_ATTRIBUTE_DIRECTORY))) {
            return true;
        }

        // Build intermediate directories iteratively to avoid infinite recursion.
        std::wstring partial;
        size_t start = 0;
        if (wide.rfind(L"\\\\?\\UNC\\", 0) == 0) {
            partial.assign(L"\\\\?\\UNC\\");
            start = 8;
        } else if (wide.rfind(L"\\\\?\\", 0) == 0) {
            partial.assign(L"\\\\?\\");
            start = 4;
        }

        for (size_t i = start; i < wide.size(); ++i) {
            wchar_t ch = wide[i];
            partial.push_back(ch);
            if (ch == L'\\' || ch == L'/') {
                if (partial.size() == 4 || partial.size() == 8) {
                    continue; // Skip prefix-only segments
                }
                if (CreateDirectoryW(partial.c_str(), nullptr)) {
                    continue;
                }
                DWORD partialErr = GetLastError();
                const DWORD partialAttr = GetFileAttributesW(partial.c_str());
                if (partialErr != ERROR_ALREADY_EXISTS &&
                    !(partialErr == ERROR_ACCESS_DENIED && partialAttr != INVALID_FILE_ATTRIBUTES &&
                      (partialAttr & FILE_ATTRIBUTE_DIRECTORY))) {
                    // Stop trying to build parents if we hit an unexpected error.
                    break;
                }
            }
        }

        if (CreateDirectoryW(wide.c_str(), nullptr)) {
            return true;
        }

        err = GetLastError();
        const DWORD finalAttr = GetFileAttributesW(wide.c_str());
        return err == ERROR_ALREADY_EXISTS ||
               (err == ERROR_ACCESS_DENIED && finalAttr != INVALID_FILE_ATTRIBUTES &&
                (finalAttr & FILE_ATTRIBUTE_DIRECTORY));
    }

    /**
     * @brief Removes the specified recursively.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool removeRecursively(const SwString& path) const override {
        WIN32_FIND_DATAW data;
        SwString search = normalizeWithSeparator(path) + "*";
        HANDLE handle = FindFirstFileW(toWide(search).c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return RemoveDirectoryW(toWide(path).c_str()) != 0;
        }
        do {
            SwString name = fromWide(data.cFileName);
            if (name == "." || name == "..") continue;
            SwString full = normalizeWithSeparator(path) + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeRecursively(full);
            } else {
                DeleteFileW(toWide(full).c_str());
            }
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
        return RemoveDirectoryW(toWide(path).c_str()) != 0;
    }

    /**
     * @brief Performs the `copyDirectory` operation.
     * @param source Value passed to the method.
     * @param destination Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool copyDirectory(const SwString& source, const SwString& destination) const override {
        if (!mkdir(destination)) {
            return false;
        }
        WIN32_FIND_DATAW data;
        SwString search = normalizeWithSeparator(source) + "*";
        HANDLE handle = FindFirstFileW(toWide(search).c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        do {
            SwString name = fromWide(data.cFileName);
            if (name == "." || name == "..") continue;
            SwString src = normalizeWithSeparator(source) + name;
            SwString dst = normalizeWithSeparator(destination) + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!copyDirectory(src, dst)) {
                    FindClose(handle);
                    return false;
                }
            } else {
                if (!swFilePlatform().copy(src, dst, true)) {
                    FindClose(handle);
                    return false;
                }
            }
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
        return true;
    }

private:
    SwString normalizeWithSeparator(const SwString& path) const {
        SwString normalized = normalizePath(path);
        if (!normalized.endsWith(pathSeparator())) {
            normalized += pathSeparator();
        }
        return normalized;
    }

    SwStringList listInternal(const SwString& path, EntryTypes flags, const SwStringList& filters) const {
        SwStringList entries;
        WIN32_FIND_DATAW data;
        SwString search = normalizeWithSeparator(path) + "*";
        // Suppress error dialogs for inaccessible paths (e.g. disconnected network drives).
        const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        HANDLE handle = FindFirstFileW(toWide(search).c_str(), &data);
        SetErrorMode(oldMode);
        if (handle == INVALID_HANDLE_VALUE) {
            return entries;
        }
        do {
            SwString name = fromWide(data.cFileName);
            if (name == "." || name == "..") continue;
            bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool accept = (isDir && flags.testFlag(EntryType::Directories)) || (!isDir && flags.testFlag(EntryType::Files));
            if (!accept) continue;
            if (!filters.isEmpty()) {
                bool match = false;
                for (const auto& filter : filters) {
                    if (matchesPattern(name, filter)) {
                        match = true;
                        break;
                    }
                }
                if (!match) continue;
            }
            entries.append(name);
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
        return entries;
    }

    void recurseFind(const SwString& directory, const SwString& filter, SwStringList& output) const {
        WIN32_FIND_DATAW data;
        SwString search = directory + "*";
        HANDLE handle = FindFirstFileW(toWide(search).c_str(), &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }
        do {
            SwString name = fromWide(data.cFileName);
            if (name == "." || name == "..") continue;
            SwString full = directory + name;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                recurseFind(full + pathSeparator(), filter, output);
            } else {
                if (filter.isEmpty() || matchesPattern(name, filter)) {
                    output.append(full);
                }
            }
        } while (FindNextFileW(handle, &data));
        FindClose(handle);
    }

    bool matchesPattern(const SwString& name, const SwString& pattern) const {
        std::string regexPattern = "^" + std::regex_replace(pattern.toStdString(), std::regex(R"(\*)"), ".*") + "$";
        std::regex regex(regexPattern, std::regex_constants::icase);
        return std::regex_match(name.toStdString(), regex);
    }
};

class SwWinFileInfoPlatform : public SwFileInfoPlatform {
public:
    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool exists(const std::string& path) const override {
        DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES;
    }

    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return `true` when the object reports file; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isFile(const std::string& path) const override {
        DWORD attributes = GetFileAttributesA(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return false;
        return !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    /**
     * @brief Returns whether the object reports dir.
     * @param path Path used by the operation.
     * @return `true` when the object reports dir; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isDir(const std::string& path) const override {
        DWORD attributes = GetFileAttributesA(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return false;
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    /**
     * @brief Performs the `absoluteFilePath` operation.
     * @param path Path used by the operation.
     * @return The requested absolute File Path.
     */
    std::string absoluteFilePath(const std::string& path) const override {
        char buffer[MAX_PATH];
        if (_fullpath(buffer, path.c_str(), MAX_PATH)) {
            return std::string(buffer);
        }
        return path;
    }

    /**
     * @brief Performs the `size` operation.
     * @param path Path used by the operation.
     * @return The current size value.
     */
    size_t size(const std::string& path) const override {
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileInfo)) {
            return 0;
        }
        LARGE_INTEGER fileSize;
        fileSize.LowPart = fileInfo.nFileSizeLow;
        fileSize.HighPart = fileInfo.nFileSizeHigh;
        return static_cast<size_t>(fileSize.QuadPart);
    }

    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     */
    void normalizePath(std::string& path) const override {
        std::replace(path.begin(), path.end(), '/', '\\');
    }
};

class SwWinStandardLocationProvider : public SwStandardLocationProvider {
public:
    /**
     * @brief Performs the `standardLocation` operation.
     * @param type Value passed to the method.
     * @return The requested standard Location.
     */
    SwString standardLocation(SwStandardLocationId type) const override {
        if (type == SwStandardLocationId::Temp) {
            wchar_t tempPath[MAX_PATH];
            DWORD len = GetTempPathW(MAX_PATH, tempPath);
            if (len > 0 && len < MAX_PATH) {
                return fromWide(tempPath);
            }
            return SwString();
        }
        if (type == SwStandardLocationId::ProgramFiles) {
            return lookupKnownFolder(FOLDERID_ProgramFiles);
        }
        if (type == SwStandardLocationId::ProgramFilesX86) {
            return lookupKnownFolder(FOLDERID_ProgramFilesX86);
        }
        if (type == SwStandardLocationId::ProgramFilesCommon) {
            return lookupKnownFolder(FOLDERID_ProgramFilesCommon);
        }
        if (type == SwStandardLocationId::ProgramFilesCommonX86) {
            return lookupKnownFolder(FOLDERID_ProgramFilesCommonX86);
        }
        REFKNOWNFOLDERID folderId = folderIdFor(type);
        return lookupKnownFolder(folderId);
    }

    /**
     * @brief Performs the `convertPath` operation.
     * @param path Path used by the operation.
     * @param type Value passed to the method.
     * @return The requested convert Path.
     */
    SwString convertPath(const SwString& path, SwStandardPathType type) const override {
        SwString result = path;
        switch (type) {
        case SwStandardPathType::Windows:
            result.replace("/", "\\");
            if (result.startsWith("\\\\?\\")) {
                result = result.mid(4);
            }
            break;
        case SwStandardPathType::WindowsLong:
            result.replace("/", "\\");
            if (!result.startsWith("\\\\?\\")) {
                result = SwString("\\\\?\\") + result;
            }
            break;
        case SwStandardPathType::Unix:
        case SwStandardPathType::Mixed:
            result.replace("\\", "/");
            break;
        default:
            break;
        }
        return result;
    }

private:
    SwString lookupKnownFolder(REFKNOWNFOLDERID folderId) const {
        wchar_t* path = nullptr;
        SwString result;
        if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, NULL, &path))) {
            result = fromWide(path);
            CoTaskMemFree(path);
        }
        return result;
    }

    REFKNOWNFOLDERID folderIdFor(SwStandardLocationId type) const {
        switch (type) {
        case SwStandardLocationId::Desktop: return FOLDERID_Desktop;
        case SwStandardLocationId::Documents: return FOLDERID_Documents;
        case SwStandardLocationId::Downloads: return FOLDERID_Downloads;
        case SwStandardLocationId::Music: return FOLDERID_Music;
        case SwStandardLocationId::Pictures: return FOLDERID_Pictures;
        case SwStandardLocationId::Videos: return FOLDERID_Videos;
        case SwStandardLocationId::Home: return FOLDERID_Profile;
        case SwStandardLocationId::AppData: return FOLDERID_RoamingAppData;
        case SwStandardLocationId::LocalAppData: return FOLDERID_LocalAppData;
        case SwStandardLocationId::RoamingAppData: return FOLDERID_RoamingAppData;
        case SwStandardLocationId::Cache: return FOLDERID_LocalAppData;
        case SwStandardLocationId::Config: return FOLDERID_RoamingAppData;
        case SwStandardLocationId::Favorites: return FOLDERID_Favorites;
        case SwStandardLocationId::StartMenu: return FOLDERID_StartMenu;
        case SwStandardLocationId::Startup: return FOLDERID_Startup;
        case SwStandardLocationId::Recent: return FOLDERID_Recent;
        case SwStandardLocationId::SendTo: return FOLDERID_SendTo;
        default: return FOLDERID_Desktop;
        }
    }
};

inline SwFilePlatform& swFilePlatform() {
    static SwWinFilePlatform instance;
    return instance;
}

inline SwDirPlatform& swDirPlatform() {
    static SwWinDirPlatform instance;
    return instance;
}

inline SwFileInfoPlatform& swFileInfoPlatform() {
    static SwWinFileInfoPlatform instance;
    return instance;
}

inline const SwStandardLocationProvider& swStandardLocationProvider() {
    static SwWinStandardLocationProvider instance;
    return instance;
}

#endif // _WIN32
