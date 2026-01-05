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
 * SwAction - Qt-like action object (≈ QAction).
 *
 * Goal:
 * - Reusable action model for menus, toolbars, shortcuts, etc.
 * - Lightweight, signal-based, and Sw*-friendly types.
 **************************************************************************************************/

#include "core/object/SwObject.h"
#include "core/types/SwString.h"

#include "graphics/SwImage.h"

class SwMenu;

class SwAction : public SwObject {
    SW_OBJECT(SwAction, SwObject)

    CUSTOM_PROPERTY(SwString, Text, SwString()) { changed(); }
    CUSTOM_PROPERTY(bool, Enabled, true) { changed(); }
    CUSTOM_PROPERTY(bool, Checkable, false) { changed(); }
    CUSTOM_PROPERTY(bool, Checked, false) { changed(); }
    CUSTOM_PROPERTY(bool, Separator, false) { changed(); }

public:
    explicit SwAction(const SwString& text = SwString(), SwObject* parent = nullptr)
        : SwObject(parent) {
        setText(text);
    }

    void setIcon(const SwImage& icon) {
        m_icon = icon;
        changed();
    }

    const SwImage& icon() const { return m_icon; }
    bool hasIcon() const { return !m_icon.isNull(); }

    void setMenu(SwMenu* menu) {
        if (m_menu == menu) {
            return;
        }
        m_menu = menu;
        changed();
    }

    SwMenu* menu() const { return m_menu; }
    bool hasMenu() const { return m_menu != nullptr; }

    void trigger() {
        if (!getEnabled()) {
            return;
        }
        if (getCheckable()) {
            setChecked(!getChecked());
        }
        triggered(getChecked());
    }

signals:
    DECLARE_SIGNAL(triggered, bool);
    DECLARE_SIGNAL_VOID(changed);

private:
    // Icons are stored by value; submenus are owned by the SwObject tree.
    SwImage m_icon;
    SwMenu* m_menu{nullptr};
};
