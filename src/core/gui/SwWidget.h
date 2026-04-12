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
#include <vector>
#include <algorithm>
#include <memory>
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
#include "SwWidgetPerfTrace.h"
#include "core/types/SwMap.h"

class SwStyle;

class Event : public SwEvent {
public:
    explicit Event(EventType type)
        : SwEvent(type) {}

    Event* clone() const override { return new Event(*this); }
};


// Classe pour l'Ã©vÃ©nement de redimensionnement
class ResizeEvent : public Event {
public:
    ResizeEvent(int w, int h) : Event(EventType::Resize), newWidth(w), newHeight(h) {}

    int width() const { return newWidth; }
    int height() const { return newHeight; }

private:
    int newWidth, newHeight;
};

class MoveEvent : public Event {
public:
    MoveEvent(int xPos, int yPos)
        : Event(EventType::Move), newX(xPos), newY(yPos) {}

    int x() const { return newX; }
    int y() const { return newY; }
    SwPoint pos() const { return SwPoint{newX, newY}; }

private:
    int newX;
    int newY;
};

// Classe pour l'Ã©vÃ©nement de dessin
class CloseEvent : public Event {
public:
    CloseEvent() : Event(EventType::Close) { accept(); }
    CloseEvent* clone() const override { return new CloseEvent(*this); }
};

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
        , globalXPosition(xPos)
        , globalYPosition(yPos)
        , deltaX(0)
        , deltaY(0)
        , speedX(0)
        , speedY(0) {}

    int x() const { return xPosition; }
    int y() const { return yPosition; }
    SwPoint pos() const { return SwPoint{xPosition, yPosition}; }
    void setX(int xPos) { xPosition = xPos; }
    void setY(int yPos) { yPosition = yPos; }
    void setPos(const SwPoint& p) {
        xPosition = p.x;
        yPosition = p.y;
    }
    int globalX() const { return globalXPosition; }
    int globalY() const { return globalYPosition; }
    SwPoint globalPos() const { return SwPoint{globalXPosition, globalYPosition}; }
    void setGlobalX(int xPos) { globalXPosition = xPos; }
    void setGlobalY(int yPos) { globalYPosition = yPos; }
    void setGlobalPos(const SwPoint& p) {
        globalXPosition = p.x;
        globalYPosition = p.y;
    }
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
    int globalXPosition{0};
    int globalYPosition{0};
    int deltaX, deltaY;
    double speedX, speedY;
};

// Classe pour un Ã©vÃ©nement de clavier
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
        , globalXPosition(xPos)
        , globalYPosition(yPos)
        , wheelDelta(delta)
        , ctrlPressed(ctrl)
        , shiftPressed(shift)
        , altPressed(alt) {}

    int x() const { return xPosition; }
    int y() const { return yPosition; }
    SwPoint pos() const { return SwPoint{xPosition, yPosition}; }
    void setX(int xPos) { xPosition = xPos; }
    void setY(int yPos) { yPosition = yPos; }
    void setPos(const SwPoint& p) {
        xPosition = p.x;
        yPosition = p.y;
    }
    int globalX() const { return globalXPosition; }
    int globalY() const { return globalYPosition; }
    SwPoint globalPos() const { return SwPoint{globalXPosition, globalYPosition}; }
    void setGlobalX(int xPos) { globalXPosition = xPos; }
    void setGlobalY(int yPos) { globalYPosition = yPos; }
    void setGlobalPos(const SwPoint& p) {
        globalXPosition = p.x;
        globalYPosition = p.y;
    }
    int delta() const { return wheelDelta; }

    bool isCtrlPressed() const { return ctrlPressed; }
    bool isShiftPressed() const { return shiftPressed; }
    bool isAltPressed() const { return altPressed; }

private:
    int xPosition{0};
    int yPosition{0};
    int globalXPosition{0};
    int globalYPosition{0};
    int wheelDelta{0};
    bool ctrlPressed{false};
    bool shiftPressed{false};
    bool altPressed{false};
};

class KeyEvent : public Event {
public:
    KeyEvent(int keyCode, bool ctrl = false, bool shift = false, bool alt = false,
             wchar_t textChar = L'\0', bool textProvided = false,
             EventType type = EventType::KeyPressEvent)
        : Event(type)
        , keyPressed(keyCode), ctrlPressed(ctrl), shiftPressed(shift), altPressed(alt)
        , textChar(textChar), textProvidedFlag(textProvided) {}

    int key() const { return keyPressed; }
    bool isCtrlPressed() const { return ctrlPressed; }
    bool isShiftPressed() const { return shiftPressed; }
    bool isAltPressed() const { return altPressed; }

    // Unicode character translated by the platform for the active keyboard layout (AZERTY, etc.).
    // L'\0' for dead keys (first press), control keys, or keys without a character output.
    wchar_t text() const { return textChar; }

    // True when the platform performed the key-to-char translation (e.g. Win32 WM_CHAR path).
    // Widgets must NOT call translateCharacter() when this is true, even when text() == L'\0'.
    bool isTextProvided() const { return textProvidedFlag; }

private:
    int keyPressed;
    bool ctrlPressed;
    bool shiftPressed;
    bool altPressed;
    wchar_t textChar{L'\0'};
    bool textProvidedFlag{false};
};



// Classe SwWidget hÃ©ritant de SwObject, representant un widget de base
class SwWidget : public SwWidgetInterface {

    /**
     * @brief Macro to declare the inheritance hierarchy for the SwWidget class.
     *
     * Specifies that `SwWidget` is derived from `SwObject` class.
     */
    SW_OBJECT(SwWidget, SwObject)
    friend class SwStyle;

    /**
     * @brief Property for the SwWidget's focus policy.
     *
     * Determines how the SwWidget handles focus events.
     *
     * @param FocusPolicy The focus policy as a `FocusPolicyEnum` value. Default is `FocusPolicyEnum::Accept`.
     */
    CUSTOM_PROPERTY(FocusPolicyEnum, FocusPolicy, FocusPolicyEnum::Accept) {
        if (value == FocusPolicyEnum::NoFocus && getFocus()) {
            setFocus(false);
        }
        update();
    }

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
        if (!value && getFocus()) {
            setFocus(false);
        }
        update();
    }

    /**
     * @brief Custom property for the SwWidget's focus state.
     *
     * Sets the focus state of the SwWidget and triggers an update to apply any visual changes.
     *
     * @param Focus The new focus state (`true` if the SwWidget is focused, `false` otherwise).
     */
private:
    bool m_Focus{false};
public:
    void setFocus(const bool& value) {
        if (value) {
            requestFocusOwnership_();
            return;
        }
        releaseFocusOwnership_();
    }
    bool getFocus() const {
        return m_Focus;
    }
    template<typename T>
    static bool register_Focus_setter(T* instance) {
        instance->propertySetterMap["Focus"] = [instance](void* value) {
            instance->setFocus(*static_cast<bool*>(value));
        };
        instance->propertyGetterMap["Focus"] = [instance]() -> void* {
            return static_cast<void*>(&instance->m_Focus);
        };
        instance->propertyArgumentTypeNameMap["Focus"] = typeid(bool).name();
        instance->propertyOwnerClassMap["Focus"] = SwDemangleClassName(typeid(T).name());
        return true;
    }
    DECLARE_SIGNAL(FocusChanged, const bool&);
protected:
    bool __Focus__prop = register_Focus_setter<typename std::remove_reference<decltype(*this)>::type>(this);
    virtual void on_Focus_changed(const bool& value) {
        SW_UNUSED(value);
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
        invalidateVisibilityCache_();
        if (!value) {
            clearFocusInHierarchy_();
        }
        invalidateRect();
        if (!m_visibilityEventInFlight) {
            Event visibilityEvent(value ? EventType::Show : EventType::Hide);
            m_visibilityEventInFlight = true;
            SwCoreApplication::sendEvent(this, &visibilityEvent);
            m_visibilityEventInFlight = false;
        }
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
        invalidateResolvedStyleSnapshot_();
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
        m_ComplexSheet.clear();
        if (m_StyleSheet.isEmpty()) {
            updateStyleTree_();
            return;
        }
        SwString processed = m_StyleSheet;
        if (!processed.contains("{")) {
            processed = SwString("%1 { %2 }").arg(className()).arg(processed);
        }
        m_ComplexSheet.parseStyleSheet(processed);
        updateStyleTree_();
    }

public:
    void setDefaultStyleSheet(const SwString& styleSheet) {
        if (m_DefaultStyleSheet == styleSheet) {
            return;
        }

        m_DefaultStyleSheet = styleSheet;
        m_DefaultComplexSheet.clear();
        if (!m_DefaultStyleSheet.isEmpty()) {
            SwString processed = m_DefaultStyleSheet;
            if (!processed.contains("{")) {
                processed = SwString("%1 { %2 }").arg(className()).arg(processed);
            }
            m_DefaultComplexSheet.parseStyleSheet(processed);
        }
        updateStyleTree_();
    }

    SwString defaultStyleSheet() const {
        return m_DefaultStyleSheet;
    }

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
          m_absX(0),
          m_absY(0),
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
        removeFocusOwnerEntriesForWidget_(this);
        m_Focus = false;
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
        m_swWidgetChildrenDirty = true;
    }

    virtual void removeChild(SwObject* child) override {
        SwObject::removeChild(child);
        m_swWidgetChildrenDirty = true;
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
            adoptLayoutWidgets_(m_layout);
            m_layout->updateGeometry();
        }
    }

    SwAbstractLayout* layout() const {
        return m_layout;
    }

private:
    struct SavedLocalGeometry_ {
        SwWidget* widget{nullptr};
        SwRect geometry{0, 0, 0, 0};
    };

    static void collectDescendantLocalGeometry_(SwWidget* root, std::vector<SavedLocalGeometry_>& out) {
        if (!root) {
            return;
        }
        for (SwObject* objChild : root->children()) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (!child) {
                continue;
            }
            SavedLocalGeometry_ savedChild;
            savedChild.widget = child;
            savedChild.geometry = child->geometry();
            out.push_back(savedChild);
            collectDescendantLocalGeometry_(child, out);
        }
    }

    static void restoreSavedLocalGeometry_(const std::vector<SavedLocalGeometry_>& saved) {
        for (const SavedLocalGeometry_& entry : saved) {
            if (!entry.widget) {
                continue;
            }
            entry.widget->move(entry.geometry.x, entry.geometry.y);
            entry.widget->resize(entry.geometry.width, entry.geometry.height);
        }
    }

    void adoptLayoutWidgets_(const SwAbstractLayout* layout) {
        if (!layout) {
            return;
        }
        layout->forEachManagedWidget([this](SwWidgetInterface* itemWidget) {
            auto* widget = dynamic_cast<SwWidget*>(itemWidget);
            if (!widget) {
                return;
            }
            if (widget == this) {
                return;
            }
            if (widget->parent() == this) {
                return;
            }

            std::vector<SavedLocalGeometry_> savedDescendants;
            collectDescendantLocalGeometry_(widget, savedDescendants);
            widget->setParent(this);
            restoreSavedLocalGeometry_(savedDescendants);
        });
    }

public:

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
     * @brief Marks the widget's area as dirty so the next paint cycle redraws it.
     *
     * A single InvalidateRect on the native window is sufficient: the paint
     * handler already walks the full widget tree and repaints every child that
     * intersects the dirty region.  Recursing into children here was causing
     * O(N) InvalidateRect + PostMessage calls for a tree of N widgets, which
     * flooded the Win32 message queue on every keystroke or property change.
     *
     */
    virtual void update() override {
        SW_WIDGET_PERF_COUNT("update");
        if (!isVisibleInHierarchy()) {
            return;
        }
        if (m_SuppressImplicitResizeUpdate_) {
            SW_WIDGET_PERF_COUNT("update.suppressedDuringResize");
            return;
        }
        invalidateRect();
        // Wake the GUI message loop so invalidations are processed promptly.
        if (auto* guiApp = SwGuiApplication::instance(false)) {
            if (auto* integration = guiApp->platformIntegration()) {
                integration->wakeUpGuiThread();
            }
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
    // move() takes coordinates relative to the parent's top-left corner.
    // Internally converts to absolute window coordinates and propagates to children.
    virtual void move(int newX, int newY) override {
        setGeometry(newX, newY, m_width, m_height);
    }

    void move(const SwPoint& point) {
        move(point.x, point.y);
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
        const SwPoint localPos = currentLocalPos_();
        setGeometry(localPos.x, localPos.y, newWidth, newHeight);
    }

    void resize(const SwSize& sizeVal) {
        resize(sizeVal.width, sizeVal.height);
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

    int x() const {
        return pos().x;
    }

    int y() const {
        return pos().y;
    }

    SwSize size() const {
        return SwSize{m_width, m_height};
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

    SwSize minimumSize() const {
        return SwSize{m_minWidth, m_minHeight};
    }

    SwSize maximumSize() const {
        return SwSize{m_maxWidth, m_maxHeight};
    }

    void setGeometry(int newX, int newY, int newWidth, int newHeight) override {
        SW_WIDGET_PERF_SCOPE("setGeometry");
        SW_WIDGET_PERF_COUNT("setGeometry.calls");
        int absX = newX;
        int absY = newY;
        if (m_parentWidgetCached) {
            absX += m_parentWidgetCached->m_absX;
            absY += m_parentWidgetCached->m_absY;
        }

        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        int effectiveMinWidth = std::max(m_minWidth, styleMin.width);
        int effectiveMinHeight = std::max(m_minHeight, styleMin.height);
        if (m_layout) {
            const SwSize layoutMinimumHint = minimumSizeHint();
            effectiveMinWidth = std::max(effectiveMinWidth, layoutMinimumHint.width);
            effectiveMinHeight = std::max(effectiveMinHeight, layoutMinimumHint.height);
        }
        const int effectiveMaxWidth = std::min(m_maxWidth, styleMax.width);
        const int effectiveMaxHeight = std::min(m_maxHeight, styleMax.height);
        newWidth = std::max(effectiveMinWidth, std::min(effectiveMaxWidth, newWidth));
        newHeight = std::max(effectiveMinHeight, std::min(effectiveMaxHeight, newHeight));

        const bool positionChanged = (m_absX != absX) || (m_absY != absY);
        const bool sizeChanged = (m_width != newWidth) || (m_height != newHeight);
        if (!positionChanged && !sizeChanged) {
            return;
        }

        const SwRect oldRect = absoluteRect_();
        if (positionChanged) {
            const int deltaX = absX - m_absX;
            const int deltaY = absY - m_absY;
            m_absX = absX;
            m_absY = absY;
            ensureSwWidgetChildren_();
            for (SwWidget* childWidget : m_swWidgetChildren) {
                childWidget->translateAbsolutePosRecursive_(deltaX, deltaY);
            }
        }
        if (sizeChanged) {
            m_width = newWidth;
            m_height = newHeight;
        }

        invalidateGeometryChange_(oldRect);

        if (positionChanged) {
            const SwPoint newPos = currentLocalPos_();
            MoveEvent event(newPos.x, newPos.y);
            SwCoreApplication::sendEvent(this, &event);
            emit moved(newPos.x, newPos.y);
        }

        if (sizeChanged) {
            ResizeEvent event(m_width, m_height);
            m_SuppressImplicitResizeUpdate_ = true;
            SwCoreApplication::sendEvent(this, &event);
            m_SuppressImplicitResizeUpdate_ = false;
            emit resized(m_width, m_height);
        }
    }

    void setGeometry(const SwRect& geometryRect) {
        setGeometry(geometryRect.x, geometryRect.y, geometryRect.width, geometryRect.height);
    }

    /**
     * @brief Returns the widget frame geometry, matching QWidget::frameGeometry().
     */
    SwRect frameGeometry() const override {
        if (dynamic_cast<SwWidget*>(SwObject::parent())) {
            return geometry();
        }
        if (m_nativeWindowHandle) {
            const SwRect nativeFrame = SwWidgetPlatformAdapter::windowFrameRect(m_nativeWindowHandle);
            if (nativeFrame.width > 0 && nativeFrame.height > 0) {
                return nativeFrame;
            }
        }
        const SwPoint topLeft = topLevelFramePosOnScreen_();
        return SwRect{topLeft.x, topLeft.y, m_width, m_height};
    }

    /**
     * @brief Returns the widget's client area dimensions (excluding frame borders).
     */
    SwSize clientSize() const override {
        return SwSize{m_width, m_height};
    }

    SwRect clientRect() const override {
        return rect();
    }

    /**
     * @brief Returns the widget-local rectangle, matching QWidget::rect().
     */
    SwRect rect() const {
        return SwRect{0, 0, m_width, m_height};
    }

    /**
     * @brief Returns the widget geometry relative to its parent (Qt-compatible).
     *
     * geometry() returns coordinates relative to the parent widget, matching QWidget::geometry().
     */
    SwRect geometry() const {
        SwRect g;
        if (auto* pw = dynamic_cast<SwWidget*>(SwObject::parent())) {
            g.x = m_absX - pw->m_absX;
            g.y = m_absY - pw->m_absY;
        } else {
            const SwPoint topLeft = topLevelClientPosOnScreen_();
            g.x = topLeft.x;
            g.y = topLeft.y;
        }
        g.width = m_width;
        g.height = m_height;
        return g;
    }

    /**
     * @brief Returns the widget position relative to its parent (Qt-compatible).
     */
    SwPoint pos() const {
        if (auto* pw = dynamic_cast<SwWidget*>(SwObject::parent())) {
            return {m_absX - pw->m_absX, m_absY - pw->m_absY};
        }
        return topLevelFramePosOnScreen_();
    }

    SwRect contentsRect() const {
        return rect();
    }

    SwSize frameSize() const {
        const SwRect fg = frameGeometry();
        return SwSize{fg.width, fg.height};
    }

    SwRect normalGeometry() const {
        if (dynamic_cast<SwWidget*>(SwObject::parent())) {
            return SwRect{};
        }
        return geometry();
    }

    /**
     * @brief Converts a point from widget-local coordinates to screen coordinates.
     *
     * Matches QWidget::mapToGlobal().
     */
    SwPoint mapToGlobal(const SwPoint& localPt) const {
        const SwPoint rootOrigin = rootClientOriginOnScreen_();
        return {rootOrigin.x + m_absX + localPt.x, rootOrigin.y + m_absY + localPt.y};
    }

    /**
     * @brief Converts a point from screen coordinates to widget-local coordinates.
     *
     * Matches QWidget::mapFromGlobal().
     */
    SwPoint mapFromGlobal(const SwPoint& globalPt) const {
        const SwPoint rootOrigin = rootClientOriginOnScreen_();
        return {globalPt.x - rootOrigin.x - m_absX, globalPt.y - rootOrigin.y - m_absY};
    }

    /**
     * @brief Converts a point from widget-local coordinates to parent coordinates, matching QWidget::mapToParent().
     */
    SwPoint mapToParent(const SwPoint& localPt) const {
        if (!dynamic_cast<SwWidget*>(SwObject::parent())) {
            return mapToGlobal(localPt);
        }
        SwPoint p = pos();
        return {p.x + localPt.x, p.y + localPt.y};
    }

    /**
     * @brief Converts a point from parent coordinates to widget-local coordinates, matching QWidget::mapFromParent().
     */
    SwPoint mapFromParent(const SwPoint& parentPt) const {
        if (!dynamic_cast<SwWidget*>(SwObject::parent())) {
            return mapFromGlobal(parentPt);
        }
        SwPoint p = pos();
        return {parentPt.x - p.x, parentPt.y - p.y};
    }

    /**
     * @brief Converts a point from this widget's coordinate space to another widget's.
     *
     * Matches QWidget::mapTo().
     */
    SwPoint mapTo(const SwWidget* target, const SwPoint& localPt) const {
        SwPoint global = mapToGlobal(localPt);
        return target ? target->mapFromGlobal(global) : global;
    }

    /**
     * @brief Converts a point from another widget's coordinates to this widget's, matching QWidget::mapFrom().
     */
    SwPoint mapFrom(const SwWidget* source, const SwPoint& sourcePt) const {
        SwPoint global = source ? source->mapToGlobal(sourcePt) : sourcePt;
        return mapFromGlobal(global);
    }

    SwWidgetPlatformHandle platformHandle() const {
        return m_nativeWindowHandle;
    }

    bool isVisibleInHierarchy() const {
        if (m_visibleInHierarchyDirty) {
            if (!getVisible()) {
                m_cachedVisibleInHierarchy = false;
            } else {
                m_cachedVisibleInHierarchy = true;
                const SwObject* p = parent();
                while (p) {
                    const SwWidget* wp = dynamic_cast<const SwWidget*>(p);
                    if (wp) {
                        m_cachedVisibleInHierarchy = wp->isVisibleInHierarchy();
                        break;
                    }
                    p = p->parent();
                }
            }
            m_visibleInHierarchyDirty = false;
        }
        return m_cachedVisibleInHierarchy;
    }

    virtual SwSize sizeHint() const override {
        SwSize hint = m_layout ? m_layout->sizeHint() : SwSize{m_width, m_height};
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        hint.width = std::max(hint.width, std::max(m_minWidth, styleMin.width));
        hint.height = std::max(hint.height, std::max(m_minHeight, styleMin.height));
        hint.width = std::min(hint.width, std::min(m_maxWidth, styleMax.width));
        hint.height = std::min(hint.height, std::min(m_maxHeight, styleMax.height));
        return hint;
    }

    virtual SwSize minimumSizeHint() const override {
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        SwSize hint{std::max(m_minWidth, styleMin.width), std::max(m_minHeight, styleMin.height)};
        if (m_layout) {
            const SwSize layoutHint = m_layout->minimumSizeHint();
            hint.width = std::max(hint.width, layoutHint.width);
            hint.height = std::max(hint.height, layoutHint.height);
        }
        hint.width = std::min(hint.width, std::min(m_maxWidth, styleMax.width));
        hint.height = std::min(hint.height, std::min(m_maxHeight, styleMax.height));
        return hint;
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

        // Coordinates are relative to this widget, matching QWidget::childAt().
        for (int i = static_cast<int>(children().size()) - 1; i >= 0; --i) {
            SwWidget* child = dynamic_cast<SwWidget*>(children()[static_cast<size_t>(i)]);
            if (!child) {
                continue;
            }
            if (child == ignore) {
                continue;
            }
            // VÃ©rifier si le pointeur est Ã  l'intÃ©rieur de l'enfant
            if (!child->isVisibleInHierarchy()) {
                continue;
            }
            const SwPoint childLocal = child->mapFromParent(SwPoint{x, y});
            if (child->isPointInside(childLocal.x, childLocal.y)) {
                SwWidget* deepChild = child->getChildUnderCursor(childLocal.x, childLocal.y, ignore);
                if (deepChild) {
                    deepestWidget = deepChild;
                }
                else {
                    deepestWidget = child;
                }
                break;
            }
        }

        return deepestWidget;
    }

    SwWidget* childAt(int x, int y) {
        return getChildUnderCursor(x, y);
    }

    const SwWidget* childAt(int x, int y) const {
        return const_cast<SwWidget*>(this)->getChildUnderCursor(x, y);
    }

    /**
     * @brief Retrieves the stylesheet associated with the SwWidget.
     *
     * Returns a pointer to the internal `StyleSheet` object used by the SwWidget.
     *
     * @return A pointer to the `StyleSheet` object.
     */
    StyleSheet* getToolSheet() override {
        if (m_EffectiveSheetDirty) {
            m_EffectiveSheet.clear();
            std::vector<const SwWidget*> chain;
            const SwWidget* current = this;
            while (current) {
                chain.push_back(current);
                current = dynamic_cast<const SwWidget*>(current->parent());
            }
            for (size_t i = chain.size(); i > 0; --i) {
                m_EffectiveSheet.mergeFrom(chain[i - 1]->m_DefaultComplexSheet);
            }
            for (size_t i = chain.size(); i > 0; --i) {
                m_EffectiveSheet.mergeFrom(chain[i - 1]->m_ComplexSheet);
            }
            m_EffectiveSheetDirty = false;
        }
        return &m_EffectiveSheet;
    }

    bool resolveStyledBackground(unsigned int stateFlags, SwColor& color, float& alpha, bool& paint) const override {
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_(stateFlags);
        if (!snapshot.hasBackgroundColor) {
            return false;
        }
        color = snapshot.backgroundColor;
        alpha = snapshot.backgroundAlpha;
        paint = snapshot.paintBackground;
        return true;
    }

    bool resolveStyledBorder(unsigned int stateFlags, SwColor& color, int& width, int& radius) const override {
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_(stateFlags);
        bool matched = false;
        if (snapshot.hasBorderColor) {
            color = snapshot.borderColor;
            matched = true;
        }
        if (snapshot.hasBorderWidth) {
            width = snapshot.borderWidth;
            matched = true;
        }
        if (snapshot.borderStyleNone) {
            width = 0;
            matched = true;
        }
        if (snapshot.hasBorderRadius) {
            radius = snapshot.borderRadius;
            matched = true;
        }
        return matched;
    }

    void resolveStyledBorderCorners(unsigned int stateFlags,
                                    int& topLeft,
                                    int& topRight,
                                    int& bottomRight,
                                    int& bottomLeft) const override {
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_(stateFlags);
        if (snapshot.hasBorderTopLeftRadius) topLeft = snapshot.borderTopLeftRadius;
        if (snapshot.hasBorderTopRightRadius) topRight = snapshot.borderTopRightRadius;
        if (snapshot.hasBorderBottomRightRadius) bottomRight = snapshot.borderBottomRightRadius;
        if (snapshot.hasBorderBottomLeftRadius) bottomLeft = snapshot.borderBottomLeftRadius;
    }

    SwColor resolveStyledTextColor(unsigned int stateFlags, const SwColor& fallback) const override {
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_(stateFlags);
        return snapshot.hasTextColor ? snapshot.textColor : fallback;
    }

    StyleSheet::BoxEdges resolveStyledPadding(unsigned int stateFlags) const override {
        return resolvedStyleSnapshot_(stateFlags).padding;
    }

    SwFont resolveStyledFont(unsigned int stateFlags) const override {
        return resolvedStyleSnapshot_(stateFlags).font;
    }

    SwString resolveStyledBoxShadow(unsigned int stateFlags) const override {
        return resolvedStyleSnapshot_(stateFlags).boxShadow;
    }

    bool hasExplicitStyledStateProperty(unsigned int stateFlags, const SwString& property) const override {
        if (property == "background-color") {
            return resolvedStyleSnapshot_(stateFlags).explicitStateBackground;
        }
        StyleSheet* sheet = const_cast<SwWidget*>(this)->getToolSheet();
        return sheet && sheet->hasExplicitStateProperty(styleSelectors_(), getObjectName(), stateFlags, property);
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

    bool dispatchMouseEventFromRoot(const MouseEvent& rootEvent) {
        return dispatchMouseEventFromRoot_(rootEvent);
    }

    bool dispatchWheelEventFromRoot(const WheelEvent& rootEvent) {
        return dispatchWheelEventFromRoot_(rootEvent);
    }

    bool dispatchKeyPressEventFromRoot(const KeyEvent& rootEvent) {
        return dispatchKeyPressEventFromRoot_(rootEvent);
    }

    bool dispatchKeyReleaseEventFromRoot(const KeyEvent& rootEvent) {
        return dispatchKeyReleaseEventFromRoot_(rootEvent);
    }

    SwWidget* focusedWidgetInHierarchy() {
        return findFocusedWidgetInHierarchy_();
    }

    SwWidget* hoveredWidgetFromRoot() const {
        const SwWidget* hoverRoot = mouseGrabScopeRoot_();
        if (!hoverRoot) {
            return nullptr;
        }

        const SwList<SwWidget*>& hoveredPath = hoverRoot->m_hoveredPath_;
        for (size_t i = hoveredPath.size(); i > 0; --i) {
            SwWidget* hoveredWidget = hoveredPath[i - 1];
            if (hoveredWidget && SwObject::isLive(hoveredWidget)) {
                return hoveredWidget;
            }
        }

        return nullptr;
    }

    void bindToNativeWindowRecursive(const SwWidgetPlatformHandle& handle) {
        setNativeWindowHandleRecursive(handle);
    }

    void unbindFromNativeWindowRecursive() {
        setNativeWindowHandleRecursive(SwWidgetPlatformHandle{});
    }


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
        invalidateEffectiveStyleCacheRecursive_();
        SwWidget *ui = dynamic_cast<SwWidget*>(parent);
        m_parentWidgetCached = ui;
        if (ui) {
            setNativeWindowHandleRecursive(ui->nativeWindowHandle());

            // Preserve the widget's existing local coordinates while translating the whole
            // subtree into the new parent's absolute coordinate space.
            const int localX = m_absX;
            const int localY = m_absY;
            setAbsolutePos_(ui->m_absX + localX, ui->m_absY + localY);
        }
        SwObject::newParentEvent(parent);
        if (m_Focus) {
            rebindFocusOwnership_();
        }
    }

    // Internal: set absolute window position and propagate delta to all children.
    void setAbsolutePos_(int absX, int absY) {
        if (m_absX == absX && m_absY == absY) {
            return;
        }
        const SwRect oldRect = absoluteRect_();
        auto inflate = [](SwRect rect, int margin) {
            rect.x -= margin;
            rect.y -= margin;
            rect.width = std::max(0, rect.width + margin * 2);
            rect.height = std::max(0, rect.height + margin * 2);
            return rect;
        };
        int deltaX = absX - m_absX;
        int deltaY = absY - m_absY;
        m_absX = absX;
        m_absY = absY;
        const SwPoint newPos = pos();
        MoveEvent event(newPos.x, newPos.y);
        SwCoreApplication::sendEvent(this, &event);
        emit moved(newPos.x, newPos.y);
        if (isVisibleInHierarchy()) {
            constexpr int kInvalidateMargin = 8;
            SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflate(oldRect, kInvalidateMargin));
            SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflate(absoluteRect_(), kInvalidateMargin));
            if (auto* guiApp = SwGuiApplication::instance(false)) {
                if (auto* integration = guiApp->platformIntegration()) {
                    integration->wakeUpGuiThread();
                }
            }
        }
        for (SwObject* objChild : children()) {
            if (auto* childWidget = dynamic_cast<SwWidget*>(objChild)) {
                childWidget->setAbsolutePos_(childWidget->m_absX + deltaX, childWidget->m_absY + deltaY);
            }
        }
    }

    SwPoint currentLocalPos_() const {
        if (m_parentWidgetCached) {
            return {m_absX - m_parentWidgetCached->m_absX, m_absY - m_parentWidgetCached->m_absY};
        }
        return {m_absX, m_absY};
    }

    void translateAbsolutePosRecursive_(int deltaX, int deltaY) {
        if (deltaX == 0 && deltaY == 0) {
            return;
        }
        m_absX += deltaX;
        m_absY += deltaY;
        ensureSwWidgetChildren_();
        for (SwWidget* childWidget : m_swWidgetChildren) {
            childWidget->translateAbsolutePosRecursive_(deltaX, deltaY);
        }
    }

    static SwRect inflateForGeometryInvalidate_(SwRect rect) {
        constexpr int kInvalidateMargin = 8;
        rect.x -= kInvalidateMargin;
        rect.y -= kInvalidateMargin;
        rect.width = std::max(0, rect.width + kInvalidateMargin * 2);
        rect.height = std::max(0, rect.height + kInvalidateMargin * 2);
        return rect;
    }

    void invalidateGeometryChange_(const SwRect& oldRect) {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SW_WIDGET_PERF_COUNT("invalidateGeometryChange.calls");
        SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflateForGeometryInvalidate_(oldRect));
        SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, inflateForGeometryInvalidate_(absoluteRect_()));
    }

    SwPoint rootClientOriginOnScreen_() const {
        const SwWidget* root = this;
        while (const auto* parentWidget = dynamic_cast<const SwWidget*>(root->parent())) {
            root = parentWidget;
        }
        if (!root) {
            return SwPoint{0, 0};
        }
        return SwWidgetPlatformAdapter::clientOriginOnScreen(root->m_nativeWindowHandle);
    }

    SwPoint topLevelClientPosOnScreen_() const {
        if (dynamic_cast<SwWidget*>(SwObject::parent())) {
            return SwPoint{m_absX, m_absY};
        }
        if (m_nativeWindowHandle) {
            return SwWidgetPlatformAdapter::clientOriginOnScreen(m_nativeWindowHandle);
        }
        return SwPoint{m_absX, m_absY};
    }

    SwPoint topLevelFramePosOnScreen_() const {
        if (dynamic_cast<SwWidget*>(SwObject::parent())) {
            return SwPoint{m_absX, m_absY};
        }
        if (m_nativeWindowHandle) {
            const SwRect nativeFrame = SwWidgetPlatformAdapter::windowFrameRect(m_nativeWindowHandle);
            if (nativeFrame.width > 0 && nativeFrame.height > 0) {
                return SwPoint{nativeFrame.x, nativeFrame.y};
            }
        }
        return topLevelClientPosOnScreen_();
    }

    SwRect absoluteRect_() const {
        return SwRect{m_absX, m_absY, m_width, m_height};
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
        SW_WIDGET_PERF_SCOPE("paintEvent");
        if (!isVisibleInHierarchy()) {
            return;
        }

        NormalizedPaintEvent_ normalized = normalizePaintEvent_(event);
        SwPainter* painter = normalized.painter;
        if (!painter) {
            return;
        }
        PaintEvent effectiveEvent(painter, normalized.paintRect);
        PaintEvent* localEvent = event;
        const bool needsSyntheticEvent =
            !event ||
            normalized.painter != event->painter() ||
            normalized.paintRect.x != event->paintRect().x ||
            normalized.paintRect.y != event->paintRect().y ||
            normalized.paintRect.width != event->paintRect().width ||
            normalized.paintRect.height != event->paintRect().height;
        if (needsSyntheticEvent) {
            localEvent = &effectiveEvent;
        }

        const SwRect rect = this->rect();
        // Read the resolved style snapshot ONCE — avoids 4 separate calls
        // to resolvedStyleSnapshot_() per frame.
        const ResolvedStyleSnapshot_& snap = resolvedStyleSnapshot_();
        const bool isTopLevel = !m_parentWidgetCached;

        SwColor bgColor     = snap.hasBackgroundColor ? snap.backgroundColor : SwColor{249, 249, 249};
        float backgroundAlpha = snap.backgroundAlpha;
        bool paintBackground = snap.hasBackgroundColor ? snap.paintBackground : isTopLevel;
        SwColor borderColor = snap.hasBorderColor ? snap.borderColor : SwColor{0, 0, 0};
        int borderWidth     = snap.hasBorderWidth ? (snap.borderStyleNone ? 0 : snap.borderWidth) : 0;
        int borderTopLeftRadius     = snap.hasBorderTopLeftRadius     ? snap.borderTopLeftRadius     : (snap.hasBorderRadius ? snap.borderRadius : 0);
        int borderTopRightRadius    = snap.hasBorderTopRightRadius    ? snap.borderTopRightRadius    : (snap.hasBorderRadius ? snap.borderRadius : 0);
        int borderBottomRightRadius = snap.hasBorderBottomRightRadius ? snap.borderBottomRightRadius : (snap.hasBorderRadius ? snap.borderRadius : 0);
        int borderBottomLeftRadius  = snap.hasBorderBottomLeftRadius  ? snap.borderBottomLeftRadius  : (snap.hasBorderRadius ? snap.borderRadius : 0);

        if (paintBackground || borderWidth > 0) {
            const bool hasRoundedCorners =
                borderTopLeftRadius > 0 || borderTopRightRadius > 0 || borderBottomRightRadius > 0 || borderBottomLeftRadius > 0;
            const SwColor fillColor = paintBackground ? bgColor : SwColor{255, 255, 255};

            if (hasRoundedCorners) {
                painter->fillRoundedRect(rect,
                                         borderTopLeftRadius,
                                         borderTopRightRadius,
                                         borderBottomRightRadius,
                                         borderBottomLeftRadius,
                                         fillColor,
                                         borderColor,
                                         borderWidth);
            } else {
                painter->fillRect(rect, fillColor, borderColor, borderWidth);
            }
        }

        const SwRect& paintRect = localEvent->paintRect();
        ensureSwWidgetChildren_();
        for (SwWidget* child : m_swWidgetChildren) {
            if (!child->isVisibleInHierarchy()) {
                continue;
            }
            const SwRect childRect = child->geometry();
            if (rectsIntersect(paintRect, childRect)) {
                paintChild_(localEvent, child);
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

    static SwRect intersectRect_(const SwRect& a, const SwRect& b) {
        const int left = (std::max)(a.x, b.x);
        const int top = (std::max)(a.y, b.y);
        const int right = (std::min)(a.x + a.width, b.x + b.width);
        const int bottom = (std::min)(a.y + a.height, b.y + b.height);
        if (right <= left || bottom <= top) {
            return SwRect{0, 0, 0, 0};
        }
        return SwRect{left, top, right - left, bottom - top};
    }

    static bool rectContainsRect_(const SwRect& outer, const SwRect& inner) {
        return inner.x >= outer.x &&
               inner.y >= outer.y &&
               (inner.x + inner.width) <= (outer.x + outer.width) &&
               (inner.y + inner.height) <= (outer.y + outer.height);
    }

    struct NormalizedPaintEvent_ {
        SwPainter* painter{nullptr};
        SwRect paintRect{0, 0, 0, 0};
        std::unique_ptr<SwOffsetPainter> ownedPainter;
    };

    NormalizedPaintEvent_ normalizePaintEvent_(PaintEvent* event) {
        NormalizedPaintEvent_ result;
        if (!event) {
            return result;
        }

        result.painter = event->painter();
        result.paintRect = event->paintRect();
        if (!result.painter) {
            return result;
        }

        if (!m_parentWidgetCached) {
            return result;
        }

        const SwRect localRect = rect();
        if (rectContainsRect_(localRect, result.paintRect)) {
            return result;
        }

        const SwRect parentRect = geometry();
        if (!rectsIntersect(result.paintRect, parentRect)) {
            return result;
        }

        SwRect childPaintRect = intersectRect_(result.paintRect, parentRect);
        if (childPaintRect.width <= 0 || childPaintRect.height <= 0) {
            result.paintRect = SwRect{0, 0, 0, 0};
            return result;
        }

        childPaintRect.x -= parentRect.x;
        childPaintRect.y -= parentRect.y;
        result.ownedPainter.reset(new SwOffsetPainter(result.painter, parentRect.x, parentRect.y));
        result.painter = result.ownedPainter.get();
        result.paintRect = childPaintRect;
        return result;
    }

    void paintChild_(PaintEvent* event, SwWidget* child) {
        if (!event || !child) {
            return;
        }
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }
        const SwRect childGeometry = child->geometry();
        SwRect childPaintRect = intersectRect_(event->paintRect(), childGeometry);
        if (childPaintRect.width <= 0 || childPaintRect.height <= 0) {
            return;
        }
        childPaintRect.x -= childGeometry.x;
        childPaintRect.y -= childGeometry.y;
        SwOffsetPainter childPainter(painter, childGeometry.x, childGeometry.y);
        PaintEvent childEvent(&childPainter, childPaintRect);
        SwCoreApplication::sendEvent(child, &childEvent);
    }

    virtual bool event(SwEvent* event) override {
        if (!event) {
            return false;
        }

        switch (event->type()) {
        case EventType::Paint:
            paintEvent(static_cast<PaintEvent*>(event));
            event->accept();
            return true;
        case EventType::Resize:
            resizeEvent(static_cast<ResizeEvent*>(event));
            event->accept();
            return true;
        case EventType::Move:
            moveEvent(static_cast<MoveEvent*>(event));
            event->accept();
            return true;
        case EventType::KeyPressEvent:
            keyPressEvent(static_cast<KeyEvent*>(event));
            return true;
        case EventType::KeyReleaseEvent:
            keyReleaseEvent(static_cast<KeyEvent*>(event));
            return true;
        case EventType::MousePressEvent:
            mousePressEvent(static_cast<MouseEvent*>(event));
            return true;
        case EventType::MouseDoubleClickEvent:
            mouseDoubleClickEvent(static_cast<MouseEvent*>(event));
            return true;
        case EventType::MouseMoveEvent:
            mouseMoveEvent(static_cast<MouseEvent*>(event));
            return true;
        case EventType::MouseReleaseEvent:
            mouseReleaseEvent(static_cast<MouseEvent*>(event));
            return true;
        case EventType::WheelEvent:
            wheelEvent(static_cast<WheelEvent*>(event));
            return true;
        case EventType::Show:
            showEvent(static_cast<Event*>(event));
            event->accept();
            return true;
        case EventType::Hide:
            releaseMouseGrabForHiddenHierarchy_();
            clearHoverRecursive_();
            hideEvent(static_cast<Event*>(event));
            event->accept();
            return true;
        default:
            return SwWidgetInterface::event(event);
        }
    }

    bool dispatchMouseEventFromRoot_(const MouseEvent& rootEvent) {
        if (!isVisibleInHierarchy()) {
            return false;
        }

        SwWidget* hoverTarget = nullptr;
        if (rootEvent.type() == EventType::MouseMoveEvent) {
            hoverTarget = updateHoverStateFromRoot_(rootEvent.pos());
        }

        SwWidget* target = nullptr;
        SwWidget* mouseGrabRoot = mouseGrabScopeRoot_();
        SwWidget* grabber = mouseGrabberWidget_();
        if (grabber &&
            (!SwObject::isLive(grabber) ||
             !mouseGrabRoot->belongsToHierarchy_(grabber) ||
             !grabber->isVisibleInHierarchy())) {
            mouseGrabberWidget_() = nullptr;
            mouseGrabButtons_() = 0;
            grabber = nullptr;
        }
        if ((rootEvent.type() == EventType::MouseMoveEvent ||
             rootEvent.type() == EventType::MouseReleaseEvent ||
             (rootEvent.type() == EventType::MousePressEvent && mouseGrabButtons_() != 0)) &&
            grabber) {
            target = grabber;
        }

        if (!target && hoverTarget) {
            target = hoverTarget;
        }
        if (!target) {
            target = getChildUnderCursor(rootEvent.x(), rootEvent.y());
        }
        if (!target) {
            target = this;
        }

        SwWidget* acceptedBy = nullptr;

        if (rootEvent.type() == EventType::MousePressEvent) {
            updateFocusForDispatch_(target);
        }

        const bool handled = deliverMouseEventToChain_(target, rootEvent, &acceptedBy);

        if (rootEvent.type() == EventType::MousePressEvent &&
            rootEvent.button() != SwMouseButton::NoButton &&
            acceptedBy) {
            mouseGrabRoot->m_mouseGrabberWidget = acceptedBy;
            mouseGrabRoot->m_mouseGrabButtons |= mouseButtonMask_(rootEvent.button());
        }

        if (rootEvent.type() == EventType::MouseReleaseEvent && rootEvent.button() != SwMouseButton::NoButton) {
            mouseGrabRoot->m_mouseGrabButtons &= ~mouseButtonMask_(rootEvent.button());
            if (mouseGrabRoot->m_mouseGrabButtons == 0) {
                mouseGrabRoot->m_mouseGrabberWidget = nullptr;
            }
        }

        return handled;
    }

    bool dispatchWheelEventFromRoot_(const WheelEvent& rootEvent) {
        if (!isVisibleInHierarchy()) {
            return false;
        }

        SwWidget* target = getChildUnderCursor(rootEvent.x(), rootEvent.y());
        if (!target) {
            target = this;
        }

        return deliverWheelEventToChain_(target, rootEvent);
    }

    bool dispatchKeyPressEventFromRoot_(const KeyEvent& rootEvent) {
        if (!isVisibleInHierarchy()) {
            return false;
        }

        SwWidget* target = findFocusedWidgetInHierarchy_();
        if (!target) {
            target = this;
        }

        return deliverKeyEventToChain_(target, rootEvent);
    }

    bool dispatchKeyReleaseEventFromRoot_(const KeyEvent& rootEvent) {
        if (!isVisibleInHierarchy()) {
            return false;
        }

        SwWidget* target = findFocusedWidgetInHierarchy_();
        if (!target) {
            target = this;
        }

        return deliverKeyEventToChain_(target, rootEvent);
    }

    /**
     * @brief Handles the key press event for the SwWidget.
     *
     * Root-level key dispatch is handled by `dispatchKeyPressEventFromRoot_()`. The base widget does
     * not perform legacy child recursion anymore.
     *
     * @param event Pointer to the `KeyEvent` containing information about the key press.
     */
    virtual void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        if (!event->isKernelDispatched()) {
            return;
        }

    }

    virtual void keyReleaseEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        if (!event->isKernelDispatched()) {
            return;
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
        SW_WIDGET_PERF_SCOPE("resizeEvent");
        if (!m_SuppressImplicitResizeUpdate_) {
            this->update();
        }
        if (m_layout) {
            m_layout->updateGeometry();
        }
    }

    virtual void moveEvent(MoveEvent* event) {
        SW_UNUSED(event);
    }

    /**
     * @brief Called when the window receives a close request (e.g. user clicks X).
     *
     * Override this to prevent closing (call event->ignore()) or to minimize to
     * system tray instead. Default implementation accepts the event.
     */
    virtual void closeEvent(CloseEvent* event) {
        SW_UNUSED(event);
        // Default: accept (allow close)
    }

    virtual void showEvent(Event* event) {
        SW_UNUSED(event);
    }

    virtual void hideEvent(Event* event) {
        SW_UNUSED(event);
    }

    /**
     * @brief Handles the mouse press event for the SwWidget.
     *
     * Root-level mouse dispatch is handled by `dispatchMouseEventFromRoot_()`. The base widget does
     * not perform legacy child recursion anymore.
     *
     * @param event Pointer to the `MouseEvent` containing the mouse press details.
     */
    virtual void mousePressEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        if (!event->isKernelDispatched()) {
            return;
        }

    }

    /**
     * @brief Handles the mouse release event for the SwWidget.
     *
     * Root-level mouse dispatch is handled by `dispatchMouseEventFromRoot_()`. The base widget does
     * not perform legacy child recursion anymore.
     *
     * @param event Pointer to the `MouseEvent` containing the mouse release details.
     */
    virtual void mouseReleaseEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        if (!event->isKernelDispatched()) {
            return;
        }

    }

    /**
     * @brief Handles the mouse double-click event for the SwWidget.
     *
     * Root-level mouse dispatch is handled by `dispatchMouseEventFromRoot_()`. The base widget does
     * not perform legacy child recursion anymore.
     *
     * @param event Pointer to the `MouseEvent` containing the double-click details.
     */
    virtual void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        if (!event->isKernelDispatched()) {
            return;
        }

    }

    /**
     * @brief Handles the mouse move event for the SwWidget.
     *
     * Root-level mouse dispatch is handled by `dispatchMouseEventFromRoot_()`. The base widget only
     * updates its own hover/cursor state for kernel-delivered events.
     *
     * @param event Pointer to the `MouseEvent` containing the mouse move details.
     */
    virtual void mouseMoveEvent(MouseEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }

        if (!event->isKernelDispatched()) {
            return;
        }

        this->setHover(isPointInside(event->x(), event->y()));

        if (!event->isAccepted() && this->getHover()) {
            SwWidgetPlatformAdapter::setCursor(getCursor());
            event->accept();
        }
    }

    virtual void wheelEvent(WheelEvent* event) override {
        if (!isVisibleInHierarchy() || !event) {
            return;
        }
        if (!event->isKernelDispatched()) {
            return;
        }

    }

protected:

    // ─── Style-resolution utilities (shared by all widgets) ───────────────

    struct ResolvedStyleSnapshot_ {
        bool valid{false};
        unsigned int stateFlags{StyleSheet::StateNone};
        SwFont baseFont{};
        SwString objectName;
        bool hasBackgroundColor{false};
        SwColor backgroundColor{0, 0, 0};
        float backgroundAlpha{1.0f};
        bool paintBackground{true};
        bool hasBorderColor{false};
        SwColor borderColor{0, 0, 0};
        bool hasBorderWidth{false};
        int borderWidth{0};
        bool borderStyleNone{false};
        bool hasBorderRadius{false};
        int borderRadius{0};
        bool hasBorderTopLeftRadius{false};
        int borderTopLeftRadius{0};
        bool hasBorderTopRightRadius{false};
        int borderTopRightRadius{0};
        bool hasBorderBottomRightRadius{false};
        int borderBottomRightRadius{0};
        bool hasBorderBottomLeftRadius{false};
        int borderBottomLeftRadius{0};
        bool hasTextColor{false};
        SwColor textColor{0, 0, 0};
        StyleSheet::BoxEdges padding{};
        SwSize minSize{0, 0};
        SwSize maxSize{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
        SwFont font{};
        bool explicitStateBackground{false};
        SwString boxShadow;
    };

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static double clampDouble(double value, double minValue, double maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static SwColor clampColor(const SwColor& c) {
        return SwColor{clampInt(c.r, 0, 255), clampInt(c.g, 0, 255), clampInt(c.b, 0, 255)};
    }

    static int parsePixelValue(const SwString& value, int defaultValue) {
        return StyleSheet::parsePixelValue(value, defaultValue);
    }

    const SwList<SwString>& styleSelectors_() const {
        if (!m_StyleSelectorsCacheInitialized) {
            m_StyleSelectorsCache = classHierarchy();
            if (!m_StyleSelectorsCache.contains("SwWidget")) {
                m_StyleSelectorsCache.append("SwWidget");
            }
            m_StyleSelectorsCacheInitialized = true;
        }
        return m_StyleSelectorsCache;
    }

    unsigned int styleStateFlags_() const {
        unsigned int flags = StyleSheet::StateNone;
        if (getHover()) {
            flags |= StyleSheet::StateHovered;
        }
        if (getFocus()) {
            flags |= StyleSheet::StateFocused;
        }
        if (!getEnable()) {
            flags |= StyleSheet::StateDisabled;
        }
        if (const_cast<SwWidget*>(this)->propertyExist("Pressed")) {
            try {
                if (const_cast<SwWidget*>(this)->property("Pressed").get<bool>()) {
                    flags |= StyleSheet::StatePressed;
                }
            } catch (...) {
            }
        }
        if (const_cast<SwWidget*>(this)->propertyExist("Checked")) {
            try {
                if (const_cast<SwWidget*>(this)->property("Checked").get<bool>()) {
                    flags |= StyleSheet::StateChecked;
                }
            } catch (...) {
            }
        }
        return flags;
    }

    SwString resolveStyleProperty_(const StyleSheet* sheet,
                                   const SwString& property,
                                   unsigned int stateFlags) const {
        if (!sheet) {
            return SwString();
        }
        return sheet->resolveStyleProperty(styleSelectors_(), getObjectName(), stateFlags, property);
    }

    const ResolvedStyleSnapshot_& resolvedStyleSnapshot_(unsigned int stateFlags = std::numeric_limits<unsigned int>::max()) const {
        if (stateFlags == std::numeric_limits<unsigned int>::max()) {
            stateFlags = styleStateFlags_();
        }

        StyleSheet* effectiveSheet = const_cast<SwWidget*>(this)->getToolSheet();
        const SwFont baseFont = getFont();
        const SwString objectName = getObjectName();

        if (m_ResolvedStyleSnapshot.valid &&
            !m_ResolvedStyleSnapshotDirty &&
            m_ResolvedStyleSnapshot.stateFlags == stateFlags &&
            m_ResolvedStyleSnapshot.baseFont == baseFont &&
            m_ResolvedStyleSnapshot.objectName == objectName) {
            return m_ResolvedStyleSnapshot;
        }

        ResolvedStyleSnapshot_ snapshot;
        snapshot.valid = true;
        snapshot.stateFlags = stateFlags;
        snapshot.baseFont = baseFont;
        snapshot.objectName = objectName;
        snapshot.font = baseFont;

        if (effectiveSheet) {
            const SwString backgroundValue = resolveStyleProperty_(effectiveSheet, "background-color", stateFlags);
            if (!backgroundValue.isEmpty()) {
                snapshot.hasBackgroundColor = true;
                snapshot.paintBackground = true;
                try {
                    snapshot.backgroundColor = clampColor(const_cast<StyleSheet*>(effectiveSheet)->parseColor(backgroundValue, &snapshot.backgroundAlpha));
                    snapshot.paintBackground = snapshot.backgroundAlpha > 0.0f;
                } catch (...) {
                    snapshot.paintBackground = false;
                    snapshot.backgroundAlpha = 1.0f;
                }
            }

            const SwString borderColorValue = resolveStyleProperty_(effectiveSheet, "border-color", stateFlags);
            if (!borderColorValue.isEmpty()) {
                snapshot.hasBorderColor = true;
                try {
                    snapshot.borderColor = clampColor(const_cast<StyleSheet*>(effectiveSheet)->parseColor(borderColorValue, nullptr));
                } catch (...) {
                }
            }

            const SwString borderWidthValue = resolveStyleProperty_(effectiveSheet, "border-width", stateFlags);
            if (!borderWidthValue.isEmpty()) {
                snapshot.hasBorderWidth = true;
                snapshot.borderWidth = clampInt(parsePixelValue(borderWidthValue, snapshot.borderWidth), 0, 20);
            }

            const SwString borderStyleValue = resolveStyleProperty_(effectiveSheet, "border-style", stateFlags);
            if (!borderStyleValue.isEmpty() && borderStyleValue.toLower() == "none") {
                snapshot.borderStyleNone = true;
            }

            const SwString borderRadiusValue = resolveStyleProperty_(effectiveSheet, "border-radius", stateFlags);
            if (!borderRadiusValue.isEmpty()) {
                snapshot.hasBorderRadius = true;
                snapshot.borderRadius = clampInt(parsePixelValue(borderRadiusValue, snapshot.borderRadius), 0, 32);
            }

            const SwString topLeftRadiusValue = resolveStyleProperty_(effectiveSheet, "border-top-left-radius", stateFlags);
            if (!topLeftRadiusValue.isEmpty()) {
                snapshot.hasBorderTopLeftRadius = true;
                snapshot.borderTopLeftRadius = clampInt(parsePixelValue(topLeftRadiusValue, snapshot.borderTopLeftRadius), 0, 32);
            }

            const SwString topRightRadiusValue = resolveStyleProperty_(effectiveSheet, "border-top-right-radius", stateFlags);
            if (!topRightRadiusValue.isEmpty()) {
                snapshot.hasBorderTopRightRadius = true;
                snapshot.borderTopRightRadius = clampInt(parsePixelValue(topRightRadiusValue, snapshot.borderTopRightRadius), 0, 32);
            }

            const SwString bottomRightRadiusValue = resolveStyleProperty_(effectiveSheet, "border-bottom-right-radius", stateFlags);
            if (!bottomRightRadiusValue.isEmpty()) {
                snapshot.hasBorderBottomRightRadius = true;
                snapshot.borderBottomRightRadius = clampInt(parsePixelValue(bottomRightRadiusValue, snapshot.borderBottomRightRadius), 0, 32);
            }

            const SwString bottomLeftRadiusValue = resolveStyleProperty_(effectiveSheet, "border-bottom-left-radius", stateFlags);
            if (!bottomLeftRadiusValue.isEmpty()) {
                snapshot.hasBorderBottomLeftRadius = true;
                snapshot.borderBottomLeftRadius = clampInt(parsePixelValue(bottomLeftRadiusValue, snapshot.borderBottomLeftRadius), 0, 32);
            }

            const SwString textColorValue = resolveStyleProperty_(effectiveSheet, "color", stateFlags);
            if (!textColorValue.isEmpty()) {
                snapshot.hasTextColor = true;
                try {
                    snapshot.textColor = clampColor(const_cast<StyleSheet*>(effectiveSheet)->parseColor(textColorValue, nullptr));
                } catch (...) {
                    snapshot.hasTextColor = false;
                }
            }

            const SwString paddingValue = resolveStyleProperty_(effectiveSheet, "padding", stateFlags);
            if (!paddingValue.isEmpty()) {
                snapshot.padding = StyleSheet::parseBoxEdges(paddingValue);
            }

            const SwString paddingTopValue = resolveStyleProperty_(effectiveSheet, "padding-top", stateFlags);
            const SwString paddingRightValue = resolveStyleProperty_(effectiveSheet, "padding-right", stateFlags);
            const SwString paddingBottomValue = resolveStyleProperty_(effectiveSheet, "padding-bottom", stateFlags);
            const SwString paddingLeftValue = resolveStyleProperty_(effectiveSheet, "padding-left", stateFlags);

            if (!paddingTopValue.isEmpty()) snapshot.padding.top = parsePixelValue(paddingTopValue, snapshot.padding.top);
            if (!paddingRightValue.isEmpty()) snapshot.padding.right = parsePixelValue(paddingRightValue, snapshot.padding.right);
            if (!paddingBottomValue.isEmpty()) snapshot.padding.bottom = parsePixelValue(paddingBottomValue, snapshot.padding.bottom);
            if (!paddingLeftValue.isEmpty()) snapshot.padding.left = parsePixelValue(paddingLeftValue, snapshot.padding.left);

            snapshot.minSize.width = std::max(0, parsePixelValue(resolveStyleProperty_(effectiveSheet, "min-width", stateFlags), 0));
            snapshot.minSize.height = std::max(0, parsePixelValue(resolveStyleProperty_(effectiveSheet, "min-height", stateFlags), 0));
            snapshot.maxSize.width = std::max(0, parsePixelValue(resolveStyleProperty_(effectiveSheet, "max-width", stateFlags), std::numeric_limits<int>::max()));
            snapshot.maxSize.height = std::max(0, parsePixelValue(resolveStyleProperty_(effectiveSheet, "max-height", stateFlags), std::numeric_limits<int>::max()));

            const SwString familyValue = resolveStyleProperty_(effectiveSheet, "font-family", stateFlags);
            const SwString sizeValue = resolveStyleProperty_(effectiveSheet, "font-size", stateFlags);
            const SwString weightValue = resolveStyleProperty_(effectiveSheet, "font-weight", stateFlags);

            const SwString family = StyleSheet::normalizeFontFamily(familyValue);
            if (!family.isEmpty()) {
                snapshot.font.setFamily(family.toStdWString());
            }

            const int px = parsePixelValue(sizeValue, 0);
            if (px > 0) {
                snapshot.font.setPixelSize(px);
            }

            if (!weightValue.isEmpty()) {
                snapshot.font.setWeight(StyleSheet::parseFontWeightValue(weightValue, snapshot.font.getWeight()));
            }

            snapshot.boxShadow = resolveStyleProperty_(effectiveSheet, "box-shadow", stateFlags);
            snapshot.explicitStateBackground = effectiveSheet->hasExplicitStateProperty(styleSelectors_(),
                                                                                        objectName,
                                                                                        stateFlags,
                                                                                        "background-color");
        }

        m_ResolvedStyleSnapshot = snapshot;
        m_ResolvedStyleSnapshotDirty = false;
        return m_ResolvedStyleSnapshot;
    }

    SwSize resolvedStyleMinimumSize_() const {
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_();
        const int borderWidth = snapshot.borderStyleNone ? 0 : std::max(0, snapshot.borderWidth);
        const int extraWidth = snapshot.padding.left + snapshot.padding.right + (borderWidth * 2);
        const int extraHeight = snapshot.padding.top + snapshot.padding.bottom + (borderWidth * 2);
        return SwSize{
            clampInt(snapshot.minSize.width + extraWidth, 0, std::numeric_limits<int>::max()),
            clampInt(snapshot.minSize.height + extraHeight, 0, std::numeric_limits<int>::max())
        };
    }

    SwSize resolvedStyleMaximumSize_() const {
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_();
        const int borderWidth = snapshot.borderStyleNone ? 0 : std::max(0, snapshot.borderWidth);
        const int extraWidth = snapshot.padding.left + snapshot.padding.right + (borderWidth * 2);
        const int extraHeight = snapshot.padding.top + snapshot.padding.bottom + (borderWidth * 2);
        SwSize result = snapshot.maxSize;
        if (result.width != std::numeric_limits<int>::max()) {
            result.width = clampInt(result.width + extraWidth, 0, std::numeric_limits<int>::max());
        }
        if (result.height != std::numeric_limits<int>::max()) {
            result.height = clampInt(result.height + extraHeight, 0, std::numeric_limits<int>::max());
        }
        return result;
    }

    SwFont resolvedStyledFont_(const StyleSheet* sheet = nullptr, unsigned int stateFlags = std::numeric_limits<unsigned int>::max()) const {
        SW_UNUSED(sheet);
        return resolvedStyleSnapshot_(stateFlags).font;
    }

    StyleSheet::BoxEdges resolvePaddingEdges_(const StyleSheet* sheet = nullptr,
                                              unsigned int stateFlags = std::numeric_limits<unsigned int>::max()) const {
        SW_UNUSED(sheet);
        return resolvedStyleSnapshot_(stateFlags).padding;
    }

    void updateStyleTree_() {
        m_EffectiveSheetDirty = true;
        invalidateResolvedStyleSnapshot_();
        update();
        const auto& directChildren = children();
        for (SwObject* objChild : directChildren) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (child) {
                child->updateStyleTree_();
            }
        }
    }

    void invalidateEffectiveStyleCacheRecursive_() {
        m_EffectiveSheetDirty = true;
        invalidateResolvedStyleSnapshot_();
        const auto& directChildren = children();
        for (SwObject* objChild : directChildren) {
            auto* child = dynamic_cast<SwWidget*>(objChild);
            if (child) {
                child->invalidateEffectiveStyleCacheRecursive_();
            }
        }
    }

    void invalidateResolvedStyleSnapshot_() {
        m_ResolvedStyleSnapshotDirty = true;
        m_ResolvedStyleSnapshot.valid = false;
    }

    void resolveBackground(const StyleSheet* sheet,
                           SwColor& outColor,
                           float& outAlpha,
                           bool& outPaint) const {
        if (!sheet) {
            return;
        }
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_();
        if (!snapshot.hasBackgroundColor) {
            return;
        }
        outPaint = snapshot.paintBackground;
        if (outPaint) {
            outColor = snapshot.backgroundColor;
        }
        outAlpha = snapshot.backgroundAlpha;
    }

    void resolveBorder(const StyleSheet* sheet,
                       SwColor& outColor,
                       int& outWidth,
                       int& outRadius) const {
        if (!sheet) {
            return;
        }
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_();
        if (snapshot.hasBorderColor) {
            outColor = snapshot.borderColor;
        }
        if (snapshot.hasBorderWidth) {
            outWidth = snapshot.borderWidth;
        }
        if (snapshot.borderStyleNone) {
            outWidth = 0;
        }
        if (snapshot.hasBorderRadius) {
            outRadius = snapshot.borderRadius;
        }
    }

    void resolveBorderCornerRadii(const StyleSheet* sheet,
                                  int& outTopLeft,
                                  int& outTopRight,
                                  int& outBottomRight,
                                  int& outBottomLeft) const {
        if (!sheet) {
            return;
        }
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_();
        if (snapshot.hasBorderTopLeftRadius) {
            outTopLeft = snapshot.borderTopLeftRadius;
        }
        if (snapshot.hasBorderTopRightRadius) {
            outTopRight = snapshot.borderTopRightRadius;
        }
        if (snapshot.hasBorderBottomRightRadius) {
            outBottomRight = snapshot.borderBottomRightRadius;
        }
        if (snapshot.hasBorderBottomLeftRadius) {
            outBottomLeft = snapshot.borderBottomLeftRadius;
        }
    }

    SwColor resolveTextColor(const StyleSheet* sheet, const SwColor& fallback) const {
        if (!sheet) {
            return fallback;
        }
        const ResolvedStyleSnapshot_& snapshot = resolvedStyleSnapshot_();
        if (!snapshot.hasTextColor) {
            return fallback;
        }
        return snapshot.textColor;
    }

    // ─── End style-resolution utilities ───────────────────────────────────

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
        return (px >= 0 && px < m_width && py >= 0 && py < m_height);
    }

    /**
     * @brief Invalidates the SwWidget's rectangular area, marking it for redrawing.
     *
     * Computes the SwWidget's rectangle based on its position and size, then requests a redraw
     * through the active platform adapter.
     */
    virtual void invalidateRect() {
        SW_WIDGET_PERF_COUNT("invalidateRect");
        SwWidgetPlatformAdapter::invalidateRect(m_nativeWindowHandle, absoluteRect_());
    }


    int m_width;
    int m_height;
    int m_minWidth;
    int m_minHeight;
    int m_maxWidth;
    int m_maxHeight;
    int m_absX, m_absY;
    SwStyle *m_style;
    SwWidgetPlatformHandle m_nativeWindowHandle;
    SwWidget* m_parentWidgetCached{nullptr};
    SwWidget* m_mouseGrabberWidget{nullptr};
    unsigned int m_mouseGrabButtons{0};
    SwList<SwWidget*> m_hoveredPath_;

private:
    void ensureSwWidgetChildren_() {
        if (!m_swWidgetChildrenDirty) return;
        m_swWidgetChildren.clear();
        for (SwObject* obj : children()) {
            if (auto* w = dynamic_cast<SwWidget*>(obj)) {
                m_swWidgetChildren.push_back(w);
            }
        }
        m_swWidgetChildrenDirty = false;
    }

    void invalidateVisibilityCache_() {
        m_visibleInHierarchyDirty = true;
        // Propagate to children. Use children() + dynamic_cast to avoid
        // triggering ensureSwWidgetChildren_() which would rebuild the cache
        // at every level during construction (O(N²) with N widgets).
        for (SwObject* obj : children()) {
            if (auto* w = dynamic_cast<SwWidget*>(obj)) {
                w->invalidateVisibilityCache_();
            }
        }
    }

    bool m_visibilityEventInFlight{false};
    bool m_SuppressImplicitResizeUpdate_{false};
    SwString m_DefaultStyleSheet;
    StyleSheet m_DefaultComplexSheet;
    StyleSheet m_ComplexSheet;
    mutable StyleSheet m_EffectiveSheet;
    mutable bool m_EffectiveSheetDirty{true};
    mutable SwList<SwString> m_StyleSelectorsCache;
    mutable bool m_StyleSelectorsCacheInitialized{false};
    mutable ResolvedStyleSnapshot_ m_ResolvedStyleSnapshot;
    mutable bool m_ResolvedStyleSnapshotDirty{true};
    mutable bool m_cachedVisibleInHierarchy{true};
    mutable bool m_visibleInHierarchyDirty{true};
    bool m_swWidgetChildrenDirty{true};
    SwList<SwWidget*> m_swWidgetChildren;
    SwAbstractLayout* m_layout;

protected:
    static bool sharesNativeWindow_(const SwWidget* left, const SwWidget* right) {
        if (!left || !right) {
            return false;
        }
        return left->m_nativeWindowHandle.nativeHandle == right->m_nativeWindowHandle.nativeHandle &&
               left->m_nativeWindowHandle.nativeDisplay == right->m_nativeWindowHandle.nativeDisplay;
    }

    SwWidget* mouseGrabScopeRoot_() {
        SwWidget* root = this;
        while (auto* parentWidget = dynamic_cast<SwWidget*>(root->parent())) {
            if (!sharesNativeWindow_(parentWidget, root)) {
                break;
            }
            root = parentWidget;
        }
        return root;
    }

    const SwWidget* mouseGrabScopeRoot_() const {
        const SwWidget* root = this;
        while (const auto* parentWidget = dynamic_cast<const SwWidget*>(root->parent())) {
            if (!sharesNativeWindow_(parentWidget, root)) {
                break;
            }
            root = parentWidget;
        }
        return root;
    }

    SwWidget*& mouseGrabberWidget_() {
        return mouseGrabScopeRoot_()->m_mouseGrabberWidget;
    }

    unsigned int& mouseGrabButtons_() {
        return mouseGrabScopeRoot_()->m_mouseGrabButtons;
    }

    void releaseMouseGrabForHiddenHierarchy_() {
        SwWidget* mouseGrabRoot = mouseGrabScopeRoot_();
        if (!mouseGrabRoot || !mouseGrabRoot->m_mouseGrabberWidget) {
            return;
        }
        if (this == mouseGrabRoot ||
            mouseGrabRoot->m_mouseGrabberWidget == this ||
            belongsToHierarchy_(mouseGrabRoot->m_mouseGrabberWidget)) {
            mouseGrabRoot->m_mouseGrabberWidget = nullptr;
            mouseGrabRoot->m_mouseGrabButtons = 0;
        }
    }

    static unsigned int mouseButtonMask_(SwMouseButton button) {
        switch (button) {
        case SwMouseButton::Left:
            return 0x1u;
        case SwMouseButton::Right:
            return 0x2u;
        case SwMouseButton::Middle:
            return 0x4u;
        case SwMouseButton::Other:
            return 0x8u;
        case SwMouseButton::NoButton:
        default:
            return 0u;
        }
    }

    bool belongsToHierarchy_(const SwWidget* widget) const {
        const SwObject* current = widget;
        while (current) {
            if (current == this) {
                return true;
            }
            current = current->parent();
        }
        return false;
    }

    SwWidget* findFocusedDescendantByState_() {
        const auto& directChildren = children();
        for (int i = static_cast<int>(directChildren.size()) - 1; i >= 0; --i) {
            SwObject* obj = directChildren[static_cast<size_t>(i)];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }
            if (SwWidget* focusedChild = child->findFocusedDescendantByState_()) {
                return focusedChild;
            }
            if (child->getFocus()) {
                return child;
            }
        }
        return nullptr;
    }

    SwWidget* findFocusedWidgetInHierarchy_() {
        SwWidget* focused = findFocusedDescendantByState_();
        if (focused && focused->isVisibleInHierarchy()) {
            return focused;
        }

        focused = currentFocusOwnerInScope_();
        if (focused && focused->isVisibleInHierarchy() && belongsToHierarchy_(focused)) {
            return focused;
        }

        if (getFocus() && isVisibleInHierarchy()) {
            return this;
        }
        return nullptr;
    }

    void updateFocusForDispatch_(SwWidget* targetWidget) {
        if (!targetWidget || targetWidget->getFocusPolicy() == FocusPolicyEnum::NoFocus) {
            return;
        }
        targetWidget->setFocus(true);
    }

    void clearHoverPathOnRoot_() {
        SwList<SwWidget*>& hoveredPath = mouseGrabScopeRoot_()->m_hoveredPath_;
        for (size_t i = hoveredPath.size(); i > 0; --i) {
            SwWidget* widget = hoveredPath[i - 1];
            if (widget && SwObject::isLive(widget)) {
                widget->setHover(false);
            }
        }
        hoveredPath.clear();
    }

    void clearHoverRecursive_() {
        if (this == mouseGrabScopeRoot_()) {
            clearHoverPathOnRoot_();
            return;
        }

        setHover(false);
        const auto& directChildren = children();
        size_t i = 0;
        while (i < directChildren.size()) {
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child) {
                child->clearHoverRecursive_();
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }
    }

    SwWidget* buildHoverPathToPoint_(const SwPoint& pointInThis, SwList<SwWidget*>& pathOut) {
        if (!isVisibleInHierarchy() || !isPointInside(pointInThis.x, pointInThis.y)) {
            return nullptr;
        }

        pathOut.append(this);

        const auto& directChildren = children();
        for (int i = static_cast<int>(directChildren.size()) - 1; i >= 0; --i) {
            SwObject* obj = directChildren[static_cast<size_t>(i)];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }

            const SwPoint childLocalPoint = child->mapFromParent(pointInThis);
            if (!child->isPointInside(childLocalPoint.x, childLocalPoint.y)) {
                continue;
            }

            SwWidget* leaf = child->buildHoverPathToPoint_(childLocalPoint, pathOut);
            return leaf ? leaf : child;
        }

        return this;
    }

    SwWidget* updateHoverStateFromRoot_(const SwPoint& pointInThis) {
        SwWidget* hoverRoot = mouseGrabScopeRoot_();
        if (hoverRoot != this) {
            return hoverRoot->updateHoverStateFromRoot_(hoverRoot->mapFrom(this, pointInThis));
        }

        SwList<SwWidget*> newHoveredPath;
        SwWidget* hoveredLeaf = buildHoverPathToPoint_(pointInThis, newHoveredPath);

        SwList<SwWidget*>& oldHoveredPath = m_hoveredPath_;
        size_t commonPrefixSize = 0;
        while (commonPrefixSize < oldHoveredPath.size() &&
               commonPrefixSize < newHoveredPath.size()) {
            SwWidget* oldWidget = oldHoveredPath[commonPrefixSize];
            if (!oldWidget || !SwObject::isLive(oldWidget) || oldWidget != newHoveredPath[commonPrefixSize]) {
                break;
            }
            ++commonPrefixSize;
        }

        for (size_t i = oldHoveredPath.size(); i > commonPrefixSize; --i) {
            SwWidget* oldWidget = oldHoveredPath[i - 1];
            if (oldWidget && SwObject::isLive(oldWidget)) {
                oldWidget->setHover(false);
            }
        }

        for (size_t i = commonPrefixSize; i < newHoveredPath.size(); ++i) {
            SwWidget* newWidget = newHoveredPath[i];
            if (newWidget) {
                newWidget->setHover(true);
            }
        }

        oldHoveredPath = newHoveredPath;
        return hoveredLeaf;
    }

    bool deliverMouseEventToChain_(SwWidget* target, const MouseEvent& rootEvent, SwWidget** acceptedBy = nullptr) {
        SwWidget* current = target ? target : this;
        while (current) {
            MouseEvent localEvent = mapMouseEventToChild_(rootEvent, this, current);
            SwCoreApplication::sendEvent(current, &localEvent);
            if (localEvent.isAccepted()) {
                if (acceptedBy) {
                    *acceptedBy = current;
                }
                return true;
            }
            if (current == this) {
                break;
            }
            current = dynamic_cast<SwWidget*>(current->parent());
        }
        return false;
    }

    bool deliverWheelEventToChain_(SwWidget* target, const WheelEvent& rootEvent) {
        SwWidget* current = target ? target : this;
        while (current) {
            WheelEvent localEvent = mapWheelEventToChild_(rootEvent, this, current);
            SwCoreApplication::sendEvent(current, &localEvent);
            if (localEvent.isAccepted()) {
                return true;
            }
            if (current == this) {
                break;
            }
            current = dynamic_cast<SwWidget*>(current->parent());
        }
        return false;
    }

    bool deliverKeyEventToChain_(SwWidget* target, const KeyEvent& rootEvent) {
        SwWidget* current = target ? target : this;
        while (current) {
            KeyEvent localEvent = rootEvent;
            SwCoreApplication::sendEvent(current, &localEvent);
            if (localEvent.isAccepted()) {
                return true;
            }
            if (current == this) {
                break;
            }
            current = dynamic_cast<SwWidget*>(current->parent());
        }
        return false;
    }

    static MouseEvent mapMouseEventToChild_(const MouseEvent& event,
                                            const SwWidget* source,
                                            const SwWidget* child) {
        const SwPoint childPos = child ? child->mapFrom(source, event.pos()) : event.pos();
        MouseEvent mapped(event.type(),
                          childPos.x,
                          childPos.y,
                          event.button(),
                          event.isCtrlPressed(),
                          event.isShiftPressed(),
                          event.isAltPressed());
        mapped.setGlobalPos(event.globalPos());
        mapped.setDeltaX(event.getDeltaX());
        mapped.setDeltaY(event.getDeltaY());
        mapped.setSpeedX(event.getSpeedX());
        mapped.setSpeedY(event.getSpeedY());
        if (event.isAccepted()) {
            mapped.accept();
        }
        return mapped;
    }

    static WheelEvent mapWheelEventToChild_(const WheelEvent& event,
                                            const SwWidget* source,
                                            const SwWidget* child) {
        const SwPoint childPos = child ? child->mapFrom(source, event.pos()) : event.pos();
        WheelEvent mapped(childPos.x,
                          childPos.y,
                          event.delta(),
                          event.isCtrlPressed(),
                          event.isShiftPressed(),
                          event.isAltPressed());
        mapped.setGlobalPos(event.globalPos());
        if (event.isAccepted()) {
            mapped.accept();
        }
        return mapped;
    }

    void setFocusState_(bool value) {
        if (m_Focus == value) {
            return;
        }
        m_Focus = value;
        on_Focus_changed(value);
        emit FocusChanged(value);
    }

    void requestFocusOwnership_() {
        if (!isFocusEligible_()) {
            return;
        }

        const FocusScopeKey_ scopeKey = focusScopeKey_();
        SwWidget* previous = validatedFocusOwnerForKey_(scopeKey);
        if (previous == this) {
            setFocusState_(true);
            return;
        }

        if (previous) {
            focusOwnerMap_().remove(scopeKey);
            previous->setFocusState_(false);
            SwWidget* reboundOwner = validatedFocusOwnerForKey_(scopeKey);
            if (reboundOwner && reboundOwner != this) {
                setFocusState_(false);
                return;
            }
        }

        focusOwnerMap_()[scopeKey] = this;
        setFocusState_(true);
    }

    void releaseFocusOwnership_() {
        removeFocusOwnerEntriesForWidget_(this);
        setFocusState_(false);
    }

    bool isFocusEligible_() const {
        return getEnable() && getFocusPolicy() != FocusPolicyEnum::NoFocus;
    }

    void clearFocusInHierarchy_() {
        if (getFocus()) {
            setFocus(false);
        }
        const auto& directChildren = children();
        size_t i = 0;
        while (i < directChildren.size()) {
            SwObject* obj = directChildren[i];
            SwWidget* child = obj ? dynamic_cast<SwWidget*>(obj) : nullptr;
            if (child) {
                child->clearFocusInHierarchy_();
            }
            if (i < directChildren.size() && directChildren[i] == obj) {
                ++i;
            }
        }
    }

    struct FocusScopeKey_ {
        const void* nativeHandle{nullptr};
        const void* nativeDisplay{nullptr};
        const SwWidget* logicalRoot{nullptr};

        FocusScopeKey_() {}

        FocusScopeKey_(const void* handle, const void* display, const SwWidget* root)
            : nativeHandle(handle)
            , nativeDisplay(display)
            , logicalRoot(root) {}

        bool operator==(const FocusScopeKey_& other) const {
            return nativeHandle == other.nativeHandle &&
                   nativeDisplay == other.nativeDisplay &&
                   logicalRoot == other.logicalRoot;
        }

        bool operator!=(const FocusScopeKey_& other) const {
            return !(*this == other);
        }

        bool operator<(const FocusScopeKey_& other) const {
            if (nativeDisplay != other.nativeDisplay) {
                return nativeDisplay < other.nativeDisplay;
            }
            if (nativeHandle != other.nativeHandle) {
                return nativeHandle < other.nativeHandle;
            }
            return logicalRoot < other.logicalRoot;
        }
    };

    FocusScopeKey_ focusScopeKey_() const {
        if (m_nativeWindowHandle.nativeHandle) {
            return FocusScopeKey_{m_nativeWindowHandle.nativeHandle, m_nativeWindowHandle.nativeDisplay, nullptr};
        }
        return FocusScopeKey_{nullptr, nullptr, focusScopeRoot_()};
    }

    SwWidget* focusScopeRoot_() const {
        SwWidget* root = const_cast<SwWidget*>(this);
        while (true) {
            SwWidget* parentWidget = dynamic_cast<SwWidget*>(root->parent());
            if (!parentWidget) {
                return root;
            }
            root = parentWidget;
        }
    }

    static SwMap<FocusScopeKey_, SwWidget*>& focusOwnerMap_() {
        static SwMap<FocusScopeKey_, SwWidget*> s_focusOwners;
        return s_focusOwners;
    }

    static void removeFocusOwnerEntriesForWidget_(const SwWidget* widget) {
        if (!widget) {
            return;
        }
        auto& owners = focusOwnerMap_();
        for (auto it = owners.begin(); it != owners.end();) {
            if (it->second == widget) {
                it = owners.erase(it);
            } else {
                ++it;
            }
        }
    }

    static SwWidget* validatedFocusOwnerForKey_(const FocusScopeKey_& scopeKey) {
        if (!scopeKey.nativeHandle && !scopeKey.logicalRoot) {
            return nullptr;
        }
        auto& owners = focusOwnerMap_();
        auto it = owners.find(scopeKey);
        if (it == owners.end()) {
            return nullptr;
        }

        SwWidget* owner = it->second;
        if (!owner || !SwObject::isLive(owner)) {
            owners.erase(it);
            return nullptr;
        }
        if (owner->focusScopeKey_() != scopeKey || !owner->isFocusEligible_()) {
            owners.erase(it);
            owner->setFocusState_(false);
            return nullptr;
        }
        return owner;
    }

    SwWidget* currentFocusOwnerInScope_() {
        return validatedFocusOwnerForKey_(focusScopeKey_());
    }

    void rebindFocusOwnership_() {
        if (!m_Focus) {
            return;
        }
        removeFocusOwnerEntriesForWidget_(this);
        focusOwnerMap_()[focusScopeKey_()] = this;
    }

    void setNativeWindowHandle(const SwWidgetPlatformHandle& handle) {
        m_nativeWindowHandle = handle;
    }

    void setNativeWindowHandleRecursive(const SwWidgetPlatformHandle& handle) {
        setNativeWindowHandle(handle);
        if (m_Focus) {
            rebindFocusOwnership_();
        }
        const auto& directChildren = children();
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



