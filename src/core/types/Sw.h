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

#pragma once

/**
 * @file src/core/types/Sw.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by Sw in the CoreSw fundamental types layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the public interface interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwFlagSet, SwSize, SwColor, SwRect, SwPoint,
 * EntryType, WindowFlag, and CursorType, plus related helper declarations.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */


#include "SwFlags.h"
#include <cctype>
#include <string>
#ifndef SW_UNUSED
#define SW_UNUSED(x) (void)(x);
#endif

namespace Sw {
enum CaseSensitivity {
    CaseInsensitive,
    CaseSensitive
};
}

template<typename Enum>
class SwFlagSet {
public:
    using Underlying = int;

    /**
     * @brief Constructs a `SwFlagSet` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwFlagSet() : value(0) {}
    /**
     * @brief Constructs a `SwFlagSet` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwFlagSet(Enum flag) : value(static_cast<Underlying>(flag)) {}
    /**
     * @brief Constructs a `SwFlagSet` instance.
     * @param raw Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwFlagSet(Underlying raw) : value(raw) {}

    /**
     * @brief Performs the `operator|=` operation.
     * @param flag Value passed to the method.
     * @return The requested operator |=.
     */
    SwFlagSet& operator|=(Enum flag) {
        value |= static_cast<Underlying>(flag);
        return *this;
    }

    /**
     * @brief Performs the `operator|` operation.
     * @param flag Value passed to the method.
     * @return The requested operator |.
     */
    SwFlagSet operator|(Enum flag) const {
        SwFlagSet copy(*this);
        copy |= flag;
        return copy;
    }

    friend SwFlagSet operator|(Enum lhs, Enum rhs) {
        SwFlagSet flags(lhs);
        flags |= rhs;
        return flags;
    }

    /**
     * @brief Sets the flag.
     * @param flag Value passed to the method.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFlag(Enum flag, bool on = true) {
        if (on) {
            value |= static_cast<Underlying>(flag);
        } else {
            value &= ~static_cast<Underlying>(flag);
        }
    }

    /**
     * @brief Performs the `testFlag` operation.
     * @param flag Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool testFlag(Enum flag) const {
        return (value & static_cast<Underlying>(flag)) != 0;
    }

    /**
     * @brief Returns the current raw.
     * @return The current raw.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Underlying raw() const { return value; }

    /**
     * @brief Returns the current underlying.
     * @return The current underlying.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    explicit operator Underlying() const { return value; }

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwFlagSet& other) const {
        return value == other.value;
    }

    /**
     * @brief Performs the `operator!=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwFlagSet& other) const {
        return !(*this == other);
    }

private:
    Underlying value;
};

// Définition des types avec le préfixe "Sw"
struct SwSize {
    int width;
    int height;
};

struct SwColor {
    int r, g, b;  // Rouge, Vert, Bleu
};

struct SwRect {
    int x, y;
    int width, height;
};

struct SwPoint {
    int x;
    int y;
};


enum class EntryType {
    Files = 0x1,        // Inclure les fichiers
    Directories = 0x2,  // Inclure les répertoires
    AllEntries = Files | Directories // Tout inclure
};

using EntryTypes = SwFlagSet<EntryType>;

enum WindowFlag {
    NoFlag = 0x0,
    FramelessWindowHint = 0x1,      // Fenêtre sans bordure
    NoMinimizeButton = 0x2,         // Pas de bouton de minimisation
    NoMaximizeButton = 0x4,         // Pas de bouton de maximisation
    NoCloseButton = 0x8,            // Pas de bouton de fermeture (attention, c'est un peu plus tricky sur Windows)
    ToolWindowHint = 0x10,          // Fenêtre outil (petite barre titre)
    StayOnTopHint = 0x20            // Toujours au-dessus (topmost)
};

using WindowFlags = SwFlagSet<WindowFlag>;

enum class CursorType {
    Arrow,
    Hand,
    IBeam,
    Cross,
    Wait,
    SizeAll,
    SizeNS,
    SizeWE,
    SizeNWSE,
    SizeNESW,
    Default,
    // Ajoutez d'autres types de curseurs si nécessaire
};


enum class FocusPolicyEnum {
    Accept,
    Strong,
    NoFocus,
    // Ajoutez d'autres types de curseurs si nécessaire
};

enum class EchoModeEnum {
    NormalEcho,                 // Texte normal
    NoEcho,                     // Aucun texte affiché
    PasswordEcho,               // Masque le texte (comme un champ de mot de passe)
    PasswordEchoOnEdit,         // Masque le texte sauf pendant la modification
};

namespace SwEnumDetail {
inline std::string trimAsciiCopy(const std::string& value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

inline std::string normalizeEnumToken(const std::string& value) {
    const std::string trimmed = trimAsciiCopy(value);
    std::string result;
    result.reserve(trimmed.size());
    for (unsigned char ch : trimmed) {
        if (ch == '-' || ch == '_') {
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

inline bool parseEnumInt(const std::string& value, int& out) {
    const std::string trimmed = trimAsciiCopy(value);
    if (trimmed.empty()) {
        return false;
    }

    try {
        std::size_t parsed = 0;
        const int parsedValue = std::stoi(trimmed, &parsed, 0);
        if (parsed != trimmed.size()) {
            return false;
        }
        out = parsedValue;
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace SwEnumDetail

inline std::string swCursorTypeToString(CursorType value) {
    switch (value) {
    case CursorType::Arrow: return "Arrow";
    case CursorType::Hand: return "Hand";
    case CursorType::IBeam: return "IBeam";
    case CursorType::Cross: return "Cross";
    case CursorType::Wait: return "Wait";
    case CursorType::SizeAll: return "SizeAll";
    case CursorType::SizeNS: return "SizeNS";
    case CursorType::SizeWE: return "SizeWE";
    case CursorType::SizeNWSE: return "SizeNWSE";
    case CursorType::SizeNESW: return "SizeNESW";
    case CursorType::Default: return "Default";
    }
    return std::to_string(static_cast<int>(value));
}

inline CursorType swCursorTypeFromString(const std::string& value,
                                         CursorType fallback = CursorType::Default) {
    const std::string token = SwEnumDetail::normalizeEnumToken(value);
    if (token == "arrow") return CursorType::Arrow;
    if (token == "hand") return CursorType::Hand;
    if (token == "ibeam") return CursorType::IBeam;
    if (token == "cross") return CursorType::Cross;
    if (token == "wait") return CursorType::Wait;
    if (token == "sizeall") return CursorType::SizeAll;
    if (token == "sizens") return CursorType::SizeNS;
    if (token == "sizewe") return CursorType::SizeWE;
    if (token == "sizenwse") return CursorType::SizeNWSE;
    if (token == "sizenesw") return CursorType::SizeNESW;
    if (token == "default") return CursorType::Default;

    int parsed = 0;
    return SwEnumDetail::parseEnumInt(value, parsed) ? static_cast<CursorType>(parsed) : fallback;
}

inline std::string swFocusPolicyToString(FocusPolicyEnum value) {
    switch (value) {
    case FocusPolicyEnum::Accept: return "Accept";
    case FocusPolicyEnum::Strong: return "Strong";
    case FocusPolicyEnum::NoFocus: return "NoFocus";
    }
    return std::to_string(static_cast<int>(value));
}

inline FocusPolicyEnum swFocusPolicyFromString(const std::string& value,
                                               FocusPolicyEnum fallback = FocusPolicyEnum::Accept) {
    const std::string token = SwEnumDetail::normalizeEnumToken(value);
    if (token == "accept") return FocusPolicyEnum::Accept;
    if (token == "strong") return FocusPolicyEnum::Strong;
    if (token == "nofocus") return FocusPolicyEnum::NoFocus;

    int parsed = 0;
    return SwEnumDetail::parseEnumInt(value, parsed) ? static_cast<FocusPolicyEnum>(parsed) : fallback;
}

inline std::string swEchoModeToString(EchoModeEnum value) {
    switch (value) {
    case EchoModeEnum::NormalEcho: return "NormalEcho";
    case EchoModeEnum::NoEcho: return "NoEcho";
    case EchoModeEnum::PasswordEcho: return "PasswordEcho";
    case EchoModeEnum::PasswordEchoOnEdit: return "PasswordEchoOnEdit";
    }
    return std::to_string(static_cast<int>(value));
}

inline EchoModeEnum swEchoModeFromString(const std::string& value,
                                         EchoModeEnum fallback = EchoModeEnum::NormalEcho) {
    const std::string token = SwEnumDetail::normalizeEnumToken(value);
    if (token == "normalecho") return EchoModeEnum::NormalEcho;
    if (token == "noecho") return EchoModeEnum::NoEcho;
    if (token == "passwordecho") return EchoModeEnum::PasswordEcho;
    if (token == "passwordechoonedit") return EchoModeEnum::PasswordEchoOnEdit;

    int parsed = 0;
    return SwEnumDetail::parseEnumInt(value, parsed) ? static_cast<EchoModeEnum>(parsed) : fallback;
}

enum FontWeight {
    DontCare = 0,
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Normal = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Heavy = 900
};

// Déclaration de l'énumération DrawTextFormat
enum DrawTextFormat {
    Top = 0x00000000,
    Left = 0x00000000,
    Center = 0x00000001,
    Right = 0x00000002,
    VCenter = 0x00000004,
    Bottom = 0x00000008,
    WordBreak = 0x00000010,
    SingleLine = 0x00000020,
    ExpandTabs = 0x00000040,
    TabStop = 0x00000080,
    NoClip = 0x00000100,
    ExternalLeading = 0x00000200,
    CalcRect = 0x00000400,
    NoPrefix = 0x00000800,
    Internal = 0x00001000
};

using DrawTextFormats = SwFlagSet<DrawTextFormat>;
