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

#include "SwString.h"
#include "SwList.h"
#include "SwDateTime.h"
#include "SwStandardLocationDefs.h"
#include "Sw.h"

class SwFilePlatform {
public:
    virtual ~SwFilePlatform() = default;
    virtual bool isFile(const SwString& path) const = 0;
    virtual bool copy(const SwString& source, const SwString& destination, bool overwrite) = 0;
    virtual bool getFileMetadata(const SwString& path, SwDateTime& creationTime, SwDateTime& lastAccessTime, SwDateTime& lastWriteTime) = 0;
    virtual bool setCreationTime(const SwString& path, SwDateTime creationTime) = 0;
    virtual bool setLastWriteDate(const SwString& path, SwDateTime lastWriteTime) = 0;
    virtual bool setLastAccessDate(const SwString& path, SwDateTime lastAccessTime) = 0;
    virtual bool setAllDates(const SwString& path, SwDateTime creationTime, SwDateTime lastAccessTime, SwDateTime lastWriteTime) = 0;
    virtual bool writeMetadata(const SwString& path, const SwString& key, const SwString& value) = 0;
    virtual SwString readMetadata(const SwString& path, const SwString& key) = 0;
};

SwFilePlatform& swFilePlatform();

class SwDirPlatform {
public:
    virtual ~SwDirPlatform() = default;
    virtual bool exists(const SwString& path) const = 0;
    virtual bool isDirectory(const SwString& path) const = 0;
    virtual SwString normalizePath(const SwString& path) const = 0;
    virtual SwString pathSeparator() const = 0;
    virtual SwStringList entryList(const SwString& path, EntryTypes flags) const = 0;
    virtual SwStringList entryList(const SwString& path, const SwStringList& filters, EntryTypes flags) const = 0;
    virtual SwStringList findFiles(const SwString& path, const SwString& filter) const = 0;
    virtual SwString absolutePath(const SwString& path) const = 0;
    virtual SwString currentPath() const = 0;
    virtual bool mkdir(const SwString& path) const = 0;
    virtual bool removeRecursively(const SwString& path) const = 0;
    virtual bool copyDirectory(const SwString& sourcePath, const SwString& destinationPath) const = 0;
};

SwDirPlatform& swDirPlatform();

class SwFileInfoPlatform {
public:
    virtual ~SwFileInfoPlatform() = default;
    virtual bool exists(const std::string& path) const = 0;
    virtual bool isFile(const std::string& path) const = 0;
    virtual bool isDir(const std::string& path) const = 0;
    virtual std::string absoluteFilePath(const std::string& path) const = 0;
    virtual size_t size(const std::string& path) const = 0;
    virtual void normalizePath(std::string& path) const = 0;
};

SwFileInfoPlatform& swFileInfoPlatform();

class SwStandardLocationProvider {
public:
    virtual ~SwStandardLocationProvider() = default;
    virtual SwString standardLocation(SwStandardLocationId type) const = 0;
    virtual SwString convertPath(const SwString& path, SwStandardPathType type) const = 0;
};

const SwStandardLocationProvider& swStandardLocationProvider();
