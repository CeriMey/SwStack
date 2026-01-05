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

/***************************************************************************************************
 * SwKeySequence - Qt-like key sequence helper (≈ QKeySequence).
 *
 * Scope (v1):
 * - Stores a single key + modifiers (Ctrl/Shift/Alt).
 * - Cross-platform matching using SwWidgetPlatformAdapter helpers for common keys.
 * - Optional string parsing (e.g. "Ctrl+Shift+A", "Alt+Up", "Esc").
 **************************************************************************************************/

#include "SwWidgetPlatformAdapter.h"

#include "core/types/SwString.h"

#include <cctype>

enum class SwKey {
    Unknown = 0,
    Character,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    Return,
    Escape,
    Backspace,
    Delete,
    Space
};

class SwKeySequence {
public:
    SwKeySequence() = default;

    SwKeySequence(char ch, bool ctrl = false, bool shift = false, bool alt = false)
        : m_key(SwKey::Character)
        , m_char(ch)
        , m_ctrl(ctrl)
        , m_shift(shift)
        , m_alt(alt) {}

    SwKeySequence(SwKey key, bool ctrl = false, bool shift = false, bool alt = false)
        : m_key(key)
        , m_ctrl(ctrl)
        , m_shift(shift)
        , m_alt(alt) {}

    bool isValid() const {
        if (m_key == SwKey::Unknown) {
            return false;
        }
        if (m_key == SwKey::Character) {
            return m_char != '\0';
        }
        return true;
    }

    SwKey key() const { return m_key; }
    char character() const { return m_char; }
    bool ctrl() const { return m_ctrl; }
    bool shift() const { return m_shift; }
    bool alt() const { return m_alt; }

    void setEnabledModifiers(bool ctrl, bool shift, bool alt) {
        m_ctrl = ctrl;
        m_shift = shift;
        m_alt = alt;
    }

    bool matches(int keyCode, bool ctrlPressed, bool shiftPressed, bool altPressed) const {
        if (!isValid()) {
            return false;
        }
        if (ctrlPressed != m_ctrl || shiftPressed != m_shift || altPressed != m_alt) {
            return false;
        }

        switch (m_key) {
        case SwKey::Character: {
            if (m_char >= 'a' && m_char <= 'z') {
                return SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, static_cast<char>(std::toupper(static_cast<unsigned char>(m_char))));
            }
            if (m_char >= 'A' && m_char <= 'Z') {
                return SwWidgetPlatformAdapter::matchesShortcutKey(keyCode, m_char);
            }
            return keyCode == static_cast<unsigned char>(m_char);
        }
        case SwKey::Up: return SwWidgetPlatformAdapter::isUpArrowKey(keyCode);
        case SwKey::Down: return SwWidgetPlatformAdapter::isDownArrowKey(keyCode);
        case SwKey::Left: return SwWidgetPlatformAdapter::isLeftArrowKey(keyCode);
        case SwKey::Right: return SwWidgetPlatformAdapter::isRightArrowKey(keyCode);
        case SwKey::Home: return SwWidgetPlatformAdapter::isHomeKey(keyCode);
        case SwKey::End: return SwWidgetPlatformAdapter::isEndKey(keyCode);
        case SwKey::Return: return SwWidgetPlatformAdapter::isReturnKey(keyCode);
        case SwKey::Escape: return SwWidgetPlatformAdapter::isEscapeKey(keyCode);
        case SwKey::Backspace: return SwWidgetPlatformAdapter::isBackspaceKey(keyCode);
        case SwKey::Delete: return SwWidgetPlatformAdapter::isDeleteKey(keyCode);
        case SwKey::Space: return keyCode == ' ';
        default: break;
        }
        return false;
    }

    SwString toString() const {
        if (!isValid()) {
            return SwString();
        }
        SwString out;
        if (m_ctrl) out += SwString("Ctrl+");
        if (m_shift) out += SwString("Shift+");
        if (m_alt) out += SwString("Alt+");

        out += keyName_();
        return out;
    }

    static SwKeySequence fromString(const SwString& text) {
        const SwString cleaned = text.trimmed();
        if (cleaned.isEmpty()) {
            return SwKeySequence();
        }

        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        SwKey key = SwKey::Unknown;
        char ch = '\0';

        SwList<SwString> parts;
        if (cleaned.contains("+")) {
            parts = cleaned.split('+');
        } else {
            parts.append(cleaned);
        }

        for (size_t i = 0; i < parts.size(); ++i) {
            SwString token = parts[i].trimmed();
            if (token.isEmpty()) {
                continue;
            }
            token = token.toLower();

            if (token == "ctrl" || token == "control") {
                ctrl = true;
                continue;
            }
            if (token == "shift") {
                shift = true;
                continue;
            }
            if (token == "alt") {
                alt = true;
                continue;
            }

            // Key token
            if (token == "up") key = SwKey::Up;
            else if (token == "down") key = SwKey::Down;
            else if (token == "left") key = SwKey::Left;
            else if (token == "right") key = SwKey::Right;
            else if (token == "home") key = SwKey::Home;
            else if (token == "end") key = SwKey::End;
            else if (token == "return" || token == "enter") key = SwKey::Return;
            else if (token == "esc" || token == "escape") key = SwKey::Escape;
            else if (token == "backspace" || token == "bs") key = SwKey::Backspace;
            else if (token == "del" || token == "delete") key = SwKey::Delete;
            else if (token == "space") key = SwKey::Space;
            else if (token.size() == 1) {
                key = SwKey::Character;
                ch = token.toUpper().toStdString()[0];
            } else {
                // Unsupported token: keep invalid.
            }
        }

        if (key == SwKey::Character) {
            return SwKeySequence(ch, ctrl, shift, alt);
        }
        return SwKeySequence(key, ctrl, shift, alt);
    }

    bool operator==(const SwKeySequence& other) const {
        return m_key == other.m_key &&
               m_char == other.m_char &&
               m_ctrl == other.m_ctrl &&
               m_shift == other.m_shift &&
               m_alt == other.m_alt;
    }

    bool operator!=(const SwKeySequence& other) const { return !(*this == other); }

private:
    SwString keyName_() const {
        switch (m_key) {
        case SwKey::Character: return SwString(m_char);
        case SwKey::Up: return SwString("Up");
        case SwKey::Down: return SwString("Down");
        case SwKey::Left: return SwString("Left");
        case SwKey::Right: return SwString("Right");
        case SwKey::Home: return SwString("Home");
        case SwKey::End: return SwString("End");
        case SwKey::Return: return SwString("Return");
        case SwKey::Escape: return SwString("Esc");
        case SwKey::Backspace: return SwString("Backspace");
        case SwKey::Delete: return SwString("Delete");
        case SwKey::Space: return SwString("Space");
        default: break;
        }
        return SwString();
    }

    SwKey m_key{SwKey::Unknown};
    char m_char{'\0'};
    bool m_ctrl{false};
    bool m_shift{false};
    bool m_alt{false};
};

