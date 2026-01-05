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
 * SwShortcut - Qt-like shortcut object (≈ QShortcut).
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
    explicit SwShortcut(SwWidget* parentWidget = nullptr)
        : SwObject(parentWidget) {}

    SwShortcut(const SwKeySequence& key, SwWidget* parentWidget)
        : SwObject(parentWidget)
        , m_key(key) {}

    void setKey(const SwKeySequence& key) {
        if (m_key == key) {
            return;
        }
        m_key = key;
        changed();
    }

    SwKeySequence key() const { return m_key; }

    void setEnabled(bool on) {
        if (m_enabled == on) {
            return;
        }
        m_enabled = on;
        changed();
    }

    bool isEnabled() const { return m_enabled; }

    static bool dispatch(SwWidget* root, KeyEvent* event) {
        if (!root || !event || event->isAccepted()) {
            return false;
        }

        SwWidget* focused = focusedWidget_(root);
        SwWidget* start = focused ? focused : root;

        SwWidget* w = start;
        while (w) {
            const std::vector<SwObject*>& kids = w->getChildren();
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

