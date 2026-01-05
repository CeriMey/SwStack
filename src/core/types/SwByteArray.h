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

#ifndef SWBYTEARRAY_H
#define SWBYTEARRAY_H
#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "SwCrypto.h"
#include "SwList.h"

class SwByteArray {
public:
    typedef char value_type;
    typedef char* iterator;
    typedef const char* const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

    static constexpr int npos = -1;

    SwByteArray();
    explicit SwByteArray(const char* str);
    SwByteArray(const char* data, size_t size);
    SwByteArray(const std::string& str);
    SwByteArray(size_t size, char ch);
    SwByteArray(std::initializer_list<char> list);
    SwByteArray(const SwByteArray& other);
    SwByteArray(SwByteArray&& other) noexcept;
    ~SwByteArray() = default;

    SwByteArray& operator=(const SwByteArray& other);
    SwByteArray& operator=(SwByteArray&& other) noexcept;
    SwByteArray& operator=(const char* str);
    SwByteArray& operator=(const std::string& str);
    SwByteArray& operator=(std::initializer_list<char> list);

    char& operator[](size_t index);
    const char& operator[](size_t index) const;

    bool operator==(const SwByteArray& other) const;
    bool operator!=(const SwByteArray& other) const { return !(*this == other); }
    bool operator<(const SwByteArray& other) const;
    bool operator>(const SwByteArray& other) const { return other < *this; }
    bool operator<=(const SwByteArray& other) const { return !(other < *this); }
    bool operator>=(const SwByteArray& other) const { return !(*this < other); }

    SwByteArray& operator+=(const SwByteArray& other);
    SwByteArray& operator+=(const char* str);
    SwByteArray& operator+=(char ch);

    friend SwByteArray operator+(const SwByteArray& lhs, const SwByteArray& rhs) {
        SwByteArray tmp(lhs);
        tmp += rhs;
        return tmp;
    }

    friend SwByteArray operator+(const SwByteArray& lhs, const char* rhs) {
        SwByteArray tmp(lhs);
        tmp += rhs;
        return tmp;
    }

    friend SwByteArray operator+(const char* lhs, const SwByteArray& rhs) {
        SwByteArray tmp(lhs);
        tmp += rhs;
        return tmp;
    }

    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    reverse_iterator rbegin();
    reverse_iterator rend();
    const_reverse_iterator rbegin() const;
    const_reverse_iterator rend() const;

    size_t size() const { return size_; }
    size_t length() const { return size_; }
    bool isEmpty() const { return size_ == 0; }
    bool isNull() const { return null_; }
    size_t capacity() const { return buffer_.capacity() > 0 ? buffer_.capacity() - 1 : 0; }

    const char* constData() const { return null_ ? nullptr : buffer_.data(); }
    const char* data() const { return constData(); }
    char* data();
    operator const char*() const { return constData(); }

    std::string toStdString() const {
        if (null_) return std::string();
        return std::string(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size_));
    }

    static SwByteArray fromStdString(const std::string& str) { return SwByteArray(str); }

    char at(size_t index) const;
    char front() const;
    char back() const;

    void clear();
    void reserve(size_t capacity);
    void squeeze();
    void resize(size_t newSize, char fillChar = '\0');
    SwByteArray& fill(char ch, int size = -1);

    SwByteArray& append(const SwByteArray& other);
    SwByteArray& append(const char* data, size_t len);
    SwByteArray& append(const char* str);
    SwByteArray& append(const std::string& str) { return append(str.data(), str.size()); }
    SwByteArray& append(char ch);

    SwByteArray& prepend(const SwByteArray& other);
    SwByteArray& prepend(const char* data, size_t len);
    SwByteArray& prepend(const char* str);
    SwByteArray& prepend(char ch);

    SwByteArray& insert(int index, char ch);
    SwByteArray& insert(int index, const SwByteArray& other);
    SwByteArray& insert(int index, const char* data, size_t len);

    SwByteArray& remove(int index, int len);
    SwByteArray& replace(int index, int len, const SwByteArray& with);
    SwByteArray& replace(int index, int len, const char* data, size_t dataLen);
    SwByteArray& replace(const SwByteArray& before, const SwByteArray& after);
    SwByteArray& replace(const char* before, const char* after);
    SwByteArray& replace(char before, char after);

    SwByteArray& push_back(char ch) { return append(ch); }
    SwByteArray& push_front(char ch) { return prepend(ch); }
    void pop_back();
    void pop_front();

    SwByteArray& chop(int len);
    SwByteArray& truncate(int len);
    SwByteArray chopped(int len) const;
    SwByteArray truncated(int len) const;
    SwByteArray left(int len) const;
    SwByteArray right(int len) const;
    SwByteArray mid(int pos, int len = -1) const;
    SwByteArray first(int len) const { return left(len); }
    SwByteArray last(int len) const { return right(len); }
    SwByteArray repeated(int times) const;
    SwByteArray reversed() const;

    SwByteArray trimmed() const;
    SwByteArray simplified() const;
    SwByteArray toLower() const;
    SwByteArray toUpper() const;

    bool startsWith(const SwByteArray& other) const;
    bool startsWith(const char* str) const;
    bool startsWith(char ch) const;
    bool endsWith(const SwByteArray& other) const;
    bool endsWith(const char* str) const;
    bool endsWith(char ch) const;

    bool contains(const SwByteArray& other) const { return indexOf(other) != npos; }
    bool contains(const char* str) const { return indexOf(str) != npos; }
    bool contains(char ch) const { return indexOf(ch) != npos; }

    int indexOf(const SwByteArray& other, int from = 0) const;
    int indexOf(const char* str, int from = 0) const;
    int indexOf(char ch, int from = 0) const;
    int lastIndexOf(const SwByteArray& other, int from = -1) const;
    int lastIndexOf(const char* str, int from = -1) const;
    int lastIndexOf(char ch, int from = -1) const;

    int count(const SwByteArray& other) const;
    int count(const char* str) const;
    int count(char ch) const;

    SwList<SwByteArray> split(char delimiter, bool keepEmptyParts = true) const;

    SwByteArray toHex() const;
    static SwByteArray fromHex(const SwByteArray& hex);
    SwByteArray toBase64() const;
    static SwByteArray fromBase64(const SwByteArray& base64);

    long long toLongLong(bool* ok = nullptr, int base = 10) const;
    unsigned long long toULongLong(bool* ok = nullptr, int base = 10) const;
    int toInt(bool* ok = nullptr, int base = 10) const;
    double toDouble(bool* ok = nullptr) const;

    SwByteArray& setNum(long long value, int base = 10);
    SwByteArray& setNum(unsigned long long value, int base = 10);
    SwByteArray& setNum(double value, char format = 'g', int precision = 6);

    static SwByteArray number(long long value, int base = 10);
    static SwByteArray number(unsigned long long value, int base = 10);
    static SwByteArray number(double value, char format = 'g', int precision = 6);

    static SwByteArray fromRawData(const char* data, size_t len) { return SwByteArray(data, len); }

    int compare(const SwByteArray& other) const;
    int compare(const char* str) const;

    void swap(SwByteArray& other) noexcept;

    friend std::ostream& operator<<(std::ostream& os, const SwByteArray& array) {
        if (!array.null_ && array.size_ > 0) {
            os.write(array.buffer_.data(), static_cast<std::streamsize>(array.size_));
        }
        return os;
    }

private:
    std::vector<char> buffer_;
    size_t size_;
    bool null_;

    static size_t sanitizePosition(int pos, size_t limit);
    void ensureNotNull();
    void ensureNullTerminator();
    void assertPointerIfNeeded(const char* data, size_t len) const;
    void assignInternal(const char* data, size_t len);
    SwByteArray slice(size_t pos, size_t len) const;
    void insertData(size_t pos, const char* data, size_t len);
    bool overlaps(const char* data, size_t len) const;
    int indexOfRaw(const char* needle, size_t len, int from) const;
    int lastIndexOfRaw(const char* needle, size_t len, int from) const;
    static unsigned char toUChar(char ch) { return static_cast<unsigned char>(ch); }
    static int hexValue(char ch);
};

//--------------------------------------------------------------------------------------------------
// Constructors
//--------------------------------------------------------------------------------------------------

inline SwByteArray::SwByteArray()
    : buffer_(1, '\0'),
      size_(0),
      null_(true) {}

inline SwByteArray::SwByteArray(const char* str)
    : buffer_(1, '\0'),
      size_(0),
      null_(true) {
    if (str) {
        assignInternal(str, std::strlen(str));
    }
}

inline SwByteArray::SwByteArray(const char* data, size_t size)
    : buffer_(1, '\0'),
      size_(0),
      null_(true) {
    if (data) {
        assignInternal(data, size);
    } else if (size > 0) {
        throw std::invalid_argument("SwByteArray: null data with length.");
    }
}

inline SwByteArray::SwByteArray(const std::string& str)
    : buffer_(1, '\0'),
      size_(0),
      null_(true) {
    assignInternal(str.data(), str.size());
}

inline SwByteArray::SwByteArray(size_t size, char ch)
    : buffer_(size + 1, '\0'),
      size_(size),
      null_(false) {
    std::fill(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size_), ch);
}

inline SwByteArray::SwByteArray(std::initializer_list<char> list)
    : buffer_(list.size() + 1, '\0'),
      size_(list.size()),
      null_(false) {
    std::copy(list.begin(), list.end(), buffer_.begin());
}

inline SwByteArray::SwByteArray(const SwByteArray& other)
    : buffer_(other.buffer_),
      size_(other.size_),
      null_(other.null_) {
    ensureNullTerminator();
}

inline SwByteArray::SwByteArray(SwByteArray&& other) noexcept
    : buffer_(std::move(other.buffer_)),
      size_(other.size_),
      null_(other.null_) {
    ensureNullTerminator();
    other.size_ = 0;
    other.null_ = true;
    other.buffer_.assign(1, '\0');
}

inline SwByteArray& SwByteArray::operator=(const SwByteArray& other) {
    if (this == &other) {
        return *this;
    }
    buffer_ = other.buffer_;
    size_ = other.size_;
    null_ = other.null_;
    ensureNullTerminator();
    return *this;
}

inline SwByteArray& SwByteArray::operator=(SwByteArray&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    buffer_ = std::move(other.buffer_);
    size_ = other.size_;
    null_ = other.null_;
    ensureNullTerminator();
    other.size_ = 0;
    other.null_ = true;
    other.buffer_.assign(1, '\0');
    return *this;
}

inline SwByteArray& SwByteArray::operator=(const char* str) {
    if (!str) {
        size_ = 0;
        null_ = true;
        buffer_.assign(1, '\0');
        return *this;
    }
    assignInternal(str, std::strlen(str));
    return *this;
}

inline SwByteArray& SwByteArray::operator=(const std::string& str) {
    assignInternal(str.data(), str.size());
    return *this;
}

inline SwByteArray& SwByteArray::operator=(std::initializer_list<char> list) {
    assignInternal(list.size() ? list.begin() : nullptr, list.size());
    return *this;
}

//--------------------------------------------------------------------------------------------------
// Iterators
//--------------------------------------------------------------------------------------------------

inline SwByteArray::iterator SwByteArray::begin() {
    return null_ ? nullptr : buffer_.data();
}

inline SwByteArray::iterator SwByteArray::end() {
    return null_ ? nullptr : buffer_.data() + static_cast<std::ptrdiff_t>(size_);
}

inline SwByteArray::const_iterator SwByteArray::begin() const {
    return null_ ? nullptr : buffer_.data();
}

inline SwByteArray::const_iterator SwByteArray::end() const {
    return null_ ? nullptr : buffer_.data() + static_cast<std::ptrdiff_t>(size_);
}

inline SwByteArray::reverse_iterator SwByteArray::rbegin() {
    return reverse_iterator(end());
}

inline SwByteArray::reverse_iterator SwByteArray::rend() {
    return reverse_iterator(begin());
}

inline SwByteArray::const_reverse_iterator SwByteArray::rbegin() const {
    return const_reverse_iterator(end());
}

inline SwByteArray::const_reverse_iterator SwByteArray::rend() const {
    return const_reverse_iterator(begin());
}

//--------------------------------------------------------------------------------------------------
// Element access
//--------------------------------------------------------------------------------------------------

inline char* SwByteArray::data() {
    ensureNotNull();
    ensureNullTerminator();
    return buffer_.data();
}

inline char SwByteArray::at(size_t index) const {
    if (index >= size_ || null_) {
        throw std::out_of_range("SwByteArray::at out of range.");
    }
    return buffer_[static_cast<std::ptrdiff_t>(index)];
}

inline char& SwByteArray::operator[](size_t index) {
    if (index >= size_) {
        throw std::out_of_range("SwByteArray::operator[] out of range.");
    }
    ensureNotNull();
    return buffer_[static_cast<std::ptrdiff_t>(index)];
}

inline const char& SwByteArray::operator[](size_t index) const {
    if (index >= size_ || null_) {
        throw std::out_of_range("SwByteArray::operator[] const out of range.");
    }
    return buffer_[static_cast<std::ptrdiff_t>(index)];
}

inline char SwByteArray::front() const {
    if (isEmpty() || null_) {
        throw std::out_of_range("SwByteArray::front on empty array.");
    }
    return buffer_.front();
}

inline char SwByteArray::back() const {
    if (isEmpty() || null_) {
        throw std::out_of_range("SwByteArray::back on empty array.");
    }
    return buffer_[static_cast<std::ptrdiff_t>(size_ - 1)];
}

//--------------------------------------------------------------------------------------------------
// Comparisons & arithmetic
//--------------------------------------------------------------------------------------------------

inline bool SwByteArray::operator==(const SwByteArray& other) const {
    if (null_ && other.null_) return true;
    if (null_ != other.null_) return false;
    if (size_ != other.size_) return false;
    if (size_ == 0) return true;
    return std::memcmp(buffer_.data(), other.buffer_.data(), size_) == 0;
}

inline bool SwByteArray::operator<(const SwByteArray& other) const {
    if (null_ && other.null_) return false;
    if (null_) return !other.isEmpty();
    if (other.null_) return false;
    const size_t minSize = std::min(size_, other.size_);
    int cmp = std::memcmp(buffer_.data(), other.buffer_.data(), minSize);
    if (cmp < 0) return true;
    if (cmp > 0) return false;
    return size_ < other.size_;
}

inline SwByteArray& SwByteArray::operator+=(const SwByteArray& other) {
    return append(other);
}

inline SwByteArray& SwByteArray::operator+=(const char* str) {
    return append(str);
}

inline SwByteArray& SwByteArray::operator+=(char ch) {
    return append(ch);
}

//--------------------------------------------------------------------------------------------------
// Basic modifiers
//--------------------------------------------------------------------------------------------------

inline void SwByteArray::clear() {
    ensureNotNull();
    size_ = 0;
    ensureNullTerminator();
}

inline void SwByteArray::reserve(size_t capacity) {
    ensureNotNull();
    buffer_.reserve(capacity + 1);
    ensureNullTerminator();
}

inline void SwByteArray::squeeze() {
    ensureNotNull();
    buffer_.shrink_to_fit();
    ensureNullTerminator();
}

inline void SwByteArray::resize(size_t newSize, char fillChar) {
    ensureNotNull();
    if (newSize > size_) {
        const size_t toInsert = newSize - size_;
        buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(size_), toInsert, fillChar);
    }
    size_ = newSize;
    ensureNullTerminator();
}

inline SwByteArray& SwByteArray::fill(char ch, int size) {
    ensureNotNull();
    size_t target = size < 0 ? size_ : static_cast<size_t>(size);
    resize(target);
    std::fill(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(target), ch);
    return *this;
}

inline bool SwByteArray::overlaps(const char* data, size_t len) const {
    if (null_ || !data || len == 0) return false;
    const char* start = buffer_.data();
    const char* end = start + static_cast<std::ptrdiff_t>(size_);
    return data >= start && data < end;
}

inline void SwByteArray::insertData(size_t pos, const char* data, size_t len) {
    assertPointerIfNeeded(data, len);
    ensureNotNull();
    if (len == 0) {
        return;
    }
    if (overlaps(data, len)) {
        std::vector<char> copy(data, data + len);
        buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(pos), copy.begin(), copy.end());
    } else {
        buffer_.insert(buffer_.begin() + static_cast<std::ptrdiff_t>(pos), data, data + len);
    }
    size_ += len;
    ensureNullTerminator();
}

//--------------------------------------------------------------------------------------------------
// Append / prepend / insert
//--------------------------------------------------------------------------------------------------

inline SwByteArray& SwByteArray::append(const SwByteArray& other) {
    if (other.null_ || other.size_ == 0) {
        ensureNotNull();
        return *this;
    }
    insertData(size_, other.constData(), other.size_);
    return *this;
}

inline SwByteArray& SwByteArray::append(const char* data, size_t len) {
    insertData(size_, data, len);
    return *this;
}

inline SwByteArray& SwByteArray::append(const char* str) {
    if (!str) {
        return *this;
    }
    return append(str, std::strlen(str));
}

inline SwByteArray& SwByteArray::append(char ch) {
    insertData(size_, &ch, 1);
    return *this;
}

inline SwByteArray& SwByteArray::prepend(const SwByteArray& other) {
    if (other.null_ || other.size_ == 0) {
        ensureNotNull();
        return *this;
    }
    insertData(0, other.constData(), other.size_);
    return *this;
}

inline SwByteArray& SwByteArray::prepend(const char* data, size_t len) {
    insertData(0, data, len);
    return *this;
}

inline SwByteArray& SwByteArray::prepend(const char* str) {
    if (!str) {
        return *this;
    }
    return prepend(str, std::strlen(str));
}

inline SwByteArray& SwByteArray::prepend(char ch) {
    insertData(0, &ch, 1);
    return *this;
}

inline SwByteArray& SwByteArray::insert(int index, char ch) {
    insertData(sanitizePosition(index, size_), &ch, 1);
    return *this;
}

inline SwByteArray& SwByteArray::insert(int index, const SwByteArray& other) {
    insertData(sanitizePosition(index, size_), other.constData(), other.size_);
    return *this;
}

inline SwByteArray& SwByteArray::insert(int index, const char* data, size_t len) {
    insertData(sanitizePosition(index, size_), data, len);
    return *this;
}

inline SwByteArray& SwByteArray::remove(int index, int len) {
    ensureNotNull();
    if (len <= 0 || size_ == 0) return *this;
    size_t pos = sanitizePosition(index, size_);
    if (pos >= size_) return *this;
    size_t count = std::min(static_cast<size_t>(len), size_ - pos);
    buffer_.erase(buffer_.begin() + static_cast<std::ptrdiff_t>(pos),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(pos + count));
    size_ -= count;
    ensureNullTerminator();
    return *this;
}

inline SwByteArray& SwByteArray::replace(int index, int len, const SwByteArray& with) {
    return replace(index, len, with.constData(), with.size_);
}

inline SwByteArray& SwByteArray::replace(int index, int len, const char* data, size_t dataLen) {
    ensureNotNull();
    size_t pos = sanitizePosition(index, size_);
    size_t toRemove = len < 0 ? size_ - pos : std::min(static_cast<size_t>(len), size_ - pos);
    if (toRemove > 0) {
        buffer_.erase(buffer_.begin() + static_cast<std::ptrdiff_t>(pos),
                      buffer_.begin() + static_cast<std::ptrdiff_t>(pos + toRemove));
        size_ -= toRemove;
    }
    insertData(pos, data, dataLen);
    return *this;
}

inline SwByteArray& SwByteArray::replace(const SwByteArray& before, const SwByteArray& after) {
    if (before.null_ || before.size_ == 0 || null_) {
        return *this;
    }
    int pos = 0;
    while ((pos = indexOf(before, pos)) != npos) {
        replace(pos, static_cast<int>(before.size_), after);
        pos += static_cast<int>(after.size_);
    }
    return *this;
}

inline SwByteArray& SwByteArray::replace(const char* before, const char* after) {
    SwByteArray beforeArr(before ? before : "");
    SwByteArray afterArr(after ? after : "");
    return replace(beforeArr, afterArr);
}

inline SwByteArray& SwByteArray::replace(char before, char after) {
    ensureNotNull();
    for (size_t i = 0; i < size_; ++i) {
        if (buffer_[static_cast<std::ptrdiff_t>(i)] == before) {
            buffer_[static_cast<std::ptrdiff_t>(i)] = after;
        }
    }
    return *this;
}

inline void SwByteArray::pop_back() {
    if (isEmpty()) return;
    --size_;
    ensureNullTerminator();
}

inline void SwByteArray::pop_front() {
    if (isEmpty()) return;
    buffer_.erase(buffer_.begin());
    --size_;
    ensureNullTerminator();
}

inline SwByteArray& SwByteArray::chop(int len) {
    if (len <= 0 || size_ == 0) return *this;
    size_t toRemove = static_cast<size_t>(len);
    if (toRemove >= size_) size_ = 0;
    else size_ -= toRemove;
    ensureNullTerminator();
    return *this;
}

inline SwByteArray& SwByteArray::truncate(int len) {
    if (len < 0) size_ = 0;
    else if (static_cast<size_t>(len) < size_) size_ = static_cast<size_t>(len);
    ensureNullTerminator();
    return *this;
}

inline SwByteArray SwByteArray::chopped(int len) const {
    SwByteArray copy(*this);
    copy.chop(len);
    return copy;
}

inline SwByteArray SwByteArray::truncated(int len) const {
    SwByteArray copy(*this);
    copy.truncate(len);
    return copy;
}

inline SwByteArray SwByteArray::left(int len) const {
    if (len <= 0 || null_) return SwByteArray();
    size_t count = std::min(static_cast<size_t>(len), size_);
    return slice(0, count);
}

inline SwByteArray SwByteArray::right(int len) const {
    if (len <= 0 || null_) return SwByteArray();
    size_t count = std::min(static_cast<size_t>(len), size_);
    return slice(size_ - count, count);
}

inline SwByteArray SwByteArray::mid(int pos, int len) const {
    if (null_) return SwByteArray();
    size_t start = sanitizePosition(pos, size_);
    size_t length = len < 0 ? size_ - start : std::min(static_cast<size_t>(len), size_ - start);
    return slice(start, length);
}

inline SwByteArray SwByteArray::repeated(int times) const {
    if (times <= 0 || null_) return SwByteArray();
    SwByteArray result(static_cast<size_t>(times) * size_, '\0');
    if (size_ == 0) return result;
    for (int i = 0; i < times; ++i) {
        std::copy(buffer_.begin(),
                  buffer_.begin() + static_cast<std::ptrdiff_t>(size_),
                  result.buffer_.begin() + static_cast<std::ptrdiff_t>(i * size_));
    }
    return result;
}

inline SwByteArray SwByteArray::reversed() const {
    if (null_) return SwByteArray();
    SwByteArray copy(*this);
    std::reverse(copy.buffer_.begin(), copy.buffer_.begin() + static_cast<std::ptrdiff_t>(size_));
    return copy;
}

inline SwByteArray SwByteArray::trimmed() const {
    if (null_) return SwByteArray();
    size_t start = 0;
    while (start < size_ && std::isspace(toUChar(buffer_[static_cast<std::ptrdiff_t>(start)]))) {
        ++start;
    }
    size_t end = size_;
    while (end > start && std::isspace(toUChar(buffer_[static_cast<std::ptrdiff_t>(end - 1)]))) {
        --end;
    }
    return slice(start, end - start);
}

inline SwByteArray SwByteArray::simplified() const {
    if (null_) return SwByteArray();
    SwByteArray result;
    result.ensureNotNull();
    for (size_t i = 0; i < size_; ++i) {
        char ch = buffer_[static_cast<std::ptrdiff_t>(i)];
        if (std::isspace(toUChar(ch))) {
            if (!result.isEmpty() &&
                result.buffer_[static_cast<std::ptrdiff_t>(result.size_ - 1)] != ' ') {
                result.append(' ');
            }
        } else {
            result.append(ch);
        }
    }
    if (!result.isEmpty() &&
        result.buffer_[static_cast<std::ptrdiff_t>(result.size_ - 1)] == ' ') {
        result.chop(1);
    }
    return result;
}

inline SwByteArray SwByteArray::toLower() const {
    if (null_) return SwByteArray();
    SwByteArray copy(*this);
    for (size_t i = 0; i < copy.size_; ++i) {
        copy.buffer_[static_cast<std::ptrdiff_t>(i)] =
            static_cast<char>(std::tolower(toUChar(copy.buffer_[static_cast<std::ptrdiff_t>(i)])));
    }
    return copy;
}

inline SwByteArray SwByteArray::toUpper() const {
    if (null_) return SwByteArray();
    SwByteArray copy(*this);
    for (size_t i = 0; i < copy.size_; ++i) {
        copy.buffer_[static_cast<std::ptrdiff_t>(i)] =
            static_cast<char>(std::toupper(toUChar(copy.buffer_[static_cast<std::ptrdiff_t>(i)])));
    }
    return copy;
}

inline bool SwByteArray::startsWith(const SwByteArray& other) const {
    if (other.null_) return true;
    if (other.size_ > size_ || null_) return other.size_ == 0;
    return std::equal(other.buffer_.begin(),
                      other.buffer_.begin() + static_cast<std::ptrdiff_t>(other.size_),
                      buffer_.begin());
}

inline bool SwByteArray::startsWith(const char* str) const {
    if (!str) return false;
    SwByteArray other(str);
    return startsWith(other);
}

inline bool SwByteArray::startsWith(char ch) const {
    return !isEmpty() && !null_ && buffer_.front() == ch;
}

inline bool SwByteArray::endsWith(const SwByteArray& other) const {
    if (other.null_) return true;
    if (other.size_ > size_ || null_) return other.size_ == 0;
    return std::equal(other.buffer_.begin(),
                      other.buffer_.begin() + static_cast<std::ptrdiff_t>(other.size_),
                      buffer_.begin() + static_cast<std::ptrdiff_t>(size_ - other.size_));
}

inline bool SwByteArray::endsWith(const char* str) const {
    if (!str) return false;
    SwByteArray other(str);
    return endsWith(other);
}

inline bool SwByteArray::endsWith(char ch) const {
    return !isEmpty() && !null_ && buffer_[static_cast<std::ptrdiff_t>(size_ - 1)] == ch;
}

//--------------------------------------------------------------------------------------------------
// Search helpers
//--------------------------------------------------------------------------------------------------

inline int SwByteArray::indexOf(const SwByteArray& other, int from) const {
    if (other.null_ || other.size_ == 0) {
        size_t start = sanitizePosition(from, size_);
        return static_cast<int>(start);
    }
    return indexOfRaw(other.constData(), other.size_, from);
}

inline int SwByteArray::indexOf(const char* str, int from) const {
    if (!str) return npos;
    return indexOfRaw(str, std::strlen(str), from);
}

inline int SwByteArray::indexOf(char ch, int from) const {
    return indexOfRaw(&ch, 1, from);
}

inline int SwByteArray::lastIndexOf(const SwByteArray& other, int from) const {
    if (other.null_ || other.size_ == 0) {
        size_t limit = sanitizePosition(from < 0 ? static_cast<int>(size_) : from, size_);
        return static_cast<int>(limit);
    }
    return lastIndexOfRaw(other.constData(), other.size_, from);
}

inline int SwByteArray::lastIndexOf(const char* str, int from) const {
    if (!str) return npos;
    return lastIndexOfRaw(str, std::strlen(str), from);
}

inline int SwByteArray::lastIndexOf(char ch, int from) const {
    return lastIndexOfRaw(&ch, 1, from);
}

inline int SwByteArray::indexOfRaw(const char* needle, size_t len, int from) const {
    if (null_ || !needle) return npos;
    if (len == 0) {
        size_t start = sanitizePosition(from, size_);
        return static_cast<int>(start);
    }
    size_t startPos = sanitizePosition(from, size_);
    if (startPos >= size_) return npos;
    const char* beginPtr = buffer_.data() + static_cast<std::ptrdiff_t>(startPos);
    const char* endPtr = buffer_.data() + static_cast<std::ptrdiff_t>(size_);
    const char* it = std::search(beginPtr, endPtr, needle, needle + len);
    if (it == endPtr) return npos;
    return static_cast<int>(it - buffer_.data());
}

inline int SwByteArray::lastIndexOfRaw(const char* needle, size_t len, int from) const {
    if (null_ || !needle) return npos;
    if (len == 0) {
        size_t limit = sanitizePosition(from < 0 ? static_cast<int>(size_) : from, size_);
        return static_cast<int>(limit);
    }
    size_t endPos;
    if (from < 0 || static_cast<size_t>(from) > size_) endPos = size_;
    else endPos = static_cast<size_t>(from);
    const char* beginPtr = buffer_.data();
    const char* endPtr = buffer_.data() + static_cast<std::ptrdiff_t>(endPos);
    const char* it = std::find_end(beginPtr, endPtr, needle, needle + len);
    if (it == endPtr) return npos;
    return static_cast<int>(it - buffer_.data());
}

inline int SwByteArray::count(const SwByteArray& other) const {
    if (other.null_) return 0;
    if (other.size_ == 0) return static_cast<int>(size_) + 1;
    int occurrences = 0;
    int pos = 0;
    while ((pos = indexOf(other, pos)) != npos) {
        ++occurrences;
        pos += static_cast<int>(other.size_);
    }
    return occurrences;
}

inline int SwByteArray::count(const char* str) const {
    if (!str) return 0;
    return count(SwByteArray(str));
}

inline int SwByteArray::count(char ch) const {
    if (null_) return 0;
    return static_cast<int>(std::count(buffer_.begin(),
                                       buffer_.begin() + static_cast<std::ptrdiff_t>(size_),
                                       ch));
}

inline SwList<SwByteArray> SwByteArray::split(char delimiter, bool keepEmptyParts) const {
    SwList<SwByteArray> parts;
    if (null_) return parts;
    size_t start = 0;
    for (size_t i = 0; i < size_; ++i) {
        if (buffer_[static_cast<std::ptrdiff_t>(i)] == delimiter) {
            size_t len = i - start;
            if (len > 0 || keepEmptyParts) {
                parts.append(slice(start, len));
            }
            start = i + 1;
        }
    }
    size_t len = size_ - start;
    if (len > 0 || keepEmptyParts) {
        parts.append(slice(start, len));
    }
    return parts;
}

//--------------------------------------------------------------------------------------------------
// Encoding
//--------------------------------------------------------------------------------------------------

inline SwByteArray SwByteArray::toHex() const {
    if (null_) return SwByteArray();
    static const char digits[] = "0123456789abcdef";
    SwByteArray result(size_ * 2, '\0');
    for (size_t i = 0; i < size_; ++i) {
        unsigned char value = toUChar(buffer_[static_cast<std::ptrdiff_t>(i)]);
        result.buffer_[static_cast<std::ptrdiff_t>(i * 2)] = digits[value >> 4];
        result.buffer_[static_cast<std::ptrdiff_t>(i * 2 + 1)] = digits[value & 0x0F];
    }
    return result;
}

inline int SwByteArray::hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

inline SwByteArray SwByteArray::fromHex(const SwByteArray& hex) {
    if (hex.null_) return SwByteArray();
    std::vector<char> cleaned;
    cleaned.reserve(hex.size_);
    for (size_t i = 0; i < hex.size_; ++i) {
        char ch = hex.buffer_[static_cast<std::ptrdiff_t>(i)];
        if (!std::isspace(toUChar(ch))) {
            cleaned.push_back(ch);
        }
    }
    if (cleaned.size() % 2 != 0) {
        throw std::invalid_argument("SwByteArray::fromHex invalid length.");
    }
    SwByteArray result(cleaned.size() / 2, '\0');
    for (size_t i = 0; i < cleaned.size(); i += 2) {
        int hi = hexValue(cleaned[i]);
        int lo = hexValue(cleaned[i + 1]);
        if (hi < 0 || lo < 0) {
            throw std::invalid_argument("SwByteArray::fromHex invalid digit.");
        }
        result.buffer_[static_cast<std::ptrdiff_t>(i / 2)] = static_cast<char>((hi << 4) | lo);
    }
    return result;
}

inline SwByteArray SwByteArray::toBase64() const {
    if (null_) return SwByteArray();
    std::string plain(buffer_.data(), buffer_.data() + static_cast<std::ptrdiff_t>(size_));
    std::string encoded = SwCrypto::base64Encode(plain);
    return SwByteArray(encoded);
}

inline SwByteArray SwByteArray::fromBase64(const SwByteArray& base64) {
    if (base64.null_) return SwByteArray();
    std::string encoded(base64.buffer_.data(),
                        base64.buffer_.data() + static_cast<std::ptrdiff_t>(base64.size_));
    std::vector<unsigned char> decoded = SwCrypto::base64Decode(encoded);
    SwByteArray result(decoded.size(), '\0');
    for (size_t i = 0; i < decoded.size(); ++i) {
        result.buffer_[static_cast<std::ptrdiff_t>(i)] = static_cast<char>(decoded[i]);
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
// Numeric conversions
//--------------------------------------------------------------------------------------------------

inline long long SwByteArray::toLongLong(bool* ok, int base) const {
    if (ok) *ok = false;
    if (null_) return 0;
    try {
        std::string str(buffer_.data(), buffer_.data() + static_cast<std::ptrdiff_t>(size_));
        size_t idx = 0;
        long long value = std::stoll(str, &idx, base);
        if (idx == str.size()) {
            if (ok) *ok = true;
            return value;
        }
    } catch (...) {}
    return 0;
}

inline unsigned long long SwByteArray::toULongLong(bool* ok, int base) const {
    if (ok) *ok = false;
    if (null_) return 0;
    try {
        std::string str(buffer_.data(), buffer_.data() + static_cast<std::ptrdiff_t>(size_));
        size_t idx = 0;
        unsigned long long value = std::stoull(str, &idx, base);
        if (idx == str.size()) {
            if (ok) *ok = true;
            return value;
        }
    } catch (...) {}
    return 0;
}

inline int SwByteArray::toInt(bool* ok, int base) const {
    if (ok) *ok = false;
    if (null_) return 0;
    try {
        std::string str(buffer_.data(), buffer_.data() + static_cast<std::ptrdiff_t>(size_));
        size_t idx = 0;
        int value = std::stoi(str, &idx, base);
        if (idx == str.size()) {
            if (ok) *ok = true;
            return value;
        }
    } catch (...) {}
    return 0;
}

inline double SwByteArray::toDouble(bool* ok) const {
    if (ok) *ok = false;
    if (null_) return 0.0;
    try {
        std::string str(buffer_.data(), buffer_.data() + static_cast<std::ptrdiff_t>(size_));
        size_t idx = 0;
        double value = std::stod(str, &idx);
        if (idx == str.size()) {
            if (ok) *ok = true;
            return value;
        }
    } catch (...) {}
    return 0.0;
}

inline SwByteArray& SwByteArray::setNum(long long value, int base) {
    *this = SwByteArray::number(value, base);
    return *this;
}

inline SwByteArray& SwByteArray::setNum(unsigned long long value, int base) {
    *this = SwByteArray::number(value, base);
    return *this;
}

inline SwByteArray& SwByteArray::setNum(double value, char format, int precision) {
    *this = SwByteArray::number(value, format, precision);
    return *this;
}

inline SwByteArray SwByteArray::number(long long value, int base) {
    if (base < 2 || base > 36) {
        throw std::invalid_argument("SwByteArray::number invalid base.");
    }
    bool negative = value < 0;
    unsigned long long temp = negative
        ? static_cast<unsigned long long>(-(value + 1)) + 1
        : static_cast<unsigned long long>(value);
    std::string digits;
    do {
        digits.push_back("0123456789abcdefghijklmnopqrstuvwxyz"[temp % base]);
        temp /= base;
    } while (temp != 0);
    if (negative) digits.push_back('-');
    std::reverse(digits.begin(), digits.end());
    return SwByteArray(digits);
}

inline SwByteArray SwByteArray::number(unsigned long long value, int base) {
    if (base < 2 || base > 36) {
        throw std::invalid_argument("SwByteArray::number invalid base.");
    }
    std::string digits;
    unsigned long long temp = value;
    do {
        digits.push_back("0123456789abcdefghijklmnopqrstuvwxyz"[temp % base]);
        temp /= base;
    } while (temp != 0);
    std::reverse(digits.begin(), digits.end());
    return SwByteArray(digits);
}

inline SwByteArray SwByteArray::number(double value, char format, int precision) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    switch (format) {
        case 'f':
        case 'F': oss << std::fixed; break;
        case 'e':
        case 'E':
            oss << std::scientific;
            if (format == 'E') {
                oss.setf(std::ios::uppercase);
            }
            break;
        default: break;
    }
    if (precision >= 0) {
        oss << std::setprecision(precision);
    }
    oss << value;
    return SwByteArray(oss.str());
}

//--------------------------------------------------------------------------------------------------
// Utilities
//--------------------------------------------------------------------------------------------------

inline int SwByteArray::compare(const SwByteArray& other) const {
    if (*this == other) return 0;
    return (*this < other) ? -1 : 1;
}

inline int SwByteArray::compare(const char* str) const {
    if (!str) return null_ ? 0 : 1;
    SwByteArray other(str);
    return compare(other);
}

inline void SwByteArray::swap(SwByteArray& other) noexcept {
    buffer_.swap(other.buffer_);
    std::swap(size_, other.size_);
    std::swap(null_, other.null_);
}

inline void SwByteArray::ensureNotNull() {
    if (!null_) return;
    buffer_.assign(1, '\0');
    size_ = 0;
    null_ = false;
}

inline void SwByteArray::ensureNullTerminator() {
    if (buffer_.empty()) {
        buffer_.push_back('\0');
    }
    if (buffer_.size() < size_ + 1) {
        buffer_.resize(size_ + 1, '\0');
    }
    buffer_[static_cast<std::ptrdiff_t>(size_)] = '\0';
}

inline void SwByteArray::assertPointerIfNeeded(const char* data, size_t len) const {
    if (!data && len > 0) {
        throw std::invalid_argument("SwByteArray: null pointer with positive length.");
    }
}

inline void SwByteArray::assignInternal(const char* data, size_t len) {
    ensureNotNull();
    if (!data && len > 0) {
        throw std::invalid_argument("SwByteArray: null pointer with data.");
    }
    size_ = len;
    buffer_.assign(len + 1, '\0');
    if (data && len > 0) {
        std::copy(data, data + len, buffer_.begin());
    }
    ensureNullTerminator();
}

inline SwByteArray SwByteArray::slice(size_t pos, size_t len) const {
    if (null_ || len == 0) return SwByteArray();
    SwByteArray result(len, '\0');
    std::copy(buffer_.begin() + static_cast<std::ptrdiff_t>(pos),
              buffer_.begin() + static_cast<std::ptrdiff_t>(pos + len),
              result.buffer_.begin());
    return result;
}

inline size_t SwByteArray::sanitizePosition(int pos, size_t limit) {
    if (pos < 0) return 0;
    size_t value = static_cast<size_t>(pos);
    return value > limit ? limit : value;
}

#ifndef QT_CORE_LIB
using QByteArray = SwByteArray;
#endif // QT_CORE_LIB

#endif // SWBYTEARRAY_H
