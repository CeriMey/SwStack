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

    SwVector() = default;
    SwVector(const SwVector&) = default;
    SwVector(SwVector&&) noexcept = default;
    SwVector(std::initializer_list<T> init) : m_data(init) {}
    explicit SwVector(size_type count, const T& value = T()) : m_data(count, value) {}

    template<typename InputIt>
    SwVector(InputIt first, InputIt last) : m_data(first, last) {}

    ~SwVector() = default;

    SwVector& operator=(const SwVector&) = default;
    SwVector& operator=(SwVector&&) noexcept = default;
    SwVector& operator=(std::initializer_list<T> init) {
        m_data = init;
        return *this;
    }

    iterator begin() noexcept { return m_data.begin(); }
    iterator end() noexcept { return m_data.end(); }
    const_iterator begin() const noexcept { return m_data.begin(); }
    const_iterator end() const noexcept { return m_data.end(); }
    const_iterator cbegin() const noexcept { return m_data.cbegin(); }
    const_iterator cend() const noexcept { return m_data.cend(); }

    reference operator[](size_type index) { return m_data[index]; }
    const_reference operator[](size_type index) const { return m_data[index]; }
    reference at(size_type index) { return m_data.at(index); }
    const_reference at(size_type index) const { return m_data.at(index); }

    reference front() { return m_data.front(); }
    const_reference front() const { return m_data.front(); }
    reference back() { return m_data.back(); }
    const_reference back() const { return m_data.back(); }

    pointer data() noexcept { return m_data.data(); }
    const_pointer data() const noexcept { return m_data.data(); }

    bool isEmpty() const noexcept { return m_data.empty(); }
    int size() const noexcept { return static_cast<int>(m_data.size()); }
    size_type count() const noexcept { return m_data.size(); }

    void clear() noexcept { m_data.clear(); }
    void reserve(size_type newCapacity) { m_data.reserve(newCapacity); }
    size_type capacity() const noexcept { return m_data.capacity(); }

    void push_back(const T& value) { m_data.push_back(value); }
    void push_back(T&& value) { m_data.push_back(std::move(value)); }
    void append(const T& value) { m_data.push_back(value); }

    template<typename... Args>
    reference emplace_back(Args&&... args) {
        m_data.emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    iterator erase(iterator pos) { return m_data.erase(pos); }
    iterator erase(iterator first, iterator last) { return m_data.erase(first, last); }

    void removeAt(int index) {
        if (index < 0 || index >= size()) {
            throw std::out_of_range("SwVector::removeAt index out of range");
        }
        m_data.erase(m_data.begin() + index);
    }

    void remove(int index) { removeAt(index); }

    void resize(size_type newSize) { m_data.resize(newSize); }
    void resize(size_type newSize, const T& value) { m_data.resize(newSize, value); }

private:
    container_type m_data;
};

#endif // SWVECTOR_H
