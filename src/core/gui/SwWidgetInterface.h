#pragma once

/**
 * @file src/core/gui/SwWidgetInterface.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwWidgetInterface in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the widget interface interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwWidgetInterface.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */

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

#include "SwObject.h"
#include "SwFont.h"
#include "StyleSheet.h"

#include "Sw.h"

class MouseEvent;
class KeyEvent;
class WheelEvent;
class PaintEvent;

class SwWidgetInterface : public SwObject {

    VIRTUAL_PROPERTY(SwFont, Font)
public:
    // Constructeur et destructeur
    /**
     * @brief Constructs a `SwWidgetInterface` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwWidgetInterface(SwObject* parent = nullptr) : SwObject(parent) {}
    /**
     * @brief Destroys the `SwWidgetInterface` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwWidgetInterface() = default;

    // MÃ©thodes purement virtuelles pour dÃ©finir les fonctionnalitÃ©s d'un widget
    /**
     * @brief Returns the current show.
     * @return The current show.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void show() = 0;
    /**
     * @brief Returns the current hide.
     * @return The current hide.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void hide() = 0;
    /**
     * @brief Returns the current update.
     * @return The current update.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void update() = 0;
    /**
     * @brief Performs the `move` operation.
     * @param newX Value passed to the method.
     * @param newY Value passed to the method.
     * @return The requested move.
     */
    virtual void move(int newX, int newY) = 0;
    /**
     * @brief Performs the `resize` operation.
     * @param newWidth Value passed to the method.
     * @param newHeight Value passed to the method.
     * @return The requested resize.
     */
    virtual void resize(int newWidth, int newHeight) = 0;
    virtual void setGeometry(int newX, int newY, int newWidth, int newHeight) {
        move(newX, newY);
        resize(newWidth, newHeight);
    }
    virtual void setGeometry(const SwRect& rect) {
        setGeometry(rect.x, rect.y, rect.width, rect.height);
    }

    // MÃ©thodes pour gÃ©rer les Ã©vÃ©nements
    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested paint Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void paintEvent(PaintEvent* event) = 0;
    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mousePressEvent(MouseEvent* event) = 0;
    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseReleaseEvent(MouseEvent* event) = 0;
    /**
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Double Click Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseDoubleClickEvent(MouseEvent* event) = 0;
    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested mouse Move Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void mouseMoveEvent(MouseEvent* event) = 0;
    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested key Press Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void keyPressEvent(KeyEvent* event) = 0;
    /**
     * @brief Handles the key Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested key Release Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void keyReleaseEvent(KeyEvent* event) { SW_UNUSED(event); }
    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     * @return The requested wheel Event.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    virtual void wheelEvent(WheelEvent* event) { SW_UNUSED(event); }

    // MÃ©thodes pour obtenir ou dÃ©finir des propriÃ©tÃ©s gÃ©nÃ©rales
    /**
     * @brief Returns the current tool Sheet.
     * @return The current tool Sheet.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual StyleSheet* getToolSheet() = 0;
    virtual bool resolveStyledBackground(unsigned int stateFlags, SwColor& color, float& alpha, bool& paint) const {
        SW_UNUSED(stateFlags);
        SW_UNUSED(color);
        SW_UNUSED(alpha);
        SW_UNUSED(paint);
        return false;
    }
    virtual bool resolveStyledBorder(unsigned int stateFlags, SwColor& color, int& width, int& radius) const {
        SW_UNUSED(stateFlags);
        SW_UNUSED(color);
        SW_UNUSED(width);
        SW_UNUSED(radius);
        return false;
    }
    virtual void resolveStyledBorderCorners(unsigned int stateFlags,
                                            int& topLeft,
                                            int& topRight,
                                            int& bottomRight,
                                            int& bottomLeft) const {
        SW_UNUSED(stateFlags);
        SW_UNUSED(topLeft);
        SW_UNUSED(topRight);
        SW_UNUSED(bottomRight);
        SW_UNUSED(bottomLeft);
    }
    virtual SwColor resolveStyledTextColor(unsigned int stateFlags, const SwColor& fallback) const {
        SW_UNUSED(stateFlags);
        return fallback;
    }
    virtual StyleSheet::BoxEdges resolveStyledPadding(unsigned int stateFlags) const {
        SW_UNUSED(stateFlags);
        return StyleSheet::BoxEdges{};
    }
    virtual SwFont resolveStyledFont(unsigned int stateFlags) const {
        SW_UNUSED(stateFlags);
        return getFont();
    }
    virtual SwString resolveStyledBoxShadow(unsigned int stateFlags) const {
        SW_UNUSED(stateFlags);
        return SwString();
    }
    virtual bool hasExplicitStyledStateProperty(unsigned int stateFlags, const SwString& property) const {
        SW_UNUSED(stateFlags);
        SW_UNUSED(property);
        return false;
    }
    /**
     * @brief Returns the widget frame geometry in window coordinates.
     * @return The current frame geometry.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwRect frameGeometry() const = 0;

    /**
     * @brief Returns the widget's local dimensions (client area, excluding frame borders).
     * @return SwSize with the usable width and height.
     */
    virtual SwSize clientSize() const {
        SwRect fg = frameGeometry();
        return SwSize{fg.width, fg.height};
    }

    /**
     * @brief Returns the widget-local rectangle available to layouts and child placement.
     * @return The origin and size of the usable client rectangle.
     */
    virtual SwRect clientRect() const {
        const SwSize client = clientSize();
        return SwRect{0,
                      0,
                      client.width < 0 ? 0 : client.width,
                      client.height < 0 ? 0 : client.height};
    }

    /**
     * @brief Returns the current size Hint.
     * @return The current size Hint.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwSize sizeHint() const {
        return SwSize{0, 0};
    }

    /**
     * @brief Returns the current minimum Size Hint.
     * @return The current minimum Size Hint.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwSize minimumSizeHint() const {
        return SwSize{0, 0};
    }

//    virtual bool isVisible() const = 0;
//    virtual int getWidth() const = 0;
//    virtual int getHeight() const = 0;
//    virtual int getX() const = 0;
//    virtual int getY() const = 0;
//    virtual void setWidth(int width) = 0;
//    virtual void setHeight(int height) = 0;
//    virtual void setX(int x) = 0;
//    virtual void setY(int y) = 0;
};
