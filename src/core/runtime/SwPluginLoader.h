#pragma once

/**
 * @file src/core/runtime/SwPluginLoader.h
 * @ingroup core_runtime
 * @brief Header-only generic plugin loader modeled after Qt's QPluginLoader.
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

#include "SwJsonDocument.h"
#include "SwLibrary.h"
#include "SwList.h"
#include "SwObject.h"
#include "SwString.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

class SwStaticPlugin;

namespace sw {
namespace plugin {

inline const SwString& metaDataSymbolV1() {
    static const SwString k("swPluginMetaDataV1");
    return k;
}

inline const SwString& createInstanceSymbolV1() {
    static const SwString k("swCreatePluginInstanceV1");
    return k;
}

inline const SwString& destroyInstanceSymbolV1() {
    static const SwString k("swDestroyPluginInstanceV1");
    return k;
}

typedef const char* (*MetaDataFnV1)();
typedef SwObject* (*CreateInstanceFnV1)(SwObject* parent);
typedef void (*DestroyInstanceFnV1)(SwObject* instance);

struct StaticPluginState {
    SwString fileName;
    MetaDataFnV1 metaDataFn{nullptr};
    CreateInstanceFnV1 createFn{nullptr};
    DestroyInstanceFnV1 destroyFn{nullptr};
    mutable std::mutex mutex;
    mutable SwJsonObject metaData;
    mutable bool metaDataInitialized{false};
    mutable SwObject* instance{nullptr};
};

inline SwJsonObject parseMetaDataJson_(const char* jsonUtf8, SwString* errOut) {
    if (errOut) *errOut = SwString();
    if (!jsonUtf8 || !jsonUtf8[0]) {
        return SwJsonObject();
    }

    SwJsonDocument doc;
    SwString err;
    if (!doc.loadFromJson(SwString(jsonUtf8), err)) {
        if (errOut) *errOut = SwString("invalid plugin metadata JSON: ") + err;
        return SwJsonObject();
    }
    if (!doc.isObject()) {
        if (errOut) *errOut = SwString("plugin metadata must be a JSON object");
        return SwJsonObject();
    }
    return doc.object();
}

inline SwJsonObject staticPluginMetaData_(const std::shared_ptr<StaticPluginState>& state, SwString* errOut) {
    if (errOut) *errOut = SwString();
    if (!state) {
        if (errOut) *errOut = SwString("invalid static plugin");
        return SwJsonObject();
    }

    std::lock_guard<std::mutex> lk(state->mutex);
    if (state->metaDataInitialized) {
        return state->metaData;
    }
    state->metaData = parseMetaDataJson_(state->metaDataFn ? state->metaDataFn() : nullptr, errOut);
    state->metaDataInitialized = true;
    return state->metaData;
}

inline SwObject* staticPluginInstance_(const std::shared_ptr<StaticPluginState>& state, SwString* errOut) {
    if (errOut) *errOut = SwString();
    if (!state) {
        if (errOut) *errOut = SwString("invalid static plugin");
        return nullptr;
    }

    std::lock_guard<std::mutex> lk(state->mutex);
    if (state->instance && SwObject::isLive(state->instance)) {
        return state->instance;
    }
    state->instance = nullptr;
    if (!state->createFn) {
        if (errOut) *errOut = SwString("static plugin missing factory symbol");
        return nullptr;
    }
    state->instance = state->createFn(nullptr);
    if (!state->instance && errOut) {
        *errOut = SwString("static plugin factory returned null");
    }
    return state->instance;
}

inline std::mutex& staticPluginRegistryMutex_() {
    static std::mutex m;
    return m;
}

inline SwList<std::shared_ptr<StaticPluginState> >& staticPluginRegistry_() {
    static SwList<std::shared_ptr<StaticPluginState> > reg;
    return reg;
}

inline bool registerStaticPlugin(const SwString& fileName,
                                 MetaDataFnV1 metaDataFn,
                                 CreateInstanceFnV1 createFn,
                                 DestroyInstanceFnV1 destroyFn) {
    if (!createFn) return false;

    std::lock_guard<std::mutex> lk(staticPluginRegistryMutex_());
    SwList<std::shared_ptr<StaticPluginState> >& reg = staticPluginRegistry_();
    for (size_t i = 0; i < reg.size(); ++i) {
        const std::shared_ptr<StaticPluginState>& existing = reg[i];
        if (!existing) continue;
        if (existing->fileName == fileName && existing->createFn == createFn) {
            if (!existing->metaDataFn && metaDataFn) existing->metaDataFn = metaDataFn;
            if (!existing->destroyFn && destroyFn) existing->destroyFn = destroyFn;
            return true;
        }
    }

    std::shared_ptr<StaticPluginState> state(new StaticPluginState());
    state->fileName = fileName;
    state->metaDataFn = metaDataFn;
    state->createFn = createFn;
    state->destroyFn = destroyFn;
    reg.append(state);
    return true;
}

inline SwList<std::shared_ptr<StaticPluginState> > staticPluginStates() {
    std::lock_guard<std::mutex> lk(staticPluginRegistryMutex_());
    return staticPluginRegistry_();
}

struct DynamicPluginState {
    mutable std::mutex mutex;
    SwString requestedFileName;
    SwString resolvedFileName;
    SwString errorString;
    SwLibrary library;
    SwJsonObject metaData;
    bool metaDataInitialized{false};
    CreateInstanceFnV1 createFn{nullptr};
    DestroyInstanceFnV1 destroyFn{nullptr};
    SwObject* instance{nullptr};
    unsigned int loadRefCount{0};
};

inline std::mutex& dynamicPluginRegistryMutex_() {
    static std::mutex m;
    return m;
}

inline std::map<std::string, std::shared_ptr<DynamicPluginState> >& dynamicPluginRegistry_() {
    static std::map<std::string, std::shared_ptr<DynamicPluginState> > reg;
    return reg;
}

inline std::shared_ptr<DynamicPluginState> lookupDynamicState_(const SwString& fileName) {
    if (fileName.isEmpty()) return std::shared_ptr<DynamicPluginState>();
    std::lock_guard<std::mutex> lk(dynamicPluginRegistryMutex_());
    std::map<std::string, std::shared_ptr<DynamicPluginState> >& reg = dynamicPluginRegistry_();
    const std::map<std::string, std::shared_ptr<DynamicPluginState> >::const_iterator it =
        reg.find(fileName.toStdString());
    if (it == reg.end()) return std::shared_ptr<DynamicPluginState>();
    return it->second;
}

inline void addDynamicAlias_(const SwString& alias, const std::shared_ptr<DynamicPluginState>& state) {
    if (alias.isEmpty() || !state) return;
    std::lock_guard<std::mutex> lk(dynamicPluginRegistryMutex_());
    dynamicPluginRegistry_()[alias.toStdString()] = state;
}

inline void removeDynamicState_(const std::shared_ptr<DynamicPluginState>& state) {
    if (!state) return;
    std::lock_guard<std::mutex> lk(dynamicPluginRegistryMutex_());
    std::map<std::string, std::shared_ptr<DynamicPluginState> >& reg = dynamicPluginRegistry_();
    for (std::map<std::string, std::shared_ptr<DynamicPluginState> >::iterator it = reg.begin(); it != reg.end();) {
        if (it->second == state) {
            it = reg.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace plugin
} // namespace sw

class SwStaticPlugin {
 public:
    SwStaticPlugin() = default;

    explicit SwStaticPlugin(const std::shared_ptr<sw::plugin::StaticPluginState>& state) : state_(state) {}

    SwJsonObject metaData() const {
        SwString ignored;
        return sw::plugin::staticPluginMetaData_(state_, &ignored);
    }

    SwObject* instance() const {
        SwString ignored;
        return sw::plugin::staticPluginInstance_(state_, &ignored);
    }

    bool isValid() const { return static_cast<bool>(state_); }

 private:
    std::shared_ptr<sw::plugin::StaticPluginState> state_;
};

class SwPluginLoader : public SwObject {
 public:
    enum LoadHint {
        ResolveAllSymbolsHint = 0x01,
        ExportExternalSymbolsHint = 0x02,
        LoadArchiveMemberHint = 0x04,
        PreventUnloadHint = 0x08,
        DeepBindHint = 0x10
    };

    typedef unsigned int LoadHints;

    explicit SwPluginLoader(SwObject* parent = nullptr) : SwObject(parent) {}

    explicit SwPluginLoader(const SwString& fileName, SwObject* parent = nullptr)
        : SwObject(parent), fileName_(fileName) {}

    ~SwPluginLoader() override = default;

    SwPluginLoader(const SwPluginLoader&) = delete;
    SwPluginLoader& operator=(const SwPluginLoader&) = delete;
    SwPluginLoader(SwPluginLoader&&) = delete;
    SwPluginLoader& operator=(SwPluginLoader&&) = delete;

    SwString fileName() const {
        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        if (!state) {
            state = sw::plugin::lookupDynamicState_(fileName_);
        }
        if (state) {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (!state->resolvedFileName.isEmpty()) {
                return state->resolvedFileName;
            }
        }
        return fileName_;
    }

    void setFileName(const SwString& fileName) {
        if (fileName_ == fileName) return;
        if (holdsLoadReference_) {
            lastError_ = SwString("cannot change fileName while plugin is loaded; call unload() first");
            return;
        }
        state_.reset();
        fileName_ = fileName;
        lastError_.clear();
        cachedMetaData_ = SwJsonObject();
        cachedMetaDataInitialized_ = false;
    }

    LoadHints loadHints() const { return loadHints_; }

    void setLoadHints(LoadHints hints) { loadHints_ = hints; }

    bool load() {
        lastError_.clear();
        if (fileName_.isEmpty()) {
            lastError_ = SwString("plugin fileName is empty");
            return false;
        }
        if (holdsLoadReference_ && isLoaded()) {
            return true;
        }

        SwLibrary temp;
        if (!temp.load(fileName_)) {
            lastError_ = temp.lastError();
            return false;
        }

        const SwString resolved = temp.path();
        std::shared_ptr<sw::plugin::DynamicPluginState> state;
        bool ownsState = false;
        {
            std::lock_guard<std::mutex> regLock(sw::plugin::dynamicPluginRegistryMutex_());
            std::map<std::string, std::shared_ptr<sw::plugin::DynamicPluginState> >& reg =
                sw::plugin::dynamicPluginRegistry_();

            if (!resolved.isEmpty()) {
                const std::map<std::string, std::shared_ptr<sw::plugin::DynamicPluginState> >::iterator itResolved =
                    reg.find(resolved.toStdString());
                if (itResolved != reg.end()) {
                    state = itResolved->second;
                }
            }
            if (!state) {
                const std::map<std::string, std::shared_ptr<sw::plugin::DynamicPluginState> >::iterator itRequested =
                    reg.find(fileName_.toStdString());
                if (itRequested != reg.end()) {
                    state = itRequested->second;
                }
            }
            if (!state) {
                state.reset(new sw::plugin::DynamicPluginState());
                ownsState = true;
                reg[fileName_.toStdString()] = state;
                if (!resolved.isEmpty()) {
                    reg[resolved.toStdString()] = state;
                }
            } else {
                reg[fileName_.toStdString()] = state;
                if (!resolved.isEmpty()) {
                    reg[resolved.toStdString()] = state;
                }
            }

            std::lock_guard<std::mutex> stateLock(state->mutex);
            if (ownsState) {
                state->library = std::move(temp);
                state->requestedFileName = fileName_;
                state->resolvedFileName = resolved;
                state->errorString.clear();
                state->metaData = SwJsonObject();
                state->metaDataInitialized = false;
                state->createFn = nullptr;
                state->destroyFn = nullptr;
            } else {
                temp.unload();
                if (!state->library.isLoaded()) {
                    const SwString effectivePath =
                        state->resolvedFileName.isEmpty() ? fileName_ : state->resolvedFileName;
                    if (!state->library.load(effectivePath)) {
                        state->errorString = state->library.lastError();
                        lastError_ = state->errorString;
                        return false;
                    }
                    state->requestedFileName = fileName_;
                    state->resolvedFileName = state->library.path();
                    state->errorString.clear();
                    state->metaData = SwJsonObject();
                    state->metaDataInitialized = false;
                    state->createFn = nullptr;
                    state->destroyFn = nullptr;
                }
            }
            ++state->loadRefCount;
        }

        state_ = state;
        holdsLoadReference_ = true;
        return true;
    }

    bool unload() {
        lastError_.clear();
        if (!holdsLoadReference_ || !state_) {
            lastError_ = SwString("plugin is not loaded by this loader");
            return false;
        }

        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        bool shouldUnload = false;
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (!state->library.isLoaded()) {
                holdsLoadReference_ = false;
                state_.reset();
                lastError_ = SwString("plugin is not loaded");
                return false;
            }
            if (state->loadRefCount == 0) {
                holdsLoadReference_ = false;
                state_.reset();
                lastError_ = SwString("plugin load reference underflow");
                return false;
            }
            --state->loadRefCount;
            shouldUnload = (state->loadRefCount == 0);
        }

        holdsLoadReference_ = false;
        state_.reset();

        if (!shouldUnload) {
            lastError_ = SwString("plugin is still in use by another loader");
            return false;
        }

        destroyDynamicInstance_(state);
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            state->library.unload();
            state->resolvedFileName.clear();
            state->errorString.clear();
            state->metaData = SwJsonObject();
            state->metaDataInitialized = false;
            state->createFn = nullptr;
            state->destroyFn = nullptr;
        }
        sw::plugin::removeDynamicState_(state);
        return true;
    }

    bool isLoaded() const {
        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        if (!state) {
            state = sw::plugin::lookupDynamicState_(fileName_);
        }
        if (!state) return false;
        std::lock_guard<std::mutex> lk(state->mutex);
        return state->library.isLoaded();
    }

    SwObject* instance() {
        lastError_.clear();
        if (!load()) {
            return nullptr;
        }
        return ensureDynamicInstance_();
    }

    SwString errorString() const {
        if (!lastError_.isEmpty()) return lastError_;
        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        if (!state) {
            state = sw::plugin::lookupDynamicState_(fileName_);
        }
        if (!state) return SwString();
        std::lock_guard<std::mutex> lk(state->mutex);
        if (!state->errorString.isEmpty()) return state->errorString;
        return state->library.lastError();
    }

    SwJsonObject metaData() const {
        lastError_.clear();
        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        if (!state) {
            state = sw::plugin::lookupDynamicState_(fileName_);
        }
        if (state) {
            return ensureDynamicMetaData_(state);
        }

        if (cachedMetaDataInitialized_) {
            return cachedMetaData_;
        }

        if (fileName_.isEmpty()) {
            lastError_ = SwString("plugin fileName is empty");
            return SwJsonObject();
        }

        SwLibrary temp;
        if (!temp.load(fileName_)) {
            lastError_ = temp.lastError();
            return SwJsonObject();
        }

        SwString err;
        cachedMetaData_ = readMetaDataFromLibrary_(temp, &err);
        if (!err.isEmpty()) lastError_ = err;
        cachedMetaDataInitialized_ = true;
        return cachedMetaData_;
    }

    void* resolve(const SwString& symbol) {
        lastError_.clear();
        if (symbol.isEmpty()) {
            lastError_ = SwString("symbol is empty");
            return nullptr;
        }
        if (!load()) {
            return nullptr;
        }
        if (!state_) {
            lastError_ = SwString("plugin state is unavailable");
            return nullptr;
        }

        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->library.isLoaded()) {
            lastError_ = SwString("plugin is not loaded");
            return nullptr;
        }

        void* sym = state_->library.resolve(symbol);
        if (!sym) {
            state_->errorString = SwString("plugin missing symbol: ") + symbol;
            lastError_ = state_->errorString;
        }
        return sym;
    }

    SwJsonObject introspectionJson() const {
        SwJsonObject out;
        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        if (!state) {
            state = sw::plugin::lookupDynamicState_(fileName_);
        }
        if (state) {
            std::lock_guard<std::mutex> lk(state->mutex);
            out = state->library.introspectionJson();
        } else {
            out["loaded"] = SwJsonValue(false);
            out["requestedPath"] = SwJsonValue(fileName_.toStdString());
            out["path"] = SwJsonValue("");
            out["lastError"] = SwJsonValue(lastError_.toStdString());
            out["platformPrefix"] = SwJsonValue(SwLibrary::platformPrefix().toStdString());
            out["platformSuffix"] = SwJsonValue(SwLibrary::platformSuffix().toStdString());
        }
        out["pluginLoaderFileName"] = SwJsonValue(fileName().toStdString());
        out["pluginLoaderError"] = SwJsonValue(errorString().toStdString());
        return out;
    }

    static SwList<SwObject*> staticInstances() {
        SwList<SwObject*> out;
        const SwList<SwStaticPlugin> plugins = staticPlugins();
        for (size_t i = 0; i < plugins.size(); ++i) {
            SwObject* inst = plugins[i].instance();
            if (inst) out.append(inst);
        }
        return out;
    }

    static SwList<SwStaticPlugin> staticPlugins() {
        SwList<SwStaticPlugin> out;
        const SwList<std::shared_ptr<sw::plugin::StaticPluginState> > states = sw::plugin::staticPluginStates();
        for (size_t i = 0; i < states.size(); ++i) {
            out.append(SwStaticPlugin(states[i]));
        }
        return out;
    }

 private:
    static SwJsonObject readMetaDataFromLibrary_(const SwLibrary& library, SwString* errOut) {
        if (errOut) *errOut = SwString();
        const void* sym = library.resolve(sw::plugin::metaDataSymbolV1());
        if (!sym) {
            return SwJsonObject();
        }

        sw::plugin::MetaDataFnV1 fn = reinterpret_cast<sw::plugin::MetaDataFnV1>(const_cast<void*>(sym));
        if (!fn) {
            if (errOut) *errOut = SwString("invalid metadata function pointer");
            return SwJsonObject();
        }
        return sw::plugin::parseMetaDataJson_(fn(), errOut);
    }

    SwJsonObject ensureDynamicMetaData_(const std::shared_ptr<sw::plugin::DynamicPluginState>& state) const {
        if (!state) return SwJsonObject();

        std::lock_guard<std::mutex> lk(state->mutex);
        if (state->metaDataInitialized) {
            return state->metaData;
        }

        SwString err;
        state->metaData = readMetaDataFromLibrary_(state->library, &err);
        state->metaDataInitialized = true;
        if (!err.isEmpty()) {
            state->errorString = err;
            lastError_ = err;
        }
        return state->metaData;
    }

    SwObject* ensureDynamicInstance_() {
        if (!state_) return nullptr;

        std::shared_ptr<sw::plugin::DynamicPluginState> state = state_;
        std::lock_guard<std::mutex> lk(state->mutex);
        if (state->instance && SwObject::isLive(state->instance)) {
            return state->instance;
        }
        state->instance = nullptr;

        if (!state->createFn) {
            void* sym = state->library.resolve(sw::plugin::createInstanceSymbolV1());
            if (!sym) {
                state->errorString = SwString("plugin missing symbol: ") + sw::plugin::createInstanceSymbolV1();
                lastError_ = state->errorString;
                return nullptr;
            }
            state->createFn = reinterpret_cast<sw::plugin::CreateInstanceFnV1>(sym);
        }
        if (!state->destroyFn) {
            void* sym = state->library.resolve(sw::plugin::destroyInstanceSymbolV1());
            if (sym) {
                state->destroyFn = reinterpret_cast<sw::plugin::DestroyInstanceFnV1>(sym);
            }
        }

        state->instance = state->createFn(nullptr);
        if (!state->instance) {
            state->errorString = SwString("plugin factory returned null");
            lastError_ = state->errorString;
        }
        return state->instance;
    }

    static void destroyDynamicInstance_(const std::shared_ptr<sw::plugin::DynamicPluginState>& state) {
        if (!state) return;

        SwObject* instance = nullptr;
        sw::plugin::DestroyInstanceFnV1 destroyFn = nullptr;
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (!state->instance || !SwObject::isLive(state->instance)) {
                state->instance = nullptr;
                return;
            }
            instance = state->instance;
            destroyFn = state->destroyFn;
            state->instance = nullptr;
        }

        if (destroyFn) {
            destroyFn(instance);
        } else {
            delete instance;
        }
    }

    SwString fileName_{};
    LoadHints loadHints_{PreventUnloadHint};
    mutable SwString lastError_{};
    mutable SwJsonObject cachedMetaData_{};
    mutable bool cachedMetaDataInitialized_{false};
    std::shared_ptr<sw::plugin::DynamicPluginState> state_{};
    bool holdsLoadReference_{false};
};

#ifdef _WIN32
#define SW_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define SW_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define SW_PLUGIN_METADATA(JsonLiteral) \
    SW_PLUGIN_EXPORT const char* swPluginMetaDataV1() { return JsonLiteral; }

#define SW_PLUGIN_FACTORY(ClassType) \
    SW_PLUGIN_EXPORT SwObject* swCreatePluginInstanceV1(SwObject* parent) { return new ClassType(parent); } \
    SW_PLUGIN_EXPORT void swDestroyPluginInstanceV1(SwObject* instance) { delete static_cast<ClassType*>(instance); }

#define SW_EXPORT_PLUGIN(ClassType, JsonLiteral) \
    SW_PLUGIN_METADATA(JsonLiteral) \
    SW_PLUGIN_FACTORY(ClassType)

#define SW_PLUGIN_DETAIL_CONCAT_INNER_(a, b) a##b
#define SW_PLUGIN_DETAIL_CONCAT_(a, b) SW_PLUGIN_DETAIL_CONCAT_INNER_(a, b)

#ifdef __COUNTER__
#define SW_PLUGIN_DETAIL_UNIQUE_ID_ __COUNTER__
#else
#define SW_PLUGIN_DETAIL_UNIQUE_ID_ __LINE__
#endif

#define SW_REGISTER_STATIC_PLUGIN(ClassType, JsonLiteral) \
    SW_REGISTER_STATIC_PLUGIN_IMPL_(ClassType, JsonLiteral, SW_PLUGIN_DETAIL_UNIQUE_ID_)

#define SW_REGISTER_STATIC_PLUGIN_IMPL_(ClassType, JsonLiteral, UniqueId) \
    namespace { \
    const char* SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_metadata_, UniqueId)() { return JsonLiteral; } \
    SwObject* SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_create_, UniqueId)(SwObject* parent) { \
        return new ClassType(parent); \
    } \
    void SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_destroy_, UniqueId)(SwObject* instance) { \
        delete static_cast<ClassType*>(instance); \
    } \
    struct SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_registrar_, UniqueId) { \
        SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_registrar_, UniqueId)() { \
            (void)::sw::plugin::registerStaticPlugin( \
                SwString(#ClassType), \
                &SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_metadata_, UniqueId), \
                &SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_create_, UniqueId), \
                &SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_destroy_, UniqueId)); \
        } \
    } SW_PLUGIN_DETAIL_CONCAT_(sw_plugin_static_registrar_instance_, UniqueId); \
    }
