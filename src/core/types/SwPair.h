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

#ifndef SWPAIR_H
#define SWPAIR_H

/**
 * @file src/core/types/SwPair.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwPair in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the pair interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwPair.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

#include <type_traits>
#include <utility>

template<typename T1, typename T2>
class SwPair {
public:
    typedef T1 first_type;
    typedef T2 second_type;

    T1 first;
    T2 second;

    /**
     * @brief Constructs a `SwPair` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair()
        : first(), second() {}

    /**
     * @brief Constructs a `SwPair` instance.
     * @param firstValue Value passed to the method.
     * @param secondValue Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair(const T1& firstValue, const T2& secondValue)
        : first(firstValue), second(secondValue) {}

    /**
     * @brief Constructs a `SwPair` instance.
     * @param firstValue Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair(T1&& firstValue, T2&& secondValue)
        : first(std::move(firstValue)), second(std::move(secondValue)) {}

    /**
     * @brief Constructs a `SwPair` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair(const SwPair& other) = default;
    /**
     * @brief Constructs a `SwPair` instance.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair(SwPair&& other) noexcept(std::is_nothrow_move_constructible<T1>::value &&
                                    std::is_nothrow_move_constructible<T2>::value) = default;

    template<typename U1, typename U2>
    /**
     * @brief Constructs a `SwPair` instance.
     * @param second Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair(const SwPair<U1, U2>& other)
        : first(other.first), second(other.second) {}

    template<typename U1, typename U2>
    /**
     * @brief Constructs a `SwPair` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPair(SwPair<U1, U2>&& other)
        : first(std::move(other.first)),
          second(std::move(other.second)) {}

    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwPair& operator=(const SwPair& other) = default;
    /**
     * @brief Performs the `operator=` operation.
     * @param value Value passed to the method.
     * @return The requested operator =.
     */
    SwPair& operator=(SwPair&& other) noexcept(std::is_nothrow_move_assignable<T1>::value &&
                                               std::is_nothrow_move_assignable<T2>::value) = default;

    template<typename U1, typename U2>
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwPair& operator=(const SwPair<U1, U2>& other) {
        first = other.first;
        second = other.second;
        return *this;
    }

    template<typename U1, typename U2>
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwPair& operator=(SwPair<U1, U2>&& other) {
        first = std::move(other.first);
        second = std::move(other.second);
        return *this;
    }

    /**
     * @brief Performs the `swap` operation.
     */
    void swap(SwPair& other) noexcept(noexcept(std::swap(first, other.first)) &&
                                      noexcept(std::swap(second, other.second))) {
        using std::swap;
        swap(first, other.first);
        swap(second, other.second);
    }
};

template<typename T1, typename T2>
inline bool operator==(const SwPair<T1, T2>& lhs, const SwPair<T1, T2>& rhs) {
    return lhs.first == rhs.first && lhs.second == rhs.second;
}

template<typename T1, typename T2>
inline bool operator!=(const SwPair<T1, T2>& lhs, const SwPair<T1, T2>& rhs) {
    return !(lhs == rhs);
}

template<typename T1, typename T2>
inline bool operator<(const SwPair<T1, T2>& lhs, const SwPair<T1, T2>& rhs) {
    return lhs.first < rhs.first ||
           (!(rhs.first < lhs.first) && lhs.second < rhs.second);
}

template<typename T1, typename T2>
inline bool operator>(const SwPair<T1, T2>& lhs, const SwPair<T1, T2>& rhs) {
    return rhs < lhs;
}

template<typename T1, typename T2>
inline bool operator<=(const SwPair<T1, T2>& lhs, const SwPair<T1, T2>& rhs) {
    return !(rhs < lhs);
}

template<typename T1, typename T2>
inline bool operator>=(const SwPair<T1, T2>& lhs, const SwPair<T1, T2>& rhs) {
    return !(lhs < rhs);
}

template<typename T1, typename T2>
inline void swap(SwPair<T1, T2>& lhs, SwPair<T1, T2>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

template<typename T1, typename T2>
inline SwPair<typename std::decay<T1>::type, typename std::decay<T2>::type>
SwMakePair(T1&& first, T2&& second) {
    return SwPair<typename std::decay<T1>::type, typename std::decay<T2>::type>(
        std::forward<T1>(first), std::forward<T2>(second));
}

#endif // SWPAIR_H
