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

#ifndef SWSTRING_H
#define SWSTRING_H

/**
 * @file src/core/types/SwString.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwString in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the string interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwString.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

static constexpr const char* kSwLogCategory_SwString = "sw.core.types.swstring";

#ifdef QT_CORE_LIB
#include <QDebug>
#endif

#include <string>
#include <iostream>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <vector>
#include <deque>
#include <functional>
#include <locale>
#include <limits>
#include <type_traits>
#include <codecvt> // used on non-Windows for UTF conversions (deprecated in C++17+, but widely available)

#include "SwDebug.h"

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include "platform/win/SwWindows.h" // for CP_UTF8, MultiByteToWideChar, WideCharToMultiByte
#endif

#include "SwList.h"
#include "SwCrypto.h"
#include "SwByteArray.h"
#include "SwChar.h"
#include "Sw.h"


class SwRegularExpression;

class SwString {

public:
    // Constructeurs
    /**
     * @brief Constructs a `SwString` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString() : data_("") {}
    /**
     * @brief Constructs a `SwString` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(const char* str) : data_(str ? str : "") {}
    SwString(const char* str, size_t len) : data_(str ? std::string(str, len) : std::string()) {}
#ifdef QT_CORE_LIB
    /**
     * @brief Constructs a `SwString` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(const QByteArray& arr) : data_(arr.constData(), arr.constData() + arr.size()) {}
#endif
    /**
     * @brief Constructs a `SwString` instance.
     * @param str Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(const std::string& str) : data_(str) {}
    /**
     * @brief Constructs a `SwString` instance.
     * @param data_ Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(const SwString& other) : data_(other.data_) {}
    /**
     * @brief Constructs a `SwString` instance.
     * @param bytes Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(const SwByteArray& bytes);
    /**
     * @brief Constructs a `SwString` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(SwString&& other) noexcept : data_(std::move(other.data_)) {}
    /**
     * @brief Constructs a `SwString` instance.
     * @param count Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(size_t count, char ch) : data_(std::string(count, ch)) {}
    /**
     * @brief Constructs a `SwString` instance.
     * @param ch Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwString(char ch) : data_(1, ch) {}

    /**
     * @brief Returns the current string&.
     * @return The current string&.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    operator std::string&() { return data_; }
    /**
     * @brief Returns the current string&.
     * @return The current string&.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    operator const std::string&() const { return data_; }

    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwString& operator=(const SwString& other) { if (this != &other) data_ = other.data_; return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    SwString& operator=(SwString&& other) noexcept { if (this != &other) data_ = std::move(other.data_); return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param data_ Value passed to the method.
     * @return The requested operator =.
     */
    SwString& operator=(const char* str) { data_ = (str ? str : ""); return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param str Value passed to the method.
     * @return The requested operator =.
     */
    SwString& operator=(const std::string& str) { data_ = str; return *this; }
    /**
     * @brief Performs the `operator=` operation.
     * @param bytes Value passed to the method.
     * @return The requested operator =.
     */
    SwString& operator=(const SwByteArray& bytes);

    /**
     * @brief Performs the `operator+=` operation.
     * @param other Value passed to the method.
     * @return The requested operator +=.
     */
    SwString& operator+=(const SwString& other) { data_ += other.data_; return *this; }

    /**
     * @brief Performs the `operator+` operation.
     * @return The requested operator +.
     */
    SwString operator+(const char* str) const { return SwString(data_ + (str ? str : "")); }
    /**
     * @brief Performs the `operator+` operation.
     * @param str Value passed to the method.
     * @return The requested operator +.
     */
    SwString operator+(const std::string& str) const { return SwString(data_ + str); }
    /**
     * @brief Performs the `operator+` operation.
     * @param data_ Value passed to the method.
     * @return The requested operator +.
     */
    SwString operator+(const SwString& other) const { return SwString(data_ + other.data_); }

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwString& other) const { return data_ == other.data_; }
    /**
     * @brief Performs the `operator!=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwString& other) const { return data_ != other.data_; }
    /**
     * @brief Performs the `operator<` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator<(const SwString& other) const { return data_ < other.data_; }
    /**
     * @brief Performs the `operator>` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator>(const SwString& other) const { return data_ > other.data_; }

    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    char operator[](size_t index) const { return data_[index]; }
    /**
     * @brief Performs the `operator[]` operation.
     * @param index Value passed to the method.
     * @return The requested operator [].
     */
    char& operator[](size_t index) { return data_[index]; }

    /**
     * @brief Returns the current string.
     * @return The current string.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    operator std::string() const { return data_; }

    // MÃ©thodes de base
    /**
     * @brief Performs the `size` operation.
     * @return The current size value.
     */
    size_t size() const { return data_.size(); }
    /**
     * @brief Performs the `length` operation.
     * @return The current length value.
     */
    size_t length() const { return data_.length(); }
    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isEmpty() const { return data_.empty(); }
    /**
     * @brief Clears the current object state.
     */
    void clear() { data_.clear(); }

    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    SwChar at(int index) const noexcept {
        if (index < 0) return SwChar('\0');
        const size_t i = static_cast<size_t>(index);
        if (i >= data_.size()) return SwChar('\0');
        return SwChar(static_cast<unsigned char>(data_[i]));
    }

    // optionnel: meme overload cote non-const (la version de lecture seule reste la reference, mais ca aide la compat)
    /**
     * @brief Performs the `at` operation.
     * @param index Value passed to the method.
     * @return The requested at.
     */
    SwChar at(int index) noexcept {
        return static_cast<const SwString&>(*this).at(index);
    }

    /**
     * @brief Returns whether the object reports int.
     * @return `true` when the object reports int; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isInt() const {
        if (data_.empty() || (data_[0] == '-' && data_.size() == 1)) return false;
        for (size_t i = (data_[0] == '-') ? 1 : 0; i < data_.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(data_[i]))) return false;
        return true;
    }

    /**
     * @brief Returns whether the object reports float.
     * @return `true` when the object reports float; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isFloat() const {
        if (data_.empty() || (data_[0] == '-' && data_.size() == 1)) return false;
        bool hasDot = false;
        bool hasDigit = false;
        for (size_t i = (data_[0] == '-') ? 1 : 0; i < data_.size(); ++i) {
            if (data_[i] == '.') {
                if (hasDot) return false;
                hasDot = true;
            } else if (std::isdigit(static_cast<unsigned char>(data_[i]))) {
                hasDigit = true;
            } else {
                return false;
            }
        }
        return hasDot && hasDigit;
    }

    /**
     * @brief Returns the current to Std String.
     * @return The current to Std String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const std::string& toStdString() const { return data_; }
    /**
     * @brief Performs the `reserve` operation.
     * @param capacity Value passed to the method.
     */
    void reserve(size_t capacity) { data_.reserve(capacity); }

    /**
     * @brief Performs the `toInt` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested to Int.
     */
    int toInt(bool* ok = nullptr) const {
        SwString cleaned = trimmed();
        std::string sanitized = sanitizeNumericString(cleaned.data_);
        long long parsedValue = 0;
        bool success = parseIntegerString(sanitized, parsedValue);
        if (success &&
            (parsedValue < std::numeric_limits<int>::min() ||
             parsedValue > std::numeric_limits<int>::max())) {
            success = false;
        }
        if (!success) {
            if (ok) *ok = false;
            swCError(kSwLogCategory_SwString) << "Invalid int conversion in SwString: " << data_;
            return 0;
        }
        if (ok) *ok = true;
        return static_cast<int>(parsedValue);
    }

    /**
     * @brief Performs the `toLongLong` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested to Long Long.
     */
    long long toLongLong(bool* ok = nullptr) const {
        SwString cleaned = trimmed();
        std::string sanitized = sanitizeNumericString(cleaned.data_);
        long long parsedValue = 0;
        bool success = parseIntegerString(sanitized, parsedValue);
        if (!success) {
            if (ok) *ok = false;
            swCError(kSwLogCategory_SwString) << "Invalid long long conversion in SwString: " << data_;
            return 0;
        }
        if (ok) *ok = true;
        return parsedValue;
    }

    /**
     * @brief Performs the `toFloat` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested to Float.
     */
    float toFloat(bool* ok = nullptr) const {
        double value = 0.0;
        if (!parseNumericDouble(value)) {
            if (ok) *ok = false;
            swCError(kSwLogCategory_SwString) << "Invalid float conversion in SwString: " << data_;
            return 0.0f;
        }
        if (ok) *ok = true;
        return static_cast<float>(value);
    }

    /**
     * @brief Performs the `toDouble` operation.
     * @param ok Optional flag updated to report success.
     * @return The requested to Double.
     */
    double toDouble(bool* ok = nullptr) const {
        double value = 0.0;
        if (!parseNumericDouble(value)) {
            if (ok) *ok = false;
            swCError(kSwLogCategory_SwString) << "Invalid double conversion in SwString: " << data_;
            return 0.0;
        }
        if (ok) *ok = true;
        return value;
    }

    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested number.
     */
    static SwString number(float value, int precision = -1) {
        std::ostringstream os;
        os << std::fixed;
        if (precision >= 0) os.precision(precision);
        os << value;
        std::string result = os.str();
        if (precision >= 0 && result.find('.') != std::string::npos) {
            result.erase(result.find_last_not_of('0') + 1);
            if (!result.empty() && result.back() == '.') result.pop_back();
        }
        return SwString(result);
    }

    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested number.
     */
    static SwString number(double value, int precision) {
        std::ostringstream os;
        os << std::fixed;
        if (precision >= 0) os.precision(precision);
        os << value;
        std::string result = os.str();
        if (precision >= 0 && result.find('.') != std::string::npos) {
            result.erase(result.find_last_not_of('0') + 1);
            if (!result.empty() && result.back() == '.') result.pop_back();
        }
        return SwString(result);
    }

    // Compatibility overloads
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwString number(int value, int base = 10) { return numberSignedImpl_<int>(value, base); }
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwString number(unsigned int value, int base = 10) { return numberUnsignedImpl_<unsigned int>(value, base); }
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwString number(long value, int base = 10) { return numberSignedImpl_<long>(value, base); }
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwString number(unsigned long value, int base = 10) { return numberUnsignedImpl_<unsigned long>(value, base); }
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwString number(long long value, int base = 10) { return numberSignedImpl_<long long>(value, base); }
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param base Value passed to the method.
     * @return The requested number.
     */
    static SwString number(unsigned long long value, int base = 10) { return numberUnsignedImpl_<unsigned long long>(value, base); }
    /**
     * @brief Performs the `number` operation.
     * @param value Value passed to the method.
     * @param format Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested number.
     */
    static SwString number(double value, char format = 'g', int precision = 6) {
        return numberFloatingImpl_(value, format, precision);
    }

    /**
     * @brief Performs the `toBase64` operation.
     * @return The requested to Base64.
     */
    SwString toBase64() const { return SwString(SwCrypto::base64Encode(data_)); }

    /**
     * @brief Returns the current de Base64.
     * @return The current de Base64.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString deBase64() {
        std::vector<unsigned char> decoded = SwCrypto::base64Decode(data_);
        return SwString(std::string(decoded.begin(), decoded.end()));
    }

    /**
     * @brief Performs the `fromBase64` operation.
     * @param base64 Value passed to the method.
     * @return The requested from Base64.
     */
    static SwString fromBase64(const SwString& base64) {
        std::vector<unsigned char> decoded = SwCrypto::base64Decode(base64.toStdString());
        return SwString(std::string(decoded.begin(), decoded.end()));
    }

    /**
     * @brief Performs the `encryptAES` operation.
     * @param key Value passed to the method.
     * @return The requested encrypt AES.
     */
    SwString encryptAES(const SwString& key) const {
        try { return SwString(SwCrypto::encryptAES(data_, key.data_)); }
        catch (const std::exception& e) { swCError(kSwLogCategory_SwString) << "Encryption error: " << e.what(); return SwString(""); }
    }

    /**
     * @brief Performs the `decryptAES` operation.
     * @param key Value passed to the method.
     * @return The requested decrypt AES.
     */
    SwString decryptAES(const SwString& key) {
        try { return SwString(SwCrypto::decryptAES(data_, key.data_)); }
        catch (const std::exception& e) { swCError(kSwLogCategory_SwString) << "Decryption error: " << e.what(); return SwString(""); }
    }

    /**
     * @brief Performs the `decryptAES` operation.
     * @param encryptedBase64 Value passed to the method.
     * @param key Value passed to the method.
     * @return The requested decrypt AES.
     */
    static SwString decryptAES(const SwString& encryptedBase64, const SwString& key) {
        try { return SwString(SwCrypto::decryptAES(encryptedBase64.data_, key.data_)); }
        catch (const std::exception& e) { swCError(kSwLogCategory_SwString) << "Decryption error: " << e.what(); return SwString(""); }
    }

    friend std::ostream& operator<<(std::ostream& os, const SwString& str) { os << str.data_; return os; }
    friend std::istream& operator>>(std::istream& is, SwString& str) { is >> str.data_; return is; }

    /**
     * @brief Performs the `split` operation.
     * @param delimiter Value passed to the method.
     * @return The requested split.
     */
    SwList<SwString> split(const char* delimiter) const {
        SwList<SwString> result;
        if (!delimiter || *delimiter == '\0') return result;
        std::string strData = this->toStdString();
        std::string strDelimiter = delimiter;
        size_t start = 0;
        size_t end = 0;
        while ((end = strData.find(strDelimiter, start)) != std::string::npos) {
            result.append(SwString(strData.substr(start, end - start).c_str()));
            start = end + strDelimiter.length();
        }
        if (start < strData.length()) result.append(SwString(strData.substr(start).c_str()));
        return result;
    }

    /**
     * @brief Performs the `split` operation.
     * @param delimiter Value passed to the method.
     * @return The requested split.
     */
    SwList<SwString> split(char delimiter) const {
        SwList<SwString> result;
        size_t start = 0;
        size_t end = data_.find(delimiter);
        while (end != std::string::npos) {
            result.append(SwString(data_.substr(start, end - start)));
            start = end + 1;
            end = data_.find(delimiter, start);
        }
        if (start < data_.size()) result.append(SwString(data_.substr(start)));
        return result;
    }

    /**
     * @brief Performs the `split` operation.
     * @param delimiter Value passed to the method.
     * @return The requested split.
     */
    SwList<SwString> split(const std::string& delimiter) const {
        if (delimiter.empty()) throw std::invalid_argument("Delimiter cannot be empty.");
        return split(SwString(delimiter));
    }

    /**
     * @brief Performs the `split` operation.
     * @param delimiter Value passed to the method.
     * @return The requested split.
     */
    SwList<SwString> split(const SwString& delimiter) const {
        if (delimiter.isEmpty()) throw std::invalid_argument("Delimiter cannot be empty.");
        SwList<SwString> result;
        size_t start = 0;
        size_t end = data_.find(delimiter.toStdString());
        while (end != std::string::npos) {
            result.append(SwString(data_.substr(start, end - start)));
            start = end + delimiter.size();
            end = data_.find(delimiter.toStdString(), start);
        }
        if (start < data_.size()) result.append(SwString(data_.substr(start)));
        return result;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param ch Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(char ch) const {
        return data_.find(ch) != std::string::npos;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param substring Value passed to the method.
     * @param cs Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwString& substring, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        return contains(substring.data_.c_str(), cs);
    }

    /**
     * @brief Performs the `contains` operation.
     * @param substring Value passed to the method.
     * @param cs Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const char* substring, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        if (!substring) {
            return false;
        }
        if (cs == Sw::CaseSensitive) {
            return data_.find(substring) != std::string::npos;
        }
        std::string haystack = data_;
        std::string needle(substring);
        toLowerInPlace(haystack);
        toLowerInPlace(needle);
        return haystack.find(needle) != std::string::npos;
    }

    /**
     * @brief Performs the `reversed` operation.
     * @return The requested reversed.
     */
    SwString reversed() const { return SwString(std::string(data_.rbegin(), data_.rend())); }

    /**
     * @brief Starts the s With managed by the object.
     * @param ch Value passed to the method.
     * @param cs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startsWith(char ch, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        if (data_.empty()) return false;
        if (cs == Sw::CaseSensitive) return data_.front() == ch;
        return std::tolower(static_cast<unsigned char>(data_.front())) == std::tolower(static_cast<unsigned char>(ch));
    }

    /**
     * @brief Starts the s With managed by the object.
     * @param prefix Prefix used by the operation.
     * @param cs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startsWith(const SwString& prefix, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        if (prefix.size() > data_.size()) {
            return false;
        }
        if (cs == Sw::CaseSensitive) {
            return data_.compare(0, prefix.size(), prefix.data_) == 0;
        }
        for (size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(data_[i])) !=
                std::tolower(static_cast<unsigned char>(prefix.data_[i]))) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Performs the `endsWith` operation.
     * @param ch Value passed to the method.
     * @param cs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool endsWith(char ch, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        if (data_.empty()) return false;
        if (cs == Sw::CaseSensitive) return data_.back() == ch;
        return std::tolower(static_cast<unsigned char>(data_.back())) == std::tolower(static_cast<unsigned char>(ch));
    }

    /**
     * @brief Performs the `endsWith` operation.
     * @param suffix Value passed to the method.
     * @param cs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool endsWith(const SwString& suffix, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        if (suffix.size() > data_.size()) return false;
        if (cs == Sw::CaseSensitive) {
            return data_.compare(data_.size() - suffix.size(), suffix.size(), suffix.data_) == 0;
        }
        size_t start = data_.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(data_[start + i])) !=
                std::tolower(static_cast<unsigned char>(suffix.data_[i]))) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Performs the `compare` operation.
     * @param other Value passed to the method.
     * @param cs Value passed to the method.
     * @return The requested compare.
     */
    int compare(const SwString& other, Sw::CaseSensitivity cs = Sw::CaseSensitive) const;
    /**
     * @brief Performs the `compare` operation.
     * @param str Value passed to the method.
     * @param cs Value passed to the method.
     * @return The requested compare.
     */
    int compare(const char* str, Sw::CaseSensitivity cs = Sw::CaseSensitive) const;

    /**
     * @brief Performs the `indexOf` operation.
     * @param ch Value passed to the method.
     * @param startIndex Value passed to the method.
     * @return The requested index Of.
     */
    int indexOf(char ch, size_t startIndex = 0) const {
        if (startIndex >= data_.size()) return -1;
        size_t pos = data_.find(ch, startIndex);
        return (pos != std::string::npos) ? static_cast<int>(pos) : -1;
    }

    /**
     * @brief Performs the `indexOf` operation.
     * @param substring Value passed to the method.
     * @param startIndex Value passed to the method.
     * @return The requested index Of.
     */
    int indexOf(const SwString& substring, size_t startIndex = 0) const {
        if (startIndex >= data_.size()) return -1;
        size_t pos = data_.find(substring.data_, startIndex);
        return (pos != std::string::npos) ? static_cast<int>(pos) : -1;
    }

    /**
     * @brief Performs the `lastIndexOf` operation.
     * @param substring Value passed to the method.
     * @return The requested last Index Of.
     */
    size_t lastIndexOf(const SwString& substring) const {
        size_t pos = data_.rfind(substring.data_);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    /**
     * @brief Performs the `lastIndexOf` operation.
     * @param character Value passed to the method.
     * @return The requested last Index Of.
     */
    size_t lastIndexOf(char character) const {
        size_t pos = data_.rfind(character);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    /**
     * @brief Performs the `firstIndexOf` operation.
     * @param substring Value passed to the method.
     * @return The requested first Index Of.
     */
    size_t firstIndexOf(const SwString& substring) const {
        size_t pos = data_.find(substring.data_);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    /**
     * @brief Performs the `firstIndexOf` operation.
     * @param character Value passed to the method.
     * @return The requested first Index Of.
     */
    size_t firstIndexOf(char character) const {
        size_t pos = data_.find(character);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    /**
     * @brief Returns the current trimmed.
     * @return The current trimmed.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString trimmed() const {
        size_t start = data_.find_first_not_of(" \t\n\r");
        size_t end = data_.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) return SwString("");
        return SwString(data_.substr(start, end - start + 1));
    }

    /**
     * @brief Returns the current to Upper.
     * @return The current to Upper.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toUpper() const {
        std::string upper(data_);
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return SwString(upper);
    }

    /**
     * @brief Returns the current to Lower.
     * @return The current to Lower.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString toLower() const {
        std::string lower(data_);
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return SwString(lower);
    }

    // --- UTF conversions: Windows uses WinAPI; Linux uses std::wstring_convert ---
    /**
     * @brief Performs the `fromWString` operation.
     * @param wideStr Value passed to the method.
     * @return The requested from WString.
     */
    static SwString fromWString(const std::wstring& wideStr) {
        if (wideStr.empty()) return SwString();
#ifdef _WIN32
        const int sourceLen = static_cast<int>(wideStr.size());
        int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), sourceLen, nullptr, 0, nullptr, nullptr);
        if (bufferSize <= 0) return SwString();
        std::string utf8Str(static_cast<size_t>(bufferSize), '\0');
        int written = WideCharToMultiByte(CP_UTF8, 0, wideStr.data(), sourceLen, &utf8Str[0], bufferSize, nullptr, nullptr);
        if (written <= 0) {
            return SwString();
        }
        return SwString(utf8Str);
#else
        try {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
            return SwString(conv.to_bytes(wideStr));
        } catch (...) {
            return SwString();
        }
#endif
    }

    /**
     * @brief Performs the `fromWCharArray` operation.
     * @param wideStr Value passed to the method.
     * @return The requested from WChar Array.
     */
    static SwString fromWCharArray(const wchar_t* wideStr) {
        if (!wideStr) return SwString();
#ifdef _WIN32
        int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (bufferSize <= 0) return SwString();
        std::string utf8Str(static_cast<size_t>(bufferSize), '\0');
        int written = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &utf8Str[0], bufferSize, nullptr, nullptr);
        if (written <= 0) {
            return SwString();
        }
        if (!utf8Str.empty() && utf8Str.back() == '\0') {
            utf8Str.pop_back();
        }
        return SwString(utf8Str);
#else
        try {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
            return SwString(conv.to_bytes(std::wstring(wideStr)));
        } catch (...) {
            return SwString();
        }
#endif
    }

    /**
     * @brief Performs the `replace` operation.
     * @param oldSub Value passed to the method.
     * @param newSub Value passed to the method.
     * @return The requested replace.
     */
    SwString& replace(const SwString& oldSub, const SwString& newSub) {
        if (oldSub == "" || oldSub == "\0") return *this;
        size_t pos = 0;
        while ((pos = data_.find(oldSub.data_, pos)) != std::string::npos) {
            data_.replace(pos, oldSub.size(), newSub.data_);
            pos += newSub.size();
        }
        return *this;
    }

    /**
     * @brief Removes the specified remove.
     * @param re Value passed to the method.
     * @return The requested remove.
     */
    SwString& remove(const SwRegularExpression& re);

    // remove(position, n)
    // - si position hors bornes => ne fait rien
    /**
     * @brief Removes the specified remove.
     * @param position Value passed to the method.
     * @param n Value passed to the method.
     * @return The requested remove.
     */
    SwString& remove(int position, int n = 1) {
        if (n == 0) return *this;

        if (position < 0) position = 0;
        const size_t size = data_.size();
        const size_t pos  = static_cast<size_t>(position);

        if (pos >= size) return *this;

        size_t len;
        if (n < 0) {
            len = std::string::npos;
        } else {
            len = static_cast<size_t>(n);
            if (len > size - pos) len = size - pos;
        }

        data_.erase(pos, len);
        return *this;
    }

    /**
     * @brief Performs the `arg` operation.
     * @param value Value passed to the method.
     * @return The requested arg.
     */
    SwString arg(const SwString& value) const {
        SwString result(*this);
        size_t start = result.data_.find('%');
        while (start != std::string::npos) {
            const size_t digitStart = start + 1;
            if (digitStart < result.data_.size()
                && std::isdigit(static_cast<unsigned char>(result.data_[digitStart]))) {
                size_t digitEnd = digitStart + 1;
                while (digitEnd < result.data_.size()
                       && std::isdigit(static_cast<unsigned char>(result.data_[digitEnd]))) {
                    ++digitEnd;
                }
                const SwString placeholder(result.data_.substr(start, digitEnd - start));
                result.replace(placeholder, value);
                break;
            }
            start = result.data_.find('%', start + 1);
        }
        return result;
    }

    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(int value) const { return arg(SwString::number(value)); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(unsigned int value) const { return arg(SwString::number(static_cast<long long>(value))); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(long value) const { return arg(SwString::number(static_cast<long long>(value))); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(unsigned long value) const { return arg(SwString::number(static_cast<unsigned long long>(value))); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(long long value) const { return arg(SwString::number(value)); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(unsigned long long value) const { return arg(SwString::number(value)); }
    /**
     * @brief Performs the `arg` operation.
     * @param value Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested arg.
     */
    SwString arg(double value, int precision = -1) const { return arg(SwString::number(value, 'g', precision < 0 ? 6 : precision)); }
    /**
     * @brief Performs the `arg` operation.
     * @param value Value passed to the method.
     * @param precision Value passed to the method.
     * @return The requested arg.
     */
    SwString arg(float value, int precision = -1) const { return arg(static_cast<double>(value), precision); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(bool value) const { return arg(SwString(value ? "true" : "false")); }
    /**
     * @brief Performs the `arg` operation.
     * @return The requested arg.
     */
    SwString arg(char value) const { return arg(SwString(std::string(1, value))); }

    /**
     * @brief Performs the `count` operation.
     * @param substring Value passed to the method.
     * @return The current count value.
     */
    size_t count(const SwString& substring) const {
        size_t pos = 0, occurrences = 0;
        while ((pos = data_.find(substring.data_, pos)) != std::string::npos) {
            ++occurrences;
            pos += substring.size();
        }
        return occurrences;
    }

    /**
     * @brief Returns the current simplified.
     * @return The current simplified.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString simplified() const {
        std::string result;
        result.reserve(data_.size());
        bool inSpace = false;
        for (char c : data_) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!inSpace) { result += ' '; inSpace = true; }
            } else { result += c; inSpace = false; }
        }
        return SwString(result);
    }

    /**
     * @brief Performs the `mid` operation.
     * @param pos Position used by the operation.
     * @param len Value passed to the method.
     * @return The requested mid.
     */
    SwString mid(int pos, int len = -1) const {
        if (pos < 0 || pos >= static_cast<int>(data_.size())) return SwString("");
        if (len < 0 || pos + len > static_cast<int>(data_.size())) len = static_cast<int>(data_.size()) - pos;
        return SwString(data_.substr(static_cast<size_t>(pos), static_cast<size_t>(len)));
    }

    /**
     * @brief Performs the `left` operation.
     * @param n Value passed to the method.
     * @return The requested left.
     */
    SwString left(int n) const {
        if (n < 0) n = 0;
        return SwString(data_.substr(0, static_cast<size_t>(n)));
    }

    /**
     * @brief Performs the `right` operation.
     * @param n Value passed to the method.
     * @return The requested right.
     */
    SwString right(size_t n) const {
        if (n >= data_.size()) return *this;
        return SwString(data_.substr(data_.size() - n));
    }

    /**
     * @brief Performs the `first` operation.
     * @return The requested first.
     */
    SwString first() const { return data_.empty() ? SwString("") : SwString(1, data_.front()); }
    /**
     * @brief Performs the `last` operation.
     * @return The requested last.
     */
    SwString last()  const { return data_.empty() ? SwString("") : SwString(1, data_.back()); }

    /**
     * @brief Performs the `append` operation.
     * @param other Value passed to the method.
     * @return The requested append.
     */
    SwString& append(const SwString& other) { data_ += other.data_; return *this; }
    /**
     * @brief Performs the `append` operation.
     * @param str Value passed to the method.
     * @return The requested append.
     */
    SwString& append(const std::string& str) { data_ += str; return *this; }
    /**
     * @brief Performs the `append` operation.
     * @return The requested append.
     */
    SwString& append(const char* cstr) { data_ += (cstr ? std::string(cstr) : std::string()); return *this; }
    SwString& append(const char* data, size_t length) { if (data && length > 0) { data_.append(data, length); } return *this; }
    /**
     * @brief Performs the `append` operation.
     * @param ch Value passed to the method.
     * @return The requested append.
     */
    SwString& append(char ch) { data_ += ch; return *this; }

    /**
     * @brief Performs the `prepend` operation.
     * @param other Value passed to the method.
     * @return The requested prepend.
     */
    SwString& prepend(const SwString& other) { data_ = other.data_ + data_; return *this; }
    /**
     * @brief Performs the `prepend` operation.
     * @param str Value passed to the method.
     * @return The requested prepend.
     */
    SwString& prepend(const std::string& str) { data_ = str + data_; return *this; }
    /**
     * @brief Performs the `prepend` operation.
     * @param data_ Value passed to the method.
     * @return The requested prepend.
     */
    SwString& prepend(const char* cstr) { data_ = (cstr ? std::string(cstr) : std::string()) + data_; return *this; }
    /**
     * @brief Performs the `prepend` operation.
     * @param ch Value passed to the method.
     * @return The requested prepend.
     */
    SwString& prepend(char ch) { data_.insert(data_.begin(), ch); return *this; }

    /**
     * @brief Performs the `substr` operation.
     * @param pos Position used by the operation.
     * @param len Value passed to the method.
     * @return The requested substr.
     */
    SwString substr(size_t pos = 0, size_t len = std::string::npos) const {
        return SwString(data_.substr(pos, len));
    }

    /**
     * @brief Performs the `erase` operation.
     * @param pos Position used by the operation.
     * @param len Value passed to the method.
     * @return The requested erase.
     */
    SwString& erase(size_t pos = 0, size_t len = std::string::npos) {
        data_.erase(pos, len);
        return *this;
    }

    /**
     * @brief Performs the `insert` operation.
     * @param pos Position used by the operation.
     * @param str Value passed to the method.
     * @return The requested insert.
     */
    SwString& insert(size_t pos, const SwString& str) {
        data_.insert(pos, str.data_);
        return *this;
    }

    /**
     * @brief Performs the `insert` operation.
     * @param pos Position used by the operation.
     * @param str Value passed to the method.
     * @return The requested insert.
     */
    SwString& insert(size_t pos, const std::string& str) {
        data_.insert(pos, str);
        return *this;
    }

    /**
     * @brief Performs the `insert` operation.
     * @param pos Position used by the operation.
     * @param count Value passed to the method.
     * @param ch Value passed to the method.
     * @return The requested insert.
     */
    SwString& insert(size_t pos, size_t count, char ch) {
        data_.insert(pos, count, ch);
        return *this;
    }

    /**
     * @brief Performs the `toUtf8` operation.
     * @return The requested to Utf8.
     */
    const char* toUtf8() const { return data_.c_str(); }

    // WARNING (fixed): previous implementation returned pointer to temporary
    /**
     * @brief Returns the current to WChar.
     * @return The current to WChar.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const wchar_t* toWChar() const {
        thread_local std::wstring wideBuffer;
        wideBuffer = toStdWString();
        return wideBuffer.c_str();
    }

    /**
     * @brief Returns the current to Std WString.
     * @return The current to Std WString.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::wstring toStdWString() const {
        if (data_.empty()) return std::wstring();
#ifdef _WIN32
        const int sourceLen = static_cast<int>(data_.size());
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, data_.data(), sourceLen, nullptr, 0);
        if (size_needed <= 0) throw std::runtime_error("Failed to convert string to wstring");
        std::wstring wstr(static_cast<size_t>(size_needed), 0);
        int converted = MultiByteToWideChar(CP_UTF8, 0, data_.data(), sourceLen, &wstr[0], size_needed);
        if (converted <= 0) throw std::runtime_error("Failed to convert string to wstring");
        return wstr;
#else
        try {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
            return conv.from_bytes(data_);
        } catch (...) {
            throw std::runtime_error("Failed to convert string to wstring");
        }
#endif
    }

    // Convert UTF-8 to Latin1 best-effort (fallback '?')
    /**
     * @brief Returns the current to Latin1.
     * @return The current to Latin1.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* toLatin1() const {
        static thread_local std::string latin1String;
        latin1String.clear();

        auto append_cp = [&](uint32_t cp){
            if (cp <= 0xFF) {
                latin1String.push_back(static_cast<char>(cp));
            } else {
                latin1String.push_back(unicodeToLatin1(cp));
            }
        };

        // Simple UTF-8 decoder
        for (size_t i = 0; i < data_.size(); ) {
            unsigned char c = static_cast<unsigned char>(data_[i]);
            if (c < 0x80) { append_cp(c); ++i; }
            else if ((c >> 5) == 0x6 && i + 1 < data_.size()) {
                uint32_t cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(data_[i+1]) & 0x3F);
                append_cp(cp); i += 2;
            } else if ((c >> 4) == 0xE && i + 2 < data_.size()) {
                uint32_t cp = ((c & 0x0F) << 12) |
                               ((static_cast<unsigned char>(data_[i+1]) & 0x3F) << 6) |
                               (static_cast<unsigned char>(data_[i+2]) & 0x3F);
                append_cp(cp); i += 3;
            } else if ((c >> 3) == 0x1E && i + 3 < data_.size()) {
                uint32_t cp = ((c & 0x07) << 18) |
                               ((static_cast<unsigned char>(data_[i+1]) & 0x3F) << 12) |
                               ((static_cast<unsigned char>(data_[i+2]) & 0x3F) << 6) |
                               (static_cast<unsigned char>(data_[i+3]) & 0x3F);
                append_cp(cp); i += 4;
            } else {
                // invalid sequence -> replacement
                latin1String.push_back('?');
                ++i;
            }
        }

        return latin1String.c_str();
    }

    /**
     * @brief Performs the `fromLatin1` operation.
     * @param str Value passed to the method.
     * @return The requested from Latin1.
     */
    static SwString fromLatin1(const char* str, size_t length) { return SwString(std::string(str, length)); }
    /**
     * @brief Performs the `fromUtf8` operation.
     * @param str Value passed to the method.
     * @param length Value passed to the method.
     * @return The requested from Utf8.
     */
    static SwString fromUtf8(const char* str, size_t length = static_cast<size_t>(-1)) {
        if (!str) {
            return SwString();
        }
        // Note: `size_t` is unsigned, so `-1` becomes SIZE_MAX.
        // Treat SIZE_MAX as a sentinel meaning "null-terminated".
        if (length == static_cast<size_t>(-1)) {
            return SwString(str);
        }
        return SwString(std::string(str, str + length));
    }


    /**
     * @brief Performs the `resize` operation.
     */
    void resize(int newSize) { data_.resize(static_cast<size_t>(newSize)); }

    /**
     * @brief Performs the `constData` operation.
     * @return The requested const Data.
     */
    const char* constData() const { return data_.c_str(); }
    /**
     * @brief Performs the `data` operation.
     * @return The requested data.
     */
    const char* data() const { return constData(); }
    /**
     * @brief Performs the `data` operation.
     * @return The requested data.
     */
    char* data() { return data_.empty() ? nullptr : &data_[0]; }

    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    char* begin() { return data_.empty() ? nullptr : &data_[0]; }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    char* end() { return data_.empty() ? nullptr : &data_[0] + data_.size(); }
    /**
     * @brief Performs the `begin` operation.
     * @return The requested begin.
     */
    const char* begin() const { return data_.data(); }
    /**
     * @brief Performs the `end` operation.
     * @return The requested end.
     */
    const char* end() const { return data_.data() + data_.size(); }

    // Sizes in UTF-16/UTF-32 code units
    /**
     * @brief Returns the current utf16 Size.
     * @return The current utf16 Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t utf16Size() const {
#ifdef _WIN32
        return toStdWString().size(); // wchar_t is UTF-16 on Windows
#else
        try {
            std::u16string u16 = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(data_);
            return u16.size();
        } catch (...) {
            return 0;
        }
#endif
    }

    /**
     * @brief Returns the current utf32 Size.
     * @return The current utf32 Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t utf32Size() const {
        try {
#ifdef _WIN32
            // Convert to UTF-32 via UTF-16 -> UTF-8 -> UTF-32 for simplicity
            std::wstring w = toStdWString();
            std::wstring_convert<std::codecvt_utf8<wchar_t>> conv8;
            std::string u8 = conv8.to_bytes(w);
            std::u32string u32 = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{}.from_bytes(u8);
            return u32.size();
#else
            std::u32string u32 = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{}.from_bytes(data_);
            return u32.size();
#endif
        } catch (...) {
            return 0;
        }
    }

    /**
     * @brief Performs the `chop` operation.
     * @param n Value passed to the method.
     * @return The requested chop.
     */
    SwString& chop(int n) {
        if (n <= 0) return *this;
        if (static_cast<size_t>(n) >= data_.size()) data_.clear();
        else data_.erase(data_.size() - static_cast<size_t>(n));
        return *this;
    }

    /**
     * @brief Performs the `join` operation.
     * @param list Value passed to the method.
     * @param separator Value passed to the method.
     * @return The requested join.
     */
    static SwString join(const SwList<SwString>& list, const SwString& separator) {
        SwString result;
        for (int i = 0; i < list.size(); ++i) {
            if (i > 0) result += separator;
            result += list[i];
        }
        return result;
    }

    /**
     * @brief Performs the `join` operation.
     * @param list Value passed to the method.
     * @param separator Value passed to the method.
     * @return The requested join.
     */
    static SwString join(const SwList<SwString>& list, char separator) {
        return join(list, SwString(std::string(1, separator)));
    }

    friend SwString operator+(const char* lhs, const SwString& rhs);

#ifdef QT_CORE_LIB
    friend QDebug operator<<(QDebug debug, const SwString& str) {
        debug.nospace() << str.toStdString().c_str();
        return debug.space();
    }
#endif

private:
    std::string data_;

    static void toLowerInPlace(std::string& value) {
        for (size_t i = 0; i < value.size(); ++i) {
            value[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
        }
    }

    static std::string sanitizeNumericString(const std::string& input);
    static bool parseIntegerString(const std::string& input, long long& value);
    bool parseNumericDouble(double& value) const;
    template <typename UnsignedT>
    static SwString numberUnsignedImpl_(UnsignedT value, int base);
    template <typename SignedT>
    static SwString numberSignedImpl_(SignedT value, int base);
    static SwString numberFloatingImpl_(double value, char format, int precision);

    char unicodeToLatin1(char32_t unicode) const {
        static const std::unordered_map<char32_t, char> unicodeToLatin1Table = {
            {0x0100, 'A'}, {0x0101, 'a'}, {0x0102, 'A'}, {0x0103, 'a'}, {0x0104, 'A'}, {0x0105, 'a'},
            {0x0106, 'C'}, {0x0107, 'c'}, {0x0108, 'C'}, {0x0109, 'c'}, {0x010A, 'C'}, {0x010B, 'c'},
            {0x010C, 'C'}, {0x010D, 'c'}, {0x010E, 'D'}, {0x010F, 'd'}, {0x0110, 'D'}, {0x0111, 'd'},
            {0x0112, 'E'}, {0x0113, 'e'}, {0x0114, 'E'}, {0x0115, 'e'}, {0x0116, 'E'}, {0x0117, 'e'},
            {0x0118, 'E'}, {0x0119, 'e'}, {0x011A, 'E'}, {0x011B, 'e'}, {0x011C, 'G'}, {0x011D, 'g'},
            {0x011E, 'G'}, {0x011F, 'g'}, {0x0120, 'G'}, {0x0121, 'g'}, {0x0122, 'G'}, {0x0123, 'g'},
            {0x0124, 'H'}, {0x0125, 'h'}, {0x0126, 'H'}, {0x0127, 'h'}, {0x0128, 'I'}, {0x0129, 'i'},
            {0x012A, 'I'}, {0x012B, 'i'}, {0x012C, 'I'}, {0x012D, 'i'}, {0x012E, 'I'}, {0x012F, 'i'},
            {0x0130, 'I'}, {0x0131, 'i'}, {0x0132, 'I'}, {0x0133, 'i'}, {0x0134, 'J'}, {0x0135, 'j'},
            {0x0136, 'K'}, {0x0137, 'k'}, {0x0138, 'k'}, {0x0139, 'L'}, {0x013A, 'l'}, {0x013B, 'L'},
            {0x013C, 'l'}, {0x013D, 'L'}, {0x013E, 'l'}, {0x013F, 'L'}, {0x0140, 'l'}, {0x0141, 'L'},
            {0x0142, 'l'}, {0x0143, 'N'}, {0x0144, 'n'}, {0x0145, 'N'}, {0x0146, 'n'}, {0x0147, 'N'},
            {0x0148, 'n'}, {0x0149, 'n'}, {0x014A, 'N'}, {0x014B, 'n'}, {0x014C, 'O'}, {0x014D, 'o'},
            {0x014E, 'O'}, {0x014F, 'o'}, {0x0150, 'O'}, {0x0151, 'o'}, {0x0152, 'O'}, {0x0153, 'o'},
            {0x0154, 'R'}, {0x0155, 'r'}, {0x0156, 'R'}, {0x0157, 'r'}, {0x0158, 'R'}, {0x0159, 'r'},
            {0x015A, 'S'}, {0x015B, 's'}, {0x015C, 'S'}, {0x015D, 's'}, {0x015E, 'S'}, {0x015F, 's'},
            {0x0160, 'S'}, {0x0161, 's'}, {0x0162, 'T'}, {0x0163, 't'}, {0x0164, 'T'}, {0x0165, 't'},
            {0x0166, 'T'}, {0x0167, 't'}, {0x0168, 'U'}, {0x0169, 'u'}, {0x016A, 'U'}, {0x016B, 'u'},
            {0x016C, 'U'}, {0x016D, 'u'}, {0x016E, 'U'}, {0x016F, 'u'}, {0x0170, 'U'}, {0x0171, 'u'},
            {0x0172, 'U'}, {0x0173, 'u'}, {0x0174, 'W'}, {0x0175, 'w'}, {0x0176, 'Y'}, {0x0177, 'y'},
            {0x0178, 'Y'}, {0x0179, 'Z'}, {0x017A, 'z'}, {0x017B, 'Z'}, {0x017C, 'z'}, {0x017D, 'Z'},
            {0x017E, 'z'}, {0x017F, 's'}, {0x0180, 'b'}, {0x0181, 'B'}, {0x0182, 'B'}, {0x0183, 'b'},
            {0x0186, 'C'}, {0x0187, 'C'}, {0x0188, 'c'}, {0x0189, 'D'}, {0x018A, 'D'}, {0x018B, 'D'},
            {0x018C, 'd'}, {0x0192, 'f'}, {0x0193, 'G'}, {0x0194, 'G'}, {0x0195, 'h'}, {0x0197, 'I'},
            {0x0198, 'K'}, {0x0199, 'k'}, {0x019A, 'l'}, {0x019B, 'l'}, {0x019C, 'M'}, {0x019D, 'N'},
            {0x019E, 'n'}, {0x019F, 'O'}, {0x01A0, 'O'}, {0x01A1, 'o'}, {0x01A2, 'Q'}, {0x01A3, 'q'},
            {0x01A4, 'P'}, {0x01A5, 'p'}, {0x01A6, 'R'}, {0x01A7, 'S'}, {0x01A8, 's'}, {0x01A9, 'T'},
            {0x01AA, 't'}, {0x01AB, 't'}, {0x01AC, 'T'}, {0x01AD, 't'}, {0x01AE, 'T'}, {0x01AF, 'U'},
            {0x01B0, 'u'}, {0x01B1, 'V'}, {0x01B2, 'Y'}, {0x01B3, 'Y'}, {0x01B4, 'y'}, {0x01B5, 'Z'},
        };
        auto it = unicodeToLatin1Table.find(unicode);
        if (it != unicodeToLatin1Table.end()) return it->second;
        return '?';
    }
};

inline SwString operator+(const char* lhs, const SwString& rhs) { return SwString(lhs ? lhs : "") + rhs; }

template <typename UnsignedT>
inline SwString SwString::numberUnsignedImpl_(UnsignedT value, int base) {
    if (base < 2 || base > 36) {
        base = 10;
    }

    std::string digits;
    do {
        const unsigned int digit = static_cast<unsigned int>(value % static_cast<UnsignedT>(base));
        digits.push_back("0123456789abcdefghijklmnopqrstuvwxyz"[digit]);
        value /= static_cast<UnsignedT>(base);
    } while (value != 0);
    std::reverse(digits.begin(), digits.end());
    return SwString(digits);
}

template <typename SignedT>
inline SwString SwString::numberSignedImpl_(SignedT value, int base) {
    typedef typename std::make_unsigned<SignedT>::type UnsignedT;
    const bool negative = value < static_cast<SignedT>(0);

    UnsignedT magnitude = 0;
    if (negative) {
        magnitude = static_cast<UnsignedT>(-(value + static_cast<SignedT>(1)));
        magnitude = static_cast<UnsignedT>(magnitude + static_cast<UnsignedT>(1));
    } else {
        magnitude = static_cast<UnsignedT>(value);
    }

    SwString out = numberUnsignedImpl_(magnitude, base);
    if (!negative) {
        return out;
    }
    return SwString("-") + out;
}

inline SwString SwString::numberFloatingImpl_(double value, char format, int precision) {
    int safePrecision = precision;
    if (safePrecision < 0) {
        safePrecision = 6;
    }

    const char lowerFormat = static_cast<char>(std::tolower(static_cast<unsigned char>(format)));
    std::ostringstream os;
    os.imbue(std::locale::classic());
    if (format == 'E' || format == 'G') {
        os << std::uppercase;
    }
    if (lowerFormat == 'f') {
        os << std::fixed;
    } else if (lowerFormat == 'e') {
        os << std::scientific;
    } else {
        os.setf(std::ios::fmtflags(0), std::ios::floatfield);
    }
    os << std::setprecision(safePrecision) << value;
    return SwString(os.str());
}

inline std::string SwString::sanitizeNumericString(const std::string& input) {
    std::string sanitized;
    sanitized.reserve(input.size());
    for (char c : input) {
        if (c != '_') {
            sanitized.push_back(c);
        }
    }
    return sanitized;
}

inline bool SwString::parseIntegerString(const std::string& input, long long& value) {
    if (input.empty()) {
        return false;
    }
    size_t pos = 0;
    bool negative = false;
    if (input[pos] == '+' || input[pos] == '-') {
        negative = (input[pos] == '-');
        ++pos;
        if (pos >= input.size()) {
            return false;
        }
    }

    if (input.size() >= pos + 2 &&
        input[pos] == '0' &&
        (input[pos + 1] == 'b' || input[pos + 1] == 'B')) {
        if (pos + 2 >= input.size()) {
            return false;
        }
        const unsigned long long maxPositive =
            static_cast<unsigned long long>(std::numeric_limits<long long>::max());
        const unsigned long long maxMagnitude = negative ? (maxPositive + 1ull) : maxPositive;
        unsigned long long accumulator = 0;
        for (size_t i = pos + 2; i < input.size(); ++i) {
            char c = input[i];
            if (c != '0' && c != '1') {
                return false;
            }
            const unsigned long long bit = static_cast<unsigned long long>(c - '0');
            if (accumulator > ((maxMagnitude - bit) >> 1)) {
                return false;
            }
            accumulator = (accumulator << 1) | bit;
        }
        if (negative) {
            if (accumulator == maxPositive + 1ull) {
                value = std::numeric_limits<long long>::min();
            } else {
                value = -static_cast<long long>(accumulator);
            }
        } else {
            value = static_cast<long long>(accumulator);
        }
        return true;
    }

    try {
        value = std::stoll(input, nullptr, 0);
        return true;
    } catch (...) {
        return false;
    }
}

inline bool SwString::parseNumericDouble(double& value) const {
    SwString cleaned = trimmed();
    std::string sanitized = sanitizeNumericString(cleaned.data_);
    if (sanitized.empty()) {
        return false;
    }

    auto hasFractionOrExponent = sanitized.find_first_of(".eE") != std::string::npos;

    if (hasFractionOrExponent) {
        try {
            value = std::stod(sanitized);
            return true;
        } catch (...) {
            // fallback to integer parsing below
        }
    }

    long long intValue = 0;
    if (parseIntegerString(sanitized, intValue)) {
        value = static_cast<double>(intValue);
        return true;
    }

    try {
        value = std::stod(sanitized);
        return true;
    } catch (...) {
        return false;
    }
}

inline int SwString::compare(const SwString& other, Sw::CaseSensitivity cs) const {
    if (cs == Sw::CaseSensitive) {
        if (data_ == other.data_) {
            return 0;
        }
        return (data_ < other.data_) ? -1 : 1;
    }

    const size_t minLen = (data_.size() < other.data_.size()) ? data_.size() : other.data_.size();
    for (size_t i = 0; i < minLen; ++i) {
        const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(data_[i])));
        const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(other.data_[i])));
        if (lhs < rhs) {
            return -1;
        }
        if (lhs > rhs) {
            return 1;
        }
    }

    if (data_.size() == other.data_.size()) {
        return 0;
    }
    return data_.size() < other.data_.size() ? -1 : 1;
}

inline int SwString::compare(const char* str, Sw::CaseSensitivity cs) const {
    if (!str) {
        return data_.empty() ? 0 : 1;
    }
    return compare(SwString(str), cs);
}

inline SwString::SwString(const SwByteArray& bytes) {
    const char* ptr = bytes.constData();
    if (!ptr || bytes.size() == 0) {
        data_.clear();
        return;
    }
    data_.assign(ptr, ptr + bytes.size());
}

inline SwString& SwString::operator=(const SwByteArray& bytes) {
    const char* ptr = bytes.constData();
    if (!ptr || bytes.size() == 0) {
        data_.clear();
    } else {
        data_.assign(ptr, ptr + bytes.size());
    }
    return *this;
}

namespace std {
template <>
struct hash<SwString> {
    /**
     * @brief Performs the `operator` operation.
     * @return The requested operator.
     */
    size_t operator()(const SwString& s) const noexcept { return std::hash<std::string>{}(s.toStdString()); }
};
}

using SwStringList = SwList<SwString>;

#endif // SWSTRING_H
