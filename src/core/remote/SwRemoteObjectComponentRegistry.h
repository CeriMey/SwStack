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

#include "SwRemoteObject.h"
#include "SwString.h"

#include <map>
#include <mutex>
#include <string>

class SwRemoteObjectComponentRegistry {
 public:
    typedef SwRemoteObject* (*CreateFn)(const SwString& sysName,
                                        const SwString& nameSpace,
                                        const SwString& objectName,
                                        SwObject* parent);

    typedef void (*DestroyFn)(SwRemoteObject* instance);

    struct Entry {
        SwString typeName;
        CreateFn create{nullptr};
        DestroyFn destroy{nullptr};
        SwString pluginPath;
    };

    bool registerComponent(const SwString& typeName,
                           CreateFn create,
                           DestroyFn destroy,
                           const SwString& pluginPath = SwString()) {
        if (typeName.isEmpty() || !create) return false;

        std::lock_guard<std::mutex> lk(mutex_);
        const std::string key = typeName.toStdString();
        std::map<std::string, Entry>::iterator it = entries_.find(key);
        if (it != entries_.end()) {
            // Idempotent if the same creator is re-registered.
            if (it->second.create == create) {
                if (!pluginPath.isEmpty()) it->second.pluginPath = pluginPath;
                if (!it->second.destroy && destroy) it->second.destroy = destroy;
                return true;
            }
            return false;
        }

        Entry e;
        e.typeName = typeName;
        e.create = create;
        e.destroy = destroy;
        e.pluginPath = pluginPath;
        entries_.insert(std::make_pair(key, e));
        return true;
    }

    bool contains(const SwString& typeName) const {
        std::lock_guard<std::mutex> lk(mutex_);
        return entries_.find(typeName.toStdString()) != entries_.end();
    }

    Entry entry(const SwString& typeName) const {
        std::lock_guard<std::mutex> lk(mutex_);
        const std::string key = typeName.toStdString();
        std::map<std::string, Entry>::const_iterator it = entries_.find(key);
        if (it == entries_.end()) return Entry{};
        return it->second;
    }

    SwStringList types() const {
        SwStringList out;
        std::lock_guard<std::mutex> lk(mutex_);
        out.reserve(entries_.size());
        for (std::map<std::string, Entry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
            out.append(SwString(it->first));
        }
        return out;
    }

    void setPluginPath(const SwString& typeName, const SwString& pluginPath) {
        if (typeName.isEmpty() || pluginPath.isEmpty()) return;
        std::lock_guard<std::mutex> lk(mutex_);
        const std::string key = typeName.toStdString();
        std::map<std::string, Entry>::iterator it = entries_.find(key);
        if (it == entries_.end()) return;
        it->second.pluginPath = pluginPath;
    }

 private:
    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;
};
