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
 * @file src/core/gui/SwKeySequence.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwKeySequence in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the key sequence interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwKey and SwKeySequence.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwKeySequence - key sequence helper.
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
    /**
     * @brief Constructs a `SwKeySequence` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwKeySequence() = default;

    /**
     * @brief Constructs a `SwKeySequence` instance.
     * @param ch Value passed to the method.
     * @param ctrl Value passed to the method.
     * @param shift Value passed to the method.
     * @param alt Value passed to the method.
     * @param alt Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwKeySequence(char ch, bool ctrl = false, bool shift = false, bool alt = false)
        : m_key(SwKey::Character)
        , m_char(ch)
        , m_ctrl(ctrl)
        , m_shift(shift)
        , m_alt(alt) {}

    /**
     * @brief Constructs a `SwKeySequence` instance.
     * @param key Value passed to the method.
     * @param ctrl Value passed to the method.
     * @param shift Value passed to the method.
     * @param alt Value passed to the method.
     * @param alt Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwKeySequence(SwKey key, bool ctrl = false, bool shift = false, bool alt = false)
        : m_key(key)
        , m_ctrl(ctrl)
        , m_shift(shift)
        , m_alt(alt) {}

    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const {
        if (m_key == SwKey::Unknown) {
            return false;
        }
        if (m_key == SwKey::Character) {
            return m_char != '\0';
        }
        return true;
    }

    /**
     * @brief Returns the current key.
     * @return The current key.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwKey key() const { return m_key; }
    /**
     * @brief Returns the current character.
     * @return The current character.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    char character() const { return m_char; }
    /**
     * @brief Returns the current ctrl.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool ctrl() const { return m_ctrl; }
    /**
     * @brief Returns the current shift.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool shift() const { return m_shift; }
    /**
     * @brief Returns the current alt.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool alt() const { return m_alt; }

    /**
     * @brief Sets the enabled Modifiers.
     * @param ctrl Value passed to the method.
     * @param shift Value passed to the method.
     * @param alt Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEnabledModifiers(bool ctrl, bool shift, bool alt) {
        m_ctrl = ctrl;
        m_shift = shift;
        m_alt = alt;
    }

    /**
     * @brief Performs the `matches` operation.
     * @param keyCode Value passed to the method.
     * @param ctrlPressed Value passed to the method.
     * @param shiftPressed Value passed to the method.
     * @param altPressed Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

    /**
     * @brief Returns the current to String.
     * @return The current to String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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

    /**
     * @brief Performs the `fromString` operation.
     * @param text Value passed to the method.
     * @return The requested from String.
     */
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

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwKeySequence& other) const {
        return m_key == other.m_key &&
               m_char == other.m_char &&
               m_ctrl == other.m_ctrl &&
               m_shift == other.m_shift &&
               m_alt == other.m_alt;
    }

    /**
     * @brief Performs the `operator!=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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
