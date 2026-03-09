#pragma once

/**
 * @file src/core/remote/SwProxyObjectBrowser.h
 * @ingroup core_remote
 * @brief Declares the public interface exposed by SwProxyObjectBrowser in the CoreSw remote and
 * IPC layer.
 *
 * This header belongs to the CoreSw remote and IPC layer. It provides the abstractions used to
 * expose objects across process boundaries and to transport data or signals between peers.
 *
 * Within that layer, this file focuses on the proxy object browser interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwProxyObjectInstance and SwProxyObjectBrowser.
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

#include "SwProxyObject.h"
#include "SwMap.h"
#include "SwPointer.h"

#include <atomic>
#include <type_traits>

template <typename RemoteT>
class SwProxyObjectInstance : public SwObject {
public:
    static_assert(std::is_base_of<sw::ipc::ProxyObjectBase, RemoteT>::value,
                  "SwProxyObjectInstance<RemoteT>: RemoteT must derive from sw::ipc::ProxyObjectBase");

    /**
     * @brief Constructs a `SwProxyObjectInstance` instance.
     * @param domain Value passed to the method.
     * @param objectFqn Value passed to the method.
     * @param clientInfo Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param clientInfo Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwProxyObjectInstance(const SwString& domain,
                          const SwString& objectFqn,
                          const SwString& clientInfo = SwString(),
                          SwObject* parent = nullptr)
        : SwObject(parent), remote_(domain, objectFqn, clientInfo) {
        setObjectName(objectFqn);
    }

    /**
     * @brief Returns the current remote.
     * @return The current remote.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    RemoteT& remote() { return remote_; }
    /**
     * @brief Returns the current remote.
     * @return The current remote.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const RemoteT& remote() const { return remote_; }

    /**
     * @brief Performs the `domain` operation.
     * @return The requested domain.
     */
    const SwString& domain() const { return remote_.domain(); }
    /**
     * @brief Performs the `objectFqn` operation.
     * @return The requested object Fqn.
     */
    const SwString& objectFqn() const { return remote_.object(); }
    /**
     * @brief Performs the `target` operation.
     * @return The requested target.
     */
    SwString target() const { return remote_.target(); }

    /**
     * @brief Returns the current name Space.
     * @return The current name Space.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString nameSpace() const {
        SwString ns;
        SwString obj;
        splitObjectFqn_(remote_.object(), ns, obj);
        return ns;
    }

    /**
     * @brief Returns the current remote Object Name.
     * @return The current remote Object Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString remoteObjectName() const {
        SwString ns;
        SwString obj;
        splitObjectFqn_(remote_.object(), ns, obj);
        return obj;
    }

    /**
     * @brief Performs the `remotePid` operation.
     * @return The requested remote Pid.
     */
    uint32_t remotePid() const { return remote_.remotePid(); }

private:
    static SwString normalizePath_(SwString x) {
        x = x.trimmed();
        x.replace("\\", "/");
        while (x.contains("//")) x.replace("//", "/");
        while (x.startsWith("/")) x = x.mid(1);
        while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
        return x;
    }

    static void splitObjectFqn_(const SwString& fqn, SwString& nsOut, SwString& objOut) {
        const SwString x = normalizePath_(fqn);
        const size_t slash = x.lastIndexOf('/');
        if (slash == static_cast<size_t>(-1)) {
            nsOut = SwString();
            objOut = x;
            return;
        }
        nsOut = x.left(static_cast<int>(slash));
        objOut = x.mid(static_cast<int>(slash) + 1);
    }

    RemoteT remote_;
};

template <typename RemoteT>
class SwProxyObjectBrowser : public SwObject {
public:
    using Instance = SwProxyObjectInstance<RemoteT>;

    DECLARE_SIGNAL(remoteAppeared, SwPointer<Instance>)
    DECLARE_SIGNAL(remoteDisappeared, SwPointer<Instance>)

    /**
     * @brief Constructs a `SwProxyObjectBrowser` instance.
     * @param domain Value passed to the method.
     * @param filter Value passed to the method.
     * @param clientInfo Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param clientInfo Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwProxyObjectBrowser(const SwString& domain,
                         const SwString& filter = SwString("*"),
                         const SwString& clientInfo = SwString(),
                         SwObject* parent = nullptr)
        : SwObject(parent),
          domain_(domain),
          filter_(filter),
          clientInfo_(clientInfo) {
        subscribeRegistryChanges_();
        refreshNow();
    }

    /**
     * @brief Destroys the `SwProxyObjectBrowser` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwProxyObjectBrowser() override {
        stop();
        clear();
    }

    /**
     * @brief Returns the current domain.
     * @return The current domain.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& domain() const { return domain_; }
    /**
     * @brief Returns the current filter.
     * @return The current filter.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& filter() const { return filter_; }
    /**
     * @brief Returns the current client Info.
     * @return The current client Info.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& clientInfo() const { return clientInfo_; }

    /**
     * @brief Returns whether the object reports active.
     * @return `true` when the object reports active; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isActive() const { return active_; }

    /**
     * @brief Sets the domain.
     * @param domain Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDomain(const SwString& domain) {
        if (domain_ == domain) return;
        domain_ = domain;
        subscribeRegistryChanges_();
        refreshNow();
    }

    /**
     * @brief Sets the filter.
     * @param filter Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFilter(const SwString& filter) {
        if (filter_ == filter) return;
        filter_ = filter;
        refreshNow();
    }

    /**
     * @brief Sets the client Info.
     * @param clientInfo Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setClientInfo(const SwString& clientInfo) {
        clientInfo_ = clientInfo;
        refreshNow();
    }

    /**
     * @brief Sets the require Type Match.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRequireTypeMatch(bool enable) {
        requireTypeMatch_ = enable;
        refreshNow();
    }

    /**
     * @brief Returns the current require Type Match.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool requireTypeMatch() const { return requireTypeMatch_; }

    /**
     * @brief Sets the require Alive.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRequireAlive(bool enable) {
        requireAlive_ = enable;
        refreshNow();
    }

    /**
     * @brief Returns the current require Alive.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool requireAlive() const { return requireAlive_; }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() {
        if (active_) return;
        active_ = true;
        subscribeRegistryChanges_();
        refreshNow();
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() {
        if (!active_) return;
        active_ = false;
        registrySub_.stop();
    }

    /**
     * @brief Performs the `size` operation.
     * @return The current size value.
     */
    size_t size() const { return instances_.size(); }

    /**
     * @brief Returns the current instances.
     * @return The current instances.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<SwPointer<Instance>> instances() const {
        SwList<SwPointer<Instance>> out;
        SwList<Instance*> values = instances_.values();
        out.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            out.append(SwPointer<Instance>(values[i]));
        }
        return out;
    }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        SwList<SwString> keys = instances_.keys();
        for (size_t i = 0; i < keys.size(); ++i) {
            Instance* inst = instances_.value(keys[i], nullptr);
            if (!inst) continue;
            delete inst;
        }
        instances_.clear();
    }

    /**
     * @brief Performs the `refreshNow` operation.
     */
    void refreshNow() {
        if (refreshing_.exchange(true, std::memory_order_acq_rel)) return;

        if (domain_.isEmpty()) {
            clear();
            refreshing_.store(false, std::memory_order_release);
            return;
        }

        const SwStringList candidates = RemoteT::candidates(domain_, requireTypeMatch_);

        SwMap<SwString, bool> want;
        want.clear();

        for (size_t i = 0; i < candidates.size(); ++i) {
            SwString candDomain;
            SwString candObject;
            if (!splitTarget_(candidates[i], candDomain, candObject)) continue;
            if (candDomain != domain_) continue;

            candObject = normalizePath_(candObject);
            if (candObject.isEmpty()) continue;
            if (!matchesObjectFilter_(candObject, filter_)) continue;

            if (!instances_.contains(candObject)) {
                Instance* inst = new Instance(domain_, candObject, clientInfo_, this);
                if (requireAlive_ && !isRemoteAliveBestEffort_(inst->remotePid())) {
                    delete inst;
                    continue;
                }
                instances_.insert(candObject, inst);
                emit remoteAppeared(SwPointer<Instance>(inst));
            } else if (requireAlive_) {
                Instance* inst = instances_.value(candObject, nullptr);
                if (inst && !isRemoteAliveBestEffort_(inst->remotePid())) {
                    want.insert(candObject, false);
                    continue;
                }
            }

            want.insert(candObject, true);
        }

        SwList<SwString> keys = instances_.keys();
        for (size_t i = 0; i < keys.size(); ++i) {
            const SwString& objectFqn = keys[i];
            if (want.contains(objectFqn) && want.value(objectFqn, false)) {
                continue;
            }

            Instance* inst = instances_.value(objectFqn, nullptr);
            instances_.remove(objectFqn);
            if (!inst) continue;

            SwPointer<Instance> ptr(inst);
            emit remoteDisappeared(ptr);
            delete inst;
        }

        refreshing_.store(false, std::memory_order_release);
    }

private:
    void subscribeRegistryChanges_() {
        registrySub_.stop();
        if (!active_) return;
        if (domain_.isEmpty()) return;

        const SwPointer<SwProxyObjectBrowser> self(this);

        ::sw::ipc::Registry reg(domain_, ::sw::ipc::detail::registryEventsObjectName_());
        ::sw::ipc::Signal<uint64_t> sig(reg, ::sw::ipc::detail::registryEventsSignalName_());
        registrySub_ = sig.connect([self](uint64_t) {
            if (!self) return;
            self->refreshNow();
        }, /*fireInitial=*/false);
    }

    static bool splitTarget_(const SwString& fqn, SwString& domainOut, SwString& objectOut) {
        SwString x = normalizePath_(fqn);
        if (x.isEmpty()) return false;
        const int slash = x.indexOf('/');
        if (slash <= 0) return false;
        domainOut = x.left(slash);
        objectOut = x.mid(slash + 1);
        return !domainOut.isEmpty() && !objectOut.isEmpty();
    }

    static SwString normalizePath_(SwString x) {
        x = x.trimmed();
        x.replace("\\", "/");
        while (x.contains("//")) x.replace("//", "/");
        while (x.startsWith("/")) x = x.mid(1);
        while (x.endsWith("/")) x = x.left(static_cast<int>(x.size()) - 1);
        return x;
    }

    static void splitObjectFqn_(const SwString& fqn, SwString& nsOut, SwString& objOut) {
        const SwString x = normalizePath_(fqn);
        const size_t slash = x.lastIndexOf('/');
        if (slash == static_cast<size_t>(-1)) {
            nsOut = SwString();
            objOut = x;
            return;
        }
        nsOut = x.left(static_cast<int>(slash));
        objOut = x.mid(static_cast<int>(slash) + 1);
    }

    static bool matchesObjectFilter_(const SwString& objectFqn, const SwString& filter) {
        const SwString obj = normalizePath_(objectFqn);
        SwString f = normalizePath_(filter);

        if (f.isEmpty() || f == SwString("*")) return true;
        if (f == obj) return true;

        if (f.endsWith("/*")) {
            const SwString prefix = f.left(static_cast<int>(f.size()) - 2);
            if (prefix.isEmpty() || prefix == SwString("*")) return true;
            return obj.startsWith(prefix + "/");
        }

        if (f.startsWith("*/")) {
            const SwString leaf = f.mid(2);
            if (leaf.isEmpty() || leaf == SwString("*")) return true;
            SwString ns;
            SwString name;
            splitObjectFqn_(obj, ns, name);
            return name == leaf;
        }

        return false;
    }

    static bool isRemoteAliveBestEffort_(uint32_t pid) {
        if (pid == 0) return false;
        const sw::ipc::detail::PidState st = sw::ipc::detail::pidStateBestEffort_(pid);
        return st != sw::ipc::detail::PidState::Dead;
    }

    SwString domain_;
    SwString filter_;
    SwString clientInfo_;
    bool requireTypeMatch_{true};
    bool requireAlive_{true};
    bool active_{true};

    typename ::sw::ipc::Signal<uint64_t>::Subscription registrySub_{};
    std::atomic_bool refreshing_{false};
    SwMap<SwString, Instance*> instances_;
};
