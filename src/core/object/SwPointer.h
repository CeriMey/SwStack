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
    explicit SwPointerControl(SwObject* object) : object_(object) {}

    SwObject* object() const noexcept { return object_.load(std::memory_order_acquire); }
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

    SwPointer() noexcept = default;
    SwPointer(std::nullptr_t) noexcept {}

    SwPointer(T* object) { set(object); }

    SwPointer(const SwPointer&) noexcept = default;
    SwPointer(SwPointer&&) noexcept = default;

    template <typename U>
    SwPointer(const SwPointer<U>& other,
              typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0) noexcept
        : ctrl_(other.ctrl_) {}

    SwPointer& operator=(const SwPointer&) noexcept = default;
    SwPointer& operator=(SwPointer&&) noexcept = default;

    template <typename U>
    SwPointer& operator=(const SwPointer<U>& other) noexcept {
        static_assert(std::is_convertible<U*, T*>::value, "SwPointer: incompatible assignment");
        ctrl_ = other.ctrl_;
        return *this;
    }

    SwPointer& operator=(T* object) {
        set(object);
        return *this;
    }

    SwPointer& operator=(std::nullptr_t) noexcept {
        clear();
        return *this;
    }

    void swap(SwPointer& other) noexcept { ctrl_.swap(other.ctrl_); }

    T* data() const noexcept {
        if (!ctrl_) return nullptr;
        SwObject* object = ctrl_->object();
        return object ? static_cast<T*>(static_cast<ObjectType*>(object)) : nullptr;
    }

    T* get() const noexcept { return data(); }

    bool isNull() const noexcept { return data() == nullptr; }

    void clear() noexcept { ctrl_.reset(); }

    explicit operator bool() const noexcept { return data() != nullptr; }

    bool operator!() const noexcept { return data() == nullptr; }

    operator T*() const noexcept { return data(); }

    T* operator->() const noexcept { return data(); }
    T& operator*() const { return *data(); }

    bool operator==(const SwPointer& other) const noexcept { return data() == other.data(); }
    bool operator!=(const SwPointer& other) const noexcept { return data() != other.data(); }
    bool operator<(const SwPointer& other) const noexcept { return data() < other.data(); }

    bool operator==(const T* other) const noexcept { return data() == other; }
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
