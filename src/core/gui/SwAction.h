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
 * @file src/core/gui/SwAction.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwAction in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the action interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwAction.
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
 * SwAction - action object.
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
    /**
     * @brief Constructs a `SwAction` instance.
     * @param text Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwAction(const SwString& text = SwString(), SwObject* parent = nullptr)
        : SwObject(parent) {
        setText(text);
    }

    /**
     * @brief Sets the icon.
     * @param icon Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIcon(const SwImage& icon) {
        m_icon = icon;
        changed();
    }

    /**
     * @brief Returns the current icon.
     * @return The current icon.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwImage& icon() const { return m_icon; }
    /**
     * @brief Returns whether the object reports icon.
     * @return `true` when the object reports icon; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool hasIcon() const { return !m_icon.isNull(); }

    /**
     * @brief Sets the menu.
     * @param menu Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMenu(SwMenu* menu) {
        if (m_menu == menu) {
            return;
        }
        m_menu = menu;
        changed();
    }

    /**
     * @brief Returns the current menu.
     * @return The current menu.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMenu* menu() const { return m_menu; }
    /**
     * @brief Returns whether the object reports menu.
     * @return `true` when the object reports menu; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasMenu() const { return m_menu != nullptr; }

    /**
     * @brief Performs the `trigger` operation.
     */
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
