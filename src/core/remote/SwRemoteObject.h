#pragma once

/**
 * @file src/core/remote/SwRemoteObject.h
 * @ingroup core_remote
 * @brief Declares the public interface exposed by SwRemoteObject in the CoreSw remote and IPC
 * layer.
 *
 * This header belongs to the CoreSw remote and IPC layer. It provides the abstractions used to
 * expose objects across process boundaries and to transport data or signals between peers.
 *
 * Within that layer, this file focuses on the remote object interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwRemoteObject.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Remote-facing declarations in this area usually coordinate identity, proxying, serialization,
 * and synchronization across runtimes.
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

#include "SwDir.h"
#include "SwFile.h"
#include "SwAny.h"
#include "SwJsonDocument.h"
#include "SwMap.h"
#include "SwObject.h"
#include "SwSharedMemorySignal.h"
#include "SwIpcRpc.h"
#include "SwTimer.h"

#include "SwMutex.h"
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>

/**
  * @brief Base SwObject that loads a layered JSON configuration and optionally shares updates
  *        over shared-memory signals (multi-process on same machine).
   *
   * Load order (merge, last wins):
   * - Global: `systemConfig/global/<objectName>.json`
   * - Local:  `systemConfig/local/<sys>_<nameSpace>_<objectName>.json` (with `/` and `\\` replaced by `_`)
   * - User:   `systemConfig/user/<sys>_<nameSpace>_<objectName>.json` (with `/` and `\\` replaced by `_`)
   *
   * Writes (ipcUpdateConfig / setConfigValue) update only the **User** file by default.
   * You can override the root directory with setConfigRootDirectory().
   */
class SwRemoteObject : public SwObject {
    SW_OBJECT(SwRemoteObject, SwObject)
 public:
    enum class ConfigSavePolicy {
        SaveToDisk,
        NoSaveToDisk
    };

    struct ConfigPaths {
        SwString globalPath;
        SwString localPath;
        SwString userPath;
    };

    DECLARE_SIGNAL(configChanged, const SwJsonObject&)
    DECLARE_SIGNAL(configLoaded, const SwJsonObject&)
    DECLARE_SIGNAL(remoteConfigReceived, uint64_t, const SwJsonObject&)
    DECLARE_SIGNAL(remoteConfigValueReceived, uint64_t, const SwString&)

	    /**
	     * @brief Constructs a `SwRemoteObject` instance.
	     * @param sysName Value passed to the method.
	     * @param nameSpace Value passed to the method.
	     * @param objectName Value passed to the method.
	     * @param parent Optional parent object that owns this instance.
	     *
	     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
	     */
	    SwRemoteObject(const SwString& sysName,
	                         const SwString& nameSpace,
	                         const SwString& objectName,
	                         SwObject* parent = nullptr)
        : SwObject(parent),
          sysName_(sysName),
          nameSpace_(nameSpace),
          configRoot_("systemConfig"),
          ipcRegistry_(sysName_, buildObjectFqn(nameSpace_, objectName)),
          shmConfig_(ipcRegistry_, SwString("__config__|") + objectName),
          alive_(new std::atomic_bool(true)) {
        setObjectName(objectName);
        publisherId_ = makePublisherId_(this);
	        ensureConfigDirectories();
 	        loadConfig();
 	        enableSharedMemoryConfig(true);
            (void)ipcExposeRpcT(SwString("system/saveAsFactory"), this, &SwRemoteObject::saveAsFactory);
            (void)ipcExposeRpcT(SwString("system/resetFactory"), this, &SwRemoteObject::resetFactory);
 	        // Publish an initial snapshot so external tools (ex: SwBridge) can read it via IPC.
 	        {
 	            SwMutexLocker lk(mutex_);
 	            if (shmConfigEnabled_) {
	                shmConfig_.publish(publisherId_, effectiveConfigJson_locked_(SwJsonDocument::JsonFormat::Compact));
	            }
	        }
	    }

    /**
     * @brief Destroys the `SwRemoteObject` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwRemoteObject() {
        // Best-effort: flush pending debounced save before shutdown.
        {
            SwMutexLocker lk(mutex_);
            flushUserDocSave_locked_();
        }
        if (alive_) alive_->store(false, std::memory_order_relaxed);
        disableSharedMemoryConfig();
        stopAllIpcSubscriptions_();
        {
            SwMutexLocker lk(rpcRespMutex_);
            rpcRespValueQueues_.clear();
            rpcRespVoidQueues_.clear();
        }
    }

    /**
     * @brief Sets the rpc Queue Capacity.
     * @param capacity Value passed to the method.
     * @return The requested rpc Queue Capacity.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static void setRpcQueueCapacity(uint32_t capacity) {
        sw::ipc::setRpcQueueCapacity(capacity);
    }

    /**
     * @brief Returns the current rpc Queue Capacity.
     * @return The current rpc Queue Capacity.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static uint32_t rpcQueueCapacity() {
        return sw::ipc::rpcQueueCapacity();
    }

    /**
     * @brief Returns the current sys Name.
     * @return The current sys Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& sysName() const { return sysName_; }
    /**
     * @brief Returns the current name Space.
     * @return The current name Space.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& nameSpace() const { return nameSpace_; }
    /**
     * @brief Performs the `objectName` operation.
     * @return The requested object Name.
     */
    SwString objectName() const { return getObjectName(); }

    /**
     * @brief Returns the current config Root Directory.
     * @return The current config Root Directory.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString configRootDirectory() const { return configRoot_; }

    /**
     * @brief Sets the config Root Directory.
     * @param rootDir Root directory used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setConfigRootDirectory(const SwString& rootDir) {
        SwJsonObject mergedCopy;
        SwList<std::function<void()>> pending;
        {
            SwMutexLocker lk(mutex_);
            configRoot_ = rootDir.isEmpty() ? SwString("systemConfig") : rootDir;
            ensureConfigDirectories();
            loadConfig_locked();
            refreshRegisteredConfigs_locked_(pending);
            mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
        }
        for (size_t i = 0; i < pending.size(); ++i) pending[i]();
        emit configLoaded(mergedCopy);
    }

    /**
     * @brief Returns the current config Paths.
     * @return The current config Paths.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ConfigPaths configPaths() const {
        ConfigPaths out;
        out.globalPath = globalConfigPath();
        out.localPath = localConfigPath();
        out.userPath = userConfigPath();
        return out;
    }

    /**
     * @brief Returns the current merged Config.
     * @return The current merged Config.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwJsonObject mergedConfig() const {
        SwMutexLocker lk(mutex_);
        return mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
    }

    /**
     * @brief Performs the `ipcFullName` operation.
     * @param leafName Value passed to the method.
     * @return The requested ipc Full Name.
     */
    SwString ipcFullName(const SwString& leafName) const {
        return buildFullName(sysName_, ipcRegistry_.object(), leafName);
    }

    /**
     * @brief Performs the `configValue` operation.
     * @param path Path used by the operation.
     * @return The requested config Value.
     */
    SwJsonValue configValue(const SwString& path) const {
        SwMutexLocker lk(mutex_);
        SwJsonValue out;
        if (!tryFindValueNoLog_(mergedDoc_.toJsonValue(), path, out)) {
            return SwJsonValue();
        }
        return out;
    }

    /**
     * @brief Returns the current config.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool loadConfig() {
        SwJsonObject mergedCopy;
        SwList<std::function<void()>> pending;
        {
            SwMutexLocker lk(mutex_);
            loadConfig_locked();
            refreshRegisteredConfigs_locked_(pending);
            mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
        }
        for (size_t i = 0; i < pending.size(); ++i) pending[i]();
        emit configLoaded(mergedCopy);
        return true;
    }

    /**
     * @brief Performs the `saveLocalConfig` operation on the associated resource.
     * @param pretty Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool saveLocalConfig(bool pretty = true) {
        SwMutexLocker lk(mutex_);
        return writeDocToFile_locked(localDoc_, localConfigPath(), pretty);
    }

    /**
     * @brief Performs the `saveUserConfig` operation on the associated resource.
     * @param pretty Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool saveUserConfig(bool pretty = true) {
        SwMutexLocker lk(mutex_);
        userDocSavePending_ = false;
        if (userDocSaveTimer_) userDocSaveTimer_->stop();
        return writeUserDocToFile_locked_(pretty);
    }

    // Merge the current User layer into the Local ("factory") layer and write the local file.
    /**
     * @brief Returns the current as Factory.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool saveAsFactory() {
        SwJsonObject mergedCopy;
        SwList<std::function<void()>> pending;
        bool changed = false;

        bool ok = true;
        bool noOp = false;
        {
            SwMutexLocker lk(mutex_);

            const bool hasUserOverrides = (userDoc_.isObject() && !userDoc_.object().isEmpty());
            if (!hasUserOverrides) {
                noOp = true;
            } else {
                SwJsonObject nextLocal = localDoc_.isObject() ? localDoc_.object() : SwJsonObject{};
                if (userDoc_.isObject()) mergeObjectDeep_(nextLocal, userDoc_.object());
                SwJsonDocument nextLocalDoc(nextLocal);

                if (!writeDocToFile_locked(nextLocalDoc, localConfigPath(), /*pretty=*/true)) {
                    ok = false;
                } else {
                    localDoc_ = nextLocalDoc;

                    const SwJsonDocument prevMerged = mergedDoc_;
                    recomputeMerged_locked();
                    if (mergedDoc_ != prevMerged) {
                        changed = true;
                    }

                    if (shmConfigEnabled_) {
                        shmConfig_.publish(publisherId_, effectiveConfigJson_locked_(SwJsonDocument::JsonFormat::Compact));
                    }

                    mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
                    refreshRegisteredConfigs_locked_(pending);
                }
            }
        }

        if (noOp) return true;

        for (size_t i = 0; i < pending.size(); ++i) pending[i]();
        if (changed) emit configChanged(mergedCopy);
        return ok;
    }

    // Reset to factory by deleting the User file and clearing in-memory overrides.
    /**
     * @brief Returns the current factory.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool resetFactory() {
        SwJsonObject mergedCopy;
        SwList<std::function<void()>> pending;
        bool changed = false;

        bool ok = true;
        bool noOp = false;
        {
            SwMutexLocker lk(mutex_);

            const SwString userPath = userConfigPath();
            const bool hasUserFile = SwFile::isFile(userPath);
            const bool hasUserOverrides = (userDoc_.isObject() && !userDoc_.object().isEmpty());
            if (!hasUserFile && !hasUserOverrides && userTouchedPaths_.isEmpty()) {
                noOp = true;
            } else {
                flushUserDocSave_locked_();

                if (SwFile::isFile(userPath)) {
                    if (std::remove(userPath.toStdString().c_str()) != 0) {
                        ok = false;
                    }
                }

                if (ok) {
                    userDoc_.setObject(SwJsonObject{});
                    userTouchedPaths_.clear();

                    const SwJsonDocument prevMerged = mergedDoc_;
                    recomputeMerged_locked();
                    if (mergedDoc_ != prevMerged) {
                        changed = true;
                    }

                    if (shmConfigEnabled_) {
                        shmConfig_.publish(publisherId_, effectiveConfigJson_locked_(SwJsonDocument::JsonFormat::Compact));
                    }

                    mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
                    refreshRegisteredConfigs_locked_(pending);
                }
            }
        }

        if (noOp) return true;
        if (!ok) return false;

        for (size_t i = 0; i < pending.size(); ++i) pending[i]();
        if (changed) emit configChanged(mergedCopy);
        return ok;
    }

    /**
      * @brief Set a user config key (path supports '/' nesting), save to disk, and publish via SHM.
      */
	    bool setConfigValue(const SwString& path,
	                        const SwJsonValue& value,
	                        bool saveToDisk = true,
	                        bool publishToShm = true) {
	        SwJsonObject mergedCopy;
	        SwList<std::function<void()>> pending;
	        SwList<std::function<void()>> publishes;
	        bool changed = false;
	        {
	            SwMutexLocker lk(mutex_);
	            SwJsonDocument candidateDoc = userDoc_;
	            ensureObjectRoot(candidateDoc);
	            candidateDoc.find(path, true) = value;
	            markConfigPathTouched_locked_(path);
	
	            const SwJsonValue baselineValue = baselineConfigValue_locked_();
	            SwJsonValue prunedUserValue;
	            SwJsonDocument nextUserDoc;
	            if (buildUserOverrideValue_(candidateDoc.toJsonValue(), baselineValue, prunedUserValue, &userTouchedPaths_) &&
	                prunedUserValue.isObject()) {
	                nextUserDoc.setObject(prunedUserValue.toObject());
	            } else {
	                nextUserDoc.setObject(SwJsonObject{});
	            }
	
	            if (userDoc_ != nextUserDoc) {
	                userDoc_ = nextUserDoc;
	                recomputeMerged_locked();
	                changed = true;
	
	                if (saveToDisk) {
	                    scheduleUserDocSave_locked_();
	                }
		                if (publishToShm && shmConfigEnabled_) {
		                    shmConfig_.publish(publisherId_, effectiveConfigJson_locked_(SwJsonDocument::JsonFormat::Compact));
		                }
	
	                mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
	                refreshRegisteredConfigs_locked_(pending);
	                if (publishToShm) {
	                    for (auto it = registeredConfigs_.begin(); it != registeredConfigs_.end(); ++it) {
	                        const RegisteredConfigEntry& entry = it.value();
	                        if (entry.configName == path && entry.publish) {
	                            publishes.append(entry.publish);
	                        }
	                    }
	                }
	            } else {
	                mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
	            }
	        }
	        for (size_t i = 0; i < publishes.size(); ++i) publishes[i]();
	        for (size_t i = 0; i < pending.size(); ++i) pending[i]();
	        if (changed) emit configChanged(mergedCopy);
	        return true;
	    }

    /**
     * @brief Performs the `enableSharedMemoryConfig` operation.
     * @param enable Value passed to the method.
     */
    void enableSharedMemoryConfig(bool enable) {
        SwMutexLocker lk(mutex_);
        if (enable == shmConfigEnabled_) return;
        shmConfigEnabled_ = enable;
        if (shmConfigEnabled_) {
            startShmConfigSubscription_locked();
        } else {
            stopShmConfigSubscription_locked();
        }
    }

    /**
     * @brief Performs the `disableSharedMemoryConfig` operation.
     */
    void disableSharedMemoryConfig() {
        SwMutexLocker lk(mutex_);
        shmConfigEnabled_ = false;
        stopShmConfigSubscription_locked();
    }

    // ---------------------------------------------------------------------
    // High-level IPC config API
    // ---------------------------------------------------------------------
    template <typename T>
    /**
     * @brief Performs the `ipcRegisterConfigT` operation.
     * @param storage Value passed to the method.
     * @param configName Value passed to the method.
     * @param defaultValue Value passed to the method.
     */
    void ipcRegisterConfigT(T& storage,
                            const SwString& configName,
                            const T& defaultValue) {
        ipcRegisterConfigT<T>(storage, configName, defaultValue, std::function<void(const T&)>{});
    }

		    template <typename T, typename Fn>
		    /**
		     * @brief Performs the `ipcRegisterConfigT` operation.
		     * @param storage Value passed to the method.
		     * @param configName Value passed to the method.
		     * @param defaultValue Value passed to the method.
		     * @param onChange Value passed to the method.
		     */
		    void ipcRegisterConfigT(T& storage,
		                            const SwString& configName,
		                            const T& defaultValue,
		                            Fn onChange) {
	        std::function<void(const T&)> cb(onChange);

	        std::shared_ptr<sw::ipc::Signal<uint64_t, SwString>> configSignal;
	        {
	            SwMutexLocker lk(mutex_);
	            const T initial = configValueFromDocs_<T>(mergedDoc_, configName, defaultValue);
	            storage = initial;

	            RegisteredConfigEntry entry;
	            entry.configName = configName;
	            entry.defaultJson = valueToJson_(defaultValue);
	            entry.fullName = buildFullName(sysName_, ipcRegistry_.object(), configName);
	            entry.onMergedChanged = [this, &storage, configName, defaultValue, cb](SwList<std::function<void()>>& pending) {
	                const T next = configValueFromDocs_<T>(mergedDoc_, configName, defaultValue);
	                if (!(storage == next)) {
	                    storage = next;
                    if (cb) {
                        pending.append([cb, next]() { cb(next); });
                    }
                }
            };

            registeredConfigs_[entry.fullName] = entry;

		            // Create the per-config IPC channel (used for explicit remote updates).
		            const SwString sigName = SwString("__cfg__|") + configName;
		            configSignal = ensureConfigSignal_(sigName);
	
		            // Publish an updated full config snapshot (includes defaults) so tools can query it via IPC.
		            if (shmConfigEnabled_) {
		                shmConfig_.publish(publisherId_, effectiveConfigJson_locked_(SwJsonDocument::JsonFormat::Compact));
		            }
		        }

        // Listen for explicit remote updates of this config and apply them locally.
        // Sender side: `ipcUpdateConfig<T>(targetObject, configName, value)`.
         if (configSignal) {
              std::shared_ptr<std::atomic_bool> alive = alive_;
              sw::ipc::detail::ScopedSubscriberObject subScope(ipcRegistry_.object());
              auto sub = configSignal->connect(
                 [this, alive, configName, &storage](uint64_t pubId, SwString payload) {
                      if (!alive || !alive->load(std::memory_order_relaxed)) return;
                       if (pubId == publisherId_) return;
 
                      T next{};
                      if (!stringToValue_(payload, next)) return;
 
                      ThreadHandle* targetThread = this->threadHandle();
                      ThreadHandle* currentThread = ThreadHandle::currentThread();
                     auto task = [this, alive, pubId, configName, &storage, next]() mutable {
                          if (!alive || !alive->load(std::memory_order_relaxed)) return;
                          if (storage == next) return;
                          ipcUpdateConfig<T>(configName, next, ConfigSavePolicy::SaveToDisk);
                          emit remoteConfigValueReceived(pubId, configName);
                      };
 
                      if (!targetThread || targetThread == currentThread) {
                         task();
                     } else {
                         targetThread->postTask(std::move(task));
                     }
                },
                /*fireInitial=*/false);
            storeIpcSubscription_(std::move(sub));
        }
    }

    template <typename T>
    /**
     * @brief Performs the `ipcUpdateConfig` operation.
     * @param configName Value passed to the method.
     * @param value Value passed to the method.
     * @param savePolicy Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool ipcUpdateConfig(const SwString& configName,
                         const T& value,
                         ConfigSavePolicy savePolicy = ConfigSavePolicy::SaveToDisk) {
        // Local-only update: modifies this object's config (disk + merged) but does NOT push to other processes.
        const bool saveToDisk = (savePolicy == ConfigSavePolicy::SaveToDisk);
        return setConfigValue(configName, valueToJson_(value), saveToDisk, /*publishToShm=*/false);
    }

    // Remote update: publish a config value to another object (no local file write on this side).
    // The target object must have registered the config with ipcRegisterConfigT (so it applies remote updates).
    template <typename T>
    /**
     * @brief Performs the `ipcUpdateConfig` operation.
     * @param targetObject Value passed to the method.
     * @param configName Value passed to the method.
     * @param value Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool ipcUpdateConfig(const SwString& targetObject,
                         const SwString& configName,
                         const T& value) {
        SwString ns, obj;
        if (!splitObjectFqn_(targetObject, ns, obj)) return false;
        const SwString sigName = SwString("__cfg__|") + configName;
        sw::ipc::Registry reg(ns, obj);
        sw::ipc::Signal<uint64_t, SwString> sig(reg, sigName);
        return sig.publish(publisherId_, valueToString_<T>(value));
    }

    // Bind a config value by full name: "ns/.../objectName#configPath" (configPath can contain '/')
    // Stores the subscription internally, returns a token usable with ipcDisconnect().
    template <typename T>
    /**
     * @brief Performs the `ipcBindConfigT` operation.
     * @param storage Value passed to the method.
     * @param configFullName Value passed to the method.
     * @return The requested ipc Bind Config T.
     */
    size_t ipcBindConfigT(T& storage, const SwString& configFullName) {
        return ipcBindConfigT<T>(storage, configFullName, std::function<void(const T&)>{});
    }

    template <typename T, typename Fn>
    /**
     * @brief Performs the `ipcBindConfigT` operation.
     * @param storage Value passed to the method.
     * @param configFullName Value passed to the method.
     * @param onChange Value passed to the method.
     * @return The requested ipc Bind Config T.
     */
    size_t ipcBindConfigT(T& storage, const SwString& configFullName, Fn onChange) {
        SwString ns, obj, leaf;
        if (!splitFullName_(configFullName, ns, obj, leaf)) {
            return 0;
        }
        const SwString sigName = SwString("__cfg__|") + leaf;

        std::function<void(const T&)> cb(onChange);

        sw::ipc::Registry reg(ns, obj);
        sw::ipc::Signal<uint64_t, SwString> sig(reg, sigName);
        sw::ipc::detail::ScopedSubscriberObject subScope(ipcRegistry_.object());
        std::shared_ptr<std::atomic_bool> alive = alive_;
        auto sub = sig.connect([this, alive, &storage, cb](uint64_t pubId, SwString payload) {
            if (!alive || !alive->load(std::memory_order_relaxed)) return;
             if (pubId == publisherId_) return;
             T next{};
             if (!stringToValue_(payload, next)) return;

             ThreadHandle* targetThread = this->threadHandle();
             ThreadHandle* currentThread = ThreadHandle::currentThread();
             auto task = [this, alive, &storage, cb, next]() mutable {
                 if (!alive || !alive->load(std::memory_order_relaxed)) return;
                 if (!(storage == next)) {
                     storage = next;
                     if (cb) cb(storage);
                 }
             };

             if (!targetThread || targetThread == currentThread) {
                 task();
             } else {
                 targetThread->postTask(std::move(task));
             }
        }, /*fireInitial=*/true);

        return storeIpcSubscription_(std::move(sub));
    }

    // Generic IPC connect by fullName: "ns/.../objectName/signalName"
    // Stores the subscription internally, returns a token usable with ipcDisconnect().
    template <typename Fn>
    /**
     * @brief Performs the `ipcConnectT` operation.
     * @param fullName Value passed to the method.
     * @param onSignal Value passed to the method.
     * @param fireInitial Value passed to the method.
     * @return The requested ipc Connect T.
     */
    size_t ipcConnectT(const SwString& fullName, Fn onSignal, bool fireInitial = true) {
        using traits = function_traits<typename std::decay<Fn>::type>;
        using args_tuple = typename traits::args_tuple;
        return IpcConnectHelper_<args_tuple>::connect(this, fullName, onSignal, fireInitial);
    }

    // Generic IPC connect by target object and signal name:
    //   - targetObject:
    //       - absolute: "/ns/.../objectName" or "ns/.../objectName"
    //       - relative: "objectName" (resolved as "<this->nameSpace()>/objectName")
    //   - leaf:         "signalName"
    //
    // The connection is owned by `context` with receiver-based lifetime semantics:
    // it is automatically stopped when `context` is destroyed.
    template <typename Fn>
    /**
     * @brief Performs the `ipcConnectScopedT` operation.
     * @param targetObject Value passed to the method.
     * @param leaf Value passed to the method.
     * @param context Value passed to the method.
     * @param onSignal Value passed to the method.
     * @param fireInitial Value passed to the method.
     * @return The requested ipc Connect Scoped T.
     */
    SwObject* ipcConnectScopedT(const SwString& targetObject,
                                const SwString& leaf,
                                SwObject* context,
                                Fn onSignal,
                                bool fireInitial = true) {
        using traits = function_traits<typename std::decay<Fn>::type>;
        using args_tuple = typename traits::args_tuple;
        return IpcConnectScopedHelper_<args_tuple>::connect(this, targetObject, leaf, context, onSignal, fireInitial);
    }

    // ---------------------------------------------------------------------
    // High-level IPC RPC API (ringbuffer-based, multi-client)
    // ---------------------------------------------------------------------
    // Handler signatures supported:
    //   - Ret(Args...)
    //   - Ret(sw::ipc::RpcContext, Args...)   // RpcContext is passed by value (decayed)
    template <typename Fn>
    /**
     * @brief Performs the `ipcExposeRpcT` operation.
     * @param methodName Value passed to the method.
     * @param handler Value passed to the method.
     * @param fireInitial Value passed to the method.
     * @return The requested ipc Expose Rpc T.
     */
    size_t ipcExposeRpcT(const SwString& methodName, Fn handler, bool fireInitial = true) {
        sw::ipc::detail::ScopedSubscriberObject subScope(ipcRegistry_.object());
        using traits = function_traits<typename std::decay<Fn>::type>;
        using R = typename traits::return_type;
        using args_tuple = typename traits::args_tuple;
        return IpcExposeRpcHelper_<args_tuple, R>::expose(this, methodName, handler, fireInitial);
    }

    // Convenience overload: expose a member function as an RPC handler.
    // Example:
    //   ipcExposeRpc(add, this, &MyClass::add);
    template <typename Obj, typename Ret, typename... Args>
    typename std::enable_if<!std::is_void<Ret>::value, size_t>::type
    /**
     * @brief Performs the `ipcExposeRpcT` operation.
     * @param methodName Value passed to the method.
     * @param obj Value passed to the method.
     * @param fireInitial Value passed to the method.
     */
    ipcExposeRpcT(const SwString& methodName,
                  Obj* obj,
                  Ret(Obj::*method)(Args...),
                  bool fireInitial = true) {
        auto wrapper = [obj, method](typename std::decay<Args>::type... args) {
            return (obj->*method)(args...);
        };
        return ipcExposeRpcT(methodName, wrapper, fireInitial);
    }

    template <typename Obj, typename Ret, typename... Args>
    typename std::enable_if<std::is_void<Ret>::value, size_t>::type
    /**
     * @brief Performs the `ipcExposeRpcT` operation.
     * @param methodName Value passed to the method.
     * @param obj Value passed to the method.
     * @param fireInitial Value passed to the method.
     */
    ipcExposeRpcT(const SwString& methodName,
                  Obj* obj,
                  Ret(Obj::*method)(Args...),
                  bool fireInitial = true) {
        auto wrapper = [obj, method](typename std::decay<Args>::type... args) {
            (obj->*method)(args...);
        };
        return ipcExposeRpcT(methodName, wrapper, fireInitial);
    }

    template <typename Obj, typename Ret, typename... Args>
    typename std::enable_if<!std::is_void<Ret>::value, size_t>::type
    /**
     * @brief Performs the `ipcExposeRpcT` operation.
     * @param methodName Value passed to the method.
     * @param obj Value passed to the method.
     * @param fireInitial Value passed to the method.
     */
    ipcExposeRpcT(const SwString& methodName,
                  Obj* obj,
                  Ret(Obj::*method)(Args...) const,
                  bool fireInitial = true) {
        auto wrapper = [obj, method](typename std::decay<Args>::type... args) {
            return (obj->*method)(args...);
        };
        return ipcExposeRpcT(methodName, wrapper, fireInitial);
    }

    template <typename Obj, typename Ret, typename... Args>
    typename std::enable_if<std::is_void<Ret>::value, size_t>::type
    /**
     * @brief Performs the `ipcExposeRpcT` operation.
     * @param methodName Value passed to the method.
     * @param obj Value passed to the method.
     * @param fireInitial Value passed to the method.
     */
    ipcExposeRpcT(const SwString& methodName,
                  Obj* obj,
                  Ret(Obj::*method)(Args...) const,
                  bool fireInitial = true) {
        auto wrapper = [obj, method](typename std::decay<Args>::type... args) {
            (obj->*method)(args...);
        };
        return ipcExposeRpcT(methodName, wrapper, fireInitial);
    }

    /**
     * @brief Performs the `ipcDisconnect` operation.
     * @param token Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool ipcDisconnect(size_t token) {
        SwMutexLocker lk(mutex_);
        auto it = ipcSubscriptions_.find(token);
        if (it == ipcSubscriptions_.end()) return false;
        if (it->second) it->second->stop();
        ipcSubscriptions_.remove(token);
        return true;
    }

 protected:
    /**
     * @brief Performs the `rpcMethodNameFromMacro_` operation.
     * @param s Value passed to the method.
     * @return The requested rpc Method Name From Macro.
     */
    static SwString rpcMethodNameFromMacro_(SwString s)
    {
        s = s.trimmed();

        // Allow `(&Class::method)` (and nested parentheses)
        while (s.size() >= 2 && s.startsWith('(') && s.endsWith(')')) {
            s = s.mid(1, static_cast<int>(s.size() - 2)).trimmed();
        }

        // If the macro-stringified token is a string literal: "\"name\"" -> "name"
        if (s.size() >= 2 && s.startsWith('"') && s.endsWith('"')) {
            return s.mid(1, static_cast<int>(s.size() - 2));
        }

        // Remove ALL whitespace
        SwString noWs;
        noWs.reserve(s.size());
        for (int i = 0; i < s.size(); ++i) {
            const auto ch = s.at(i);      
            if (!ch.isSpace()) noWs.append(ch);
        }
        s = noWs;

        // Remove leading '&' (possibly repeated)
        while (!s.isEmpty() && s.startsWith('&')) {
            s.remove(0, 1);
        }

        // Keep leaf after last `::`
        const size_t pos = s.lastIndexOf("::");
        if (pos != static_cast<size_t>(-1) && (pos + 2) < s.size()) {
            s = s.mid(static_cast<int>(pos + 2));
        }

        return s;
    }


    /**
     * @brief Performs the `sanitizeSegment` operation.
     * @param in Value passed to the method.
     * @return The requested sanitize Segment.
     */
    static SwString sanitizeSegment(const SwString& in) {
        std::string s = in.toStdString();
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const bool ok =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                (c == '_') || (c == '-') || (c == '.');
            if (!ok) s[i] = '_';
        }
        if (s.empty()) s = "root";
        return SwString(s);
    }

    /**
     * @brief Builds the full Name requested by the caller.
     * @param ns Value passed to the method.
     * @param obj Value passed to the method.
     * @param leaf Value passed to the method.
     * @return The resulting full Name.
     */
    static SwString buildFullName(const SwString& ns, const SwString& obj, const SwString& leaf) {
        return buildObjectFqn(ns, obj) + "#" + leaf;
    }

    // Human-readable "namespace/objectName" (no leading '/').
    /**
     * @brief Builds the object Fqn requested by the caller.
     * @param ns Value passed to the method.
     * @param obj Value passed to the method.
     * @return The resulting object Fqn.
     */
    static SwString buildObjectFqn(const SwString& ns, const SwString& obj) {
        std::string nsStd = ns.toStdString();
        for (size_t i = 0; i < nsStd.size(); ++i) {
            if (nsStd[i] == '\\') nsStd[i] = '/';
        }
        while (!nsStd.empty() && nsStd.front() == '/') nsStd.erase(0, 1);
        while (!nsStd.empty() && nsStd.back() == '/') nsStd.pop_back();

        if (nsStd.empty()) return obj;
        return SwString(nsStd) + "/" + obj;
    }

    /**
     * @brief Performs the `splitFullName_` operation.
     * @param fullName Value passed to the method.
     * @param nsOut Value passed to the method.
     * @param objOut Value passed to the method.
     * @param leafOut Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool splitFullName_(const SwString& fullName, SwString& nsOut, SwString& objOut, SwString& leafOut) const {
        SwString path = fullName.trimmed();
        path.replace("\\", "/");
        while (path.contains("//")) path.replace("//", "/");
        const bool hadLeadingSlash = path.startsWith("/");
        while (path.startsWith("/")) path = path.mid(1);
        while (path.endsWith("/")) path = path.left(static_cast<int>(path.size()) - 1);
        if (path.isEmpty()) return false;

        const std::string s = path.toStdString();
        const size_t sharp = s.find('#');
        if (sharp != std::string::npos) {
            SwString objectPart = SwString(s.substr(0, sharp));
            leafOut = SwString(s.substr(sharp + 1));
            if (leafOut.isEmpty()) return false;

            objectPart.replace("\\", "/");
            while (objectPart.contains("//")) objectPart.replace("//", "/");
            while (objectPart.startsWith("/")) objectPart = objectPart.mid(1);
            while (objectPart.endsWith("/")) objectPart = objectPart.left(static_cast<int>(objectPart.size()) - 1);
            if (objectPart.isEmpty()) return false;

            // Accept either:
            //   - full: "sysName/nameSpace/objectName#leaf"
            //   - abs:  "/nameSpace/objectName#leaf"
            //   - rel:  "objectName#leaf" (resolved as "<this->nameSpace()>/objectName#leaf")
            const std::string sysPrefix = (sysName_.isEmpty() ? std::string() : (sysName_.toStdString() + "/"));
            const std::string op = objectPart.toStdString();
            const bool hadSysPrefix = (!sysPrefix.empty() && op.rfind(sysPrefix, 0) == 0);

            SwString objPart = hadSysPrefix ? SwString(op.substr(sysPrefix.size())) : objectPart;
            if (objPart.isEmpty()) return false;

            if (!hadLeadingSlash && !hadSysPrefix) {
                if (!objPart.contains("/")) {
                    objPart = buildObjectFqn(nameSpace_, objPart);
                }
            }

            nsOut = sysName_;
            objOut = objPart;
            return !objOut.isEmpty();
        }

        // Slash form: ".../leaf" (leaf is a single segment; use '#' if leaf must contain '/').
        SwList<SwString> partsRaw = path.split('/');
        SwList<SwString> parts;
        for (size_t i = 0; i < partsRaw.size(); ++i) {
            if (!partsRaw[i].isEmpty()) parts.append(partsRaw[i]);
        }
        if (parts.size() < 2) return false;

        leafOut = parts[parts.size() - 1];
        if (leafOut.isEmpty()) return false;

        SwString objectPart;
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!objectPart.isEmpty()) objectPart += "/";
            objectPart += parts[i];
        }
        if (objectPart.isEmpty()) return false;

        const std::string sysPrefix = (sysName_.isEmpty() ? std::string() : (sysName_.toStdString() + "/"));
        const std::string op = objectPart.toStdString();
        const bool hadSysPrefix = (!sysPrefix.empty() && op.rfind(sysPrefix, 0) == 0);

        SwString objPart = hadSysPrefix ? SwString(op.substr(sysPrefix.size())) : objectPart;
        if (objPart.isEmpty()) return false;

        if (!hadLeadingSlash && !hadSysPrefix) {
            const std::string objStd = objPart.toStdString();
            if (objStd.find('/') == std::string::npos) {
                objPart = buildObjectFqn(nameSpace_, objPart);
            }
        }

        nsOut = sysName_;
        objOut = objPart;
        return !objOut.isEmpty();
    }

    /**
     * @brief Returns the current global Config Path.
     * @return The current global Config Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString globalConfigPath() const {
        const SwString o = sanitizeSegment(getObjectName());
        return configRootAbsolute_() + "/global/" + o + ".json";
    }

    /**
     * @brief Returns the current local Config Path.
     * @return The current local Config Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString localConfigPath() const {
        const SwString sys = sanitizeNsForFile_(sysName_);
        const SwString obj = sanitizeNsForFile_(ipcRegistry_.object());
        return configRootAbsolute_() + "/local/" + sys + "_" + obj + ".json";
    }

    /**
     * @brief Returns the current user Config Path.
     * @return The current user Config Path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString userConfigPath() const {
        const SwString sys = sanitizeNsForFile_(sysName_);
        const SwString obj = sanitizeNsForFile_(ipcRegistry_.object());
        return configRootAbsolute_() + "/user/" + sys + "_" + obj + ".json";
    }

    /**
     * @brief Performs the `ensureConfigDirectories` operation.
     */
    void ensureConfigDirectories() const {
        const SwString root = configRootAbsolute_();
        SwDir::mkpathAbsolute(root + "/global", /*normalizeInput=*/false);
        SwDir::mkpathAbsolute(root + "/local",  /*normalizeInput=*/false);
        SwDir::mkpathAbsolute(root + "/user",   /*normalizeInput=*/false);
    }

    /**
     * @brief Performs the `ensureObjectRoot` operation.
     * @param doc Value passed to the method.
     * @return The requested ensure Object Root.
     */
    static void ensureObjectRoot(SwJsonDocument& doc) {
        if (!doc.isObject()) {
            doc.setObject(SwJsonObject{});
        }
    }

	    /**
	     * @brief Performs the `mergeValueDeep_` operation.
	     * @param target Value passed to the method.
	     * @param src Value passed to the method.
	     * @return The requested merge Value Deep.
	     */
	    static void mergeValueDeep_(SwJsonValue& target, const SwJsonValue& src) {
	        if (target.isObject() && src.isObject()) {
	            SwJsonObject t(target.toObject());
	            SwJsonObject s(src.toObject());
	            mergeObjectDeep_(t, s);
	            target = SwJsonValue(t);
	            return;
	        }
	        target = src;
	    }

	    /**
	     * @brief Performs the `mergeObjectDeep_` operation.
	     * @param target Value passed to the method.
	     * @param src Value passed to the method.
	     * @return The requested merge Object Deep.
	     */
	    static void mergeObjectDeep_(SwJsonObject& target, const SwJsonObject& src) {
	        SwJsonObject::Container data = src.data();
        for (SwJsonObject::Container::const_iterator it = data.begin(); it != data.end(); ++it) {
            const SwString k(it->first);
            if (target.contains(k) && target[k].isObject() && it->second.isObject()) {
                SwJsonValue tv = target[k];
                mergeValueDeep_(tv, it->second);
                target[k] = tv;
            } else {
                target.insert(it->first, it->second);
            }
	        }
	    }
	
	    /**
	     * @brief Performs the `jsonDeepEqual_` operation.
	     * @param a Value passed to the method.
	     * @param b Value passed to the method.
	     * @return The requested json Deep Equal.
	     */
	    static bool jsonDeepEqual_(const SwJsonValue& a, const SwJsonValue& b) {
	        // Treat numbers as equal even if stored as int vs float (ex: 3 vs 3.0).
	        if (a.isDouble() && b.isDouble()) {
	            if (a.isInt() && b.isInt()) {
	                return a.toLongLong() == b.toLongLong();
	            }
	            return a.toDouble() == b.toDouble();
	        }
	
	        if (a.isNull() && b.isNull()) return true;
	        if (a.isBool() && b.isBool()) return a.toBool() == b.toBool();
	        if (a.isString() && b.isString()) return a.toString() == b.toString();
	
	        if (a.isObject() && b.isObject()) {
	            const SwJsonObject ao = a.toObject();
	            const SwJsonObject bo = b.toObject();
	            if (ao.size() != bo.size()) return false;
	            for (auto it = ao.begin(); it != ao.end(); ++it) {
	                const SwString key = it.key();
	                if (!bo.contains(key)) return false;
	                if (!jsonDeepEqual_(it.value(), bo[key])) return false;
	            }
	            return true;
	        }

	        if (a.isArray() && b.isArray()) {
	            const SwJsonArray aa = a.toArray();
	            const SwJsonArray ba = b.toArray();
	            if (aa.size() != ba.size()) return false;
	            for (size_t i = 0; i < aa.size(); ++i) {
	                if (!jsonDeepEqual_(aa[i], ba[i])) return false;
	            }
	            return true;
	        }
	
	        return false;
	    }
	
	    /**
	     * @brief Performs the `normalizeConfigPath_` operation.
	     * @param rawPath Value passed to the method.
	     * @return The requested normalize Config Path.
	     */
	    static SwString normalizeConfigPath_(const SwString& rawPath) {
	        SwString path = rawPath.trimmed();
	        path.replace("\\", "/");
	        while (path.contains("//")) path.replace("//", "/");
	        while (path.startsWith("/")) path = path.mid(1);
	        while (path.endsWith("/")) path = path.left(static_cast<int>(path.size()) - 1);
	        return path;
	    }
	
	    /**
	     * @brief Returns the current user Meta Root Key.
	     * @return The current user Meta Root Key.
	     *
	     * @details The returned value reflects the state currently stored by the instance.
	     */
	    static const char* userMetaRootKey_() { return "__swconfig__"; }
	    /**
	     * @brief Returns the current user Meta Touched Key.
	     * @return The current user Meta Touched Key.
	     *
	     * @details The returned value reflects the state currently stored by the instance.
	     */
	    static const char* userMetaTouchedKey_() { return "touched"; }
	    /**
	     * @brief Returns the current user Meta Version.
	     * @return The current user Meta Version.
	     *
	     * @details The returned value reflects the state currently stored by the instance.
	     */
	    static int userMetaVersion_() { return 1; }
	
	    /**
	     * @brief Performs the `mergeTouchedPathsFromMeta_` operation.
	     * @param metaVal Value passed to the method.
	     * @param inOutTouched Value passed to the method.
	     * @return The requested merge Touched Paths From Meta.
	     */
	    static void mergeTouchedPathsFromMeta_(const SwJsonValue& metaVal, SwMap<SwString, bool>& inOutTouched) {
	        if (metaVal.isObject()) {
	            const SwJsonObject metaObj(metaVal.toObject());
	            mergeTouchedPathsFromMeta_(metaObj[userMetaTouchedKey_()], inOutTouched);
	            return;
	        }
	        if (metaVal.isArray()) {
	            const SwJsonArray arr = metaVal.toArray();
	            for (size_t i = 0; i < arr.size(); ++i) {
	                const SwJsonValue v = arr[i];
	                if (!v.isString()) continue;
	                const SwString p(v.toString());
	                const SwString norm = normalizeConfigPath_(p);
	                if (!norm.isEmpty()) inOutTouched.insert(norm, true);
	            }
	        }
	    }
	
	    /**
	     * @brief Performs the `stripTouchedMetaFromDoc_` operation.
	     * @param doc Value passed to the method.
	     * @param inOutTouched Value passed to the method.
	     * @return The requested strip Touched Meta From Doc.
	     */
	    static void stripTouchedMetaFromDoc_(SwJsonDocument& doc, SwMap<SwString, bool>* inOutTouched) {
	        if (!doc.isObject()) return;
	        SwJsonObject root = doc.object();
	        if (!root.contains(userMetaRootKey_())) return;
	
	        const SwJsonValue metaVal = root[userMetaRootKey_()];
	        if (inOutTouched) mergeTouchedPathsFromMeta_(metaVal, *inOutTouched);
	
	        root.remove(std::string(userMetaRootKey_()));
	        doc.setObject(root);
	    }
	
	    /**
	     * @brief Performs the `collectLeafConfigPaths_` operation.
	     * @param v Value passed to the method.
	     * @param pathPrefix Value passed to the method.
	     * @param outTouched Output value filled by the method.
	     * @return The requested collect Leaf Config Paths.
	     */
	    static void collectLeafConfigPaths_(const SwJsonValue& v,
	                                       const SwString& pathPrefix,
	                                       SwMap<SwString, bool>& outTouched) {
	        if (v.isObject()) {
	            const SwJsonObject obj = v.toObject();
	            for (auto it = obj.begin(); it != obj.end(); ++it) {
	                const SwString key = it.key();
	                if (key == SwString(userMetaRootKey_())) continue;
	                const SwString next = pathPrefix.isEmpty() ? key : (pathPrefix + "/" + key);
	                collectLeafConfigPaths_(it.value(), next, outTouched);
	            }
	            return;
	        }
	        if (pathPrefix.isEmpty()) return;
	        outTouched.insert(normalizeConfigPath_(pathPrefix), true);
	    }

	    /**
	     * @brief Returns whether the object reports keep Path.
	     * @param keepPaths Value passed to the method.
	     * @param rawPath Value passed to the method.
	     * @return The requested keep Path.
	     *
	     * @details This query does not modify the object state.
	     */
	    static bool shouldKeepPath_(const SwMap<SwString, bool>* keepPaths, const SwString& rawPath) {
	        if (!keepPaths) return false;
	        const SwString path = normalizeConfigPath_(rawPath);
	        if (path.isEmpty()) return false;
	        return keepPaths->contains(path);
	    }
	
	    /**
	     * @brief Performs the `markConfigPathTouched_locked_` operation.
	     * @param rawPath Value passed to the method.
	     */
	    void markConfigPathTouched_locked_(const SwString& rawPath) {
	        const SwString path = normalizeConfigPath_(rawPath);
	        if (path.isEmpty()) return;
	        userTouchedPaths_.insert(path, true);
	    }
	
	    /**
	     * @brief Performs the `injectTouchedMeta_locked_` operation.
	     * @param doc Value passed to the method.
	     */
	    void injectTouchedMeta_locked_(SwJsonDocument& doc) const {
	        ensureObjectRoot(doc);
	        SwJsonObject root = doc.object();
	
	        SwJsonObject meta;
	        meta["v"] = SwJsonValue(userMetaVersion_());
	
	        SwJsonArray touched;
	        for (auto it = userTouchedPaths_.begin(); it != userTouchedPaths_.end(); ++it) {
	            const SwString path = normalizeConfigPath_(it.key());
	            if (path.isEmpty()) continue;
	            touched.append(SwJsonValue(path.toStdString()));
	        }
	        meta[userMetaTouchedKey_()] = SwJsonValue(touched);
	
	        root[userMetaRootKey_()] = SwJsonValue(meta);
	        doc.setObject(root);
	    }

	    // Build a minimal "user override" JSON value by removing entries equal to the base value,
	    // except for paths present in `keepPaths` (sticky persistence: keep keys that were set at least once).
	    // Returns false if the candidate is entirely redundant (no entries remain).
	    /**
	     * @brief Builds the user Override Value requested by the caller.
	     * @param candidate Value passed to the method.
	     * @param base Value passed to the method.
	     * @param outOverride Output value filled by the method.
	     * @param keepPaths Value passed to the method.
	     * @param pathPrefix Value passed to the method.
	     * @return The resulting user Override Value.
	     */
	    static bool buildUserOverrideValue_(const SwJsonValue& candidate,
	                                        const SwJsonValue& base,
	                                        SwJsonValue& outOverride,
	                                        const SwMap<SwString, bool>* keepPaths = nullptr,
	                                        const SwString& pathPrefix = SwString()) {
	        if (candidate.isObject()) {
	            const SwJsonObject candidateObj = candidate.toObject();
	            if (!pathPrefix.isEmpty() && shouldKeepPath_(keepPaths, pathPrefix)) {
	                outOverride = candidate;
	                return true;
	            }
	            if (base.isObject()) {
	                const SwJsonObject baseObj = base.toObject();
	                SwJsonObject outObj;
	                for (auto it = candidateObj.begin(); it != candidateObj.end(); ++it) {
	                    const SwString key = it.key();
	                    const SwJsonValue& candidateChild = it.value();
	                    const SwJsonValue baseChild =
	                        baseObj.contains(key) ? baseObj[key] : SwJsonValue();
	
	                    SwJsonValue prunedChild;
	                    const SwString childPath = pathPrefix.isEmpty() ? key : (pathPrefix + "/" + key);
	                    if (buildUserOverrideValue_(candidateChild, baseChild, prunedChild, keepPaths, childPath)) {
	                        outObj.insert(key.toStdString(), prunedChild);
	                    }
	                }
	                if (outObj.isEmpty()) return false;
	                outOverride = SwJsonValue(outObj);
	                return true;
	            }
	
	            // Base is not an object: keep the whole object (it can't be reduced further vs a primitive base).
	            outOverride = candidate;
	            return true;
	        }
	
	        if (candidate.isArray()) {
	            if (jsonDeepEqual_(candidate, base) && !shouldKeepPath_(keepPaths, pathPrefix)) return false;
	            outOverride = candidate;
	            return true;
	        }
	
	        if (jsonDeepEqual_(candidate, base) && !shouldKeepPath_(keepPaths, pathPrefix)) return false;
	        outOverride = candidate;
	        return true;
	    }
	
	    /**
	     * @brief Performs the `effectiveConfigJson_locked_` operation.
	     * @param format Value passed to the method.
	     * @return The requested effective Config Json locked.
	     */
	    SwString effectiveConfigJson_locked_(SwJsonDocument::JsonFormat format) const {
	        SwJsonObject effective = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
	        SwJsonDocument doc(effective);
	
	        for (auto it = registeredConfigs_.begin(); it != registeredConfigs_.end(); ++it) {
	            const RegisteredConfigEntry& entry = it.value();
	            SwJsonValue exists;
	            if (!tryFindValueNoLog_(doc.toJsonValue(), entry.configName, exists)) {
	                doc.find(entry.configName, /*createIfNotExist=*/true) = entry.defaultJson;
	            }
	        }
	
	        injectTouchedMeta_locked_(doc);
	
	        return doc.toJson(format);
	    }
	
	    /**
	     * @brief Returns the current baseline Config Value locked.
	     * @return The current baseline Config Value locked.
	     *
	     * @details The returned value reflects the state currently stored by the instance.
	     */
	    SwJsonValue baselineConfigValue_locked_() const {
	        SwJsonObject base;
	        if (globalDoc_.isObject()) mergeObjectDeep_(base, globalDoc_.object());
	        if (localDoc_.isObject())  mergeObjectDeep_(base, localDoc_.object());
	
	        SwJsonDocument baselineDoc(base);
	        for (auto it = registeredConfigs_.begin(); it != registeredConfigs_.end(); ++it) {
	            const RegisteredConfigEntry& entry = it.value();
	            SwJsonValue exists;
	            if (!tryFindValueNoLog_(baselineDoc.toJsonValue(), entry.configName, exists)) {
	                baselineDoc.find(entry.configName, /*createMissing=*/true) = entry.defaultJson;
	            }
	        }
	        return baselineDoc.toJsonValue();
	    }

	    /**
	     * @brief Performs the `tryFindValueNoLog_` operation.
	     * @param root Value passed to the method.
	     * @param rawPath Value passed to the method.
	     * @param out Value passed to the method.
	     * @return The requested try Find Value No Log.
	     */
	    static bool tryFindValueNoLog_(const SwJsonValue& root, const SwString& rawPath, SwJsonValue& out) {
	        SwString path = rawPath;
	        path.replace("\\", "/");
	        SwList<SwString> tokens = path.split('/');

        SwJsonValue current = root;
        for (int i = 0; i < tokens.size(); ++i) {
            const SwString token = tokens[i];
            if (token.isEmpty()) continue;
            if (!current.isObject()) return false;
            SwJsonObject obj = current.toObject();
            if (!obj.contains(token)) return false;
            current = obj[token];
        }
        out = current;
        return true;
    }

    /**
     * @brief Performs the `loadDocFromFile_locked` operation on the associated resource.
     * @param path Path used by the operation.
     * @param outDoc Output value filled by the method.
     * @return `true` on success; otherwise `false`.
     */
    bool loadDocFromFile_locked(const SwString& path, SwJsonDocument& outDoc) const {
        if (!SwFile::isFile(path)) return false;
        SwFile f(path);
        if (!f.open(SwFile::Read)) return false;
        const SwString raw = f.readAll();
        f.close();

        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(raw.toStdString(), err) || !d.isObject()) {
            return false;
        }
        outDoc = d;
        return true;
    }

    /**
     * @brief Performs the `writeDocToFile_locked` operation on the associated resource.
     * @param doc Value passed to the method.
     * @param path Path used by the operation.
     * @param pretty Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool writeDocToFile_locked(const SwJsonDocument& doc, const SwString& path, bool pretty) const {
        SwJsonDocument toWrite = doc;
        ensureObjectRoot(toWrite);
        SwFile f(path);
        SwDir::mkpathAbsolute(f.getDirectory(), /*normalizeInput=*/false);
        if (!f.open(SwFile::Write)) return false;
        const SwString json = toWrite.toJson(pretty ? SwJsonDocument::JsonFormat::Pretty
                                                    : SwJsonDocument::JsonFormat::Compact);
        const bool ok = f.write(json + "\n");
        f.close();
        return ok;
    }
	
	    /**
	     * @brief Performs the `writeUserDocToFile_locked_` operation on the associated resource.
	     * @param pretty Value passed to the method.
	     * @return `true` on success; otherwise `false`.
	     */
	    bool writeUserDocToFile_locked_(bool pretty) {
	        SwJsonDocument toWrite = userDoc_;
	        ensureObjectRoot(toWrite);
	        stripTouchedMetaFromDoc_(toWrite, /*inOutTouched=*/nullptr);
	        return writeDocToFile_locked(toWrite, userConfigPath(), pretty);
	    }

    /**
     * @brief Performs the `recomputeMerged_locked` operation.
     */
    void recomputeMerged_locked() {
        SwJsonObject merged;
        if (globalDoc_.isObject()) mergeObjectDeep_(merged, globalDoc_.object());
        if (localDoc_.isObject())  mergeObjectDeep_(merged, localDoc_.object());
        if (userDoc_.isObject())   mergeObjectDeep_(merged, userDoc_.object());
        mergedDoc_.setObject(merged);
    }

    /**
     * @brief Returns the current config locked.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool loadConfig_locked() {
        globalDoc_ = SwJsonDocument(SwJsonObject{});
        localDoc_  = SwJsonDocument(SwJsonObject{});
        userDoc_   = SwJsonDocument(SwJsonObject{});
	        userTouchedPaths_.clear();

        SwJsonDocument tmp;
        if (loadDocFromFile_locked(globalConfigPath(), tmp)) {
            globalDoc_ = tmp;
        }

        if (loadDocFromFile_locked(localConfigPath(), tmp)) {
            localDoc_ = tmp;
        }

        auto adoptUserDoc = [this](const SwJsonDocument& doc) {
            userDoc_ = doc;
            ensureObjectRoot(userDoc_);
            stripTouchedMetaFromDoc_(userDoc_, &userTouchedPaths_);
            collectLeafConfigPaths_(userDoc_.toJsonValue(), SwString(), userTouchedPaths_);
        };
        if (loadDocFromFile_locked(userConfigPath(), tmp)) {
            adoptUserDoc(tmp);
        }
        recomputeMerged_locked();
        return true;
    }

	    /**
	     * @brief Performs the `applyRemoteConfig` operation.
	     * @param remotePublisherId Value passed to the method.
	     * @param json Value passed to the method.
	     */
	    void applyRemoteConfig(uint64_t remotePublisherId, const SwString& json) {
	        SwJsonObject mergedCopy;
	        SwList<std::function<void()>> pending;
	        bool changed = false;
	        {
	            SwMutexLocker lk(mutex_);
	            SwJsonDocument d;
	            SwString err;
	            if (!d.loadFromJson(json.toStdString(), err) || !d.isObject()) {
	                return;
	            }
	            const size_t touchedBefore = userTouchedPaths_.size();
	            stripTouchedMetaFromDoc_(d, &userTouchedPaths_);
	            const SwJsonValue baselineValue = baselineConfigValue_locked_();
	            SwJsonValue prunedUserValue;
	            SwJsonDocument nextUserDoc;
	            if (buildUserOverrideValue_(d.toJsonValue(), baselineValue, prunedUserValue, &userTouchedPaths_) &&
	                prunedUserValue.isObject()) {
	                nextUserDoc.setObject(prunedUserValue.toObject());
	            } else {
	                nextUserDoc.setObject(SwJsonObject{});
	            }
	            collectLeafConfigPaths_(nextUserDoc.toJsonValue(), SwString(), userTouchedPaths_);
	            const bool touchedChanged = (userTouchedPaths_.size() != touchedBefore);

	            if (userDoc_ != nextUserDoc) {
	                userDoc_ = nextUserDoc;
	                recomputeMerged_locked();
	                scheduleUserDocSave_locked_();
	                refreshRegisteredConfigs_locked_(pending);
	                mergedCopy = mergedDoc_.isObject() ? mergedDoc_.object() : SwJsonObject{};
	                changed = true;
	            } else if (touchedChanged) {
	                scheduleUserDocSave_locked_();
	            }
	        }
	        if (changed) {
	            for (size_t i = 0; i < pending.size(); ++i) pending[i]();
	            emit configChanged(mergedCopy);
	            emit remoteConfigReceived(remotePublisherId, mergedCopy);
	        }
	    }

    /**
     * @brief Starts the shm Config Subscription locked managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void startShmConfigSubscription_locked() {
        stopShmConfigSubscription_locked();
        std::shared_ptr<std::atomic_bool> alive = alive_;
        sw::ipc::detail::ScopedSubscriberObject subScope(ipcRegistry_.object());
        shmConfigSub_ = shmConfig_.connect([this, alive](uint64_t pubId, SwString json) {
            if (!alive || !alive->load(std::memory_order_relaxed)) return;
            if (pubId == publisherId_) return;

            ThreadHandle* targetThread = this->threadHandle();
            ThreadHandle* currentThread = ThreadHandle::currentThread();
            auto task = [this, alive, pubId, json]() mutable {
                if (!alive || !alive->load(std::memory_order_relaxed)) return;
                applyRemoteConfig(pubId, json);
            };

            if (!targetThread || targetThread == currentThread) {
                task();
            } else {
                targetThread->postTask(std::move(task));
            }
        }, /*fireInitial=*/true);
    }

    /**
     * @brief Stops the shm Config Subscription locked managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stopShmConfigSubscription_locked() {
        shmConfigSub_.stop();
    }

    /**
     * @brief Performs the `sanitizeNsForFile_` operation.
     * @param nsIn Value passed to the method.
     * @return The requested sanitize Ns For File.
     */
    static SwString sanitizeNsForFile_(const SwString& nsIn) {
        std::string s = nsIn.toStdString();
        for (size_t i = 0; i < s.size(); ++i) {
            const char c = s[i];
            if (c == '/' || c == '\\') s[i] = '_';
        }
        while (!s.empty() && (s.front() == '_' || s.front() == '/')) s.erase(0, 1);
        while (!s.empty() && (s.back() == '_' || s.back() == '/')) s.pop_back();
        if (s.empty()) s = "root";
        return sanitizeSegment(SwString(s));
    }

    /**
     * @brief Returns the current config Root Absolute.
     * @return The current config Root Absolute.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString configRootAbsolute_() const {
        SwString root = configRoot_.isEmpty() ? SwString("systemConfig") : configRoot_;
        const std::string s = root.toStdString();
        const bool isAbs = (!s.empty() && (s[0] == '/' || s[0] == '\\')) ||
                           (s.size() > 1 && s[1] == ':');
        if (isAbs) return root;
        return SwDir::currentPath() + root;
    }

protected:
    SwString sysName_;
    SwString nameSpace_;
    SwString configRoot_;

    // IPC registry for derived-class signals.
    sw::ipc::Registry ipcRegistry_;

 private:
    struct IIpcSubscription;

    class IpcConnection final : public SwObject {
     public:
        /**
         * @brief Destroys the `final` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        ~IpcConnection() override { stop(); }

        /**
         * @brief Stops the underlying activity managed by the object.
         *
         * @details The call affects the runtime state associated with the underlying resource or service.
         */
        void stop() {
            if (!sub_) return;
            sub_->stop();
            sub_.reset();
        }

     private:
        friend class SwRemoteObject;
        IpcConnection(SwObject* parent, std::shared_ptr<IIpcSubscription> sub)
            : SwObject(parent), sub_(std::move(sub)) {}

        std::shared_ptr<IIpcSubscription> sub_;
    };

    void stopAllIpcSubscriptions_() {
        SwMutexLocker lk(mutex_);
        for (auto it = ipcSubscriptions_.begin(); it != ipcSubscriptions_.end(); ++it) {
            if (it.value()) it.value()->stop();
        }
        ipcSubscriptions_.clear();
    }

    void refreshRegisteredConfigs_locked_(SwList<std::function<void()>>& pending) {
        for (auto it = registeredConfigs_.begin(); it != registeredConfigs_.end(); ++it) {
            RegisteredConfigEntry& entry = it.value();
            if (entry.onMergedChanged) entry.onMergedChanged(pending);
        }
    }

    // Debounced disk save for user config file (Pretty output is preserved).
    // This avoids rewriting JSON files repeatedly during bursts (ex: Web "Apply all").
    void scheduleUserDocSave_locked_() {
        userDocSavePending_ = true;

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            flushUserDocSave_locked_();
            return;
        }

        if (!userDocSaveTimer_) {
            userDocSaveTimer_ = new SwTimer(userDocSaveDebounceMs_, this);
            userDocSaveTimer_->setSingleShot(true);
            std::shared_ptr<std::atomic_bool> alive = alive_;
            userDocSaveTimer_->connect(userDocSaveTimer_, &SwTimer::timeout, [this, alive]() {
                if (!alive || !alive->load(std::memory_order_relaxed)) return;
                SwMutexLocker lk(mutex_);
                flushUserDocSave_locked_();
            });
        }

        userDocSaveTimer_->stop();
        userDocSaveTimer_->start(userDocSaveDebounceMs_);
    }

    void flushUserDocSave_locked_() {
        if (!userDocSavePending_) return;
        userDocSavePending_ = false;
        writeUserDocToFile_locked_(/*pretty=*/true);
        if (userDocSaveTimer_) userDocSaveTimer_->stop();
    }

	    struct RegisteredConfigEntry {
	        SwString configName;
	        SwString fullName;
	        SwJsonValue defaultJson;
	        /**
	         * @brief Performs the `function<void` operation.
	         * @return The requested function<void.
	         */
	        std::function<void(SwList<std::function<void()>>&)> onMergedChanged;
	        /**
	         * @brief Returns the current function<void.
	         * @return The current function<void.
	         *
	         * @details The returned value reflects the state currently stored by the instance.
	         */
	        std::function<void()> publish;
	    };

    struct IIpcSubscription {
        /**
         * @brief Destroys the `IIpcSubscription` instance.
         *
         * @details Use this hook to release any resources that remain associated with the instance.
         */
        virtual ~IIpcSubscription() {}
        /**
         * @brief Returns the current stop.
         * @return The current stop.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        virtual void stop() = 0;
    };

    template <typename SubT>
    struct IpcSubscriptionHolder : IIpcSubscription {
        SubT sub;
        /**
         * @brief Constructs a `IpcSubscriptionHolder` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit IpcSubscriptionHolder(SubT&& s) : sub(std::move(s)) {}
        /**
         * @brief Stops the underlying activity managed by the object.
         *
         * @details The call affects the runtime state associated with the underlying resource or service.
         */
        void stop() override { sub.stop(); }
    };

    template <typename Tuple>
    struct IpcConnectHelper_ {};

    template <typename... A>
    struct IpcConnectHelper_<std::tuple<A...>> {
        template <typename Self, typename Fn>
        /**
         * @brief Performs the `connect` operation.
         * @param self Value passed to the method.
         * @param fullName Value passed to the method.
         * @param cb Value passed to the method.
         * @param fireInitial Value passed to the method.
         * @return The requested connect.
         */
        static size_t connect(Self* self, const SwString& fullName, Fn cb, bool fireInitial) {
            SwString ns, obj, leaf;
            if (!self || !self->splitFullName_(fullName, ns, obj, leaf)) {
                return 0;
            }
            sw::ipc::Registry reg(ns, obj);
            sw::ipc::Signal<A...> sig(reg, leaf);
            sw::ipc::detail::ScopedSubscriberObject subScope(self->ipcRegistry_.object());
            std::shared_ptr<std::atomic_bool> alive = self ? self->alive_ : std::shared_ptr<std::atomic_bool>();
            auto wrapped = [self, alive, cb](A... args) mutable {
                if (!alive || !alive->load(std::memory_order_relaxed)) return;
                ThreadHandle* targetThread = self->threadHandle();
                ThreadHandle* currentThread = ThreadHandle::currentThread();
                auto task = [cb, args...]() mutable { cb(args...); };
                if (!targetThread || targetThread == currentThread) {
                    task();
                } else {
                    targetThread->postTask(std::move(task));
                }
            };
            auto sub = sig.connect(wrapped, fireInitial);
            return self->storeIpcSubscription_(std::move(sub));
        }
    };

    bool splitObjectFqn_(const SwString& objectFqn, SwString& nsOut, SwString& objOut) const {
        SwString x = objectFqn.trimmed();
        x.replace("\\", "/");
        while (x.contains("//")) x.replace("//", "/");
        const bool hadLeadingSlash = x.startsWith("/");
        while (x.startsWith("/")) x = x.mid(1);
        while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
        if (x.isEmpty()) return false;

        // Accept either:
        //   - full: "sysName/nameSpace/objectName"
        //   - abs:  "/nameSpace/objectName"
        //   - rel:  "objectName" (resolved as "<this->nameSpace()>/objectName")
        const std::string sysPrefix = (sysName_.isEmpty() ? std::string() : (sysName_.toStdString() + "/"));
        const std::string s = x.toStdString();
        const bool hadSysPrefix = (!sysPrefix.empty() && s.rfind(sysPrefix, 0) == 0);

        nsOut = sysName_;
        SwString objPart = hadSysPrefix ? SwString(s.substr(sysPrefix.size())) : x;
        if (objPart.isEmpty()) return false;

        if (!hadLeadingSlash && !hadSysPrefix) {
            const std::string objStd = objPart.toStdString();
            if (objStd.find('/') == std::string::npos) {
                objPart = buildObjectFqn(nameSpace_, objPart);
            }
        }

        objOut = objPart;
        return !objOut.isEmpty();
    }

    template <typename Tuple>
    struct IpcConnectScopedHelper_ {};

    template <typename... A>
    struct IpcConnectScopedHelper_<std::tuple<A...>> {
        template <typename Self, typename Fn>
        /**
         * @brief Performs the `connect` operation.
         * @param self Value passed to the method.
         * @param targetObject Value passed to the method.
         * @param leaf Value passed to the method.
         * @param context Value passed to the method.
         * @param cb Value passed to the method.
         * @param fireInitial Value passed to the method.
         * @return The requested connect.
         */
        static SwObject* connect(Self* self,
                                 const SwString& targetObject,
                                 const SwString& leaf,
                                 SwObject* context,
                                 Fn cb,
                                 bool fireInitial) {
            if (!context) return nullptr;
            SwString ns, obj;
            if (!self || !self->splitObjectFqn_(targetObject, ns, obj)) {
                return nullptr;
            }
            sw::ipc::Registry reg(ns, obj);
            sw::ipc::Signal<A...> sig(reg, leaf);
            sw::ipc::detail::ScopedSubscriberObject subScope(self->ipcRegistry_.object());
            auto wrapped = [context, cb](A... args) mutable {
                if (!context) return;
                ThreadHandle* targetThread = context->threadHandle();
                ThreadHandle* currentThread = ThreadHandle::currentThread();
                auto task = [cb, args...]() mutable { cb(args...); };
                if (!targetThread || targetThread == currentThread) {
                    task();
                } else {
                    targetThread->postTask(std::move(task));
                }
            };
            auto sub = sig.connect(wrapped, fireInitial);
            return self->createIpcConnection_(context, std::move(sub));
        }
    };

    template <typename Tuple, typename Ret>
    struct IpcExposeRpcHelper_ {};

    template <typename Ret, typename... A>
    struct IpcExposeRpcHelper_<std::tuple<A...>, Ret> {
        template <typename Self, typename Fn>
        /**
         * @brief Performs the `expose` operation.
         * @param self Value passed to the method.
         * @param methodName Value passed to the method.
         * @param handler Value passed to the method.
         * @param fireInitial Value passed to the method.
         * @return The requested expose.
         */
        static size_t expose(Self* self, const SwString& methodName, Fn handler, bool fireInitial) {
            return self->template ipcExposeRpcNoCtx_<Ret, A...>(methodName, handler, fireInitial);
        }
    };

    template <typename Ret, typename... A>
    struct IpcExposeRpcHelper_<std::tuple<sw::ipc::RpcContext, A...>, Ret> {
        template <typename Self, typename Fn>
        /**
         * @brief Performs the `expose` operation.
         * @param self Value passed to the method.
         * @param methodName Value passed to the method.
         * @param handler Value passed to the method.
         * @param fireInitial Value passed to the method.
         * @return The requested expose.
         */
        static size_t expose(Self* self, const SwString& methodName, Fn handler, bool fireInitial) {
            return self->template ipcExposeRpcWithCtx_<Ret, A...>(methodName, handler, fireInitial);
        }
    };

    template <size_t Capacity, typename Ret>
    typename std::enable_if<!std::is_void<Ret>::value, void>::type
    rpcRespondValueCap_(const SwString& methodName,
                        uint64_t callId,
                        uint32_t clientPid,
                        bool ok,
                        const SwString& err,
                        const Ret& value) {
        typedef sw::ipc::RingQueue<Capacity, uint64_t, bool, SwString, Ret> RespQueue;
        const SwString queueName = sw::ipc::rpcResponseQueueName(methodName, clientPid);

        std::shared_ptr<void> holder;
        {
            SwMutexLocker lk(rpcRespMutex_);
            auto it = rpcRespValueQueues_.find(queueName);
            if (it != rpcRespValueQueues_.end()) {
                holder = it.value();
            } else {
                std::shared_ptr<RespQueue> q(new RespQueue(ipcRegistry_, queueName));
                holder = q;
                rpcRespValueQueues_[queueName] = holder;
            }
        }

        RespQueue* resp = static_cast<RespQueue*>(holder.get());
        if (!resp) return;
        (void)resp->push(callId, ok, err, value);
    }

    template <typename Ret>
    typename std::enable_if<!std::is_void<Ret>::value, void>::type
    rpcRespondValue_(const SwString& methodName,
                     uint64_t callId,
                     uint32_t clientPid,
                     bool ok,
                     const SwString& err,
                     const Ret& value) {
        switch (sw::ipc::rpcQueueCapacity()) {
            case 10u:  rpcRespondValueCap_<10, Ret>(methodName, callId, clientPid, ok, err, value); break;
            case 25u:  rpcRespondValueCap_<25, Ret>(methodName, callId, clientPid, ok, err, value); break;
            case 50u:  rpcRespondValueCap_<50, Ret>(methodName, callId, clientPid, ok, err, value); break;
            case 100u: rpcRespondValueCap_<100, Ret>(methodName, callId, clientPid, ok, err, value); break;
            case 200u: rpcRespondValueCap_<200, Ret>(methodName, callId, clientPid, ok, err, value); break;
            case 500u: rpcRespondValueCap_<500, Ret>(methodName, callId, clientPid, ok, err, value); break;
            default:   rpcRespondValueCap_<100, Ret>(methodName, callId, clientPid, ok, err, value); break;
        }
    }

    template <size_t Capacity, typename Ret>
    typename std::enable_if<std::is_void<Ret>::value, void>::type
    rpcRespondVoidCap_(const SwString& methodName,
                       uint64_t callId,
                       uint32_t clientPid,
                       bool ok,
                       const SwString& err) {
        typedef sw::ipc::RingQueue<Capacity, uint64_t, bool, SwString> RespQueue;
        const SwString queueName = sw::ipc::rpcResponseQueueName(methodName, clientPid);

        std::shared_ptr<void> holder;
        {
            SwMutexLocker lk(rpcRespMutex_);
            auto it = rpcRespVoidQueues_.find(queueName);
            if (it != rpcRespVoidQueues_.end()) {
                holder = it.value();
            } else {
                std::shared_ptr<RespQueue> q(new RespQueue(ipcRegistry_, queueName));
                holder = q;
                rpcRespVoidQueues_[queueName] = holder;
            }
        }

        RespQueue* resp = static_cast<RespQueue*>(holder.get());
        if (!resp) return;
        (void)resp->push(callId, ok, err);
    }

    template <typename Ret>
    typename std::enable_if<std::is_void<Ret>::value, void>::type
    rpcRespondVoid_(const SwString& methodName,
                    uint64_t callId,
                    uint32_t clientPid,
                    bool ok,
                    const SwString& err) {
        switch (sw::ipc::rpcQueueCapacity()) {
            case 10u:  rpcRespondVoidCap_<10, Ret>(methodName, callId, clientPid, ok, err); break;
            case 25u:  rpcRespondVoidCap_<25, Ret>(methodName, callId, clientPid, ok, err); break;
            case 50u:  rpcRespondVoidCap_<50, Ret>(methodName, callId, clientPid, ok, err); break;
            case 100u: rpcRespondVoidCap_<100, Ret>(methodName, callId, clientPid, ok, err); break;
            case 200u: rpcRespondVoidCap_<200, Ret>(methodName, callId, clientPid, ok, err); break;
            case 500u: rpcRespondVoidCap_<500, Ret>(methodName, callId, clientPid, ok, err); break;
            default:   rpcRespondVoidCap_<100, Ret>(methodName, callId, clientPid, ok, err); break;
        }
    }

    template <typename Ret, typename Handler, typename... A>
    typename std::enable_if<!std::is_void<Ret>::value, void>::type
    rpcInvokeNoCtx_(const Handler& handlerFn,
                    const SwString& methodName,
                    uint64_t callId,
                    uint32_t clientPid,
                    const SwString& /*clientInfo*/,
                    const A&... args) {
        const Ret out = handlerFn(args...);
        rpcRespondValue_<Ret>(methodName, callId, clientPid, /*ok=*/true, SwString(), out);
    }

    template <typename Ret, typename Handler, typename... A>
    typename std::enable_if<std::is_void<Ret>::value, void>::type
    rpcInvokeNoCtx_(const Handler& handlerFn,
                    const SwString& methodName,
                    uint64_t callId,
                    uint32_t clientPid,
                    const SwString& /*clientInfo*/,
                    const A&... args) {
        handlerFn(args...);
        rpcRespondVoid_<Ret>(methodName, callId, clientPid, /*ok=*/true, SwString());
    }

    template <typename Ret, typename Handler, typename... A>
    typename std::enable_if<!std::is_void<Ret>::value, void>::type
    rpcInvokeWithCtx_(const Handler& handlerFn,
                      const SwString& methodName,
                      uint64_t callId,
                      uint32_t clientPid,
                      const SwString& clientInfo,
                      const A&... args) {
        sw::ipc::RpcContext ctx;
        ctx.clientPid = clientPid;
        ctx.clientInfo = clientInfo;
        const Ret out = handlerFn(ctx, args...);
        rpcRespondValue_<Ret>(methodName, callId, clientPid, /*ok=*/true, SwString(), out);
    }

    template <typename Ret, typename Handler, typename... A>
    typename std::enable_if<std::is_void<Ret>::value, void>::type
    rpcInvokeWithCtx_(const Handler& handlerFn,
                      const SwString& methodName,
                      uint64_t callId,
                      uint32_t clientPid,
                      const SwString& clientInfo,
                      const A&... args) {
        sw::ipc::RpcContext ctx;
        ctx.clientPid = clientPid;
        ctx.clientInfo = clientInfo;
        handlerFn(ctx, args...);
        rpcRespondVoid_<Ret>(methodName, callId, clientPid, /*ok=*/true, SwString());
    }

    template <size_t Capacity, typename Ret, typename... A, typename Fn>
    size_t ipcExposeRpcNoCtxCap_(const SwString& methodName, Fn handler, bool fireInitial) {
        std::function<Ret(A...)> fn(handler);
        sw::ipc::RingQueue<Capacity, uint64_t, uint32_t, SwString, A...> req(
            ipcRegistry_, sw::ipc::rpcRequestQueueName(methodName));
        std::shared_ptr<std::atomic_bool> alive = alive_;
        const SwString methodCopy = methodName;

        auto sub = req.connect([this, alive, fn, methodCopy](uint64_t callId,
                                                            uint32_t clientPid,
                                                            SwString clientInfo,
                                                            A... args) {
            if (!alive || !alive->load(std::memory_order_relaxed)) return;
            ThreadHandle* targetThread = this->threadHandle();
            ThreadHandle* currentThread = ThreadHandle::currentThread();
            auto task = [this, alive, fn, methodCopy, callId, clientPid, clientInfo, args...]() mutable {
                if (!alive || !alive->load(std::memory_order_relaxed)) return;
                this->template rpcInvokeNoCtx_<Ret>(fn, methodCopy, callId, clientPid, clientInfo, args...);
            };
            if (!targetThread || targetThread == currentThread) {
                task();
            } else {
                targetThread->postTask(std::move(task));
            }
        }, fireInitial);
        return storeIpcSubscription_(std::move(sub));
    }

    template <typename Ret, typename... A, typename Fn>
    size_t ipcExposeRpcNoCtx_(const SwString& methodName, Fn handler, bool fireInitial) {
        switch (sw::ipc::rpcQueueCapacity()) {
            case 10u:  return ipcExposeRpcNoCtxCap_<10, Ret, A...>(methodName, handler, fireInitial);
            case 25u:  return ipcExposeRpcNoCtxCap_<25, Ret, A...>(methodName, handler, fireInitial);
            case 50u:  return ipcExposeRpcNoCtxCap_<50, Ret, A...>(methodName, handler, fireInitial);
            case 100u: return ipcExposeRpcNoCtxCap_<100, Ret, A...>(methodName, handler, fireInitial);
            case 200u: return ipcExposeRpcNoCtxCap_<200, Ret, A...>(methodName, handler, fireInitial);
            case 500u: return ipcExposeRpcNoCtxCap_<500, Ret, A...>(methodName, handler, fireInitial);
            default:   return ipcExposeRpcNoCtxCap_<100, Ret, A...>(methodName, handler, fireInitial);
        }
    }

    template <size_t Capacity, typename Ret, typename... A, typename Fn>
    size_t ipcExposeRpcWithCtxCap_(const SwString& methodName, Fn handler, bool fireInitial) {
        std::function<Ret(sw::ipc::RpcContext, A...)> fn(handler);
        sw::ipc::RingQueue<Capacity, uint64_t, uint32_t, SwString, A...> req(
            ipcRegistry_, sw::ipc::rpcRequestQueueName(methodName));
        std::shared_ptr<std::atomic_bool> alive = alive_;
        const SwString methodCopy = methodName;

        auto sub = req.connect([this, alive, fn, methodCopy](uint64_t callId,
                                                            uint32_t clientPid,
                                                            SwString clientInfo,
                                                            A... args) {
            if (!alive || !alive->load(std::memory_order_relaxed)) return;
            ThreadHandle* targetThread = this->threadHandle();
            ThreadHandle* currentThread = ThreadHandle::currentThread();
            auto task = [this, alive, fn, methodCopy, callId, clientPid, clientInfo, args...]() mutable {
                if (!alive || !alive->load(std::memory_order_relaxed)) return;
                this->template rpcInvokeWithCtx_<Ret>(fn, methodCopy, callId, clientPid, clientInfo, args...);
            };
            if (!targetThread || targetThread == currentThread) {
                task();
            } else {
                targetThread->postTask(std::move(task));
            }
        }, fireInitial);
        return storeIpcSubscription_(std::move(sub));
    }

    template <typename Ret, typename... A, typename Fn>
    size_t ipcExposeRpcWithCtx_(const SwString& methodName, Fn handler, bool fireInitial) {
        switch (sw::ipc::rpcQueueCapacity()) {
            case 10u:  return ipcExposeRpcWithCtxCap_<10, Ret, A...>(methodName, handler, fireInitial);
            case 25u:  return ipcExposeRpcWithCtxCap_<25, Ret, A...>(methodName, handler, fireInitial);
            case 50u:  return ipcExposeRpcWithCtxCap_<50, Ret, A...>(methodName, handler, fireInitial);
            case 100u: return ipcExposeRpcWithCtxCap_<100, Ret, A...>(methodName, handler, fireInitial);
            case 200u: return ipcExposeRpcWithCtxCap_<200, Ret, A...>(methodName, handler, fireInitial);
            case 500u: return ipcExposeRpcWithCtxCap_<500, Ret, A...>(methodName, handler, fireInitial);
            default:   return ipcExposeRpcWithCtxCap_<100, Ret, A...>(methodName, handler, fireInitial);
        }
    }

    template <typename SubT>
    size_t storeIpcSubscription_(SubT&& sub) {
        SwMutexLocker lk(mutex_);
        const size_t token = nextIpcToken_++;
        ipcSubscriptions_[token] = std::shared_ptr<IIpcSubscription>(
             new IpcSubscriptionHolder<typename std::decay<SubT>::type>(std::forward<SubT>(sub)));
        return token;
    }

    template <typename SubT>
    SwObject* createIpcConnection_(SwObject* context, SubT&& sub) {
        std::shared_ptr<IIpcSubscription> holder(
            new IpcSubscriptionHolder<typename std::decay<SubT>::type>(std::forward<SubT>(sub)));
        return new IpcConnection(context, std::move(holder));
    }

    std::shared_ptr<sw::ipc::Signal<uint64_t, SwString>> ensureConfigSignal_(const SwString& signalName) {
        auto it = configSignals_.find(signalName);
        if (it != configSignals_.end() && it->second) return it->second;
        std::shared_ptr<sw::ipc::Signal<uint64_t, SwString>> sig(
            new sw::ipc::Signal<uint64_t, SwString>(ipcRegistry_, signalName));
        configSignals_[signalName] = sig;
        return sig;
    }

    template <typename T>
    static SwJsonValue valueToJson_(const T& v) {
        return SwJsonValue(valueToString_<T>(v).toStdString());
    }

    static SwJsonValue valueToJson_(const SwString& v) { return SwJsonValue(v.toStdString()); }
    static SwJsonValue valueToJson_(const bool& v) { return SwJsonValue(v); }
    static SwJsonValue valueToJson_(const int& v) { return SwJsonValue(v); }
    static SwJsonValue valueToJson_(const float& v) { return SwJsonValue(static_cast<double>(v)); }
    static SwJsonValue valueToJson_(const double& v) { return SwJsonValue(v); }

    static SwJsonValue valueToJson_(const SwAny& v) {
        const std::string& tn = v.typeName();
        if (tn == typeid(bool).name()) return SwJsonValue(v.get<bool>());
        if (tn == typeid(int).name()) return SwJsonValue(v.get<int>());
        if (tn == typeid(float).name()) return SwJsonValue(static_cast<double>(v.get<float>()));
        if (tn == typeid(double).name()) return SwJsonValue(v.get<double>());
        if (tn == typeid(SwString).name()) return SwJsonValue(v.get<SwString>().toStdString());
        if (tn == typeid(SwJsonValue).name()) return v.get<SwJsonValue>();
        if (tn == typeid(SwJsonObject).name()) return SwJsonValue(v.get<SwJsonObject>());
        if (tn == typeid(SwJsonArray).name()) return SwJsonValue(v.get<SwJsonArray>());
        if (tn == typeid(SwJsonDocument).name()) return v.get<SwJsonDocument>().toJsonValue();

        const SwJsonValue j = v.toJsonValue();
        if (!j.isNull()) return j;
        const SwString s = v.toString();
        if (!s.isEmpty()) return SwJsonValue(s.toStdString());
        return SwJsonValue();
    }

    template <typename T>
    static SwJsonValue valueToJson_(const SwList<T>& v) {
        SwJsonArray a;
        for (size_t i = 0; i < v.size(); ++i) {
            a.append(valueToJson_(v[i]));
        }
        return SwJsonValue(a);
    }

    template <typename V>
    static SwJsonValue valueToJson_(const SwMap<SwString, V>& v) {
        SwJsonObject o;
        for (auto it = v.begin(); it != v.end(); ++it) {
            o[it.key()] = valueToJson_(it.value());
        }
        return SwJsonValue(o);
    }

    template <typename T>
    static SwString valueToString_(const T& v) {
        SwAny a = SwAny::from<T>(v);
        SwAny b = a.convert<SwString>();
        return b.get<SwString>();
    }

    static SwString valueToString_(const SwAny& v) {
        SwJsonDocument d;
        const SwJsonValue j = valueToJson_(v);
        if (j.isObject()) d.setObject(j.toObject());
        else if (j.isArray()) d.setArray(j.toArray());
        else if (j.isString()) return SwString(j.toString());
        else if (j.isBool()) return j.toBool() ? SwString("true") : SwString("false");
        else if (j.isInt()) return SwString::number(j.toLongLong());
        else if (j.isDouble()) return SwString::number(j.toDouble());
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    template <typename T>
    static SwString valueToString_(const SwList<T>& v) {
        SwJsonDocument d;
        const SwJsonValue j = valueToJson_(v);
        if (j.isArray()) d.setArray(j.toArray());
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    template <typename V>
    static SwString valueToString_(const SwMap<SwString, V>& v) {
        SwJsonDocument d;
        const SwJsonValue j = valueToJson_(v);
        if (j.isObject()) d.setObject(j.toObject());
        return d.toJson(SwJsonDocument::JsonFormat::Compact);
    }

    template <typename T>
    static bool stringToValue_(const SwString& s, T& out) {
        try {
            SwAny a(s);
            SwAny b = a.convert<T>();
            out = b.get<T>();
            return true;
        } catch (...) {
            return false;
        }
    }

    static bool stringToValue_(const SwString& s, SwString& out) { out = s; return true; }

    static bool stringToValue_(const SwString& s, SwAny& out) {
        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(s.toStdString(), err)) {
            out = SwAny(s);
            return true;
        }
        const SwJsonValue j = d.toJsonValue();
        if (j.isBool()) { out = SwAny(j.toBool()); return true; }
        if (j.isInt()) { out = SwAny(static_cast<long long>(j.toLongLong())); return true; }
        if (j.isDouble()) { out = SwAny(j.toDouble()); return true; }
        if (j.isString()) { out = SwAny(SwString(j.toString())); return true; }
        out = SwAny::from<SwJsonValue>(j);
        return true;
    }

    template <typename T>
    static bool stringToValue_(const SwString& s, SwList<T>& out) {
        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(s.toStdString(), err) || !d.isArray()) return false;
        return jsonToValue_(d.toJsonValue(), out);
    }

    template <typename V>
    static bool stringToValue_(const SwString& s, SwMap<SwString, V>& out) {
        SwJsonDocument d;
        SwString err;
        if (!d.loadFromJson(s.toStdString(), err) || !d.isObject()) return false;
        return jsonToValue_(d.toJsonValue(), out);
    }

    template <typename T>
    static bool jsonToValue_(const SwJsonValue& j, T& out) {
        if (!j.isString()) return false;
        return stringToValue_<T>(SwString(j.toString()), out);
    }

    static bool jsonToValue_(const SwJsonValue& j, SwAny& out) {
        if (j.isNull()) { out = SwAny(); return true; }
        if (j.isBool()) { out = SwAny(j.toBool()); return true; }
        if (j.isInt()) { out = SwAny(static_cast<long long>(j.toLongLong())); return true; }
        if (j.isDouble()) { out = SwAny(j.toDouble()); return true; }
        if (j.isString()) { out = SwAny(SwString(j.toString())); return true; }
        out = SwAny::from<SwJsonValue>(j);
        return true;
    }

    template <typename T>
    static bool jsonToValue_(const SwJsonValue& j, SwList<T>& out) {
        if (j.isString()) return stringToValue_(SwString(j.toString()), out);
        if (!j.isArray()) return false;

        out.clear();
        const SwJsonArray a = j.toArray();
        for (size_t i = 0; i < a.size(); ++i) {
            T item{};
            if (!jsonToValue_(a[i], item)) return false;
            out.append(item);
        }
        return true;
    }

    template <typename V>
    static bool jsonToValue_(const SwJsonValue& j, SwMap<SwString, V>& out) {
        if (j.isString()) return stringToValue_(SwString(j.toString()), out);
        if (!j.isObject()) return false;

        out.clear();
        const SwJsonObject o = j.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            V vv{};
            if (!jsonToValue_(it.value(), vv)) return false;
            out.insert(it.key(), vv);
        }
        return true;
    }

    static bool jsonToValue_(const SwJsonValue& j, SwString& out) {
        if (!j.isString()) return false;
        out = SwString(j.toString());
        return true;
    }
    static bool jsonToValue_(const SwJsonValue& j, bool& out) {
        if (j.isBool()) { out = j.toBool(); return true; }
        if (j.isInt()) { out = j.toLongLong() != 0; return true; }
        if (j.isDouble()) { out = j.toDouble() != 0.0; return true; }
        if (j.isString()) { return stringToValue_<bool>(SwString(j.toString()), out); }
        return false;
    }
    static bool jsonToValue_(const SwJsonValue& j, int& out) {
        if (j.isInt()) { out = j.toInt(); return true; }
        if (j.isDouble()) { out = static_cast<int>(j.toDouble()); return true; }
        if (j.isBool()) { out = j.toBool() ? 1 : 0; return true; }
        if (j.isString()) { return stringToValue_<int>(SwString(j.toString()), out); }
        return false;
    }
    static bool jsonToValue_(const SwJsonValue& j, float& out) {
        if (j.isDouble()) { out = static_cast<float>(j.toDouble()); return true; }
        if (j.isInt()) { out = static_cast<float>(j.toLongLong()); return true; }
        if (j.isString()) { return stringToValue_<float>(SwString(j.toString()), out); }
        return false;
    }
    static bool jsonToValue_(const SwJsonValue& j, double& out) {
        if (j.isDouble()) { out = j.toDouble(); return true; }
        if (j.isInt()) { out = static_cast<double>(j.toLongLong()); return true; }
        if (j.isString()) { return stringToValue_<double>(SwString(j.toString()), out); }
        return false;
    }

    template <typename T>
    static T configValueFromDocs_(const SwJsonDocument& mergedDoc, const SwString& key, const T& defaultValue) {
        SwJsonValue j;
        if (!tryFindValueNoLog_(mergedDoc.toJsonValue(), key, j)) {
            return defaultValue;
        }
        T out = defaultValue;
        if (!jsonToValue_(j, out)) return defaultValue;
        return out;
    }

    static uint64_t makePublisherId_(const void* self) {
#ifdef _WIN32
        const uint64_t pid = static_cast<uint64_t>(::GetCurrentProcessId());
#else
        const uint64_t pid = static_cast<uint64_t>(::getpid());
#endif
        const uint64_t salt = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(self));
        return (pid << 32) ^ (salt * 0x9e3779b97f4a7c15ull);
    }

    mutable SwMutex mutex_;
    SwJsonDocument globalDoc_;
    SwJsonDocument localDoc_;
    SwJsonDocument userDoc_;
    SwJsonDocument mergedDoc_;
	
	    // Config paths that were explicitly set at least once (sticky persistence).
	    // Stored as normalized "a/b/c" paths (no leading/trailing '/').
	    SwMap<SwString, bool> userTouchedPaths_;

    bool shmConfigEnabled_{false};
    uint64_t publisherId_{0};

    sw::ipc::Signal<uint64_t, SwString> shmConfig_;
    sw::ipc::Signal<uint64_t, SwString>::Subscription shmConfigSub_;

    std::shared_ptr<std::atomic_bool> alive_;

    SwMap<SwString, RegisteredConfigEntry> registeredConfigs_; // key=fullName
    SwMap<SwString, std::shared_ptr<sw::ipc::Signal<uint64_t, SwString>>> configSignals_; // key=sigName

    size_t nextIpcToken_{1};
    SwMap<size_t, std::shared_ptr<IIpcSubscription>> ipcSubscriptions_;

    mutable SwMutex rpcRespMutex_;
    SwMap<SwString, std::shared_ptr<void>> rpcRespValueQueues_;
    SwMap<SwString, std::shared_ptr<void>> rpcRespVoidQueues_;

    SwTimer* userDocSaveTimer_{nullptr};
    bool userDocSavePending_{false};
    int userDocSaveDebounceMs_{100}; // 50–200ms typical; Pretty is preserved, only delayed/coalesced.
};

// -------------------------------------------------------------------------
// Macros requested (usage inside a SwRemoteObject-derived class/method)
// -------------------------------------------------------------------------
// Supports optional lambda overloads:
//   ipcRegisterConfig(type, var, "name", default)
//   ipcRegisterConfig(type, var, "name", default, [](const type&){...})
//   ipcBindConfig(type, var, "ns/obj#name")
//   ipcBindConfig(type, var, "ns/obj#name", [](const type&){...})

#define SW_CFG_CAT_(a, b) a##b
#define SW_CFG_CAT(a, b) SW_CFG_CAT_(a, b)
#define SW_CFG_NARG_(...) SW_CFG_NARG_I_(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define SW_CFG_NARG_I_(_1, _2, _3, _4, _5, _6, N, ...) N
#define SW_CFG_OVERLOAD(name, ...) SW_CFG_CAT(name, SW_CFG_NARG_(__VA_ARGS__))(__VA_ARGS__)

#define ipcRegisterConfig_4(type, container, configName, defaultVal) \
    this->ipcRegisterConfigT<type>(container, SwString(configName), defaultVal)
#define ipcRegisterConfig_5(type, container, configName, defaultVal, lambda) \
    this->ipcRegisterConfigT<type>(container, SwString(configName), defaultVal, lambda)
#define ipcRegisterConfig(...) SW_CFG_OVERLOAD(ipcRegisterConfig_, __VA_ARGS__)

#define ipcBindConfig_3(type, container, configFullName) \
    this->ipcBindConfigT<type>(container, SwString(configFullName))
#define ipcBindConfig_4(type, container, configFullName, lambda) \
    this->ipcBindConfigT<type>(container, SwString(configFullName), lambda)
#define ipcBindConfig(...) SW_CFG_OVERLOAD(ipcBindConfig_, __VA_ARGS__)

#define ipcConnect_2(fullName, lambda) \
    this->ipcConnectT(SwString(fullName), lambda)
#define ipcConnect_3(fullName, lambda, fireInitial) \
    this->ipcConnectT(SwString(fullName), lambda, fireInitial)
#define ipcConnect_4(targetObject, leaf, context, lambda) \
    this->ipcConnectScopedT(SwString(targetObject), SwString(leaf), context, lambda)
#define ipcConnect_5(targetObject, leaf, context, lambda, fireInitial) \
    this->ipcConnectScopedT(SwString(targetObject), SwString(leaf), context, lambda, fireInitial)
#define ipcConnect(...) SW_CFG_OVERLOAD(ipcConnect_, __VA_ARGS__)

// RPC helpers:
//   ipcExposeRpc(add, [](int a,int b){...})
//   ipcExposeRpc(add, [](int a,int b){...}, /*fireInitial=*/true)
//   ipcExposeRpc(add, this, &MyClass::add)
//   ipcExposeRpc(add, this, &MyClass::add, /*fireInitial=*/true)
//   ipcExposeRpc(&MyClass::add)
// If you need a non-identifier method name, use ipcExposeRpcStr("weird/name", ...).
#define ipcExposeRpc_1(methodPtr) \
    this->ipcExposeRpcT(this->rpcMethodNameFromMacro_(SwString(#methodPtr)), this, methodPtr)
#define ipcExposeRpc_2(methodName, lambda) \
    this->ipcExposeRpcT(this->rpcMethodNameFromMacro_(SwString(#methodName)), lambda)
#define ipcExposeRpc_3(methodName, lambda, fireInitial) \
    this->ipcExposeRpcT(this->rpcMethodNameFromMacro_(SwString(#methodName)), lambda, fireInitial)
#define ipcExposeRpc_4(methodName, obj, method, fireInitial) \
    this->ipcExposeRpcT(this->rpcMethodNameFromMacro_(SwString(#methodName)), obj, method, fireInitial)
#define ipcExposeRpc(...) SW_CFG_OVERLOAD(ipcExposeRpc_, __VA_ARGS__)

#define ipcExposeRpcStr_2(methodName, lambda) \
    this->ipcExposeRpcT(SwString(methodName), lambda)
#define ipcExposeRpcStr_3(methodName, lambda, fireInitial) \
    this->ipcExposeRpcT(SwString(methodName), lambda, fireInitial)
#define ipcExposeRpcStr_4(methodName, obj, method, fireInitial) \
    this->ipcExposeRpcT(SwString(methodName), obj, method, fireInitial)
#define ipcExposeRpcStr(...) SW_CFG_OVERLOAD(ipcExposeRpcStr_, __VA_ARGS__)
