#pragma once

/**
 * @file src/core/fs/SwDir.h
 * @ingroup core_fs
 * @brief Declares the public interface exposed by SwDir in the CoreSw filesystem layer.
 *
 * This header belongs to the CoreSw filesystem layer. It wraps platform-specific path, directory,
 * settings, and related utility services behind framework-native types.
 *
 * Within that layer, this file focuses on the dir interface. The declarations exposed here define
 * the stable surface that adjacent code can rely on while the implementation remains free to
 * evolve behind the header.
 *
 * The main declarations in this header are SwDir.
 *
 * Directory and path declarations here usually define normalization, lookup, traversal, and
 * platform-neutral path manipulation rules that other modules can depend on.
 *
 * Filesystem declarations in this area are meant to keep file and path behavior predictable
 * across platforms while staying inside the Sw* type system.
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

#include <string>
#include <iostream>

#include "Sw.h"
#include "SwString.h"
#include "SwList.h"
#include "SwFile.h"
#include "SwDebug.h"
#include "platform/SwPlatformSelector.h"
static constexpr const char* kSwLogCategory_SwDir = "sw.core.fs.swdir";


class SwDir {
public:
    /**
     * @brief Constructs a `SwDir` instance.
     * @param path Path used by the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwDir(const SwString& path = ".") {
        setPath(path);
    }

    /**
     * @brief Destroys the `SwDir` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwDir() = default;

    /**
     * @brief Returns the current exists.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool exists() const {
        return swDirPlatform().exists(m_path);
    }

    /**
     * @brief Returns the current path.
     * @return The current path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString path() const {
        return m_path;
    }

    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return The requested exists.
     */
    static bool exists(const SwString& path) {
        return swDirPlatform().exists(path);
    }

    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     * @return The requested normalize Path.
     */
    static SwString normalizePath(const SwString& path) {
        return swDirPlatform().normalizePath(path);
    }

    /**
     * @brief Performs the `mkpathAbsolute` operation.
     * @param path Path used by the operation.
     * @param normalizeInput Value passed to the method.
     * @return The requested mkpath Absolute.
     */
    static bool mkpathAbsolute(const SwString& path, bool normalizeInput = true) {
        SwString target = path;
        if (normalizeInput) {
            target = normalizePath(path);
        }
        return createPathTree(target);
    }

    /**
     * @brief Performs the `mkpath` operation.
     * @param subPath Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool mkpath(const SwString& subPath) const {
        SwString target = absoluteFilePath(subPath);
        return createPathTree(target);
    }

    /**
     * @brief Sets the path.
     * @param path Path used by the operation.
     * @return `true` on success; otherwise `false`.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    bool setPath(const SwString& path) {
        SwString normalized = normalizePath(path);
        if (!swDirPlatform().isDirectory(normalized)) {
            swCError(kSwLogCategory_SwDir) << "Error set path failed!";
            return false;
        }
        m_path = normalized;
        if (!m_path.endsWith(swDirPlatform().pathSeparator())) {
            m_path += swDirPlatform().pathSeparator();
        }
        return true;
    }

    /**
     * @brief Performs the `entryList` operation.
     * @param flags Flags that refine the operation.
     * @return The requested entry List.
     */
    SwStringList entryList(EntryTypes flags) const {
        return swDirPlatform().entryList(m_path, flags);
    }

    /**
     * @brief Performs the `entryList` operation.
     * @param filters Value passed to the method.
     * @param flags Flags that refine the operation.
     * @return The requested entry List.
     */
    SwStringList entryList(const SwStringList& filters, EntryTypes flags = EntryType::AllEntries) const {
        return swDirPlatform().entryList(m_path, filters, flags);
    }

    /**
     * @brief Returns the current absolute Path.
     * @return The current absolute Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString absolutePath() const {
        return swDirPlatform().absolutePath(m_path);
    }

    /**
     * @brief Performs the `absoluteFilePath` operation.
     * @param relativePath Value passed to the method.
     * @return The requested absolute File Path.
     */
    SwString absoluteFilePath(const SwString& relativePath) const {
        if (relativePath.isEmpty()) {
            swCError(kSwLogCategory_SwDir) << "Relative path cannot be empty.";
            return SwString();
        }
        if (swDirPlatform().isDirectory(relativePath) || relativePath.startsWith("/") || relativePath.toStdString().find(':') != std::string::npos) {
            return relativePath;
        }
        return swDirPlatform().absolutePath(m_path + relativePath);
    }

    /**
     * @brief Performs the `mkdir` operation.
     * @param path Path used by the operation.
     * @return The requested mkdir.
     */
    static bool mkdir(const SwString& path) {
        return swDirPlatform().mkdir(path);
    }

    /**
     * @brief Removes the specified recursively.
     * @param path Path used by the operation.
     * @return The requested recursively.
     */
    static bool removeRecursively(const SwString& path) {
        return swDirPlatform().removeRecursively(path);
    }

    /**
     * @brief Returns the current recursively.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool removeRecursively() {
        return swDirPlatform().removeRecursively(m_path);
    }

    /**
     * @brief Performs the `copy` operation.
     * @param sourcePath Value passed to the method.
     * @param destinationPath Value passed to the method.
     * @return The requested copy.
     */
    static bool copy(const SwString& sourcePath, const SwString& destinationPath) {
        return swDirPlatform().copyDirectory(sourcePath, destinationPath);
    }

    /**
     * @brief Performs the `findFiles` operation.
     * @param filter Value passed to the method.
     * @return The requested find Files.
     */
    SwStringList findFiles(const SwString& filter) const {
        return swDirPlatform().findFiles(m_path, filter);
    }

    /**
     * @brief Returns the current current Path.
     * @return The current current Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwString currentPath() {
        return swDirPlatform().currentPath();
    }

    /**
     * @brief Returns whether the provided path is absolute on the active platform.
     * @param path Path used by the operation.
     * @return `true` when the path is absolute; otherwise `false`.
     */
    static bool isAbsolutePath(const SwString& path) {
        if (path.isEmpty()) {
            return false;
        }
        if (path.startsWith("/") || path.startsWith("\\")) {
            return true;
        }
        return path.size() > 1 && path[1] == ':';
    }

    /**
     * @brief Returns the current dir Name.
     * @return The current dir Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString dirName() const {
        if (m_path.isEmpty()) {
            swCError(kSwLogCategory_SwDir) << "Path is empty, cannot retrieve directory name.";
            return SwString();
        }
        SwString pathCopy = m_path;
        pathCopy.replace("\\", "/");
        return pathCopy.split("/").last();
    }

private:
    SwString m_path;

    static bool createPathTree(const SwString& rawPath) {
        if (rawPath.isEmpty()) {
            return false;
        }
#ifdef _WIN32
        // Windows path creation is delegated to the platform implementation
        // because it already handles long-path (\\?\) and UNC prefixes.
        if (swDirPlatform().mkdir(rawPath)) {
            return true;
        }
        return swDirPlatform().isDirectory(normalizePath(rawPath));
#else
        SwString normalized = rawPath;
        normalized.replace("\\", "/");
        while (normalized.endsWith("/") && normalized.size() > 1) {
            normalized.chop(1);
        }
        if (normalized.isEmpty()) {
            return false;
        }
        SwList<SwString> parts = normalized.split('/');
        SwString current;
#ifdef _WIN32
        if (parts.size() > 0 && parts[0].size() > 1 && parts[0][1] == ':') {
            current = parts[0];
            parts.removeAt(0);
        }
#endif
        if (current.isEmpty() && normalized.startsWith("/")) {
            current = "/";
        }
        for (int i = 0; i < parts.size(); ++i) {
            SwString part = parts[i];
            if (part.isEmpty()) {
                continue;
            }
            if (!current.isEmpty() && !current.endsWith("/") && !current.endsWith("\\")) {
                current += "/";
            }
            current += part;
            SwString native = normalizePath(current);
            if (!swDirPlatform().isDirectory(native)) {
                if (!swDirPlatform().mkdir(native)) {
                    return false;
                }
            }
        }
        return true;
#endif
    }
};
