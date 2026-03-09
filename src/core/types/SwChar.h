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

/**
 * @file src/core/types/SwChar.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwChar in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the char interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwChar.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */

#ifdef QT_CORE_LIB
#include <QChar>
#include <QDebug>
#endif

#include <cctype>
#include <cstdint>
#include <string>

// SwChar: compact character type with a compatibility-oriented API.
// - Stores a 16-bit code unit.
// - Provides best-effort classification without ICU.
// - Can "act like a string" via operator std::string() (UTF-8 encoded)
//   so SwString().append(SwChar) can work via SwString::append(const std::string&).
class SwChar {
public:
    using ushort = std::uint16_t;
    using uint   = std::uint32_t;

    // Constructors
    /**
     * @brief Constructs a `SwChar` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    constexpr SwChar() noexcept : u_(0) {}
    /**
     * @brief Constructs a `SwChar` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    constexpr SwChar(char c) noexcept : u_(static_cast<ushort>(static_cast<unsigned char>(c))) {}
    /**
     * @brief Constructs a `SwChar` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    constexpr SwChar(unsigned char c) noexcept : u_(static_cast<ushort>(c)) {}
    /**
     * @brief Constructs a `SwChar` instance.
     * @param uc Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    constexpr SwChar(ushort uc) noexcept : u_(uc) {}

    // Static constructors
    /**
     * @brief Performs the `fromLatin1` operation.
     * @param c Value passed to the method.
     * @return The requested from Latin1.
     */
    static constexpr SwChar fromLatin1(char c) noexcept { return SwChar(c); }
    /**
     * @brief Performs the `fromAscii` operation.
     * @return The requested from Ascii.
     */
    static constexpr SwChar fromAscii(char c) noexcept  { return SwChar(static_cast<unsigned char>(c)); }
    /**
     * @brief Performs the `fromUcs2` operation.
     * @param uc Value passed to the method.
     * @return The requested from Ucs2.
     */
    static constexpr SwChar fromUcs2(ushort uc) noexcept { return SwChar(uc); }
    /**
     * @brief Performs the `fromUcs4` operation.
     * @param uc Value passed to the method.
     * @return The requested from Ucs4.
     */
    static constexpr SwChar fromUcs4(uint uc) noexcept {
        // Best-effort: if it fits in one UTF-16 code unit, keep it; else U+FFFD
        return (uc <= 0xFFFFu) ? SwChar(static_cast<ushort>(uc)) : SwChar(static_cast<ushort>(0xFFFDu));
    }

    // Accessors
    /**
     * @brief Returns whether the object reports null.
     * @return The current null.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    constexpr bool isNull() const noexcept { return u_ == 0; }
    /**
     * @brief Returns the current unicode.
     * @return The current unicode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    constexpr ushort unicode() const noexcept { return u_; } // underlying code unit

    // Surrogate helpers
    /**
     * @brief Returns whether the object reports high Surrogate.
     * @return The requested high Surrogate.
     *
     * @details This query does not modify the object state.
     */
    constexpr bool isHighSurrogate() const noexcept { return (u_ >= 0xD800u && u_ <= 0xDBFFu); }
    /**
     * @brief Returns whether the object reports low Surrogate.
     * @return The requested low Surrogate.
     *
     * @details This query does not modify the object state.
     */
    constexpr bool isLowSurrogate()  const noexcept { return (u_ >= 0xDC00u && u_ <= 0xDFFFu); }
    /**
     * @brief Returns whether the object reports surrogate.
     * @return The requested surrogate.
     *
     * @details This query does not modify the object state.
     */
    constexpr bool isSurrogate()     const noexcept { return (u_ >= 0xD800u && u_ <= 0xDFFFu); }

    // Conversions
    /**
     * @brief Performs the `toLatin1` operation.
     * @return The requested to Latin1.
     */
    constexpr char toLatin1() const noexcept { return (u_ <= 0x00FFu) ? static_cast<char>(u_) : '?'; }
    /**
     * @brief Performs the `toAscii` operation.
     * @return The requested to Ascii.
     */
    constexpr char toAscii()  const noexcept { return (u_ <= 0x007Fu) ? static_cast<char>(u_) : '\0'; }

    // UTF-8 encoding (so it can "pass as a string")
    /**
     * @brief Returns the current to Std String.
     * @return The current to Std String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::string toStdString() const {
        // Replace surrogates by U+FFFD
        uint cp = isSurrogate() ? 0xFFFDu : static_cast<uint>(u_);
        return encodeUtf8(cp);
    }

    /**
     * @brief Performs the `toUtf8String` operation.
     * @return The requested to Utf8 String.
     */
    std::string toUtf8String() const { return toStdString(); }

    // Implicit conversion to std::string (UTF-8)
    // -> makes SwString().append(SwChar) work via append(const std::string&).
    /**
     * @brief Performs the `string` operation.
     * @return The requested string.
     */
    operator std::string() const { return toStdString(); }

    // Classification (best-effort)
    /**
     * @brief Returns whether the object reports space.
     * @return `true` when the object reports space; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Returns whether the object reports digit.
     * @return `true` when the object reports digit; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isDigit() const noexcept {
        if (u_ <= 0x00FFu) return std::isdigit(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    /**
     * @brief Returns whether the object reports letter.
     * @return `true` when the object reports letter; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isLetter() const noexcept {
        if (u_ <= 0x00FFu) return std::isalpha(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    /**
     * @brief Returns whether the object reports letter Or Number.
     * @return `true` when the object reports letter Or Number; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isLetterOrNumber() const noexcept {
        if (u_ <= 0x00FFu) return std::isalnum(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    /**
     * @brief Returns whether the object reports upper.
     * @return `true` when the object reports upper; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isUpper() const noexcept {
        if (u_ <= 0x00FFu) return std::isupper(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    /**
     * @brief Returns whether the object reports lower.
     * @return `true` when the object reports lower; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isLower() const noexcept {
        if (u_ <= 0x00FFu) return std::islower(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    /**
     * @brief Returns whether the object reports punct.
     * @return `true` when the object reports punct; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isPunct() const noexcept {
        if (u_ <= 0x00FFu) return std::ispunct(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    /**
     * @brief Returns whether the object reports hex Digit.
     * @return `true` when the object reports hex Digit; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isHexDigit() const noexcept {
        if (u_ <= 0x00FFu) return std::isxdigit(static_cast<unsigned char>(u_)) != 0;
        return false;
    }

    // Case conversion (best-effort)
    /**
     * @brief Returns the current to Upper.
     * @return The current to Upper.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwChar toUpper() const noexcept {
        if (u_ <= 0x00FFu) {
            const unsigned char c = static_cast<unsigned char>(u_);
            return SwChar(static_cast<unsigned char>(std::toupper(c)));
        }
        return *this;
    }

    /**
     * @brief Returns the current to Lower.
     * @return The current to Lower.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    // Optional interop
    /**
     * @brief Constructs a `SwChar` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    constexpr SwChar(const QChar& qc) noexcept : u_(static_cast<ushort>(qc.unicode())) {}
    /**
     * @brief Performs the `toQChar` operation.
     * @return The requested to QChar.
     */
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
