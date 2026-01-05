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

#include "platform/SwPlatformSelector.h"

class SwFileInfo {
public:
    explicit SwFileInfo(const std::string& filePath = "") : m_filePath(filePath) {
        swFileInfoPlatform().normalizePath(m_filePath);
    }

    ~SwFileInfo() = default;

    bool exists() const {
        return swFileInfoPlatform().exists(m_filePath);
    }

    bool isFile() const {
        return swFileInfoPlatform().isFile(m_filePath);
    }

    bool isDir() const {
        return swFileInfoPlatform().isDir(m_filePath);
    }

    std::string fileName() const {
        auto pos = m_filePath.find_last_of("/\\");
        return (pos == std::string::npos) ? m_filePath : m_filePath.substr(pos + 1);
    }

    std::string baseName() const {
        auto name = fileName();
        auto pos = name.find_last_of('.');
        return (pos == std::string::npos) ? name : name.substr(0, pos);
    }

    std::string suffix() const {
        auto name = fileName();
        auto pos = name.find_last_of('.');
        return (pos == std::string::npos) ? "" : name.substr(pos + 1);
    }

    std::string absoluteFilePath() const {
        return swFileInfoPlatform().absoluteFilePath(m_filePath);
    }

    size_t size() const {
        return swFileInfoPlatform().size(m_filePath);
    }

private:
    std::string m_filePath;
};
