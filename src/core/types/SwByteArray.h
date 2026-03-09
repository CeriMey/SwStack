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

/**
 * @file src/core/types/SwByteArray.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwByteArray in the CoreSw fundamental types
 * layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the byte array interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwByteArray.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

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

    /**
     * @brief Constructs a `SwByteArray` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray();
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param str Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwByteArray(const char* str);
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param data Value passed to the method.
     * @param size Size value used by the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray(const char* data, size_t size);
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param str Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray(const std::string& str);
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param size Size value used by the operation.
     * @param ch Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray(size_t size, char ch);
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param list Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray(std::initializer_list<char> list);
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray(const SwByteArray& other);
    /**
     * @brief Constructs a `SwByteArray` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwByteArray(SwByteArray&& other) noexcept;
    /**
     * @brief Destroys the `SwByteArray` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwByteArray() = default;

    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwByteArray& operator=(const SwByteArray& other);
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwByteArray& operator=(SwByteArray&& other) noexcept;
    /**
     * @brief Performs the `operator=` operation.
     * @param str Value passed to the method.
     * @return The requested operator =.
     */
    SwByteArray& operator=(const char* str);
    /**
     * @brief Performs the `operator=` operation.
     * @param str Value passed to the method.
     * @return The requested operator =.
     */
    SwByteArray& operator=(const std::string& str);
    /**
     * @brief Performs the `operator=` operation.
     * @param list Value passed to the method.
     * @return The requested operator =.
     */
    SwByteArray& operator=(std::initializer_list<char> list);

    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    char& operator[](size_t index);
    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    const char& operator[](size_t index) const;

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwByteArray& other) const;
    /**
     * @brief Performs the `operator!=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwByteArray& other) const { return !(*this == other); }
    /**
     * @brief Performs the `operator<` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator<(const SwByteArray& other) const;
    /**
     * @brief Performs the `operator>` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator>(const SwByteArray& other) const { return other < *this; }
    /**
     * @brief Performs the `operator<=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator<=(const SwByteArray& other) const { return !(other < *this); }
    /**
     * @brief Performs the `operator>=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator>=(const SwByteArray& other) const { return !(*this < other); }

    /**
     * @brief Performs the `operator+=` operation.
     * @param other Value passed to the method.
     * @return The requested operator +=.
     */
    SwByteArray& operator+=(const SwByteArray& other);
    /**
     * @brief Performs the `operator+=` operation.
     * @param str Value passed to the method.
     * @return The requested operator +=.
     */
    SwByteArray& operator+=(const char* str);
    /**
     * @brief Performs the `operator+=` operation.
     * @param ch Value passed to the method.
     * @return The requested operator +=.
     */
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

    /**
     * @brief Returns the current begin.
     * @return The current begin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    iterator begin();
    /**
     * @brief Returns the current end.
     * @return The current end.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    iterator end();
    /**
     * @brief Returns the current begin.
     * @return The current begin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const_iterator begin() const;
    /**
     * @brief Returns the current end.
     * @return The current end.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const_iterator end() const;
    /**
     * @brief Performs the `cbegin` operation.
     * @return The requested cbegin.
     */
    const_iterator cbegin() const { return begin(); }
    /**
     * @brief Performs the `cend` operation.
     * @return The requested cend.
     */
    const_iterator cend() const { return end(); }
    /**
     * @brief Returns the current rbegin.
     * @return The current rbegin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    reverse_iterator rbegin();
    /**
     * @brief Returns the current rend.
     * @return The current rend.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    reverse_iterator rend();
    /**
     * @brief Returns the current rbegin.
     * @return The current rbegin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const_reverse_iterator rbegin() const;
    /**
     * @brief Returns the current rend.
     * @return The current rend.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const_reverse_iterator rend() const;

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t size() const { return size_; }
    /**
     * @brief Returns the current length.
     * @return The current length.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t length() const { return size_; }
    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEmpty() const { return size_ == 0; }
    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isNull() const { return null_; }
    /**
     * @brief Performs the `capacity` operation.
     * @return The requested capacity.
     */
    size_t capacity() const { return buffer_.capacity() > 0 ? buffer_.capacity() - 1 : 0; }

    /**
     * @brief Performs the `constData` operation.
     * @return The requested const Data.
     */
    const char* constData() const { return null_ ? nullptr : buffer_.data(); }
    /**
     * @brief Performs the `data` operation.
     * @return The requested data.
     */
    const char* data() const { return constData(); }
    /**
     * @brief Returns the current data.
     * @return The current data.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    char* data();
    /**
     * @brief Performs the `char*` operation.
     * @return The requested char*.
     */
    operator const char*() const { return constData(); }

    /**
     * @brief Returns the current to Std String.
     * @return The current to Std String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string toStdString() const {
        if (null_) return std::string();
        return std::string(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(size_));
    }

    /**
     * @brief Performs the `fromStdString` operation.
     * @param str Value passed to the method.
     * @return The requested from Std String.
     */
    static SwByteArray fromStdString(const std::string& str) { return SwByteArray(str); }

    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    char at(size_t index) const;
    /**
     * @brief Returns the current front.
     * @return The current front.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    char front() const;
    /**
     * @brief Returns the current back.
     * @return The current back.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    char back() const;

    /**
     * @brief Clears the current object state.
     */
    void clear();
    /**
     * @brief Performs the `reserve` operation.
     * @param capacity Value passed to the method.
     */
    void reserve(size_t capacity);
    /**
     * @brief Performs the `squeeze` operation.
     */
    void squeeze();
    /**
     * @brief Performs the `resize` operation.
     * @param newSize Value passed to the method.
     * @param fillChar Value passed to the method.
     */
    void resize(size_t newSize, char fillChar = '\0');
    /**
     * @brief Performs the `fill` operation.
     * @param ch Value passed to the method.
     * @param size Size value used by the operation.
     * @return The requested fill.
     */
    SwByteArray& fill(char ch, int size = -1);

    /**
     * @brief Performs the `append` operation.
     * @param other Value passed to the method.
     * @return The requested append.
     */
    SwByteArray& append(const SwByteArray& other);
    /**
     * @brief Performs the `append` operation.
     * @param data Value passed to the method.
     * @param len Value passed to the method.
     * @return The requested append.
     */
    SwByteArray& append(const char* data, size_t len);
    /**
     * @brief Performs the `append` operation.
     * @param str Value passed to the method.
     * @return The requested append.
     */
    SwByteArray& append(const char* str);
    /**
     * @brief Performs the `append` operation.
     * @return The requested append.
     */
    SwByteArray& append(const std::string& str) { return append(str.data(), str.size()); }
    /**
     * @brief Performs the `append` operation.
     * @param ch Value passed to the method.
     * @return The requested append.
     */
    SwByteArray& append(char ch);

    /**
     * @brief Performs the `prepend` operation.
     * @param other Value passed to the method.
     * @return The requested prepend.
     */
    SwByteArray& prepend(const SwByteArray& other);
    /**
     * @brief Performs the `prepend` operation.
     * @param data Value passed to the method.
     * @param len Value passed to the method.
     * @return The requested prepend.
     */
    SwByteArray& prepend(const char* data, size_t len);
    /**
     * @brief Performs the `prepend` operation.
     * @param str Value passed to the method.
     * @return The requested prepend.
     */
    SwByteArray& prepend(const char* str);
    /**
     * @brief Performs the `prepend` operation.
     * @param ch Value passed to the method.
     * @return The requested prepend.
     */
    SwByteArray& prepend(char ch);

    /**
     * @brief Performs the `insert` operation.
     * @param index Value passed to the method.
     * @param ch Value passed to the method.
     * @return The requested insert.
     */
    SwByteArray& insert(int index, char ch);
    /**
     * @brief Performs the `insert` operation.
     * @param index Value passed to the method.
     * @param other Value passed to the method.
     * @return The requested insert.
     */
    SwByteArray& insert(int index, const SwByteArray& other);
    /**
     * @brief Performs the `insert` operation.
     * @param index Value passed to the method.
     * @param data Value passed to the method.
     * @param len Value passed to the method.
     * @return The requested insert.
     */
    SwByteArray& insert(int index, const char* data, size_t len);

    /**
     * @brief Removes the specified remove.
     * @param index Value passed to the method.
     * @param len Value passed to the method.
     * @return The requested remove.
     */
    SwByteArray& remove(int index, int len);
    /**
     * @brief Performs the `replace` operation.
     * @param index Value passed to the method.
     * @param len Value passed to the method.
     * @param with Value passed to the method.
     * @return The requested replace.
     */
    SwByteArray& replace(int index, int len, const SwByteArray& with);
    /**
     * @brief Performs the `replace` operation.
     * @param index Value passed to the method.
     * @param len Value passed to the method.
     * @param data Value passed to the method.
     * @param dataLen Value passed to the method.
     * @return The requested replace.
     */
    SwByteArray& replace(int index, int len, const char* data, size_t dataLen);
    /**
     * @brief Performs the `replace` operation.
     * @param before Value passed to the method.
     * @param after Value passed to the method.
     * @return The requested replace.
     */
    SwByteArray& replace(const SwByteArray& before, const SwByteArray& after);
    /**
     * @brief Performs the `replace` operation.
     * @param before Value passed to the method.
     * @param after Value passed to the method.
     * @return The requested replace.
     */
    SwByteArray& replace(const char* before, const char* after);
    /**
     * @brief Performs the `replace` operation.
     * @param before Value passed to the method.
     * @param after Value passed to the method.
     * @return The requested replace.
     */
    SwByteArray& replace(char before, char after);

    /**
     * @brief Performs the `push_back` operation.
     * @param ch Value passed to the method.
     * @return The requested push back.
     */
    SwByteArray& push_back(char ch) { return append(ch); }
    /**
     * @brief Performs the `push_front` operation.
     * @param ch Value passed to the method.
     * @return The requested push front.
     */
    SwByteArray& push_front(char ch) { return prepend(ch); }
    /**
     * @brief Performs the `pop_back` operation.
     */
    void pop_back();
    /**
     * @brief Performs the `pop_front` operation.
     */
    void pop_front();

    /**
     * @brief Performs the `chop` operation.
     * @param len Value passed to the method.
     * @return The requested chop.
     */
    SwByteArray& chop(int len);
    /**
     * @brief Performs the `truncate` operation.
     * @param len Value passed to the method.
     * @return The requested truncate.
     */
    SwByteArray& truncate(int len);
    /**
     * @brief Performs the `chopped` operation.
     * @param len Value passed to the method.
     * @return The requested chopped.
     */
    SwByteArray chopped(int len) const;
    /**
     * @brief Performs the `truncated` operation.
     * @param len Value passed to the method.
     * @return The requested truncated.
     */
    SwByteArray truncated(int len) const;
    /**
     * @brief Performs the `left` operation.
     * @param len Value passed to the method.
     * @return The requested left.
     */
    SwByteArray left(int len) const;
    /**
     * @brief Performs the `right` operation.
     * @param len Value passed to the method.
     * @return The requested right.
     */
    SwByteArray right(int len) const;
    /**
     * @brief Performs the `mid` operation.
     * @param pos Position used by the operation.
     * @param len Value passed to the method.
     * @return The requested mid.
     */
    SwByteArray mid(int pos, int len = -1) const;
    /**
     * @brief Performs the `first` operation.
     * @param len Value passed to the method.
     * @return The requested first.
     */
    SwByteArray first(int len) const { return left(len); }
    /**
     * @brief Performs the `last` operation.
     * @param len Value passed to the method.
     * @return The requested last.
     */
    SwByteArray last(int len) const { return right(len); }
    /**
     * @brief Performs the `repeated` operation.
     * @param times Value passed to the method.
     * @return The requested repeated.
     */
    SwByteArray repeated(int times) const;
    /**
     * @brief Returns the current reversed.
     * @return The current reversed.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray reversed() const;

    /**
     * @brief Returns the current trimmed.
     * @return The current trimmed.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray trimmed() const;
    /**
     * @brief Returns the current simplified.
     * @return The current simplified.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray simplified() const;
    /**
     * @brief Returns the current to Lower.
     * @return The current to Lower.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray toLower() const;
    /**
     * @brief Returns the current to Upper.
     * @return The current to Upper.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray toUpper() const;

    /**
     * @brief Starts the s With managed by the object.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startsWith(const SwByteArray& other) const;
    /**
     * @brief Starts the s With managed by the object.
     * @param str Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startsWith(const char* str) const;
    /**
     * @brief Starts the s With managed by the object.
     * @param ch Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startsWith(char ch) const;
    /**
     * @brief Performs the `endsWith` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool endsWith(const SwByteArray& other) const;
    /**
     * @brief Performs the `endsWith` operation.
     * @param str Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool endsWith(const char* str) const;
    /**
     * @brief Performs the `endsWith` operation.
     * @param ch Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool endsWith(char ch) const;

    /**
     * @brief Performs the `contains` operation.
     * @param other Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwByteArray& other) const { return indexOf(other) != npos; }
    /**
     * @brief Performs the `contains` operation.
     * @param str Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const char* str) const { return indexOf(str) != npos; }
    /**
     * @brief Performs the `contains` operation.
     * @param ch Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(char ch) const { return indexOf(ch) != npos; }

    /**
     * @brief Performs the `indexOf` operation.
     * @param other Value passed to the method.
     * @param from Value passed to the method.
     * @return The requested index Of.
     */
    int indexOf(const SwByteArray& other, int from = 0) const;
    /**
     * @brief Performs the `indexOf` operation.
     * @param str Value passed to the method.
     * @param from Value passed to the method.
     * @return The requested index Of.
     */
    int indexOf(const char* str, int from = 0) const;
    /**
     * @brief Performs the `indexOf` operation.
     * @param ch Value passed to the method.
     * @param from Value passed to the method.
     * @return The requested index Of.
     */
    int indexOf(char ch, int from = 0) const;
    /**
     * @brief Performs the `lastIndexOf` operation.
     * @param other Value passed to the method.
     * @param from Value passed to the method.
     * @return The requested last Index Of.
     */
    int lastIndexOf(const SwByteArray& other, int from = -1) const;
    /**
     * @brief Performs the `lastIndexOf` operation.
     * @param str Value passed to the method.
     * @param from Value passed to the method.
     * @return The requested last Index Of.
     */
    int lastIndexOf(const char* str, int from = -1) const;
    /**
     * @brief Performs the `lastIndexOf` operation.
     * @param ch Value passed to the method.
     * @param from Value passed to the method.
     * @return The requested last Index Of.
     */
    int lastIndexOf(char ch, int from = -1) const;

    /**
     * @brief Performs the `count` operation.
     * @param other Value passed to the method.
     * @return The current count value.
     */
    int count(const SwByteArray& other) const;
    /**
     * @brief Performs the `count` operation.
     * @param str Value passed to the method.
     * @return The current count value.
     */
    int count(const char* str) const;
    /**
     * @brief Performs the `count` operation.
     * @param ch Value passed to the method.
     * @return The current count value.
     */
    int count(char ch) const;

    /**
     * @brief Performs the `split` operation.
     * @param delimiter Value passed to the method.
     * @param keepEmptyParts Value passed to the method.
     * @return The requested split.
     */
    SwList<SwByteArray> split(char delimiter, bool keepEmptyParts = true) const;

    /**
     * @brief Returns the current to Hex.
     * @return The current to Hex.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray toHex() const;
    /**
     * @brief Performs the `fromHex` operation.
     * @param hex Value passed to the method.
     * @return The requested from Hex.
     */
    static SwByteArray fromHex(const SwByteArray& hex);
    /**
     * @brief Returns the current to Base64.
     * @return The current to Base64.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray toBase64() const;
    /**
     * @brief Performs the `fromBase64` operation.
     * @param base64 Value passed to the method.
     * @return The requested from Base64.
     */
    static SwByteArray fromBase64(const SwByteArray& base64);

    /**
     * @brief Performs the `toLongLong` operation.
     * @param ok Optional flag updated to report success.
     * @param base Value passed to the method.
     * @return The requested to Long Long.
     */
    long long toLongLong(bool* ok = nullptr, int base = 10) const;
    /**
     * @brief Performs the `toULongLong` operation.
     * @param ok Optional flag updated to report success.
     * @param base Value passed to the method.
     * @return The requested to ULong Long.
     */
    unsigned long long toULongLong(bool* ok = nullptr, int base = 10) const;
    /**
     * @brief Performs the `toInt` operation.
     * @param ok Optional flag updated to report success.
     * @param base Value passed to the method.
     * @return The requested to Int.
     */
    int toInt(bool* ok = nullptr, int base = 10) const;
    /**
     * @brief Performs the `toDouble` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested to Double.
     */
    double toDouble(bool* ok = nullptr) const;

    /**
     * @brief Sets the num.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested num.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    SwByteArray& setNum(long long value, int base = 10);
    /**
     * @brief Sets the num.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested num.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    SwByteArray& setNum(unsigned long long value, int base = 10);
    /**
     * @brief Sets the num.
     * @param value Value passed to the method.
     * @param format Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested num.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    SwByteArray& setNum(double value, char format = 'g', int precision = 6);

    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwByteArray number(long long value, int base = 10);
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwByteArray number(unsigned long long value, int base = 10);
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param format Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested number.
     */
    static SwByteArray number(double value, char format = 'g', int precision = 6);

    /**
     * @brief Performs the `fromRawData` operation.
     * @param data Value passed to the method.
     * @param len Value passed to the method.
     * @return The requested from Raw Data.
     */
    static SwByteArray fromRawData(const char* data, size_t len) { return SwByteArray(data, len); }

    /**
     * @brief Performs the `compare` operation.
     * @param other Value passed to the method.
     * @return The requested compare.
     */
    int compare(const SwByteArray& other) const;
    /**
     * @brief Performs the `compare` operation.
     * @param str Value passed to the method.
     * @return The requested compare.
     */
    int compare(const char* str) const;

    /**
     * @brief Performs the `swap` operation.
     * @param other Value passed to the method.
     */
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
    : buffer_(size + 1, ch),
      size_(size),
      null_(false) {
    buffer_[size] = '\0';
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
#endif // optional core-lib interop

#endif // SWBYTEARRAY_H
