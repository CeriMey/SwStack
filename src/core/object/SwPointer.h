#pragma once

/**
 * @file src/core/object/SwPointer.h
 * @ingroup core_object
 * @brief Declares the public interface exposed by SwPointer in the CoreSw object model layer.
 *
 * This header belongs to the CoreSw object model layer. It defines parent and child ownership,
 * runtime typing, and the signal-slot machinery that many other modules build upon.
 *
 * Within that layer, this file focuses on the pointer interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwPointer.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Object-model declarations here establish how instances are identified, connected, owned, and
 * moved across execution contexts.
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

#include "SwObject.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>

namespace sw {
namespace detail {

class SwPointerControl {
public:
    /**
     * @brief Constructs a `SwPointerControl` instance.
     * @param object Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwPointerControl(SwObject* object) : object_(object) {}

    /**
     * @brief Performs the `object` operation.
     * @param memory_order_acquire Value passed to the method.
     * @return The requested object.
     */
    SwObject* object() const noexcept { return object_.load(std::memory_order_acquire); }
    /**
     * @brief Clears the current object state.
     * @param memory_order_release Value passed to the method.
     */
    void clear() noexcept { object_.store(nullptr, std::memory_order_release); }

private:
    std::atomic<SwObject*> object_{nullptr};
};

inline std::mutex& swPointerRegistryMutex_() {
    static std::mutex m;
    return m;
}

inline std::map<SwObject*, std::shared_ptr<SwPointerControl>>& swPointerRegistry_() {
    static std::map<SwObject*, std::shared_ptr<SwPointerControl>> map;
    return map;
}

inline void swPointerRegistryErase_(SwObject* object) {
    std::lock_guard<std::mutex> lk(swPointerRegistryMutex_());
    auto& map = swPointerRegistry_();
    auto it = map.find(object);
    if (it != map.end()) {
        map.erase(it);
    }
}

inline std::shared_ptr<SwPointerControl> swPointerControlFor_(SwObject* object) {
    if (!object) {
        return {};
    }

    std::shared_ptr<SwPointerControl> ctrl;
    {
        std::lock_guard<std::mutex> lk(swPointerRegistryMutex_());
        auto& map = swPointerRegistry_();
        auto it = map.find(object);
        if (it != map.end()) {
            return it->second;
        }

        ctrl = std::make_shared<SwPointerControl>(object);
        map[object] = ctrl;
    }

    std::weak_ptr<SwPointerControl> weak = ctrl;
    SwObject::connect(object, &SwObject::destroyed, [weak, object]() {
        if (auto locked = weak.lock()) {
            locked->clear();
        }
        swPointerRegistryErase_(object);
    }, DirectConnection);

    return ctrl;
}

} // namespace detail
} // namespace sw

template <typename T>
class SwPointer {
    template <typename U>
    friend class SwPointer;

public:
    using ElementType = T;
    using ObjectType = typename std::remove_const<T>::type;

    static_assert(std::is_base_of<SwObject, ObjectType>::value, "SwPointer<T>: T must derive from SwObject");

    /**
     * @brief Constructs a `SwPointer` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointer() noexcept = default;
    /**
     * @brief Constructs a `SwPointer` instance.
     * @param nullptr_t Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointer(std::nullptr_t) noexcept {}

    /**
     * @brief Constructs a `SwPointer` instance.
     * @param object Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointer(T* object) { set(object); }

    /**
     * @brief Constructs a `SwPointer` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointer(const SwPointer&) noexcept = default;
    /**
     * @brief Constructs a `SwPointer` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointer(SwPointer&&) noexcept = default;

    template <typename U>
    /**
     * @brief Constructs a `SwPointer` instance.
     * @param other Value passed to the method.
     * @param type Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointer(const SwPointer<U>& other,
              typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0) noexcept
        : ctrl_(other.ctrl_) {}

    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwPointer& operator=(const SwPointer&) noexcept = default;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwPointer& operator=(SwPointer&&) noexcept = default;

    template <typename U>
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwPointer& operator=(const SwPointer<U>& other) noexcept {
        static_assert(std::is_convertible<U*, T*>::value, "SwPointer: incompatible assignment");
        ctrl_ = other.ctrl_;
        return *this;
    }

    /**
     * @brief Performs the `operator=` operation.
     * @param object Value passed to the method.
     * @return The requested operator =.
     */
    SwPointer& operator=(T* object) {
        set(object);
        return *this;
    }

    /**
     * @brief Performs the `operator=` operation.
     * @param nullptr_t Value passed to the method.
     * @return The requested operator =.
     */
    SwPointer& operator=(std::nullptr_t) noexcept {
        clear();
        return *this;
    }

    /**
     * @brief Performs the `swap` operation.
     * @param ctrl_ Value passed to the method.
     */
    void swap(SwPointer& other) noexcept { ctrl_.swap(other.ctrl_); }

    /**
     * @brief Returns the current data.
     * @return The current data.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T* data() const noexcept {
        if (!ctrl_) return nullptr;
        SwObject* object = ctrl_->object();
        return object ? static_cast<T*>(static_cast<ObjectType*>(object)) : nullptr;
    }

    /**
     * @brief Performs the `get` operation.
     * @return The requested get.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T* get() const noexcept { return data(); }

    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isNull() const noexcept { return data() == nullptr; }

    /**
     * @brief Clears the current object state.
     */
    void clear() noexcept { ctrl_.reset(); }

    /**
     * @brief Performs the `bool` operation.
     * @return The requested bool.
     */
    explicit operator bool() const noexcept { return data() != nullptr; }

    /**
     * @brief Performs the `operator!` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!() const noexcept { return data() == nullptr; }

    /**
     * @brief Performs the `T*` operation.
     * @return The requested t*.
     */
    operator T*() const noexcept { return data(); }

    /**
     * @brief Performs the `operator->` operation.
     * @return The requested operator ->.
     */
    T* operator->() const noexcept { return data(); }
    /**
     * @brief Performs the `operator*` operation.
     * @return The requested operator *.
     */
    T& operator*() const { return *data(); }

    /**
     * @brief Performs the `operator==` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwPointer& other) const noexcept { return data() == other.data(); }
    /**
     * @brief Performs the `operator!=` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwPointer& other) const noexcept { return data() != other.data(); }
    /**
     * @brief Performs the `operator<` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator<(const SwPointer& other) const noexcept { return data() < other.data(); }

    /**
     * @brief Performs the `operator==` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const T* other) const noexcept { return data() == other; }
    /**
     * @brief Performs the `operator!=` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const T* other) const noexcept { return data() != other; }

private:
    void set(T* object) {
        ctrl_ = sw::detail::swPointerControlFor_(const_cast<ObjectType*>(object));
    }

    std::shared_ptr<sw::detail::SwPointerControl> ctrl_;
};

template <typename T>
inline void swap(SwPointer<T>& a, SwPointer<T>& b) noexcept {
    a.swap(b);
}
