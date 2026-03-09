#pragma once

/**
 * @file src/core/platform/SwPlatform.h
 * @ingroup core_platform
 * @brief Declares the public interface exposed by SwPlatform in the CoreSw core platform
 * abstraction layer.
 *
 * This header belongs to the CoreSw core platform abstraction layer. It encapsulates low-level
 * filesystem and standard-location services that differ across supported operating systems.
 *
 * Within that layer, this file focuses on the platform interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwFilePlatform, SwDirPlatform, SwFileInfoPlatform, and
 * SwStandardLocationProvider.
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

#include "SwString.h"
#include "SwList.h"
#include "SwDateTime.h"
#include "SwStandardLocationDefs.h"
#include "Sw.h"

class SwFilePlatform {
public:
    /**
     * @brief Destroys the `SwFilePlatform` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwFilePlatform() = default;
    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return The requested file.
     *
     * @details This query does not modify the object state.
     */
    virtual bool isFile(const SwString& path) const = 0;
    /**
     * @brief Performs the `copy` operation.
     * @param source Value passed to the method.
     * @param destination Value passed to the method.
     * @param overwrite Value passed to the method.
     * @return The requested copy.
     */
    virtual bool copy(const SwString& source, const SwString& destination, bool overwrite) = 0;
    /**
     * @brief Performs the `getFileMetadata` operation.
     * @param path Path used by the operation.
     * @param creationTime Value passed to the method.
     * @param lastAccessTime Value passed to the method.
     * @param lastWriteTime Value passed to the method.
     * @return The requested file Metadata.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual bool getFileMetadata(const SwString& path, SwDateTime& creationTime, SwDateTime& lastAccessTime, SwDateTime& lastWriteTime) = 0;
    /**
     * @brief Sets the creation Time.
     * @param path Path used by the operation.
     * @param creationTime Value passed to the method.
     * @return The requested creation Time.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual bool setCreationTime(const SwString& path, SwDateTime creationTime) = 0;
    /**
     * @brief Sets the last Write Date.
     * @param path Path used by the operation.
     * @param lastWriteTime Value passed to the method.
     * @return The requested last Write Date.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual bool setLastWriteDate(const SwString& path, SwDateTime lastWriteTime) = 0;
    /**
     * @brief Sets the last Access Date.
     * @param path Path used by the operation.
     * @param lastAccessTime Value passed to the method.
     * @return The requested last Access Date.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual bool setLastAccessDate(const SwString& path, SwDateTime lastAccessTime) = 0;
    /**
     * @brief Sets the all Dates.
     * @param path Path used by the operation.
     * @param creationTime Value passed to the method.
     * @param lastAccessTime Value passed to the method.
     * @param lastWriteTime Value passed to the method.
     * @return The requested all Dates.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual bool setAllDates(const SwString& path, SwDateTime creationTime, SwDateTime lastAccessTime, SwDateTime lastWriteTime) = 0;
    /**
     * @brief Performs the `writeMetadata` operation on the associated resource.
     * @param path Path used by the operation.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     * @return The requested metadata.
     */
    virtual bool writeMetadata(const SwString& path, const SwString& key, const SwString& value) = 0;
    /**
     * @brief Performs the `readMetadata` operation on the associated resource.
     * @param path Path used by the operation.
     * @param key Value passed to the method.
     * @return The resulting metadata.
     */
    virtual SwString readMetadata(const SwString& path, const SwString& key) = 0;
};

SwFilePlatform& swFilePlatform();

class SwDirPlatform {
public:
    /**
     * @brief Destroys the `SwDirPlatform` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwDirPlatform() = default;
    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return The requested exists.
     */
    virtual bool exists(const SwString& path) const = 0;
    /**
     * @brief Returns whether the object reports directory.
     * @param path Path used by the operation.
     * @return The requested directory.
     *
     * @details This query does not modify the object state.
     */
    virtual bool isDirectory(const SwString& path) const = 0;
    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     * @return The requested normalize Path.
     */
    virtual SwString normalizePath(const SwString& path) const = 0;
    /**
     * @brief Returns the current path Separator.
     * @return The current path Separator.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwString pathSeparator() const = 0;
    /**
     * @brief Performs the `entryList` operation.
     * @param path Path used by the operation.
     * @param flags Flags that refine the operation.
     * @return The requested entry List.
     */
    virtual SwStringList entryList(const SwString& path, EntryTypes flags) const = 0;
    /**
     * @brief Performs the `entryList` operation.
     * @param path Path used by the operation.
     * @param filters Value passed to the method.
     * @param flags Flags that refine the operation.
     * @return The requested entry List.
     */
    virtual SwStringList entryList(const SwString& path, const SwStringList& filters, EntryTypes flags) const = 0;
    /**
     * @brief Performs the `findFiles` operation.
     * @param path Path used by the operation.
     * @param filter Value passed to the method.
     * @return The requested find Files.
     */
    virtual SwStringList findFiles(const SwString& path, const SwString& filter) const = 0;
    /**
     * @brief Performs the `absolutePath` operation.
     * @param path Path used by the operation.
     * @return The requested absolute Path.
     */
    virtual SwString absolutePath(const SwString& path) const = 0;
    /**
     * @brief Returns the current current Path.
     * @return The current current Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwString currentPath() const = 0;
    /**
     * @brief Performs the `mkdir` operation.
     * @param path Path used by the operation.
     * @return The requested mkdir.
     */
    virtual bool mkdir(const SwString& path) const = 0;
    /**
     * @brief Removes the specified recursively.
     * @param path Path used by the operation.
     * @return The requested recursively.
     */
    virtual bool removeRecursively(const SwString& path) const = 0;
    /**
     * @brief Performs the `copyDirectory` operation.
     * @param sourcePath Value passed to the method.
     * @param destinationPath Value passed to the method.
     * @return The requested copy Directory.
     */
    virtual bool copyDirectory(const SwString& sourcePath, const SwString& destinationPath) const = 0;
};

SwDirPlatform& swDirPlatform();

class SwFileInfoPlatform {
public:
    /**
     * @brief Destroys the `SwFileInfoPlatform` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwFileInfoPlatform() = default;
    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return The requested exists.
     */
    virtual bool exists(const std::string& path) const = 0;
    /**
     * @brief Returns whether the object reports file.
     * @param path Path used by the operation.
     * @return The requested file.
     *
     * @details This query does not modify the object state.
     */
    virtual bool isFile(const std::string& path) const = 0;
    /**
     * @brief Returns whether the object reports dir.
     * @param path Path used by the operation.
     * @return The requested dir.
     *
     * @details This query does not modify the object state.
     */
    virtual bool isDir(const std::string& path) const = 0;
    /**
     * @brief Performs the `absoluteFilePath` operation.
     * @param path Path used by the operation.
     * @return The requested absolute File Path.
     */
    virtual std::string absoluteFilePath(const std::string& path) const = 0;
    /**
     * @brief Performs the `size` operation.
     * @param path Path used by the operation.
     * @return The current size value.
     */
    virtual size_t size(const std::string& path) const = 0;
    /**
     * @brief Performs the `normalizePath` operation.
     * @param path Path used by the operation.
     * @return The requested normalize Path.
     */
    virtual void normalizePath(std::string& path) const = 0;
};

SwFileInfoPlatform& swFileInfoPlatform();

class SwStandardLocationProvider {
public:
    /**
     * @brief Destroys the `SwStandardLocationProvider` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwStandardLocationProvider() = default;
    /**
     * @brief Performs the `standardLocation` operation.
     * @param type Value passed to the method.
     * @return The requested standard Location.
     */
    virtual SwString standardLocation(SwStandardLocationId type) const = 0;
    /**
     * @brief Performs the `convertPath` operation.
     * @param path Path used by the operation.
     * @param type Value passed to the method.
     * @return The requested convert Path.
     */
    virtual SwString convertPath(const SwString& path, SwStandardPathType type) const = 0;
};

const SwStandardLocationProvider& swStandardLocationProvider();
