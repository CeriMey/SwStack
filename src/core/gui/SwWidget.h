#pragma once
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

#include "SwWidgetInterface.h"
#include "SwLayout.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <limits>
#include <exception>
#include "SwFont.h"
#include "SwStyle.h"
#include "StyleSheet.h"
#include "SwString.h"
#include "Sw.h"
#include "SwPainter.h"
#include "SwGuiApplication.h"
#include "SwWidgetPlatformAdapter.h"

class SwStyle;



// Enum pour les types d'événements
enum class EventType {
    Paint,
    Resize,
    Move,
    KeyPressEvent,
    MousePressEvent,
    MouseDoubleClickEvent,
    MouseMoveEvent,
    MouseReleaseEvent,
    WheelEvent,
    Show,
    Hide
};

// Classe de base pour un événement
class Event {
public:
    Event(EventType type) : eventType(type), accepted(false) {}

    EventType type() const { return eventType; }

    // Marquer l'événement comme accepté
    void accept() { accepted = true; }

    // Marquer l'événement comme refusé
    void ignore() { accepted = false; }

    // Vérifier si l'événement a été accepté
    bool isAccepted() const { return accepted; }

private:
    EventType eventType;
    bool accepted;  // Nouveau membre pour suivre si l'événement est accepté ou non
};


// Classe pour l'événement de redimensionnement
class ResizeEvent : public Event {
public:
    ResizeEvent(int w, int h) : Event(EventType::Resize), newWidth(w), newHeight(h) {}

    int width() const { return newWidth; }
    int height() const { return newHeight; }

private:
    int newWidth, newHeight;
};

// Classe pour l'événement de dessin
class PaintEvent : public Event {
public:
    PaintEvent(SwPainter* painter, const SwRect& paintRect)
        : Event(EventType::Paint), m_painter(painter), m_rect(paintRect) {}

    SwPainter* painter() const { return m_painter; }
    const SwRect& paintRect() const { return m_rect; }

private:
    SwPainter* m_painter;
    SwRect m_rect;
};

// Classe pour un clic de souris
class MouseEvent : public Event {
public:
    MouseEvent(EventType type,
               int xPos,
               int yPos,
               SwMouseButton button = SwMouseButton::NoButton,
               bool ctrl = false,
               bool shift = false,
               bool alt = false)
        : Event(type)
        , xPosition(xPos)
        , yPosition(yPos)
        , m_button(button)
        , ctrlPressed(ctrl)
        , shiftPressed(shift)
        , altPressed(alt)
        , deltaX(0)
        , deltaY(0)
        , speedX(0)
        , speedY(0) {}

    int x() const { return xPosition; }
    int y() const { return yPosition; }
    void setX(int xPos) { xPosition = xPos; }
    void setY(int yPos) { yPosition = yPos; }
    SwMouseButton button() const { return m_button; }
    void setButton(SwMouseButton button) { m_button = button; }
    int getDeltaX() const { return deltaX; }
    void setDeltaX(int dx) { deltaX = dx; }
    int getDeltaY() const { return deltaY; }
    void setDeltaY(int dy) { deltaY = dy; }
    double getSpeedX() const { return speedX; }
    void setSpeedX(double sx) { speedX = sx; }
    double getSpeedY() const { return speedY; }
    void setSpeedY(double sy) { speedY = sy; }

    bool isCtrlPressed() const { return ctrlPressed; }
    bool isShiftPressed() const { return shiftPressed; }
    bool isAltPressed() const { return altPressed; }

    void setCtrlPressed(bool on) { ctrlPressed = on; }
    void setShiftPressed(bool on) { shiftPressed = on; }
    void setAltPressed(bool on) { altPressed = on; }

private:
    int xPosition, yPosition;
    SwMouseButton m_button{SwMouseButton::NoButton};
    bool ctrlPressed{false};
    bool shiftPressed{false};
    bool altPressed{false};
    int deltaX, deltaY;
    double speedX, speedY;
};

// Classe pour un événement de clavier
class WheelEvent : public Event {
public:
    WheelEvent(int xPos,
               int yPos,
               int delta,
               bool ctrl = false,
               bool shift = false,
               bool alt = false)
        : Event(EventType::WheelEvent)
        , xPosition(xPos)
        , yPosition(yPos)
        , wheelDelta(delta)
        , ctrlPressed(ctrl)
        , shiftPressed(shift)
        , altPressed(alt) {}

    int x() const { return xPosition; }
    int y() const { return yPosition; }
    int delta() const { return wheelDelta; }

    bool isCtrlPressed() const { return ctrlPressed; }
    bool isShiftPressed() const { return shiftPressed; }
    bool isAltPressed() const { return altPressed; }

private:
    int xPosition{0};
    int yPosition{0};
    int wheelDelta{0};
    bool ctrlPressed{false};
    bool shiftPressed{false};
    bool altPressed{false};
};

class KeyEvent : public Event {
public:
    KeyEvent(int keyCode, bool ctrl = false, bool shift = false, bool alt = false)
        : Event(EventType::KeyPressEvent), keyPressed(keyCode), ctrlPressed(ctrl), shiftPressed(shift), altPressed(alt) {}

    int key() const { return keyPressed; }
    bool isCtrlPressed() const { return ctrlPressed; }
    bool isShiftPressed() const { return shiftPressed; }
    bool isAltPressed() const { return altPressed; }

private:
    int keyPressed;
    bool ctrlPressed;
    bool shiftPressed;
    bool altPressed;
};



// Classe SwWidget héritant de SwObject, simulant QWidget
class SwWidget : public SwWidgetInterface {

    /**
     * @brief Macro to declare the inheritance hierarchy for the SwWidget class.
     *
     * Specifies that `SwWidget` is derived from `SwObject` class.
     */
    SW_OBJECT(SwWidget, SwObject)

    /**
     * @brief Property for the SwWidget's focus policy.
     *
     * Determines how the SwWidget handles focus events.
     *
     * @param FocusPolicy The focus policy as a `FocusPolicyEnum` value. Default is `FocusPolicyEnum::Accept`.
     */
    PROPERTY(FocusPolicyEnum, FocusPolicy, FocusPolicyEnum::Accept)

    /**
     * @brief Property for the SwWidget's tooltip text.
     *
     * Sets or retrieves the tooltip text displayed when the user hovers over the SwWidget.
     *
     * @param ToolTips The tooltip text as an `SwString`.
     */
    PROPERTY(SwString, ToolTips, "")

    /**
     * @brief Custom property for the SwWidget's enabled state.
     *
     * Sets the enabled state of the SwWidget and triggers an update to apply any visual changes.
     *
     * @param Enable The new enabled state (`true` if the SwWidget is enabled, `false` otherwise).
     */
    CUSTOM_PROPERTY(bool, Enable, true) {
        update();
    }

    /**
     * @brief Custom property for the SwWidget's focus state.
     *
     * Sets the focus state of the SwWidget and triggers an update to apply any visual changes.
     *
     * @param Focus The new focus state (`true` if the SwWidget is focused, `false` otherwise).
     */
    CUSTOM_PROPERTY(bool, Focus, false) {
        update();
    }

    /**
     * @brief Custom property for the SwWidget's hover state.
     *
     * Sets the hover state of the SwWidget and triggers an update to apply any visual changes.
     *
     * @param Hover The new hover state (`true` if the SwWidget is hovered, `false` otherwise).
     */
    CUSTOM_PROPERTY(bool, Hover, false) {
        update();
    }

    /**
     * @brief Custom property for the SwWidget's visibility.
     *
     * Sets the visibility state of the SwWidget and invalidates its rectangular area to trigger a redraw.
     *
     * @param Visible The new visibility state (`true` for visible, `false` for hidden).
     */
    CUSTOM_PROPERTY(bool, Visible, true) {
        invalidateRect();
    }

    /**
     * @brief Custom property for the SwWidget's cursor type.
     *
     * Sets the cursor type for the SwWidget and loads the corresponding Windows cursor resource.
     * If the cursor type is not recognized, no default cursor is assigned.
     *
     * @param Cursor The new cursor type as a `CursorType` enumeration value.
     */
    CUSTOM_PROPERTY(CursorType, Cursor, CursorType::Default) {
        SwWidgetPlatformAdapter::setCursor(m_Cursor);
    }

    /**
     * @brief Custom override property for the SwWidget's font.
     *
     * Sets the SwWidget's font and triggers an update to apply the changes.
     *
     * @param Font The new font as an `SwFont` object.
     */
    CUSTOM_OVERRIDE_PROPERTY(SwFont, Font, SwFont()) {
        update();
    }

    /**
     * @brief Custom property for the SwWidget's style sheet.
     *
     * Sets the SwWidget's style sheet and triggers its parsing.
     * After parsing, the SwWidget is updated to apply the new style.
     *
     * @param StyleSheet The new style sheet as an `SwString`.
     */
    CUSTOM_PROPERTY(SwString, StyleSheet, "") {
        m_ComplexSheet.styles.clear();
        if (m_StyleSheet.isEmpty()) {
            update();
            return;
        }
        SwString processed = m_StyleSheet;
        if (!processed.contains("{")) {
            processed = SwString("%1 { %2 }").arg(className()).arg(processed);
        }
        m_ComplexSheet.parseStyleSheet(processed.toStdString());
        update();
    }

public:

    /**
     * @brief Constructor for the SwWidget class.
     *
     * Initializes a SwWidget with default dimensions, registered properties, and a default style.
     * If a parent is specified, the SwWidget will be linked to it, and a new parent event will be triggered.
     *
     * @param parent Pointer to the parent SwWidget. If nullptr, the SwWidget is standalone.
     */
    SwWidget(SwWidget* parent = nullptr)
        : SwWidgetInterface(parent),
          m_layout(nullptr),
          x(0),
          y(0),
          m_minWidth(0),
          m_minHeight(0),
          m_maxWidth(std::numeric_limits<int>::max()),
          m_maxHeight(std::numeric_limits<int>::max()),
          m_style(new SwStyle()),
          m_nativeWindowHandle{} {
        m_width = 100;
        m_height = 100;

        setCursor(CursorType::Arrow);
        if (SwObject::parent()) {
            this->newParentEvent(SwObject::parent());
        }
    }

    /**
     * @brief Destructor for the SwWidget class.
     *
     * Cleans up the SwWidget by deleting all its child SwWidgets to ensure proper memory management.
     */
    virtual ~SwWidget() {
        m_layout = nullptr;
    }

    /**
     * @brief Adds a child SwWidget to the current SwWidget.
     *
     * Links the specified SwWidget as a child of this SwWidget using the base `SwObject` implementation.
     *
     * @param child Pointer to the child SwWidget to be added.
     */
    virtual void addChild(SwObject* child) override {
        SwObject::addChild(child);
    }

    virtual void removeChild(SwObject* child) override {
        SwObject::removeChild(child);
    }

    void setLayout(SwAbstractLayout* layout) {
        if (m_layout == layout) {
            if (m_layout) {
                m_layout->updateGeometry();
            }
            return;
        }
        if (m_layout) {
            m_layout->setParentWidget(nullptr);
            m_layout->setParent(nullptr);
            delete m_layout;
        }
        m_layout = layout;
        if (m_layout) {
            m_layout->setParentWidget(this);
            m_layout->setParent(this);
            m_layout->updateGeometry();
        }
    }

    SwAbstractLayout* layout() const {
        return m_layout;
    }

    /**
     * @brief Displays the SwWidget by setting its visibility to true.
     *
     * Overrides the base implementation to make the SwWidget visible.
     */
    virtual void show() override {
        setVisible(true);
    }

    /**
     * @brief Hides the SwWidget by setting its visibility to false.
     *
     * Overrides the base implementation to make the SwWidget invisible.
     * Outputs a message to the console indicating that the SwWidget has been hidden.
     */
    virtual void hide() override {
        setVisible(false);
    }

    /**
     * @brief Redraws the SwWidget and propagates the update event to its children.
     *
     * If the SwWidget is visible, it invalidates its rectangle to trigger a redraw and
     * recursively calls the `update` method on all child SwWidgets.
     */
    virtual void update() override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        invalidateRect();
        // Wake the GUI message loop so invalidations are processed promptly.
        if (auto* guiApp = SwGuiApplication::instance(false)) {
            if (auto* integration = guiApp->platformIntegration()) {
                integration->wakeUpGuiThread();
            }
        }

        // Propagation de l'événement de dessin aux enfants
        for (SwObject* objChild : getChildren()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child) {
                continue;
            }
            child->update();
        }
    }

    /**
     * @brief Moves the SwWidget to a new position and propagates the move event to its children.
     *
     * Updates the SwWidget's position, emits the `moved` signal, and triggers a redraw if the SwWidget is visible.
     * The movement is also propagated to all child SwWidgets relative to the new position.
     *
     * @param newX The new X-coordinate of the SwWidget.
     * @param newY The new Y-coordinate of the SwWidget.
     */
    virtual void move(int newX, int newY) override {
        if (x == newX && y == newY) {
            return;
        }
        const SwRect oldRect = getRect();
        auto inflate = [](SwRect rect, int margin) {
            rect.x -= margin;
            rect.y -= margin;
            rect.width = std::max(0, rect.width + margin * 2);
            rect.height = std::max(0, rect.height + margin * 2);
            return rect;
        };
        int deltaX = newX - x;
        int deltaY = newY - y;
        x = newX;
        y = newY;
        emit moved(x, y);
        if (isVisibleInHierarchy()) {
            // Invalidate both the old and new geometry so the previous pixels are cleared.
            constexpr int kInvalidateMargin = 8;
            SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflate(oldRect, kInvalidateMargin));
            SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflate(getRect(), kInvalidateMargin));
            // Wake the GUI message loop so invalidations are processed promptly.
            if (auto* guiApp = SwGuiApplication::instance(false)) {
                if (auto* integration = guiApp->platformIntegration()) {
                    integration->wakeUpGuiThread();
                }
            }
        }
        for (SwObject* objChild : getChildren()) {
            if (auto* childWidget = dynamic_cast<SwWidget*>(objChild)) {
                childWidget->move(childWidget->x + deltaX, childWidget->y + deltaY);
            }
        }
    }

    /**
     * @brief Resizes the SwWidget and triggers the resize event.
     *
     * Updates the SwWidget's width and height, emits the `resized` signal,
     * and calls the `resizeEvent` to handle the resize logic.
     *
     * @param newWidth The new width of the SwWidget.
     * @param newHeight The new height of the SwWidget.
     */
    virtual void resize(int newWidth, int newHeight) override {
        const SwRect oldRect = getRect();
        newWidth = std::max(m_minWidth, std::min(m_maxWidth, newWidth));
        newHeight = std::max(m_minHeight, std::min(m_maxHeight, newHeight));
        if (m_width == newWidth && m_height == newHeight) {
            return;
        }
        m_width = newWidth;
        m_height = newHeight;
        if (isVisibleInHierarchy()) {
            // When shrinking, the old area must be repainted too.
            constexpr int kInvalidateMargin = 8;
            auto inflate = [](SwRect rect, int margin) {
                rect.x -= margin;
                rect.y -= margin;
                rect.width = std::max(0, rect.width + margin * 2);
                rect.height = std::max(0, rect.height + margin * 2);
                return rect;
            };
            SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflate(oldRect, kInvalidateMargin));
            SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflate(getRect(), kInvalidateMargin));
        }
        ResizeEvent event(m_width, m_height);
        resizeEvent(&event);
        emit resized(m_width, m_height);
    }

    /**
     * @brief Returns the current width of the widget.
     */
    int width() const {
        return m_width;
    }

    /**
     * @brief Returns the current height of the widget.
     */
    int height() const {
        return m_height;
    }

    void setMinimumSize(int minW, int minH) {
        m_minWidth = std::max(0, minW);
        m_minHeight = std::max(0, minH);
        if (m_minWidth > m_maxWidth) {
            m_maxWidth = m_minWidth;
        }
        if (m_minHeight > m_maxHeight) {
            m_maxHeight = m_minHeight;
        }
        resize(std::max(m_width, m_minWidth), std::max(m_height, m_minHeight));
    }

    void setMaximumSize(int maxW, int maxH) {
        m_maxWidth = std::max(0, maxW);
        m_maxHeight = std::max(0, maxH);
        if (m_maxWidth < m_minWidth) {
            m_minWidth = m_maxWidth;
        }
        if (m_maxHeight < m_minHeight) {
            m_minHeight = m_maxHeight;
        }
        resize(std::min(m_width, m_maxWidth), std::min(m_height, m_maxHeight));
    }

    /**
     * @brief Retrieves the current rectangle representing the SwWidget's position and size.
     *
     * Computes and returns a `RECT` structure with the SwWidget's coordinates and dimensions.
     *
     * @return A `RECT` structure containing the SwWidget's position (left, top) and size (right, bottom).
     */
    SwRect getRect() const override {
        SwRect rectVal;
        rectVal.x = x;
        rectVal.y = y;
        rectVal.width = m_width;
        rectVal.height = m_height;
        return rectVal;
    }

    SwWidgetPlatformHandle platformHandle() const {
        return m_nativeWindowHandle;
    }

    bool isVisibleInHierarchy() const {
        if (!getVisible()) {
            return false;
        }

        const SwObject* p = parent();
        while (p) {
            const SwWidget* widgetParent = dynamic_cast<const SwWidget*>(p);
            if (widgetParent && !widgetParent->getVisible()) {
                return false;
            }
            p = p->parent();
        }
        return true;
    }

    virtual SwRect sizeHint() const {
        return SwRect{x, y, m_width, m_height};
    }

    virtual SwRect minimumSizeHint() const {
        return SwRect{x, y, m_minWidth, m_minHeight};
    }

    /**
     * @brief Retrieves the deepest child SwWidget located under the given cursor position.
     *
     * Iterates through the SwWidget's children to determine which one contains the specified coordinates.
     * If multiple nested children match, the method returns the deepest child.
     *
     * @param x The X-coordinate of the cursor.
     * @param y The Y-coordinate of the cursor.
     * @return A pointer to the deepest child SwWidget under the cursor, or nullptr if none is found.
     */
    SwWidget* getChildUnderCursor(int x, int y, SwWidget* ignore = nullptr) {
        SwWidget* deepestWidget = nullptr;

        // Parcourir tous les enfants (ordre d'empilement: dernier ajoutÃ© = au-dessus).
        for (int i = static_cast<int>(getChildren().size()) - 1; i >= 0; --i) {
            SwWidget* child = dynamic_cast<SwWidget*>(getChildren()[static_cast<size_t>(i)]);
            if (!child) {
                continue;
            }
            if (child == ignore) {
                continue;
            }
            // Vérifier si le pointeur est à l'intérieur de l'enfant
            if (!child->isVisibleInHierarchy()) {
                continue;
            }
            if (child->isPointInside(x, y)) {
                // Appeler récursivement getChildUnderCursor pour trouver l'enfant le plus profond
                SwWidget* deepChild = child->getChildUnderCursor(x, y, ignore);
                if (deepChild) {
                    deepestWidget = deepChild;  // Si on trouve un enfant plus profond, on le garde
                }
                else {
                    deepestWidget = child;  // Sinon, on garde cet enfant
                }
                break;
            }
        }

        return deepestWidget;
    }

    /**
     * @brief Retrieves the stylesheet associated with the SwWidget.
     *
     * Returns a pointer to the internal `StyleSheet` object used by the SwWidget.
     *
     * @return A pointer to the `StyleSheet` object.
     */
    StyleSheet* getToolSheet() override {
        return &m_ComplexSheet;
    }

signals:
    /**
     * @brief Signal emitted when the SwWidget is resized.
     *
     * Parameters:
     * - `int newWidth`: The new width of the SwWidget.
     * - `int newHeight`: The new height of the SwWidget.
     */
    DECLARE_SIGNAL(resized, int, int);

    /**
     * @brief Signal emitted when the SwWidget is moved.
     *
     * Parameters:
     * - `int newX`: The new X-coordinate of the SwWidget.
     * - `int newY`: The new Y-coordinate of the SwWidget.
     */
    DECLARE_SIGNAL(moved, int, int);

    /**
     * @brief Signal emitted when the visibility of the SwWidget changes.
     *
     * Parameters:
     * - `bool isVisible`: The new visibility state of the SwWidget.
     */
    DECLARE_SIGNAL_VOID(visibilityChanged);


protected:

    /**
     * @brief Handles the event when the SwWidget gets a new parent.
     *
     * Updates the SwWidget's platform window handle and visibility state based on the new parent.
     * Calls the base `SwObject::newParentEvent` to ensure proper propagation.
     *
     * @param parent Pointer to the new parent object.
     */
    virtual void newParentEvent(SwObject* parent) override {
        SwWidget *ui = dynamic_cast<SwWidget*>(parent);
        if (ui) {
            setNativeWindowHandle(ui->nativeWindowHandle());
        }
        SwObject::newParentEvent(parent);
    }

    /**
     * @brief Handles the paint event for the SwWidget.
     *
     * Draws the SwWidget's background using a specified color and propagates the paint event
     * to visible child SwWidgets whose rectangles intersect the paint area.
     *
     * @param event Pointer to the `PaintEvent` containing the context and paint area.
     */
    virtual void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }

        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        SwRect rect = getRect();
        SwColor bgColor = {100, 149, 237};
        bool paintBackground = true;
        std::vector<SwString> selectors = classHierarchy();
        bool hasSwWidgetSelector = false;
        for (const SwString& selector : selectors) {
            if (selector == "SwWidget") {
                hasSwWidgetSelector = true;
                break;
            }
        }
        if (!hasSwWidgetSelector) {
            selectors.emplace_back("SwWidget");
        }
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            SwString value = m_ComplexSheet.getStyleProperty(selector.toStdString(), "background-color");
            if (!value.isEmpty()) {
                float alpha = 1.0f;
                try {
                    SwColor resolved = m_ComplexSheet.parseColor(value.toStdString(), &alpha);
                    if (alpha <= 0.0f) {
                        paintBackground = false;
                    } else {
                        bgColor = resolved;
                        paintBackground = true;
                    }
                } catch (const std::exception&) {
                    // Ignore invalid colors and keep default background
                }
                break;
            }
        }

        if (paintBackground) {
            this->m_style->drawBackground(rect, painter, bgColor);
        }

        const SwRect& paintRect = event->paintRect();
        for (SwObject* objChild : getChildren()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child) {
                continue;
            }
            SwRect childRect = child->getRect();

            // Vérifier si l'enfant est visible et si son rectangle intersecte la zone de peinture
            if (child->isVisibleInHierarchy() && rectsIntersect(paintRect, childRect)) {
                child->paintEvent(event);
            }
        }
    }

    /**
     * @brief Utility function to check if two Windows RECT structures intersect.
     *
     * Determines whether the rectangles `r1` and `r2` overlap by evaluating their boundaries.
     *
     * @param r1 The first RECT structure.
     * @param r2 The second RECT structure.
     * @return `true` if the rectangles intersect, `false` otherwise.
     */
    bool rectsIntersect(const SwRect& r1, const SwRect& r2) {
        return !((r1.x + r1.width) < r2.x ||
                 r1.x > (r2.x + r2.width) ||
                 (r1.y + r1.height) < r2.y ||
                 r1.y > (r2.y + r2.height));
    }

    /**
     * @brief Handles the key press event for the SwWidget.
     *
     * Processes the key press event and propagates it to the child SwWidgets.
     * If the event is marked as accepted by any child, further propagation stops.
     *
     * @param event Pointer to the `KeyEvent` containing information about the key press.
     */
    virtual void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        const std::vector<SwObject*>& directChildren = getChildren();
        size_t i = 0;
        while (i < directChildren.size()) {
            if (event->isAccepted()) {
                return;
            }

            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child) {
                child->keyPressEvent(event);
            }

            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }
    }

    /**
     * @brief Handles the resize event for the SwWidget.
     *
     * Triggers an update to redraw the SwWidget after a resize.
     * Optionally, a mechanism could be added to notify child SwWidgets of the parent's size change.
     *
     * @param event Pointer to the `ResizeEvent` containing the new dimensions of the SwWidget.
     */
    virtual void resizeEvent(ResizeEvent* event) {
        this->update();
        if (m_layout) {
            m_layout->updateGeometry();
        }
    }

    /**
     * @brief Handles the mouse press event for the SwWidget.
     *
     * Determines the deepest child SwWidget under the cursor and propagates the event to it.
     * If the target SwWidget has a focus policy, it is given focus, and other child SwWidgets lose focus.
     * If the event is marked as accepted by any child, further propagation stops.
     *
     * @param event Pointer to the `MouseEvent` containing the mouse press details.
     */
    virtual void mousePressEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        // Trouver l'enfant le plus profond sous le pointeur de la souris
        SwWidget* targetWidget = getChildUnderCursor(event->x(), event->y());
        SwWidget* targetTopChild = nullptr;
        if (targetWidget) {
            targetTopChild = targetWidget;
            while (targetTopChild && targetTopChild->parent() && targetTopChild->parent() != this) {
                targetTopChild = dynamic_cast<SwWidget*>(targetTopChild->parent());
            }
            if (targetTopChild && targetTopChild->parent() != this) {
                targetTopChild = nullptr;
            }
        }

        if (targetWidget) {
            if (targetWidget->getFocusPolicy() != FocusPolicyEnum::NoFocus) {
                auto clearFocusRecursively = [&](auto&& self, SwWidget* widget) -> void {
                    if (!widget) {
                        return;
                    }
                    const std::vector<SwObject*>& directChildren = widget->getChildren();
                    size_t i = 0;
                    while (i < directChildren.size()) {
                        SwObject* obj = directChildren[i];
                        SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
                        if (child && child->isVisibleInHierarchy()) {
                            if (child != targetWidget) {
                                child->setFocus(false);
                            }
                            self(self, child);
                        }
                        if (i < directChildren.size() && directChildren[i] == obj) {
                            ++i;
                        }
                    }
                };
                clearFocusRecursively(clearFocusRecursively, this);
                targetWidget->setFocus(true);
            }
            targetWidget->mousePressEvent(event);
        }

        const std::vector<SwObject*>& directChildren = getChildren();
        size_t i = 0;
        while (i < directChildren.size()) {
            if (event->isAccepted()) {
                return;
            }
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child && child != targetTopChild && child->isVisibleInHierarchy()) {
                if (child->isPointInside(event->x(), event->y())) {
                    child->mousePressEvent(event);
                }
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }

        event->accept();
    }

    /**
     * @brief Handles the mouse release event for the SwWidget.
     *
     * Propagates the mouse release event to all child SwWidgets.
     * Note: propagation does not stop on acceptance, so widgets can reliably reset pressed/drag states.
     *
     * @param event Pointer to the `MouseEvent` containing the mouse release details.
     */
    virtual void mouseReleaseEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        const std::vector<SwObject*>& directChildren = getChildren();
        size_t i = 0;
        while (i < directChildren.size()) {
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child && child->isVisibleInHierarchy()) {
                child->mouseReleaseEvent(event);
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }
    }

    /**
     * @brief Handles the mouse double-click event for the SwWidget.
     *
     * Propagates the mouse double-click event to all child SwWidgets that contain the cursor position.
     * If the event is marked as accepted by any child, further propagation stops.
     *
     * @param event Pointer to the `MouseEvent` containing the double-click details.
     */
    virtual void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        const std::vector<SwObject*>& directChildren = getChildren();
        size_t i = 0;
        while (i < directChildren.size()) {
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child && child->isVisibleInHierarchy()) {
                if (event->isAccepted()) {
                    return;
                }
                if (child->isPointInside(event->x(), event->y())) {
                    child->mouseDoubleClickEvent(event);
                }
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }
    }

    /**
     * @brief Handles the mouse move event for the SwWidget.
     *
     * Propagates the mouse move event to all child SwWidgets.
     * Updates the hover state of the current SwWidget based on the cursor position.
     * If the event is not accepted and the SwWidget is hovered, it sets the cursor and marks the event as accepted.
     *
     * @param event Pointer to the `MouseEvent` containing the mouse move details.
     */
    virtual void mouseMoveEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }

        
        //c'est l'enfant qui decide donc c'est lui qui parlera le derneir
        const std::vector<SwObject*>& directChildren = getChildren();
        size_t i = 0;
        while (i < directChildren.size()) {
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child && child->isVisibleInHierarchy()) {
                child->mouseMoveEvent(event);
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }

        this->setHover(isPointInside(event->x(), event->y()));

        if (!event->isAccepted() && this->getHover()) {
            SwWidgetPlatformAdapter::setCursor(getCursor());
            event->accept();
        }
    }

protected:

    /**
     * @brief Checks if a given point is inside the SwWidget's boundaries.
     *
     * Determines whether the point specified by its X and Y coordinates lies within the SwWidget's rectangle.
     *
     * @param px The X-coordinate of the point.
     * @param py The Y-coordinate of the point.
     * @return `true` if the point is inside the SwWidget, `false` otherwise.
     */
    bool isPointInside(int px, int py) const {
        return (px >= x && px <= x + m_width && py >= y && py <= y + m_height);
    }

    /**
     * @brief Invalidates the SwWidget's rectangular area, marking it for redrawing.
     *
     * Computes the SwWidget's rectangle based on its position and size, then requests a redraw
     * through the active platform adapter.
     */
    virtual void invalidateRect() {
        SwRect rect;
        rect.x = x;
        rect.y = y;
        rect.width = m_width;
        rect.height = m_height;
        SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, rect);
    }


    int m_width;
    int m_height;
    int m_minWidth;
    int m_minHeight;
    int m_maxWidth;
    int m_maxHeight;
    int x, y;
    SwStyle *m_style;
    SwWidgetPlatformHandle m_nativeWindowHandle;

private:
    StyleSheet m_ComplexSheet;
    SwAbstractLayout* m_layout;

protected:
    void setNativeWindowHandle(const SwWidgetPlatformHandle& handle) {
        m_nativeWindowHandle = handle;
    }

    void setNativeWindowHandleRecursive(const SwWidgetPlatformHandle& handle) {
        setNativeWindowHandle(handle);
        const std::vector<SwObject*>& directChildren = getChildren();
        size_t i = 0;
        while (i < directChildren.size()) {
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child) {
                child->setNativeWindowHandleRecursive(handle);
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }
    }

    const SwWidgetPlatformHandle& nativeWindowHandle() const {
        return m_nativeWindowHandle;
    }
};
