#pragma once

/**
 * @file src/core/platform/SwPlatformPosix.h
 * @ingroup core_platform
 * @brief Declares the public interface exposed by SwPlatformPosix in the CoreSw core platform
 * abstraction layer.
 *
 * This header belongs to the CoreSw core platform abstraction layer. It encapsulates low-level
 * filesystem and standard-location services that differ across supported operating systems.
 *
 * Within that layer, this file focuses on the platform posix interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwPosixFilePlatform, SwPosixDirPlatform,
 * SwPosixFileInfoPlatform, and SwPosixStandardLocationProvider.
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

#ifndef _WIN32

#include "SwPlatform.h"
#include "SwString.h"
#include "SwList.h"
#include "SwDateTime.h"
#include "SwDebug.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <regex>
#include <vector>
#include <string>
#include <sstream>
#include <limits.h>
#include <fstream>
static constexpr const char* kSwLogCategory_SwPlatformPosix = "sw.core.platform.swplatformposix";

#ifdef __linux__
#include <sys/xattr.h>
#endif

class SwPosixFilePlatform : public SwFilePlatform {
public:
    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return `true` when the object reports file; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isFile(const SwString& path) const override {
        struct stat info;
        if (stat(path.toStdString().c_str(), &info) != 0) {
            return false;
        }
        return S_ISREG(info.st_mode);
    }

    /**
     * @brief Performs the `copy` operation.
     * @param source Value passed to the method.
     * @param destination Value passed to the method.
     * @param overwrite Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool copy(const SwString& source, const SwString& destination, bool overwrite) override {
        if (!overwrite) {
            struct stat info; if (stat(destination.toStdString().c_str(), &info) == 0) return false;
        }
        std::ifstream src(source.toStdString(), std::ios::binary);
        std::ofstream dst(destination.toStdString(), std::ios::binary | std::ios::trunc);
        if (!src.is_open() || !dst.is_open()) {
            return false;
        }
        dst << src.rdbuf();
        return dst.good();
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
        struct stat info;
        if (stat(path.toStdString().c_str(), &info) != 0) {
            return false;
        }
        creationTime = SwDateTime(info.st_ctime);
        lastAccessTime = SwDateTime(info.st_atime);
        lastWriteTime = SwDateTime(info.st_mtime);
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
        (void)path; (void)creationTime;
        swCWarning(kSwLogCategory_SwPlatformPosix) << "Creation time update not supported on this platform.";
        return false;
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
        struct stat info;
        if (stat(path.toStdString().c_str(), &info) != 0) {
            return false;
        }
        struct utimbuf times;
        times.actime = info.st_atime;
        times.modtime = lastWriteTime.toTimeT();
        return utime(path.toStdString().c_str(), &times) == 0;
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
        struct stat info;
        if (stat(path.toStdString().c_str(), &info) != 0) {
            return false;
        }
        struct utimbuf times;
        times.actime = lastAccessTime.toTimeT();
        times.modtime = info.st_mtime;
        return utime(path.toStdString().c_str(), &times) == 0;
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
        (void)creationTime;
        struct utimbuf times;
        times.actime = lastAccessTime.toTimeT();
        times.modtime = lastWriteTime.toTimeT();
        return utime(path.toStdString().c_str(), &times) == 0;
    }

    /**
     * @brief Performs the `writeMetadata` operation on the associated resource.
     * @param path Path used by the operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool writeMetadata(const SwString& path, const SwString& key, const SwString& value) override {
#ifdef __linux__
        std::string attr = "user." + key.toStdString();
        std::string data = value.toStdString();
        if (setxattr(path.toStdString().c_str(), attr.c_str(), data.data(), data.size(), 0) != 0) {
            swCError(kSwLogCategory_SwPlatformPosix) << "Failed to set xattr: " << strerror(errno);
            return false;
        }
        return true;
#else
        (void)path; (void)key; (void)value;
        swCWarning(kSwLogCategory_SwPlatformPosix) << "Extended attributes not available.";
        return false;
#endif
    }

    /**
     * @brief Performs the `readMetadata` operation on the associated resource.
     * @param path Path used by the operation.
     * @param key Value passed to the method.
     * @return The resulting metadata.
     */
    SwString readMetadata(const SwString& path, const SwString& key) override {
#ifdef __linux__
        std::string attr = "user." + key.toStdString();
        ssize_t size = getxattr(path.toStdString().c_str(), attr.c_str(), nullptr, 0);
        if (size <= 0) return SwString();
        std::string data(static_cast<size_t>(size), '\0');
        if (getxattr(path.toStdString().c_str(), attr.c_str(), &data[0], data.size()) != size) {
            return SwString();
        }
        return SwString(data);
#else
        (void)path; (void)key;
        return SwString();
#endif
    }
};

class SwPosixDirPlatform : public SwDirPlatform {
public:
    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool exists(const SwString& path) const override {
        struct stat info;
        return stat(path.toStdString().c_str(), &info) == 0;
    }

    /**
     * @brief Returns whether the object reports directory.
     * @param path Path used by the operation.
     * @return `true` when the object reports directory; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isDirectory(const SwString& path) const override {
        struct stat info;
        return stat(path.toStdString().c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    }

    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     * @return The requested normalize Path.
     */
    SwString normalizePath(const SwString& path) const override {
        SwString normalized = path;
        normalized.replace("\\", "/");
        return normalized;
    }

    /**
     * @brief Performs the `pathSeparator` operation.
     * @return The requested path Separator.
     */
    SwString pathSeparator() const override { return SwString("/"); }

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
        char buffer[PATH_MAX];
        if (realpath(path.toStdString().c_str(), buffer)) {
            return SwString(buffer);
        }
        return path;
    }

    /**
     * @brief Returns the current current Path.
     * @return The current current Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString currentPath() const override {
        char buffer[PATH_MAX];
        if (getcwd(buffer, sizeof(buffer))) {
            SwString result(buffer);
            if (!result.endsWith(pathSeparator())) result += pathSeparator();
            return result;
        }
        return SwString();
    }

    /**
     * @brief Performs the `mkdir` operation.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool mkdir(const SwString& path) const override {
        std::string current;
        std::string full = normalizePath(path).toStdString();
        std::stringstream ss(full);
        std::string segment;
        while (std::getline(ss, segment, '/')) {
            if (segment.empty()) continue;
            current += "/" + segment;
            ::mkdir(current.c_str(), 0755);
        }
        struct stat info;
        return stat(full.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    }

    /**
     * @brief Removes the specified recursively.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool removeRecursively(const SwString& path) const override {
        return removeRecursiveInternal(path.toStdString());
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
        DIR* dir = opendir(source.toStdString().c_str());
        if (!dir) return false;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            SwString srcPath = normalizeWithSeparator(source) + name.c_str();
            SwString dstPath = normalizeWithSeparator(destination) + name.c_str();
            if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
                copyDirectory(srcPath, dstPath);
            } else {
                swFilePlatform().copy(srcPath, dstPath, true);
            }
        }
        closedir(dir);
        return true;
    }

private:
    SwString normalizeWithSeparator(const SwString& path) const {
        SwString normalized = normalizePath(path);
        if (!normalized.endsWith(pathSeparator())) normalized += pathSeparator();
        return normalized;
    }

    SwStringList listInternal(const SwString& path, EntryTypes flags, const SwStringList& filters) const {
        SwStringList entries;
        DIR* dir = opendir(path.toStdString().c_str());
        if (!dir) return entries;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            bool isDir = entry->d_type == DT_DIR;
            if (entry->d_type == DT_UNKNOWN) {
                struct stat st;
                std::string fullPath = path.toStdString();
                if (!fullPath.empty() && fullPath.back() != '/') fullPath += "/";
                fullPath += name;
                if (stat(fullPath.c_str(), &st) == 0) {
                    isDir = S_ISDIR(st.st_mode);
                }
            }
            if ((isDir && flags.testFlag(EntryType::Directories)) || (!isDir && flags.testFlag(EntryType::Files))) {
                if (!filters.isEmpty() && !matchesAnyPattern(name, filters)) {
                    continue;
                }
                entries.append(SwString(name));
            }
        }
        closedir(dir);
        return entries;
    }

    bool matchesAnyPattern(const std::string& name, const SwStringList& filters) const {
        if (filters.isEmpty()) {
            return true;
        }
        for (const auto& filter : filters) {
            if (matchesPattern(name, filter.toStdString())) {
                return true;
            }
        }
        return false;
    }

    bool matchesPattern(const std::string& name, const std::string& pattern) const {
        std::string regexPattern = "^" + std::regex_replace(pattern, std::regex(R"(\*)"), ".*") + "$";
        std::regex regex(regexPattern, std::regex_constants::icase);
        return std::regex_match(name, regex);
    }

    void recurseFind(const SwString& directory, const SwString& filter, SwStringList& out) const {
        DIR* dir = opendir(directory.toStdString().c_str());
        if (!dir) return;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            SwString full = directory + name.c_str();
            bool isDir = entry->d_type == DT_DIR;
            if (entry->d_type == DT_UNKNOWN) {
                struct stat st;
                std::string pathStr = full.toStdString();
                if (stat(pathStr.c_str(), &st) == 0) {
                    isDir = S_ISDIR(st.st_mode);
                }
            }
            if (isDir) {
                recurseFind(full + pathSeparator(), filter, out);
            } else {
                if (filter.isEmpty() || matchesPattern(name, filter.toStdString())) {
                    out.append(full);
                }
            }
        }
        closedir(dir);
    }

    bool removeRecursiveInternal(const std::string& path) const {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            return ::remove(path.c_str()) == 0;
        }
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            std::string child = path + "/" + name;
            if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
                removeRecursiveInternal(child);
            } else {
                ::remove(child.c_str());
            }
        }
        closedir(dir);
        return ::rmdir(path.c_str()) == 0;
    }
};

class SwPosixFileInfoPlatform : public SwFileInfoPlatform {
public:
    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool exists(const std::string& path) const override {
        struct stat info; return stat(path.c_str(), &info) == 0;
    }
    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return `true` when the object reports file; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isFile(const std::string& path) const override {
        struct stat info; return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
    }
    /**
     * @brief Returns whether the object reports dir.
     * @param path Path used by the operation.
     * @return `true` when the object reports dir; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isDir(const std::string& path) const override {
        struct stat info; return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
    }
    /**
     * @brief Performs the `absoluteFilePath` operation.
     * @param path Path used by the operation.
     * @return The requested absolute File Path.
     */
    std::string absoluteFilePath(const std::string& path) const override {
        char buffer[PATH_MAX];
        if (realpath(path.c_str(), buffer)) return std::string(buffer);
        return path;
    }
    /**
     * @brief Performs the `size` operation.
     * @param path Path used by the operation.
     * @return The current size value.
     */
    size_t size(const std::string& path) const override {
        struct stat info; if (stat(path.c_str(), &info) != 0) return 0; return static_cast<size_t>(info.st_size);
    }
    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     */
    void normalizePath(std::string& path) const override {
        std::replace(path.begin(), path.end(), '\\', '/');
    }
};

class SwPosixStandardLocationProvider : public SwStandardLocationProvider {
public:
    /**
     * @brief Performs the `standardLocation` operation.
     * @param type Value passed to the method.
     * @return The requested standard Location.
     */
    SwString standardLocation(SwStandardLocationId type) const override {
        const char* home = getenv("HOME");
        SwString base = home ? SwString(home) : SwString("/");
        switch (type) {
        case SwStandardLocationId::Desktop: return base + "/Desktop";
        case SwStandardLocationId::Documents: return base + "/Documents";
        case SwStandardLocationId::Downloads: return base + "/Downloads";
        case SwStandardLocationId::Music: return base + "/Music";
        case SwStandardLocationId::Pictures: return base + "/Pictures";
        case SwStandardLocationId::Videos: return base + "/Videos";
        case SwStandardLocationId::Home: return base;
        case SwStandardLocationId::Temp: return SwString("/tmp");
        default: return base;
        }
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
        case SwStandardPathType::WindowsLong:
            result.replace("/", "\\");
            break;
        case SwStandardPathType::Unix:
        case SwStandardPathType::Mixed:
        default:
            result.replace("\\", "/");
            break;
        }
        return result;
    }
};

inline SwFilePlatform& swFilePlatform() {
    static SwPosixFilePlatform instance;
    return instance;
}

inline SwDirPlatform& swDirPlatform() {
    static SwPosixDirPlatform instance;
    return instance;
}

inline SwFileInfoPlatform& swFileInfoPlatform() {
    static SwPosixFileInfoPlatform instance;
    return instance;
}

inline const SwStandardLocationProvider& swStandardLocationProvider() {
    static SwPosixStandardLocationProvider instance;
    return instance;
}

#endif // !_WIN32
