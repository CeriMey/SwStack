#ifndef SWSETTINGS_H
#define SWSETTINGS_H
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

#include "SwAny.h"
#include "SwString.h"
#include "SwStandardPaths.h"
#include "SwJsonValue.h"
#include "SwJsonDocument.h"
#include "SwJsonObject.h"
#include "SwJsonArray.h"
#include "SwFile.h"
#include "SwFileInfo.h"

#include <map>
#include <vector>

class SwSettings {
public:
    SwSettings(const SwString& organization = SwString(),
               const SwString& application = SwString())
        : organization_(organization),
          application_(application),
          dirty_(false) {
        initializePaths();
        loadFromDisk();
    }

    ~SwSettings() {
        sync();
    }

    void setValue(const SwString& key, const SwAny& value) {
        if (key.isEmpty()) {
            return;
        }
        SwString fullKey = canonicalKey(key);
        values_[fullKey] = anyToJson(value);
        dirty_ = true;
    }

    SwAny value(const SwString& key, const SwAny& defaultValue = SwAny()) const {
        if (key.isEmpty()) {
            return defaultValue;
        }
        SwString fullKey = canonicalKey(key);
        auto it = values_.find(fullKey);
        if (it == values_.end()) {
            return defaultValue;
        }
        return jsonToAny(it->second);
    }

    bool contains(const SwString& key) const {
        if (key.isEmpty()) {
            return false;
        }
        return values_.find(canonicalKey(key)) != values_.end();
    }

    void remove(const SwString& key) {
        if (key.isEmpty()) {
            return;
        }
        SwString fullKey = canonicalKey(key);
        std::vector<SwString> toErase;
        for (const auto& entry : values_) {
            if (entry.first == fullKey || entry.first.startsWith(fullKey + "/")) {
                toErase.push_back(entry.first);
            }
        }
        for (const SwString& k : toErase) {
            values_.erase(k);
        }
        if (!toErase.empty()) {
            dirty_ = true;
        }
    }

    void clear() {
        if (values_.empty()) {
            return;
        }
        values_.clear();
        dirty_ = true;
    }

    void sync() {
        if (!dirty_) {
            return;
        }
        saveToDisk();
    }

    bool isDirty() const {
        return dirty_;
    }

    void beginGroup(const SwString& prefix) {
        SwString clean = prefix.trimmed();
        if (!clean.isEmpty()) {
            groupStack_.push_back(clean);
        }
    }

    void endGroup() {
        if (!groupStack_.empty()) {
            groupStack_.pop_back();
        }
    }

    SwString group() const {
        SwString result;
        for (size_t i = 0; i < groupStack_.size(); ++i) {
            if (!result.isEmpty()) {
                result += "/";
            }
            result += groupStack_[i];
        }
        return result;
    }

private:
    using ValueMap = std::map<SwString, SwJsonValue>;

    void initializePaths() {
        SwString base = SwStandardPaths::writableLocation(SwStandardPaths::AppConfigLocation);
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::AppDataLocation);
        }
        if (base.isEmpty()) {
            base = SwStandardPaths::writableLocation(SwStandardPaths::HomeLocation);
        }
        SwString organizationName = sanitizeName(organization_.isEmpty() ? SwString("SwCore") : organization_);
        SwString applicationName = sanitizeName(application_.isEmpty() ? SwString("Application") : application_);
        SwString dir = joinPath(joinPath(base, organizationName), applicationName);
        ensureDirectory(dir);
        filePath_ = joinPath(dir, "settings.json");
    }

    SwString canonicalKey(const SwString& key) const {
        SwString trimmed = key.trimmed();
        SwString prefix = group();
        if (prefix.isEmpty()) {
            return trimmed;
        }
        if (trimmed.isEmpty()) {
            return prefix;
        }
        return prefix + "/" + trimmed;
    }

    void loadFromDisk() {
        values_.clear();
        SwFileInfo info(filePath_.toStdString());
        if (!info.exists() || info.isDir()) {
            dirty_ = false;
            return;
        }
        SwFile file(filePath_);
        if (!file.open(SwFile::Read)) {
            dirty_ = false;
            return;
        }
        SwString content = file.readAll();
        file.close();
        if (content.isEmpty()) {
            dirty_ = false;
            return;
        }
        SwJsonDocument doc = SwJsonDocument::fromJson(content.toStdString());
        SwJsonValue root;
        if (doc.isObject()) {
            root = SwJsonValue(doc.object());
        } else {
            dirty_ = false;
            return;
        }
        flattenJson(SwString(), root, values_);
        dirty_ = false;
    }

    void saveToDisk() {
        SwJsonObject root;
        for (const auto& entry : values_) {
            buildJsonTree(entry.first, entry.second, root);
        }
        SwJsonDocument doc(root);
        SwString json = doc.toJson(SwJsonDocument::JsonFormat::Compact);
        SwString directory = parentDirectory(filePath_);
        ensureDirectory(directory);

        SwFile file(filePath_);
        if (!file.open(SwFile::Write)) {
            return;
        }
        file.write(json);
        file.close();
        dirty_ = false;
    }

    static SwJsonValue anyToJson(const SwAny& value) {
        if (value.typeName().empty()) {
            return SwJsonValue();
        }
        const std::string& type = value.typeName();
        if (type == typeid(bool).name()) {
            return SwJsonValue(value.toBool());
        }
        if (type == typeid(int).name()) {
            return SwJsonValue(value.toInt());
        }
        if (type == typeid(double).name()) {
            return SwJsonValue(value.toDouble());
        }
        if (type == typeid(float).name()) {
            return SwJsonValue(static_cast<double>(value.toDouble()));
        }
        if (type == typeid(SwString).name()) {
            return SwJsonValue(value.get<SwString>().toStdString());
        }
        if (type == typeid(std::string).name()) {
            return SwJsonValue(value.get<std::string>());
        }
        if (type == typeid(SwJsonObject).name()) {
            return SwJsonValue(value.get<SwJsonObject>());
        }
        if (type == typeid(SwJsonArray).name()) {
            return SwJsonValue(value.get<SwJsonArray>());
        }
        if (value.canConvert<SwJsonValue>()) {
            return value.convert<SwJsonValue>().get<SwJsonValue>();
        }
        if (value.canConvert<SwString>()) {
            return SwJsonValue(value.convert<SwString>().get<SwString>().toStdString());
        }
        if (value.canConvert<std::string>()) {
            return SwJsonValue(value.convert<std::string>().get<std::string>());
        }
        return SwJsonValue();
    }

    static SwAny jsonToAny(const SwJsonValue& value) {
        if (value.isNull()) {
            return SwAny();
        }
        if (value.isBool()) {
            return SwAny::from(value.toBool());
        }
        if (value.isString()) {
            return SwAny::from(SwString(value.toString()));
        }
        if (value.isDouble()) {
            return SwAny::from(value.toDouble());
        }
        if (value.isObject()) {
            return SwAny::from(*value.toObject());
        }
        if (value.isArray()) {
            return SwAny::from(*value.toArray());
        }
        return SwAny::from(value.toString());
    }

    static void buildJsonTree(const SwString& key, const SwJsonValue& value, SwJsonObject& root) {
        SwString normalized = key;
        normalized.replace("\\", "/");
        SwList<SwString> parts = normalized.split('/');
        SwJsonObject* current = &root;
        for (int i = 0; i < parts.size(); ++i) {
            SwString part = parts[i];
            if (part.isEmpty()) {
                continue;
            }
            if (i == parts.size() - 1) {
                (*current)[part] = value;
            } else {
                SwJsonValue& node = (*current)[part];
                if (!node.isObject()) {
                    node = SwJsonObject();
                }
                current = node.toObject().get();
            }
        }
    }

    static void flattenJson(const SwString& prefix, const SwJsonValue& value, ValueMap& out) {
        if (value.isObject()) {
            auto object = value.toObject();
            auto data = object->data();
            for (const auto& entry : data) {
                SwString key(entry.first);
                SwString fullKey = prefix.isEmpty() ? key : prefix + "/" + key;
                flattenJson(fullKey, entry.second, out);
            }
        } else {
            out[prefix] = value;
        }
    }

    static SwString sanitizeName(const SwString& input) {
        if (input.isEmpty()) {
            return SwString();
        }
        SwString cleaned = input.trimmed();
        cleaned.replace("\\", "_");
        cleaned.replace("/", "_");
        return cleaned;
    }

    static SwString parentDirectory(const SwString& filePath) {
        SwString normalized = filePath;
        normalized.replace("\\", "/");
        while (normalized.endsWith("/") && normalized.size() > 1) {
            normalized.chop(1);
        }
        SwList<SwString> parts = normalized.split('/');
        if (parts.size() <= 1) {
            return SwString();
        }
        SwString result;
        for (int i = 0; i < parts.size() - 1; ++i) {
            SwString part = parts[i];
            if (part.isEmpty()) {
                continue;
            }
            if (!result.isEmpty()) {
                result += "/";
            }
            result += part;
        }
        return result;
    }

    static bool ensureDirectory(const SwString& path) {
        if (path.isEmpty()) {
            return false;
        }
        SwString normalized = path;
        normalized.replace("\\", "/");
        SwString nativeInitial = platformPath(normalized);
        if (swDirPlatform().isDirectory(nativeInitial)) {
            return true;
        }
        SwList<SwString> parts = normalized.split('/');
        SwString current;
        int startIndex = 0;
        if (normalized.startsWith(SwString("/"))) {
            current = SwString("/");
        }
#ifdef _WIN32
        if (normalized.size() >= 2 && normalized[1] == ':') {
            const SwString drive = normalized.left(2);
            current = drive;
            while (startIndex < parts.size() && parts[startIndex].isEmpty()) {
                ++startIndex;
            }
            if (startIndex < parts.size() && parts[startIndex] == drive) {
                ++startIndex;
            }
        }
#endif
        for (int i = startIndex; i < parts.size(); ++i) {
            SwString part = parts[i];
            if (part.isEmpty()) {
                continue;
            }
            if (!current.isEmpty() && !current.endsWith("/")) {
                current += "/";
            }
            current += part;
            SwString native = platformPath(current);
            if (!swDirPlatform().isDirectory(native)) {
                if (!swDirPlatform().mkdir(native)) {
                    return false;
                }
            }
        }
        return true;
    }

    static SwString platformPath(const SwString& path) {
#ifdef _WIN32
        return SwStandardLocation::convertPath(path, SwStandardPathType::Windows);
#else
        return SwStandardLocation::convertPath(path, SwStandardPathType::Unix);
#endif
    }

    static SwString joinPath(const SwString& base, const SwString& child) {
        if (base.isEmpty()) {
            return child;
        }
        if (child.isEmpty()) {
            return base;
        }
        SwString result(base);
        if (!result.endsWith("/") && !result.endsWith("\\")) {
            result += "/";
        }
        result += child;
        return result;
    }

    SwString organization_;
    SwString application_;
    SwString filePath_;
    ValueMap values_;
    std::vector<SwString> groupStack_;
    bool dirty_;
};

#endif // SWSETTINGS_H
