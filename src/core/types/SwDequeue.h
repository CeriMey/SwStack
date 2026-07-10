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

#ifndef SWDEQUEUE_H
#define SWDEQUEUE_H

/**
 * @file src/core/types/SwDequeue.h
 * @ingroup core_types
 * @brief Declares a Qt-like wrapper around std::deque for the CoreSw fundamental types layer.
 */

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>

template<typename T>
class SwDequeue {
public:
    using value_type = T;
    using container_type = std::deque<T>;
    using size_type = typename container_type::size_type;
    using difference_type = typename container_type::difference_type;
    using reference = typename container_type::reference;
    using const_reference = typename container_type::const_reference;
    using pointer = typename container_type::pointer;
    using const_pointer = typename container_type::const_pointer;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;
    using reverse_iterator = typename container_type::reverse_iterator;
    using const_reverse_iterator = typename container_type::const_reverse_iterator;

    SwDequeue() = default;
    SwDequeue(const SwDequeue&) = default;
    SwDequeue(SwDequeue&&) noexcept = default;
    SwDequeue(std::initializer_list<T> init) : m_data(init) {}
    SwDequeue(const container_type& values) : m_data(values) {}
    SwDequeue(container_type&& values) noexcept : m_data(std::move(values)) {}
    explicit SwDequeue(size_type count, const T& value = T()) : m_data(count, value) {}

    template<typename InputIt>
    SwDequeue(InputIt first, InputIt last) : m_data(first, last) {}

    ~SwDequeue() = default;

    SwDequeue& operator=(const SwDequeue&) = default;
    SwDequeue& operator=(SwDequeue&&) noexcept = default;

    SwDequeue& operator=(std::initializer_list<T> init) {
        m_data = init;
        return *this;
    }

    bool operator==(const SwDequeue& other) const { return m_data == other.m_data; }
    bool operator!=(const SwDequeue& other) const { return !(*this == other); }

    SwDequeue operator+(const SwDequeue& other) const {
        SwDequeue result(*this);
        result += other;
        return result;
    }

    SwDequeue& operator+=(const SwDequeue& other) {
        append(other);
        return *this;
    }

    SwDequeue& operator<<(const T& value) {
        append(value);
        return *this;
    }

    SwDequeue& operator<<(T&& value) {
        append(std::move(value));
        return *this;
    }

    reference operator[](size_type index) { return m_data[index]; }
    const_reference operator[](size_type index) const { return m_data[index]; }

    iterator begin() noexcept { return m_data.begin(); }
    iterator end() noexcept { return m_data.end(); }
    const_iterator begin() const noexcept { return m_data.begin(); }
    const_iterator end() const noexcept { return m_data.end(); }
    const_iterator cbegin() const noexcept { return m_data.cbegin(); }
    const_iterator cend() const noexcept { return m_data.cend(); }

    reverse_iterator rbegin() noexcept { return m_data.rbegin(); }
    reverse_iterator rend() noexcept { return m_data.rend(); }
    const_reverse_iterator rbegin() const noexcept { return m_data.rbegin(); }
    const_reverse_iterator rend() const noexcept { return m_data.rend(); }
    const_reverse_iterator crbegin() const noexcept { return m_data.crbegin(); }
    const_reverse_iterator crend() const noexcept { return m_data.crend(); }

    bool isEmpty() const noexcept { return m_data.empty(); }
    bool empty() const noexcept { return m_data.empty(); }
    int size() const noexcept { return static_cast<int>(m_data.size()); }
    size_type count() const noexcept { return m_data.size(); }

    size_type count(const T& value) const {
        return static_cast<size_type>(std::count(m_data.begin(), m_data.end(), value));
    }

    void clear() noexcept { m_data.clear(); }

    void append(const T& value) { m_data.push_back(value); }
    void append(T&& value) { m_data.push_back(std::move(value)); }
    void append(const SwDequeue& other) { m_data.insert(m_data.end(), other.m_data.begin(), other.m_data.end()); }

    template<typename InputIt>
    void append(InputIt first, InputIt last) {
        m_data.insert(m_data.end(), first, last);
    }

    void prepend(const T& value) { m_data.push_front(value); }
    void prepend(T&& value) { m_data.push_front(std::move(value)); }

    void enqueue(const T& value) { append(value); }
    void enqueue(T&& value) { append(std::move(value)); }

    void push_back(const T& value) { append(value); }
    void push_back(T&& value) { append(std::move(value)); }
    void push_front(const T& value) { prepend(value); }
    void push_front(T&& value) { prepend(std::move(value)); }

    template<typename... Args>
    reference emplaceBack(Args&&... args) {
        m_data.emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    template<typename... Args>
    reference emplaceFront(Args&&... args) {
        m_data.emplace_front(std::forward<Args>(args)...);
        return m_data.front();
    }

    template<typename... Args>
    reference emplace_back(Args&&... args) {
        return emplaceBack(std::forward<Args>(args)...);
    }

    template<typename... Args>
    reference emplace_front(Args&&... args) {
        return emplaceFront(std::forward<Args>(args)...);
    }

    reference at(size_type index) { return m_data.at(index); }
    const_reference at(size_type index) const { return m_data.at(index); }

    T value(size_type index, const T& defaultValue = T()) const {
        return index < m_data.size() ? m_data[index] : defaultValue;
    }

    reference first() {
        ensureNotEmpty_("SwDequeue::first");
        return m_data.front();
    }

    const_reference first() const {
        ensureNotEmpty_("SwDequeue::first");
        return m_data.front();
    }

    reference last() {
        ensureNotEmpty_("SwDequeue::last");
        return m_data.back();
    }

    const_reference last() const {
        ensureNotEmpty_("SwDequeue::last");
        return m_data.back();
    }

    reference front() { return first(); }
    const_reference front() const { return first(); }
    reference back() { return last(); }
    const_reference back() const { return last(); }
    reference head() { return first(); }
    const_reference head() const { return first(); }

    T takeFirst() {
        ensureNotEmpty_("SwDequeue::takeFirst");
        T value = std::move(m_data.front());
        m_data.pop_front();
        return value;
    }

    T takeLast() {
        ensureNotEmpty_("SwDequeue::takeLast");
        T value = std::move(m_data.back());
        m_data.pop_back();
        return value;
    }

    T dequeue() {
        return takeFirst();
    }

    bool tryTakeFirst(T& out) {
        if (m_data.empty()) {
            return false;
        }
        out = std::move(m_data.front());
        m_data.pop_front();
        return true;
    }

    bool tryTakeLast(T& out) {
        if (m_data.empty()) {
            return false;
        }
        out = std::move(m_data.back());
        m_data.pop_back();
        return true;
    }

    bool tryDequeue(T& out) {
        return tryTakeFirst(out);
    }

    void removeFirst() {
        if (!m_data.empty()) {
            m_data.pop_front();
        }
    }

    void removeLast() {
        if (!m_data.empty()) {
            m_data.pop_back();
        }
    }

    void pop_front() { removeFirst(); }
    void pop_back() { removeLast(); }

    void insert(int index, const T& value) {
        m_data.insert(iteratorAtInsertIndex_(index), value);
    }

    void insert(int index, T&& value) {
        m_data.insert(iteratorAtInsertIndex_(index), std::move(value));
    }

    iterator insert(iterator pos, const T& value) { return m_data.insert(pos, value); }
    iterator insert(iterator pos, T&& value) { return m_data.insert(pos, std::move(value)); }

    iterator erase(iterator pos) { return m_data.erase(pos); }
    iterator erase(iterator first, iterator last) { return m_data.erase(first, last); }

    void removeAt(int index) {
        m_data.erase(iteratorAtIndex_(index, "SwDequeue::removeAt"));
    }

    void remove(int index) {
        removeAt(index);
    }

    T takeAt(int index) {
        iterator it = iteratorAtIndex_(index, "SwDequeue::takeAt");
        T value = std::move(*it);
        m_data.erase(it);
        return value;
    }

    int removeAll(const T& value) {
        const size_type oldSize = m_data.size();
        m_data.erase(std::remove(m_data.begin(), m_data.end(), value), m_data.end());
        return static_cast<int>(oldSize - m_data.size());
    }

    bool removeOne(const T& value) {
        iterator it = std::find(m_data.begin(), m_data.end(), value);
        if (it == m_data.end()) {
            return false;
        }
        m_data.erase(it);
        return true;
    }

    bool replace(int index, const T& value) {
        if (!isValidIndex_(index)) {
            return false;
        }
        m_data[static_cast<size_type>(index)] = value;
        return true;
    }

    bool replace(int index, T&& value) {
        if (!isValidIndex_(index)) {
            return false;
        }
        m_data[static_cast<size_type>(index)] = std::move(value);
        return true;
    }

    bool contains(const T& value) const {
        return std::find(m_data.begin(), m_data.end(), value) != m_data.end();
    }

    int indexOf(const T& value, int from = 0) const {
        if (from < 0) {
            from = 0;
        }
        if (from >= size()) {
            return -1;
        }

        const_iterator beginIt = m_data.begin() + from;
        const_iterator it = std::find(beginIt, m_data.end(), value);
        return it == m_data.end() ? -1 : static_cast<int>(std::distance(m_data.begin(), it));
    }

    int lastIndexOf(const T& value, int from = -1) const {
        if (m_data.empty()) {
            return -1;
        }

        int start = from < 0 || from >= size() ? size() - 1 : from;
        for (int i = start; i >= 0; --i) {
            if (m_data[static_cast<size_type>(i)] == value) {
                return i;
            }
        }
        return -1;
    }

    bool startsWith(const T& value) const {
        return !m_data.empty() && m_data.front() == value;
    }

    bool endsWith(const T& value) const {
        return !m_data.empty() && m_data.back() == value;
    }

    SwDequeue mid(int index, int length = -1) const {
        if (index < 0) {
            index = 0;
        }
        if (index >= size() || length == 0) {
            return SwDequeue();
        }

        const size_type firstIndex = static_cast<size_type>(index);
        const size_type available = m_data.size() - firstIndex;
        const size_type resultLength =
            length < 0 || static_cast<size_type>(length) > available
                ? available
                : static_cast<size_type>(length);

        return SwDequeue(m_data.begin() + firstIndex, m_data.begin() + firstIndex + resultLength);
    }

    void swapItemsAt(int firstIndex, int secondIndex) {
        if (!isValidIndex_(firstIndex) || !isValidIndex_(secondIndex)) {
            throw std::out_of_range("SwDequeue::swapItemsAt index out of range");
        }
        std::swap(m_data[static_cast<size_type>(firstIndex)], m_data[static_cast<size_type>(secondIndex)]);
    }

    void swap(int firstIndex, int secondIndex) {
        swapItemsAt(firstIndex, secondIndex);
    }

    void swap(SwDequeue& other) noexcept {
        m_data.swap(other.m_data);
    }

    void resize(size_type newSize) {
        m_data.resize(newSize);
    }

    void resize(size_type newSize, const T& value) {
        m_data.resize(newSize, value);
    }

    void deleteAll() {
        for (iterator it = m_data.begin(); it != m_data.end(); ++it) {
            delete *it;
        }
        m_data.clear();
    }

    container_type toStdDeque() const {
        return m_data;
    }

    const container_type& stdDeque() const noexcept {
        return m_data;
    }

    container_type& stdDeque() noexcept {
        return m_data;
    }

private:
    void ensureNotEmpty_(const char* functionName) const {
        if (m_data.empty()) {
            throw std::out_of_range(std::string(functionName) + " called on an empty container");
        }
    }

    bool isValidIndex_(int index) const noexcept {
        return index >= 0 && index < size();
    }

    iterator iteratorAtIndex_(int index, const char* functionName) {
        if (!isValidIndex_(index)) {
            throw std::out_of_range(std::string(functionName) + " index out of range");
        }
        return m_data.begin() + index;
    }

    iterator iteratorAtInsertIndex_(int index) {
        if (index < 0 || index > size()) {
            throw std::out_of_range("SwDequeue::insert index out of range");
        }
        return m_data.begin() + index;
    }

    container_type m_data;
};

template<typename T>
using SwDeque = SwDequeue<T>;

#endif // SWDEQUEUE_H
