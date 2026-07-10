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

#ifndef SWBYTERINGBUFFER_H
#define SWBYTERINGBUFFER_H

/**
 * @file src/core/types/SwByteRingBuffer.h
 * @ingroup core_types
 * @brief Declares a dynamic byte ring buffer for stream IO hot paths.
 */

#include "SwByteArray.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

class SwByteRingBuffer {
public:
    SwByteRingBuffer() = default;

    explicit SwByteRingBuffer(std::size_t capacity) {
        reserve(capacity);
    }

    bool isEmpty() const noexcept { return m_size == 0; }
    bool empty() const noexcept { return m_size == 0; }
    std::size_t size() const noexcept { return m_size; }
    std::size_t count() const noexcept { return m_size; }
    std::size_t capacity() const noexcept { return m_buffer.size(); }

    void clear() noexcept {
        m_head = 0;
        m_size = 0;
    }

    void reserve(std::size_t capacity) {
        if (capacity > m_buffer.size()) {
            reallocate_(capacity);
        }
    }

    void append(const SwByteArray& bytes) {
        append(bytes.constData(), bytes.size());
    }

    void append(const char* data, std::size_t length) {
        if (!data || length == 0) {
            return;
        }

        ensureCapacity_(m_size + length);
        const std::size_t tail = physicalIndex_(m_size);
        const std::size_t first = std::min(length, m_buffer.size() - tail);
        std::memcpy(m_buffer.data() + tail, data, first);
        if (first < length) {
            std::memcpy(m_buffer.data(), data + first, length - first);
        }
        m_size += length;
    }

    const char* contiguousData() const noexcept {
        return m_size == 0 ? nullptr : m_buffer.data() + m_head;
    }

    char* contiguousData() noexcept {
        return m_size == 0 ? nullptr : m_buffer.data() + m_head;
    }

    std::size_t contiguousSize() const noexcept {
        if (m_size == 0) {
            return 0;
        }
        return std::min(m_size, m_buffer.size() - m_head);
    }

    char operator[](std::size_t index) const noexcept {
        return index < m_size ? m_buffer[physicalIndex_(index)] : '\0';
    }

    int indexOf(const char* needle) const {
        if (!needle || m_size == 0) {
            return SwByteArray::npos;
        }

        std::size_t needleLength = 0;
        while (needle[needleLength] != '\0') {
            ++needleLength;
        }
        return indexOfData_(needle, needleLength);
    }

    int indexOf(const SwByteArray& needle) const {
        return indexOfData_(needle.constData(), needle.size());
    }

    bool startsWith(const SwByteArray& prefix) const {
        if (prefix.size() > m_size) {
            return false;
        }
        const char* prefixData = prefix.constData();
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            if ((*this)[i] != prefixData[i]) {
                return false;
            }
        }
        return true;
    }

    bool startsWith(const char* prefix) const {
        if (!prefix) {
            return false;
        }
        std::size_t length = 0;
        while (prefix[length] != '\0') {
            ++length;
        }
        if (length > m_size) {
            return false;
        }
        for (std::size_t i = 0; i < length; ++i) {
            if ((*this)[i] != prefix[i]) {
                return false;
            }
        }
        return true;
    }

    SwByteArray left(std::size_t maxLength) const {
        return mid(0, maxLength);
    }

    SwByteArray mid(std::size_t offset, std::size_t maxLength) const {
        if (offset >= m_size) {
            return SwByteArray();
        }

        const std::size_t n = std::min(maxLength, m_size - offset);
        SwByteArray out;
        if (n == 0) {
            return out;
        }
        out.resize(n);
        copyRangeTo_(offset, out.data(), n);
        return out;
    }

    void consume(std::size_t length) noexcept {
        const std::size_t consumed = std::min(length, m_size);
        if (consumed == 0) {
            return;
        }

        m_head = physicalIndex_(consumed);
        m_size -= consumed;
        if (m_size == 0) {
            m_head = 0;
        }
    }

    SwByteArray read(std::size_t maxLength) {
        const std::size_t n = std::min(maxLength, m_size);
        SwByteArray out;
        if (n == 0) {
            return out;
        }

        out.resize(n);
        copyTo_(out.data(), n);
        consume(n);
        return out;
    }

    std::size_t readInto(char* destination, std::size_t maxLength) {
        const std::size_t n = std::min(maxLength, m_size);
        if (!destination || n == 0) {
            return 0;
        }

        copyTo_(destination, n);
        consume(n);
        return n;
    }

    std::size_t readTo(SwByteArray& destination, std::size_t maxLength) {
        const std::size_t n = std::min(maxLength, m_size);
        if (n == 0) {
            return 0;
        }

        const std::size_t oldSize = destination.size();
        destination.resize(oldSize + n);
        copyTo_(destination.data() + oldSize, n);
        consume(n);
        return n;
    }

    SwByteArray toByteArray() const {
        SwByteArray out;
        if (m_size == 0) {
            return out;
        }
        out.resize(m_size);
        copyTo_(out.data(), m_size);
        return out;
    }

private:
    int indexOfData_(const char* needle, std::size_t needleLength) const {
        if (!needle || needleLength > m_size) {
            return SwByteArray::npos;
        }
        if (needleLength == 0) {
            return 0;
        }

        const std::size_t limit = m_size - needleLength;
        for (std::size_t i = 0; i <= limit; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needleLength; ++j) {
                if ((*this)[i + j] != needle[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return static_cast<int>(i);
            }
        }
        return SwByteArray::npos;
    }

    std::size_t physicalIndex_(std::size_t logicalOffset) const noexcept {
        const std::size_t capacity = m_buffer.size();
        if (capacity == 0) {
            return 0;
        }
        std::size_t index = m_head + logicalOffset;
        if (index >= capacity) {
            index -= capacity;
        }
        return index;
    }

    void ensureCapacity_(std::size_t required) {
        if (required <= m_buffer.size()) {
            return;
        }

        std::size_t nextCapacity = m_buffer.empty() ? static_cast<std::size_t>(4096) : m_buffer.size();
        while (nextCapacity < required) {
            nextCapacity *= 2;
        }
        reallocate_(nextCapacity);
    }

    void reallocate_(std::size_t capacity) {
        std::vector<char> next(capacity);
        if (m_size != 0) {
            copyTo_(next.data(), m_size);
        }
        m_buffer.swap(next);
        m_head = 0;
    }

    void copyTo_(char* destination, std::size_t length) const {
        copyRangeTo_(0, destination, length);
    }

    void copyRangeTo_(std::size_t offset, char* destination, std::size_t length) const {
        if (!destination || length == 0 || m_size == 0) {
            return;
        }
        if (offset >= m_size) {
            return;
        }

        const std::size_t n = std::min(length, m_size - offset);
        const std::size_t start = physicalIndex_(offset);
        const std::size_t first = std::min(n, m_buffer.size() - start);
        std::memcpy(destination, m_buffer.data() + start, first);
        if (first < n) {
            std::memcpy(destination + first, m_buffer.data(), n - first);
        }
    }

    std::vector<char> m_buffer;
    std::size_t m_head{0};
    std::size_t m_size{0};
};

#endif // SWBYTERINGBUFFER_H
