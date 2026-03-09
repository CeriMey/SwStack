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
 * @file src/core/gui/SwShortcut.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwShortcut in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the shortcut interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwShortcut.
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
 * SwShortcut - shortcut object.
 *
 * Scope (v1):
 * - Single-key sequence (SwKeySequence) attached to a widget.
 * - Dispatches from the focused widget up to the window root.
 **************************************************************************************************/

#include "SwKeySequence.h"
#include "SwWidget.h"

#include "core/object/SwObject.h"

class SwShortcut : public SwObject {
    SW_OBJECT(SwShortcut, SwObject)

public:
    /**
     * @brief Constructs a `SwShortcut` instance.
     * @param parentWidget Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwShortcut(SwWidget* parentWidget = nullptr)
        : SwObject(parentWidget) {}

    /**
     * @brief Constructs a `SwShortcut` instance.
     * @param key Value passed to the method.
     * @param key Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwShortcut(const SwKeySequence& key, SwWidget* parentWidget)
        : SwObject(parentWidget)
        , m_key(key) {}

    /**
     * @brief Sets the key.
     * @param key Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setKey(const SwKeySequence& key) {
        if (m_key == key) {
            return;
        }
        m_key = key;
        changed();
    }

    /**
     * @brief Returns the current key.
     * @return The current key.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwKeySequence key() const { return m_key; }

    /**
     * @brief Sets the enabled.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEnabled(bool on) {
        if (m_enabled == on) {
            return;
        }
        m_enabled = on;
        changed();
    }

    /**
     * @brief Returns whether the object reports enabled.
     * @return `true` when the object reports enabled; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Performs the `dispatch` operation.
     * @param root Value passed to the method.
     * @param event Event object forwarded by the framework.
     * @return The requested dispatch.
     */
    static bool dispatch(SwWidget* root, KeyEvent* event) {
        if (!root || !event || event->isAccepted()) {
            return false;
        }

        SwWidget* focused = focusedWidget_(root);
        SwWidget* start = focused ? focused : root;

        SwWidget* w = start;
        while (w) {
            const auto& kids = w->children();
            for (SwObject* obj : kids) {
                auto* sc = dynamic_cast<SwShortcut*>(obj);
                if (!sc) {
                    continue;
                }
                if (!sc->m_enabled || !sc->m_key.isValid()) {
                    continue;
                }
                if (sc->m_key.matches(event->key(), event->isCtrlPressed(), event->isShiftPressed(), event->isAltPressed())) {
                    sc->activated();
                    event->accept();
                    return true;
                }
            }

            if (w == root) {
                break;
            }
            w = dynamic_cast<SwWidget*>(w->parent());
        }

        return false;
    }

signals:
    DECLARE_SIGNAL_VOID(activated);
    DECLARE_SIGNAL_VOID(changed);

private:
    static SwWidget* focusedWidget_(SwWidget* root) {
        if (!root) {
            return nullptr;
        }
        for (SwWidget* w : root->findChildren<SwWidget>()) {
            if (!w) {
                continue;
            }
            if (w->getFocus() && w->isVisibleInHierarchy()) {
                return w;
            }
        }
        return root->getFocus() ? root : nullptr;
    }

    SwKeySequence m_key;
    bool m_enabled{true};
};
