#pragma once
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
    explicit SwDir(const SwString& path = ".") {
        setPath(path);
    }

    ~SwDir() = default;

    bool exists() const {
        return swDirPlatform().exists(m_path);
    }

    SwString path() const {
        return m_path;
    }

    static bool exists(const SwString& path) {
        return swDirPlatform().exists(path);
    }

    static SwString normalizePath(const SwString& path) {
        return swDirPlatform().normalizePath(path);
    }

    static bool mkpathAbsolute(const SwString& path, bool normalizeInput = true) {
        SwString target = path;
        if (normalizeInput) {
            target = normalizePath(path);
        }
        return createPathTree(target);
    }

    bool mkpath(const SwString& subPath) const {
        SwString target = absoluteFilePath(subPath);
        return createPathTree(target);
    }

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

    SwStringList entryList(EntryTypes flags) const {
        return swDirPlatform().entryList(m_path, flags);
    }

    SwStringList entryList(const SwStringList& filters, EntryTypes flags = EntryType::AllEntries) const {
        return swDirPlatform().entryList(m_path, filters, flags);
    }

    SwString absolutePath() const {
        return swDirPlatform().absolutePath(m_path);
    }

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

    static bool mkdir(const SwString& path) {
        return swDirPlatform().mkdir(path);
    }

    static bool removeRecursively(const SwString& path) {
        return swDirPlatform().removeRecursively(path);
    }

    bool removeRecursively() {
        return swDirPlatform().removeRecursively(m_path);
    }

    static bool copy(const SwString& sourcePath, const SwString& destinationPath) {
        return swDirPlatform().copyDirectory(sourcePath, destinationPath);
    }

    SwStringList findFiles(const SwString& filter) const {
        return swDirPlatform().findFiles(m_path, filter);
    }

    static SwString currentPath() {
        return swDirPlatform().currentPath();
    }

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

    static bool isAbsolutePath(const SwString& path) {
        if (path.isEmpty()) {
            return false;
        }
        if (path.startsWith("/") || path.startsWith("\\")) {
            return true;
        }
        return path.size() > 1 && path[1] == ':';
    }

    static bool createPathTree(const SwString& rawPath) {
        if (rawPath.isEmpty()) {
            return false;
        }
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
    }
};
