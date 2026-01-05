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
static constexpr const char* kSwLogCategory_SwString = "sw.core.types.swstring";

#ifdef QT_CORE_LIB
#include <QDebug>
#endif

#include <string>
#include <iostream>
#include <cstring>
#include <cctype>
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <deque>
#include <functional>
#include <locale>
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
    SwString() : data_("") {}
    SwString(const char* str) : data_(str ? str : "") {}
#ifdef QT_CORE_LIB
    SwString(const QByteArray& arr) : data_(arr.constData(), arr.constData() + arr.size()) {}
#endif
    SwString(const std::string& str) : data_(str) {}
    SwString(const SwString& other) : data_(other.data_) {}
    SwString(const SwByteArray& bytes);
    SwString(SwString&& other) noexcept : data_(std::move(other.data_)) {}
    SwString(size_t count, char ch) : data_(std::string(count, ch)) {}
    SwString(char ch) : data_(1, ch) {}

    operator std::string&() { return data_; }
    operator const std::string&() const { return data_; }

    SwString& operator=(const SwString& other) { if (this != &other) data_ = other.data_; return *this; }
    SwString& operator=(SwString&& other) noexcept { if (this != &other) data_ = std::move(other.data_); return *this; }
    SwString& operator=(const char* str) { data_ = (str ? str : ""); return *this; }
    SwString& operator=(const std::string& str) { data_ = str; return *this; }
    SwString& operator=(const SwByteArray& bytes);

    SwString& operator+=(const SwString& other) { data_ += other.data_; return *this; }

    SwString operator+(const char* str) const { return SwString(data_ + (str ? str : "")); }
    SwString operator+(const std::string& str) const { return SwString(data_ + str); }
    SwString operator+(const SwString& other) const { return SwString(data_ + other.data_); }

    bool operator==(const SwString& other) const { return data_ == other.data_; }
    bool operator!=(const SwString& other) const { return data_ != other.data_; }
    bool operator<(const SwString& other) const { return data_ < other.data_; }
    bool operator>(const SwString& other) const { return data_ > other.data_; }

    char operator[](size_t index) const { return data_[index]; }
    char& operator[](size_t index) { return data_[index]; }

    operator std::string() const { return data_; }

    // Méthodes de base
    size_t size() const { return data_.size(); }
    size_t length() const { return data_.length(); }
    bool isEmpty() const { return data_.empty(); }
    void clear() { data_.clear(); }

    SwChar at(int index) const noexcept {
        if (index < 0) return SwChar('\0');
        const size_t i = static_cast<size_t>(index);
        if (i >= data_.size()) return SwChar('\0');
        return SwChar(static_cast<unsigned char>(data_[i]));
    }

    // optionnel: même overload côté non-const (QString::at() est const, mais ça aide la compat)
    SwChar at(int index) noexcept {
        return static_cast<const SwString&>(*this).at(index);
    }

    bool isInt() const {
        if (data_.empty() || (data_[0] == '-' && data_.size() == 1)) return false;
        for (size_t i = (data_[0] == '-') ? 1 : 0; i < data_.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(data_[i]))) return false;
        return true;
    }

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

    const std::string& toStdString() const { return data_; }
    void reserve(size_t capacity) { data_.reserve(capacity); }

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

    static SwString number(double value, int precision = -1) {
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

    static SwString number(int value) { return SwString(std::to_string(value)); }

    SwString toBase64() const { return SwString(SwCrypto::base64Encode(data_)); }

    SwString deBase64() {
        std::vector<unsigned char> decoded = SwCrypto::base64Decode(data_);
        return SwString(std::string(decoded.begin(), decoded.end()));
    }

    static SwString fromBase64(const SwString& base64) {
        std::vector<unsigned char> decoded = SwCrypto::base64Decode(base64.toStdString());
        return SwString(std::string(decoded.begin(), decoded.end()));
    }

    SwString encryptAES(const SwString& key) const {
        try { return SwString(SwCrypto::encryptAES(data_, key.data_)); }
        catch (const std::exception& e) { swCError(kSwLogCategory_SwString) << "Encryption error: " << e.what(); return SwString(""); }
    }

    SwString decryptAES(const SwString& key) {
        try { return SwString(SwCrypto::decryptAES(data_, key.data_)); }
        catch (const std::exception& e) { swCError(kSwLogCategory_SwString) << "Decryption error: " << e.what(); return SwString(""); }
    }

    static SwString decryptAES(const SwString& encryptedBase64, const SwString& key) {
        try { return SwString(SwCrypto::decryptAES(encryptedBase64.data_, key.data_)); }
        catch (const std::exception& e) { swCError(kSwLogCategory_SwString) << "Decryption error: " << e.what(); return SwString(""); }
    }

    friend std::ostream& operator<<(std::ostream& os, const SwString& str) { os << str.data_; return os; }
    friend std::istream& operator>>(std::istream& is, SwString& str) { is >> str.data_; return is; }

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

    SwList<SwString> split(const std::string& delimiter) const {
        if (delimiter.empty()) throw std::invalid_argument("Delimiter cannot be empty.");
        return split(SwString(delimiter));
    }

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

    bool contains(const SwString& substring, Sw::CaseSensitivity cs = Sw::CaseSensitive) const {
        return contains(substring.data_.c_str(), cs);
    }

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

    SwString reversed() const { return SwString(std::string(data_.rbegin(), data_.rend())); }

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

    int compare(const SwString& other, Sw::CaseSensitivity cs = Sw::CaseSensitive) const;
    int compare(const char* str, Sw::CaseSensitivity cs = Sw::CaseSensitive) const;

    int indexOf(const SwString& substring, size_t startIndex = 0) const {
        if (startIndex >= data_.size()) return -1;
        size_t pos = data_.find(substring.data_, startIndex);
        return (pos != std::string::npos) ? static_cast<int>(pos) : -1;
    }

    size_t lastIndexOf(const SwString& substring) const {
        size_t pos = data_.rfind(substring.data_);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    size_t lastIndexOf(char character) const {
        size_t pos = data_.rfind(character);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    size_t firstIndexOf(const SwString& substring) const {
        size_t pos = data_.find(substring.data_);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    size_t firstIndexOf(char character) const {
        size_t pos = data_.find(character);
        return (pos != std::string::npos) ? pos : static_cast<size_t>(-1);
    }

    SwString trimmed() const {
        size_t start = data_.find_first_not_of(" \t\n\r");
        size_t end = data_.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) return SwString("");
        return SwString(data_.substr(start, end - start + 1));
    }

    SwString toUpper() const {
        std::string upper(data_);
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return SwString(upper);
    }

    SwString toLower() const {
        std::string lower(data_);
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return SwString(lower);
    }

    // --- UTF conversions: Windows uses WinAPI; Linux uses std::wstring_convert ---
    static SwString fromWString(const std::wstring& wideStr) {
        if (wideStr.empty()) return SwString();
#ifdef _WIN32
        int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (bufferSize <= 0) return SwString();
        std::string utf8Str(static_cast<size_t>(bufferSize - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], bufferSize, nullptr, nullptr);
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

    static SwString fromWCharArray(const wchar_t* wideStr) {
        if (!wideStr) return SwString();
#ifdef _WIN32
        int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (bufferSize <= 0) return SwString();
        std::string utf8Str(static_cast<size_t>(bufferSize - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &utf8Str[0], bufferSize, nullptr, nullptr);
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

    SwString& replace(const SwString& oldSub, const SwString& newSub) {
        if (oldSub == "" || oldSub == "\0") return *this;
        size_t pos = 0;
        while ((pos = data_.find(oldSub.data_, pos)) != std::string::npos) {
            data_.replace(pos, oldSub.size(), newSub.data_);
            pos += newSub.size();
        }
        return *this;
    }

    SwString& remove(const SwRegularExpression& re);

    // remove(position, n)
    // - si position hors bornes => ne fait rien
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

    size_t count(const SwString& substring) const {
        size_t pos = 0, occurrences = 0;
        while ((pos = data_.find(substring.data_, pos)) != std::string::npos) {
            ++occurrences;
            pos += substring.size();
        }
        return occurrences;
    }

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

    SwString mid(int pos, int len = -1) const {
        if (pos < 0 || pos >= static_cast<int>(data_.size())) return SwString("");
        if (len < 0 || pos + len > static_cast<int>(data_.size())) len = static_cast<int>(data_.size()) - pos;
        return SwString(data_.substr(static_cast<size_t>(pos), static_cast<size_t>(len)));
    }

    SwString left(int n) const {
        if (n < 0) n = 0;
        return SwString(data_.substr(0, static_cast<size_t>(n)));
    }

    SwString right(size_t n) const {
        if (n >= data_.size()) return *this;
        return SwString(data_.substr(data_.size() - n));
    }

    SwString first() const { return data_.empty() ? SwString("") : SwString(1, data_.front()); }
    SwString last()  const { return data_.empty() ? SwString("") : SwString(1, data_.back()); }

    SwString& append(const SwString& other) { data_ += other.data_; return *this; }
    SwString& append(const std::string& str) { data_ += str; return *this; }
    SwString& append(const char* cstr) { data_ += (cstr ? std::string(cstr) : std::string()); return *this; }
    SwString& append(char ch) { data_ += ch; return *this; }

    SwString& prepend(const SwString& other) { data_ = other.data_ + data_; return *this; }
    SwString& prepend(const std::string& str) { data_ = str + data_; return *this; }
    SwString& prepend(const char* cstr) { data_ = (cstr ? std::string(cstr) : std::string()) + data_; return *this; }
    SwString& prepend(char ch) { data_.insert(data_.begin(), ch); return *this; }

    SwString substr(size_t pos = 0, size_t len = std::string::npos) const {
        return SwString(data_.substr(pos, len));
    }

    SwString& erase(size_t pos = 0, size_t len = std::string::npos) {
        data_.erase(pos, len);
        return *this;
    }

    SwString& insert(size_t pos, const SwString& str) {
        data_.insert(pos, str.data_);
        return *this;
    }

    SwString& insert(size_t pos, const std::string& str) {
        data_.insert(pos, str);
        return *this;
    }

    SwString& insert(size_t pos, size_t count, char ch) {
        data_.insert(pos, count, ch);
        return *this;
    }

    const char* toUtf8() const { return data_.c_str(); }

    // WARNING (fixed): previous implementation returned pointer to temporary
    const wchar_t* toWChar() const {
        thread_local std::wstring wideBuffer;
        wideBuffer = toStdWString();
        return wideBuffer.c_str();
    }

    std::wstring toStdWString() const {
        if (data_.empty()) return std::wstring();
#ifdef _WIN32
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, data_.c_str(), -1, nullptr, 0);
        if (size_needed <= 0) throw std::runtime_error("Failed to convert string to wstring");
        std::wstring wstr(static_cast<size_t>(size_needed), 0);
        MultiByteToWideChar(CP_UTF8, 0, data_.c_str(), -1, &wstr[0], size_needed);
        if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
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

    static SwString fromLatin1(const char* str, size_t length) { return SwString(std::string(str, length)); }
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


    void resize(int newSize) { data_.resize(static_cast<size_t>(newSize)); }

    const char* data() const { return data_.data(); }
    char* data() { return data_.empty() ? nullptr : &data_[0]; }

    char* begin() { return data_.empty() ? nullptr : &data_[0]; }
    char* end() { return data_.empty() ? nullptr : &data_[0] + data_.size(); }
    const char* begin() const { return data_.data(); }
    const char* end() const { return data_.data() + data_.size(); }

    // Sizes in UTF-16/UTF-32 code units
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

    SwString& chop(int n) {
        if (n <= 0) return *this;
        if (static_cast<size_t>(n) >= data_.size()) data_.clear();
        else data_.erase(data_.size() - static_cast<size_t>(n));
        return *this;
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
        long long accumulator = 0;
        for (size_t i = pos + 2; i < input.size(); ++i) {
            char c = input[i];
            if (c != '0' && c != '1') {
                return false;
            }
            accumulator = (accumulator << 1) | (c - '0');
        }
        value = negative ? -accumulator : accumulator;
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
    size_t operator()(const SwString& s) const noexcept { return std::hash<std::string>{}(s.toStdString()); }
};
}

using SwStringList = SwList<SwString>;

#endif // SWSTRING_H
