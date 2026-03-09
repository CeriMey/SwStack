#pragma once

/**
 * @file
 * @ingroup core_fs
 * @brief Declares `SwFileInfo`, a lightweight filesystem metadata facade.
 *
 * The type normalizes a path once and then forwards existence, type, name, suffix,
 * size, and absolute-path queries to the active platform backend. It is intended
 * for cheap inspection of filesystem entries, not for opening or streaming files.
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

#include "platform/SwPlatformSelector.h"

class SwFileInfo {
public:
    /**
     * @brief Constructs a `SwFileInfo` instance.
     * @param filePath Path of the target file.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwFileInfo(const std::string& filePath = "") : m_filePath(filePath) {
        swFileInfoPlatform().normalizePath(m_filePath);
    }

    /**
     * @brief Destroys the `SwFileInfo` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwFileInfo() = default;

    /**
     * @brief Returns the current exists.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool exists() const {
        return swFileInfoPlatform().exists(m_filePath);
    }

    /**
     * @brief Returns whether the object reports file.
     * @return `true` when the object reports file; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isFile() const {
        return swFileInfoPlatform().isFile(m_filePath);
    }

    /**
     * @brief Returns whether the object reports dir.
     * @return `true` when the object reports dir; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isDir() const {
        return swFileInfoPlatform().isDir(m_filePath);
    }

    /**
     * @brief Returns the current file Name.
     * @return The current file Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string fileName() const {
        auto pos = m_filePath.find_last_of("/\\");
        return (pos == std::string::npos) ? m_filePath : m_filePath.substr(pos + 1);
    }

    /**
     * @brief Returns the current base Name.
     * @return The current base Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string baseName() const {
        auto name = fileName();
        auto pos = name.find_last_of('.');
        return (pos == std::string::npos) ? name : name.substr(0, pos);
    }

    /**
     * @brief Returns the current suffix.
     * @return The current suffix.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string suffix() const {
        auto name = fileName();
        auto pos = name.find_last_of('.');
        return (pos == std::string::npos) ? "" : name.substr(pos + 1);
    }

    /**
     * @brief Returns the current absolute File Path.
     * @return The current absolute File Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string absoluteFilePath() const {
        return swFileInfoPlatform().absoluteFilePath(m_filePath);
    }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t size() const {
        return swFileInfoPlatform().size(m_filePath);
    }

private:
    std::string m_filePath;
};
