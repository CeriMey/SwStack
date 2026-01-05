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

#include "SwFlags.h"
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

    SwFlagSet() : value(0) {}
    SwFlagSet(Enum flag) : value(static_cast<Underlying>(flag)) {}
    SwFlagSet(Underlying raw) : value(raw) {}

    SwFlagSet& operator|=(Enum flag) {
        value |= static_cast<Underlying>(flag);
        return *this;
    }

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

    void setFlag(Enum flag, bool on = true) {
        if (on) {
            value |= static_cast<Underlying>(flag);
        } else {
            value &= ~static_cast<Underlying>(flag);
        }
    }

    bool testFlag(Enum flag) const {
        return (value & static_cast<Underlying>(flag)) != 0;
    }

    Underlying raw() const { return value; }

    explicit operator Underlying() const { return value; }

    bool operator==(const SwFlagSet& other) const {
        return value == other.value;
    }

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
