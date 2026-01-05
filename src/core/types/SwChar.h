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

#ifndef SWCHAR_H
#define SWCHAR_H
#ifdef QT_CORE_LIB
#include <QChar>
#include <QDebug>
#endif

#include <cctype>
#include <cstdint>
#include <string>

// SwChar: Qt-like (subset) character type.
// - Stores a 16-bit code unit (like QChar).
// - Provides best-effort classification without ICU/Qt.
// - Can "act like a string" via operator std::string() (UTF-8 encoded)
//   so SwString().append(SwChar) can work via SwString::append(const std::string&).
class SwChar {
public:
    using ushort = std::uint16_t;
    using uint   = std::uint32_t;

    // Constructors (Qt-like)
    constexpr SwChar() noexcept : u_(0) {}
    constexpr SwChar(char c) noexcept : u_(static_cast<ushort>(static_cast<unsigned char>(c))) {}
    constexpr SwChar(unsigned char c) noexcept : u_(static_cast<ushort>(c)) {}
    constexpr SwChar(ushort uc) noexcept : u_(uc) {}

    // Static ctors (Qt-like)
    static constexpr SwChar fromLatin1(char c) noexcept { return SwChar(c); }
    static constexpr SwChar fromAscii(char c) noexcept  { return SwChar(static_cast<unsigned char>(c)); }
    static constexpr SwChar fromUcs2(ushort uc) noexcept { return SwChar(uc); }
    static constexpr SwChar fromUcs4(uint uc) noexcept {
        // Best-effort: if it fits in one UTF-16 code unit, keep it; else U+FFFD
        return (uc <= 0xFFFFu) ? SwChar(static_cast<ushort>(uc)) : SwChar(static_cast<ushort>(0xFFFDu));
    }

    // Accessors
    constexpr bool isNull() const noexcept { return u_ == 0; }
    constexpr ushort unicode() const noexcept { return u_; } // QChar::unicode()

    // Surrogates (Qt-like subset)
    constexpr bool isHighSurrogate() const noexcept { return (u_ >= 0xD800u && u_ <= 0xDBFFu); }
    constexpr bool isLowSurrogate()  const noexcept { return (u_ >= 0xDC00u && u_ <= 0xDFFFu); }
    constexpr bool isSurrogate()     const noexcept { return (u_ >= 0xD800u && u_ <= 0xDFFFu); }

    // Conversions (Qt-like)
    constexpr char toLatin1() const noexcept { return (u_ <= 0x00FFu) ? static_cast<char>(u_) : '?'; }
    constexpr char toAscii()  const noexcept { return (u_ <= 0x007Fu) ? static_cast<char>(u_) : '\0'; }

    // UTF-8 encoding (so it can "pass as a string")
    std::string toStdString() const {
        // Replace surrogates by U+FFFD
        uint cp = isSurrogate() ? 0xFFFDu : static_cast<uint>(u_);
        return encodeUtf8(cp);
    }

    std::string toUtf8String() const { return toStdString(); }

    // Implicit conversion to std::string (UTF-8)
    // -> makes SwString().append(SwChar) work via append(const std::string&).
    operator std::string() const { return toStdString(); }

    // Classification (best-effort)
    bool isSpace() const noexcept {
        if (u_ <= 0x00FFu) {
            return std::isspace(static_cast<unsigned char>(u_)) != 0;
        }
        // Common Unicode spaces (best-effort)
        switch (u_) {
            case 0x0085: // NEL
            case 0x00A0: // NBSP
            case 0x1680:
            case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
            case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
            case 0x200A:
            case 0x2028:
            case 0x2029:
            case 0x202F:
            case 0x205F:
            case 0x3000:
                return true;
            default:
                return false;
        }
    }

    bool isDigit() const noexcept {
        if (u_ <= 0x00FFu) return std::isdigit(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    bool isLetter() const noexcept {
        if (u_ <= 0x00FFu) return std::isalpha(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    bool isLetterOrNumber() const noexcept {
        if (u_ <= 0x00FFu) return std::isalnum(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    bool isUpper() const noexcept {
        if (u_ <= 0x00FFu) return std::isupper(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    bool isLower() const noexcept {
        if (u_ <= 0x00FFu) return std::islower(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    bool isPunct() const noexcept {
        if (u_ <= 0x00FFu) return std::ispunct(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    bool isHexDigit() const noexcept {
        if (u_ <= 0x00FFu) return std::isxdigit(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    // Case conversion (best-effort)
    SwChar toUpper() const noexcept {
        if (u_ <= 0x00FFu) {
            const unsigned char c = static_cast<unsigned char>(u_);
            return SwChar(static_cast<unsigned char>(std::toupper(c)));
        }
        return *this;
    }

    SwChar toLower() const noexcept {
        if (u_ <= 0x00FFu) {
            const unsigned char c = static_cast<unsigned char>(u_);
            return SwChar(static_cast<unsigned char>(std::tolower(c)));
        }
        return *this;
    }

    // Operators
    friend constexpr bool operator==(SwChar a, SwChar b) noexcept { return a.u_ == b.u_; }
    friend constexpr bool operator!=(SwChar a, SwChar b) noexcept { return a.u_ != b.u_; }
    friend constexpr bool operator<(SwChar a, SwChar b)  noexcept { return a.u_ <  b.u_; }
    friend constexpr bool operator>(SwChar a, SwChar b)  noexcept { return a.u_ >  b.u_; }
    friend constexpr bool operator<=(SwChar a, SwChar b) noexcept { return a.u_ <= b.u_; }
    friend constexpr bool operator>=(SwChar a, SwChar b) noexcept { return a.u_ >= b.u_; }

#ifdef QT_CORE_LIB
    // Optional Qt interop
    constexpr SwChar(const QChar& qc) noexcept : u_(static_cast<ushort>(qc.unicode())) {}
    QChar toQChar() const { return QChar(static_cast<ushort>(u_)); }

    friend QDebug operator<<(QDebug debug, const SwChar& ch) {
        debug.nospace() << QChar(ch.u_);
        return debug.space();
    }
#endif

private:
    ushort u_;

    static std::string encodeUtf8(uint cp) {
        std::string out;

        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0x10FFFFu) {
            out.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            // U+FFFD
            out.push_back(static_cast<char>(0xEFu));
            out.push_back(static_cast<char>(0xBFu));
            out.push_back(static_cast<char>(0xBDu));
        }

        return out;
    }
};

#endif // SWCHAR_H
