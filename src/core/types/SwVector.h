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

#ifndef SWVECTOR_H
#define SWVECTOR_H

/**
 * @file src/core/types/SwVector.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwVector in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the vector interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwVector.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

#include <vector>
#include <initializer_list>
#include <stdexcept>
#include <utility>

template<typename T>
class SwVector {
public:
    using value_type = T;
    using container_type = std::vector<T>;
    using size_type = typename container_type::size_type;
    using difference_type = typename container_type::difference_type;
    using reference = typename container_type::reference;
    using const_reference = typename container_type::const_reference;
    using pointer = typename container_type::pointer;
    using const_pointer = typename container_type::const_pointer;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;

    /**
     * @brief Constructs a `SwVector` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVector() = default;
    /**
     * @brief Constructs a `SwVector` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVector(const SwVector&) = default;
    /**
     * @brief Constructs a `SwVector` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVector(SwVector&&) noexcept = default;
    /**
     * @brief Constructs a `SwVector` instance.
     * @param init Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVector(std::initializer_list<T> init) : m_data(init) {}
    /**
     * @brief Constructs a `SwVector` instance.
     * @param count Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwVector(size_type count, const T& value = T()) : m_data(count, value) {}

    template<typename InputIt>
    /**
     * @brief Constructs a `SwVector` instance.
     * @param first Value passed to the method.
     * @param last Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVector(InputIt first, InputIt last) : m_data(first, last) {}

    /**
     * @brief Destroys the `SwVector` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwVector() = default;

    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwVector& operator=(const SwVector&) = default;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwVector& operator=(SwVector&&) noexcept = default;
    /**
     * @brief Performs the `operator=` operation.
     * @param init Value passed to the method.
     * @return The requested operator =.
     */
    SwVector& operator=(std::initializer_list<T> init) {
        m_data = init;
        return *this;
    }

    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    iterator begin() noexcept { return m_data.begin(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    iterator end() noexcept { return m_data.end(); }
    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    const_iterator begin() const noexcept { return m_data.begin(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    const_iterator end() const noexcept { return m_data.end(); }
    /**
     * @brief Performs the `cbegin` operation.
     * @return The requested cbegin.
     */
    const_iterator cbegin() const noexcept { return m_data.cbegin(); }
    /**
     * @brief Performs the `cend` operation.
     * @return The requested cend.
     */
    const_iterator cend() const noexcept { return m_data.cend(); }

    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    reference operator[](size_type index) { return m_data[index]; }
    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    const_reference operator[](size_type index) const { return m_data[index]; }
    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    reference at(size_type index) { return m_data.at(index); }
    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    const_reference at(size_type index) const { return m_data.at(index); }

    /**
     * @brief Performs the `front` operation.
     * @return The requested front.
     */
    reference front() { return m_data.front(); }
    /**
     * @brief Performs the `front` operation.
     * @return The requested front.
     */
    const_reference front() const { return m_data.front(); }
    /**
     * @brief Performs the `back` operation.
     * @return The requested back.
     */
    reference back() { return m_data.back(); }
    /**
     * @brief Performs the `back` operation.
     * @return The requested back.
     */
    const_reference back() const { return m_data.back(); }

    /**
     * @brief Performs the `data` operation.
     * @return The requested data.
     */
    pointer data() noexcept { return m_data.data(); }
    /**
     * @brief Performs the `data` operation.
     * @return The requested data.
     */
    const_pointer data() const noexcept { return m_data.data(); }

    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isEmpty() const noexcept { return m_data.empty(); }
    /**
     * @brief Performs the `size` operation.
     * @return The current size value.
     */
    int size() const noexcept { return static_cast<int>(m_data.size()); }
    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    size_type count() const noexcept { return m_data.size(); }

    /**
     * @brief Clears the current object state.
     */
    void clear() noexcept { m_data.clear(); }
    /**
     * @brief Performs the `reserve` operation.
     * @param newCapacity Value passed to the method.
     */
    void reserve(size_type newCapacity) { m_data.reserve(newCapacity); }
    /**
     * @brief Performs the `capacity` operation.
     * @return The requested capacity.
     */
    size_type capacity() const noexcept { return m_data.capacity(); }

    /**
     * @brief Performs the `push_back` operation.
     * @param value Value passed to the method.
     */
    void push_back(const T& value) { m_data.push_back(value); }
    /**
     * @brief Performs the `push_back` operation.
     */
    void push_back(T&& value) { m_data.push_back(std::move(value)); }
    /**
     * @brief Performs the `append` operation.
     * @param value Value passed to the method.
     */
    void append(const T& value) { m_data.push_back(value); }

    template<typename... Args>
    /**
     * @brief Performs the `emplace_back` operation.
     * @param args Value passed to the method.
     * @return The requested emplace back.
     */
    reference emplace_back(Args&&... args) {
        m_data.emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    /**
     * @brief Performs the `erase` operation.
     * @param pos Position used by the operation.
     * @return The requested erase.
     */
    iterator erase(iterator pos) { return m_data.erase(pos); }
    /**
     * @brief Performs the `erase` operation.
     * @param first Value passed to the method.
     * @param last Value passed to the method.
     * @return The requested erase.
     */
    iterator erase(iterator first, iterator last) { return m_data.erase(first, last); }

    /**
     * @brief Removes the specified at.
     * @param index Value passed to the method.
     */
    void removeAt(int index) {
        if (index < 0 || index >= size()) {
            throw std::out_of_range("SwVector::removeAt index out of range");
        }
        m_data.erase(m_data.begin() + index);
    }

    /**
     * @brief Removes the specified remove.
     * @param index Value passed to the method.
     */
    void remove(int index) { removeAt(index); }

    /**
     * @brief Performs the `resize` operation.
     * @param newSize Value passed to the method.
     */
    void resize(size_type newSize) { m_data.resize(newSize); }
    /**
     * @brief Performs the `resize` operation.
     * @param newSize Value passed to the method.
     * @param value Value passed to the method.
     */
    void resize(size_type newSize, const T& value) { m_data.resize(newSize, value); }

private:
    container_type m_data;
};

#endif // SWVECTOR_H
