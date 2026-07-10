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

#ifndef SWRINGQUEUE_H
#define SWRINGQUEUE_H

/**
 * @file src/core/types/SwRingQueue.h
 * @ingroup core_types
 * @brief Declares a fixed-capacity FIFO ring queue for hot paths.
 */

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>

template<typename T>
class SwRingQueue {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    SwRingQueue() = default;

    explicit SwRingQueue(size_type capacity) {
        allocate_(capacity);
    }

    SwRingQueue(std::initializer_list<T> init)
        : SwRingQueue(init.size()) {
        for (const T& value : init) {
            enqueue(value);
        }
    }

    template<typename InputIt>
    SwRingQueue(InputIt first, InputIt last)
        : SwRingQueue(static_cast<size_type>(std::distance(first, last))) {
        for (; first != last; ++first) {
            enqueue(*first);
        }
    }

    SwRingQueue(const SwRingQueue& other)
        : SwRingQueue(other.m_capacity) {
        for (size_type i = 0; i < other.m_size; ++i) {
            enqueue(other[i]);
        }
    }

    SwRingQueue(SwRingQueue&& other) noexcept {
        moveFrom_(other);
    }

    ~SwRingQueue() {
        releaseStorage_();
    }

    SwRingQueue& operator=(const SwRingQueue& other) {
        if (this == &other) {
            return *this;
        }
        SwRingQueue copy(other);
        swap(copy);
        return *this;
    }

    SwRingQueue& operator=(SwRingQueue&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        releaseStorage_();
        moveFrom_(other);
        return *this;
    }

    bool operator==(const SwRingQueue& other) const {
        if (m_size != other.m_size) {
            return false;
        }
        for (size_type i = 0; i < m_size; ++i) {
            if ((*this)[i] != other[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const SwRingQueue& other) const {
        return !(*this == other);
    }

    reference operator[](size_type index) {
        return *ptrAt_(physicalIndex_(index));
    }

    const_reference operator[](size_type index) const {
        return *ptrAt_(physicalIndex_(index));
    }

    reference at(size_type index) {
        if (index >= m_size) {
            throw std::out_of_range("SwRingQueue::at index out of range");
        }
        return (*this)[index];
    }

    const_reference at(size_type index) const {
        if (index >= m_size) {
            throw std::out_of_range("SwRingQueue::at index out of range");
        }
        return (*this)[index];
    }

    bool isEmpty() const noexcept { return m_size == 0; }
    bool empty() const noexcept { return m_size == 0; }
    bool isFull() const noexcept { return m_capacity != 0 && m_size == m_capacity; }
    size_type size() const noexcept { return m_size; }
    size_type count() const noexcept { return m_size; }
    size_type capacity() const noexcept { return m_capacity; }

    void reserveCapacity(size_type capacity) {
        if (capacity > m_capacity) {
            setCapacity(capacity);
        }
    }

    size_type setCapacity(size_type capacity) {
        if (capacity == m_capacity) {
            return 0;
        }

        const size_type keepCount = std::min(m_size, capacity);
        const size_type droppedCount = m_size - keepCount;
        allocator_type allocator;
        T* nextData = capacity > 0 ? allocator.allocate(capacity) : nullptr;
        size_type constructed = 0;

        try {
            const size_type firstKept = m_size - keepCount;
            for (; constructed < keepCount; ++constructed) {
                traits_type::construct(allocator,
                                       nextData + constructed,
                                       std::move((*this)[firstKept + constructed]));
            }
        } catch (...) {
            for (size_type i = 0; i < constructed; ++i) {
                traits_type::destroy(allocator, nextData + i);
            }
            if (nextData) {
                allocator.deallocate(nextData, capacity);
            }
            throw;
        }

        releaseStorage_();
        m_data = nextData;
        m_capacity = capacity;
        m_head = 0;
        m_size = keepCount;
        return droppedCount;
    }

    void clear() noexcept {
        destroyAll_();
        m_head = 0;
        m_size = 0;
    }

    bool enqueue(const T& value) {
        return tryConstructBack_(value);
    }

    bool enqueue(T&& value) {
        return tryConstructBack_(std::move(value));
    }

    bool append(const T& value) {
        return enqueue(value);
    }

    bool append(T&& value) {
        return enqueue(std::move(value));
    }

    bool push_back(const T& value) {
        return enqueue(value);
    }

    bool push_back(T&& value) {
        return enqueue(std::move(value));
    }

    template<typename... Args>
    bool emplaceBack(Args&&... args) {
        if (m_size >= m_capacity) {
            return false;
        }
        traits_type::construct(m_allocator, ptrAt_(tailIndex_()), std::forward<Args>(args)...);
        ++m_size;
        return true;
    }

    template<typename... Args>
    bool emplace_back(Args&&... args) {
        return emplaceBack(std::forward<Args>(args)...);
    }

    bool enqueueOverwrite(const T& value, T* dropped = nullptr) {
        return overwriteBack_(value, dropped);
    }

    bool enqueueOverwrite(T&& value, T* dropped = nullptr) {
        return overwriteBack_(std::move(value), dropped);
    }

    template<typename... Args>
    bool emplaceBackOverwrite(T* dropped, Args&&... args) {
        if (m_capacity == 0) {
            return false;
        }
        if (m_size == m_capacity) {
            moveFrontTo_(dropped);
            destroyFront_();
        }
        return emplaceBack(std::forward<Args>(args)...);
    }

    bool tryDequeue(T& out) {
        if (m_size == 0) {
            return false;
        }
        out = std::move(front());
        destroyFront_();
        return true;
    }

    T dequeue() {
        return takeFirst();
    }

    T takeFirst() {
        if (m_size == 0) {
            throw std::out_of_range("SwRingQueue::takeFirst called on an empty queue");
        }
        T value = std::move(front());
        destroyFront_();
        return value;
    }

    T takeLast() {
        if (m_size == 0) {
            throw std::out_of_range("SwRingQueue::takeLast called on an empty queue");
        }
        const size_type index = physicalIndex_(m_size - 1);
        T value = std::move(*ptrAt_(index));
        traits_type::destroy(m_allocator, ptrAt_(index));
        --m_size;
        return value;
    }

    void pop_front() {
        if (m_size != 0) {
            destroyFront_();
        }
    }

    void pop_back() {
        if (m_size == 0) {
            return;
        }
        const size_type index = physicalIndex_(m_size - 1);
        traits_type::destroy(m_allocator, ptrAt_(index));
        --m_size;
    }

    void removeFirst() { pop_front(); }
    void removeLast() { pop_back(); }

    reference front() {
        if (m_size == 0) {
            throw std::out_of_range("SwRingQueue::front called on an empty queue");
        }
        return *ptrAt_(m_head);
    }

    const_reference front() const {
        if (m_size == 0) {
            throw std::out_of_range("SwRingQueue::front called on an empty queue");
        }
        return *ptrAt_(m_head);
    }

    reference back() {
        if (m_size == 0) {
            throw std::out_of_range("SwRingQueue::back called on an empty queue");
        }
        return *ptrAt_(physicalIndex_(m_size - 1));
    }

    const_reference back() const {
        if (m_size == 0) {
            throw std::out_of_range("SwRingQueue::back called on an empty queue");
        }
        return *ptrAt_(physicalIndex_(m_size - 1));
    }

    reference first() { return front(); }
    const_reference first() const { return front(); }
    reference head() { return front(); }
    const_reference head() const { return front(); }
    reference last() { return back(); }
    const_reference last() const { return back(); }

    void swap(SwRingQueue& other) noexcept {
        using std::swap;
        swap(m_allocator, other.m_allocator);
        swap(m_data, other.m_data);
        swap(m_capacity, other.m_capacity);
        swap(m_head, other.m_head);
        swap(m_size, other.m_size);
    }

private:
    using allocator_type = std::allocator<T>;
    using traits_type = std::allocator_traits<allocator_type>;

    template<typename U>
    bool tryConstructBack_(U&& value) {
        if (m_size >= m_capacity) {
            return false;
        }
        traits_type::construct(m_allocator, ptrAt_(tailIndex_()), std::forward<U>(value));
        ++m_size;
        return true;
    }

    template<typename U>
    bool overwriteBack_(U&& value, T* dropped) {
        if (m_capacity == 0) {
            return false;
        }
        if (m_size == m_capacity) {
            moveFrontTo_(dropped);
            destroyFront_();
        }
        return tryConstructBack_(std::forward<U>(value));
    }

    void moveFrontTo_(T* out) {
        if (out) {
            *out = std::move(front());
        }
    }

    T* ptrAt_(size_type physicalIndex) const noexcept {
        return m_data + physicalIndex;
    }

    size_type physicalIndex_(size_type logicalIndex) const noexcept {
        size_type index = m_head + logicalIndex;
        if (index >= m_capacity) {
            index -= m_capacity;
        }
        return index;
    }

    size_type tailIndex_() const noexcept {
        size_type index = m_head + m_size;
        if (index >= m_capacity) {
            index -= m_capacity;
        }
        return index;
    }

    void destroyFront_() noexcept {
        traits_type::destroy(m_allocator, ptrAt_(m_head));
        ++m_head;
        if (m_head == m_capacity) {
            m_head = 0;
        }
        --m_size;
        if (m_size == 0) {
            m_head = 0;
        }
    }

    void destroyAll_() noexcept {
        while (m_size != 0) {
            destroyFront_();
        }
    }

    void allocate_(size_type capacity) {
        if (capacity == 0) {
            return;
        }
        m_data = m_allocator.allocate(capacity);
        m_capacity = capacity;
    }

    void releaseStorage_() noexcept {
        destroyAll_();
        if (m_data) {
            m_allocator.deallocate(m_data, m_capacity);
        }
        m_data = nullptr;
        m_capacity = 0;
        m_head = 0;
    }

    void moveFrom_(SwRingQueue& other) noexcept {
        m_data = other.m_data;
        m_capacity = other.m_capacity;
        m_head = other.m_head;
        m_size = other.m_size;

        other.m_data = nullptr;
        other.m_capacity = 0;
        other.m_head = 0;
        other.m_size = 0;
    }

    allocator_type m_allocator;
    T* m_data{nullptr};
    size_type m_capacity{0};
    size_type m_head{0};
    size_type m_size{0};
};

#endif // SWRINGQUEUE_H
