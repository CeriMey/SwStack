#pragma once

/**
 * @file src/core/gui/graphics/SwGraphicsSceneEvent.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwGraphicsSceneEvent in the CoreSw graphics
 * layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the graphics scene event interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwGraphicsSceneEventType, SwGraphicsSceneEvent,
 * SwGraphicsSceneMouseEvent, SwGraphicsSceneWheelEvent, SwGraphicsSceneHoverEvent,
 * SwGraphicsSceneContextMenuEvent, SwGraphicsSceneDragDropEvent, and SwGraphicsSceneMoveEvent,
 * plus related helper declarations.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Graphics-facing declarations here define the data flow from high-level UI state to lower-level
 * rendering backends.
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

/***************************************************************************************************
 * SwGraphicsSceneEvent — Qt-like event classes for graphics items.
 *
 *   SwGraphicsSceneEvent          (base)
 *   SwGraphicsSceneMouseEvent     (press / move / release / double-click)
 *   SwGraphicsSceneKeyEvent       (press / release)
 *   SwGraphicsSceneWheelEvent
 *   SwGraphicsSceneHoverEvent     (enter / move / leave)
 *   SwGraphicsSceneContextMenuEvent
 *   SwGraphicsSceneDragDropEvent  (drag-enter / move / leave / drop)
 *   SwGraphicsSceneMoveEvent      (item moved)
 *   SwGraphicsSceneResizeEvent    (proxy widget resized)
 *   SwFocusEvent                  (focus in/out)
 **************************************************************************************************/

/**
 * @file
 * @brief Declares the event payloads exchanged by graphics views, scenes, and items.
 *
 * This header defines the lightweight event objects used by the scene-graph
 * layer to forward mouse, wheel, hover, drag-and-drop, context-menu, move,
 * resize, and focus notifications. The structure of the API intentionally
 * follows Qt's graphics-view event family so custom items can be ported or
 * reasoned about with familiar concepts.
 */

#include "SwGraphicsTypes.h"
#include "Sw.h"
#include "SwEvent.h"
#include "platform/SwPlatformIntegration.h"

class SwWidget;
class SwGraphicsItem;
class SwGraphicsScene;
class KeyEvent;

// ---------------------------------------------------------------------------
// Event type enum for graphics scene events
// ---------------------------------------------------------------------------
enum class SwGraphicsSceneEventType {
    NoEvent,
    GraphicsSceneMousePress,
    GraphicsSceneMouseRelease,
    GraphicsSceneMouseMove,
    GraphicsSceneMouseDoubleClick,
    GraphicsSceneKeyPress,
    GraphicsSceneKeyRelease,
    GraphicsSceneWheel,
    GraphicsSceneHoverEnter,
    GraphicsSceneHoverMove,
    GraphicsSceneHoverLeave,
    GraphicsSceneContextMenu,
    GraphicsSceneDragEnter,
    GraphicsSceneDragMove,
    GraphicsSceneDragLeave,
    GraphicsSceneDrop,
    GraphicsSceneMove,
    GraphicsSceneResize,
    FocusIn,
    FocusOut
};

enum class SwGraphicsSceneKeyDispatchType {
    Press,
    Release
};

// ---------------------------------------------------------------------------
// Keyboard modifier flags (matching Qt::KeyboardModifier)
// ---------------------------------------------------------------------------
namespace SwKeyboardModifier {
    enum Modifier {
        NoModifier   = 0x00,
        ShiftModifier   = 0x01,
        ControlModifier = 0x02,
        AltModifier     = 0x04,
        MetaModifier    = 0x08
    };
}

// ---------------------------------------------------------------------------
// Mouse button flags
// ---------------------------------------------------------------------------
namespace SwMouseButtons {
    enum Button {
        NoButton     = 0x00,
        LeftButton   = 0x01,
        RightButton  = 0x02,
        MiddleButton = 0x04
    };
}

// ---------------------------------------------------------------------------
// SwGraphicsSceneEvent (base)
// ---------------------------------------------------------------------------
/**
 * @brief Base class for all graphics-scene specific events.
 */
class SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneEvent` instance.
     * @param type Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwGraphicsSceneEvent(SwGraphicsSceneEventType type)
        : m_type(type) {}
    /**
     * @brief Destroys the `SwGraphicsSceneEvent` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwGraphicsSceneEvent() = default;

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwGraphicsSceneEventType type() const { return m_type; }

    /**
     * @brief Performs the `accept` operation.
     */
    void accept() { m_accepted = true; }
    /**
     * @brief Performs the `ignore` operation.
     */
    void ignore() { m_accepted = false; }
    /**
     * @brief Returns whether the object reports accepted.
     * @return `true` when the object reports accepted; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isAccepted() const { return m_accepted; }
    /**
     * @brief Sets the accepted.
     * @param a Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAccepted(bool a) { m_accepted = a; }

    /**
     * @brief Returns the current widget.
     * @return The current widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* widget() const { return m_widget; }
    /**
     * @brief Sets the widget.
     * @param w Width value.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWidget(SwWidget* w) { m_widget = w; }

private:
    SwGraphicsSceneEventType m_type{SwGraphicsSceneEventType::NoEvent};
    bool m_accepted{false};
    SwWidget* m_widget{nullptr};
};

/**
 * @brief Kernel-visible wrapper used while a scene dispatches input toward graphics items.
 *
 * This event is synchronous-only: it borrows the wrapped payload for the duration of the
 * current `SwCoreApplication::sendEvent` call and must not be posted.
 */
class SwGraphicsSceneDispatchEvent : public SwEvent {
public:
    SwGraphicsSceneDispatchEvent(SwGraphicsScene* scene,
                                 SwGraphicsItem* targetItem,
                                 SwGraphicsSceneEvent* graphicsEvent)
        : SwEvent(EventType::GraphicsSceneDispatch)
        , m_scene(scene)
        , m_targetItem(targetItem)
        , m_graphicsEvent(graphicsEvent) {}

    SwGraphicsSceneDispatchEvent(SwGraphicsScene* scene,
                                 SwGraphicsItem* targetItem,
                                 KeyEvent* keyEvent,
                                 SwGraphicsSceneKeyDispatchType keyDispatchType)
        : SwEvent(EventType::GraphicsSceneDispatch)
        , m_scene(scene)
        , m_targetItem(targetItem)
        , m_keyEvent(keyEvent)
        , m_keyDispatchType(keyDispatchType) {}

    SwGraphicsScene* scene() const { return m_scene; }
    SwGraphicsItem* targetItem() const { return m_targetItem; }

    SwGraphicsSceneEvent* graphicsEvent() const { return m_graphicsEvent; }
    bool hasGraphicsEvent() const { return m_graphicsEvent != nullptr; }

    KeyEvent* keyEvent() const { return m_keyEvent; }
    bool hasKeyEvent() const { return m_keyEvent != nullptr; }
    SwGraphicsSceneKeyDispatchType keyDispatchType() const { return m_keyDispatchType; }

private:
    SwGraphicsScene* m_scene{nullptr};
    SwGraphicsItem* m_targetItem{nullptr};
    SwGraphicsSceneEvent* m_graphicsEvent{nullptr};
    KeyEvent* m_keyEvent{nullptr};
    SwGraphicsSceneKeyDispatchType m_keyDispatchType{SwGraphicsSceneKeyDispatchType::Press};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneMouseEvent
// ---------------------------------------------------------------------------
class SwGraphicsSceneMouseEvent : public SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneMouseEvent` instance.
     * @param type Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwGraphicsSceneMouseEvent(SwGraphicsSceneEventType type)
        : SwGraphicsSceneEvent(type) {}

    // Position in item-local coordinates
    /**
     * @brief Returns the current pos.
     * @return The current pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF pos() const { return m_pos; }
    /**
     * @brief Sets the pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(const SwPointF& p) { m_pos = p; }

    // Position in scene coordinates
    /**
     * @brief Returns the current scene Pos.
     * @return The current scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF scenePos() const { return m_scenePos; }
    /**
     * @brief Sets the scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScenePos(const SwPointF& p) { m_scenePos = p; }

    // Position in screen coordinates
    /**
     * @brief Returns the current screen Pos.
     * @return The current screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint screenPos() const { return m_screenPos; }
    /**
     * @brief Sets the screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScreenPos(const SwPoint& p) { m_screenPos = p; }

    // Button that triggered the event
    /**
     * @brief Returns the current button.
     * @return The current button.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMouseButton button() const { return m_button; }
    /**
     * @brief Sets the button.
     * @param b Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setButton(SwMouseButton b) { m_button = b; }

    // Buttons currently pressed (bitmask of SwMouseButtons::Button)
    /**
     * @brief Returns the current buttons.
     * @return The current buttons.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int buttons() const { return m_buttons; }
    /**
     * @brief Sets the buttons.
     * @param b Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setButtons(int b) { m_buttons = b; }

    // Keyboard modifiers (bitmask of SwKeyboardModifier::Modifier)
    /**
     * @brief Returns the current modifiers.
     * @return The current modifiers.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int modifiers() const { return m_modifiers; }
    /**
     * @brief Sets the modifiers.
     * @param m Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModifiers(int m) { m_modifiers = m; }

    // Last position (for move events)
    /**
     * @brief Returns the current last Pos.
     * @return The current last Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF lastPos() const { return m_lastPos; }
    /**
     * @brief Sets the last Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLastPos(const SwPointF& p) { m_lastPos = p; }

    /**
     * @brief Returns the current last Scene Pos.
     * @return The current last Scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF lastScenePos() const { return m_lastScenePos; }
    /**
     * @brief Sets the last Scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLastScenePos(const SwPointF& p) { m_lastScenePos = p; }

    /**
     * @brief Returns the current last Screen Pos.
     * @return The current last Screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint lastScreenPos() const { return m_lastScreenPos; }
    /**
     * @brief Sets the last Screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLastScreenPos(const SwPoint& p) { m_lastScreenPos = p; }

    // Button-down positions
    /**
     * @brief Performs the `buttonDownPos` operation.
     * @param SwMouseButton Value passed to the method.
     * @return The requested button Down Pos.
     */
    SwPointF buttonDownPos(SwMouseButton /*btn*/) const { return m_buttonDownPos; }
    /**
     * @brief Sets the button Down Pos.
     * @param SwMouseButton Value passed to the method.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setButtonDownPos(SwMouseButton /*btn*/, const SwPointF& p) { m_buttonDownPos = p; }

    /**
     * @brief Performs the `buttonDownScenePos` operation.
     * @param SwMouseButton Value passed to the method.
     * @return The requested button Down Scene Pos.
     */
    SwPointF buttonDownScenePos(SwMouseButton /*btn*/) const { return m_buttonDownScenePos; }
    /**
     * @brief Sets the button Down Scene Pos.
     * @param SwMouseButton Value passed to the method.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setButtonDownScenePos(SwMouseButton /*btn*/, const SwPointF& p) { m_buttonDownScenePos = p; }

    /**
     * @brief Performs the `buttonDownScreenPos` operation.
     * @param SwMouseButton Value passed to the method.
     * @return The requested button Down Screen Pos.
     */
    SwPoint buttonDownScreenPos(SwMouseButton /*btn*/) const { return m_buttonDownScreenPos; }
    /**
     * @brief Sets the button Down Screen Pos.
     * @param SwMouseButton Value passed to the method.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setButtonDownScreenPos(SwMouseButton /*btn*/, const SwPoint& p) { m_buttonDownScreenPos = p; }

private:
    SwPointF m_pos{};
    SwPointF m_scenePos{};
    SwPoint m_screenPos{0, 0};
    SwMouseButton m_button{SwMouseButton::NoButton};
    int m_buttons{0};
    int m_modifiers{0};
    SwPointF m_lastPos{};
    SwPointF m_lastScenePos{};
    SwPoint m_lastScreenPos{0, 0};
    SwPointF m_buttonDownPos{};
    SwPointF m_buttonDownScenePos{};
    SwPoint m_buttonDownScreenPos{0, 0};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneKeyEvent
// ---------------------------------------------------------------------------
class SwGraphicsSceneKeyEvent : public SwGraphicsSceneEvent {
public:
    explicit SwGraphicsSceneKeyEvent(SwGraphicsSceneEventType type)
        : SwGraphicsSceneEvent(type) {}

    int key() const { return m_key; }
    void setKey(int key) { m_key = key; }

    bool isCtrlPressed() const { return m_ctrlPressed; }
    void setCtrlPressed(bool pressed) { m_ctrlPressed = pressed; }

    bool isShiftPressed() const { return m_shiftPressed; }
    void setShiftPressed(bool pressed) { m_shiftPressed = pressed; }

    bool isAltPressed() const { return m_altPressed; }
    void setAltPressed(bool pressed) { m_altPressed = pressed; }

    wchar_t text() const { return m_textChar; }
    void setText(wchar_t text) { m_textChar = text; }

    bool isTextProvided() const { return m_textProvided; }
    void setTextProvided(bool provided) { m_textProvided = provided; }

private:
    int m_key{0};
    bool m_ctrlPressed{false};
    bool m_shiftPressed{false};
    bool m_altPressed{false};
    wchar_t m_textChar{L'\0'};
    bool m_textProvided{false};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneWheelEvent
// ---------------------------------------------------------------------------
class SwGraphicsSceneWheelEvent : public SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneWheelEvent` instance.
     * @param GraphicsSceneWheel Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGraphicsSceneWheelEvent()
        : SwGraphicsSceneEvent(SwGraphicsSceneEventType::GraphicsSceneWheel) {}

    /**
     * @brief Returns the current pos.
     * @return The current pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF pos() const { return m_pos; }
    /**
     * @brief Sets the pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(const SwPointF& p) { m_pos = p; }

    /**
     * @brief Returns the current scene Pos.
     * @return The current scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF scenePos() const { return m_scenePos; }
    /**
     * @brief Sets the scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScenePos(const SwPointF& p) { m_scenePos = p; }

    /**
     * @brief Returns the current screen Pos.
     * @return The current screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint screenPos() const { return m_screenPos; }
    /**
     * @brief Sets the screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScreenPos(const SwPoint& p) { m_screenPos = p; }

    /**
     * @brief Returns the current delta.
     * @return The current delta.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int delta() const { return m_delta; }
    /**
     * @brief Sets the delta.
     * @param d Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDelta(int d) { m_delta = d; }

    /**
     * @brief Returns the current modifiers.
     * @return The current modifiers.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int modifiers() const { return m_modifiers; }
    /**
     * @brief Sets the modifiers.
     * @param m Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModifiers(int m) { m_modifiers = m; }

    /**
     * @brief Returns the current orientation.
     * @return The current orientation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int orientation() const { return m_orientation; }
    /**
     * @brief Sets the orientation.
     * @param o Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOrientation(int o) { m_orientation = o; }

private:
    SwPointF m_pos{};
    SwPointF m_scenePos{};
    SwPoint m_screenPos{0, 0};
    int m_delta{0};
    int m_modifiers{0};
    int m_orientation{0}; // 0 = vertical, 1 = horizontal
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneHoverEvent
// ---------------------------------------------------------------------------
class SwGraphicsSceneHoverEvent : public SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneHoverEvent` instance.
     * @param type Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwGraphicsSceneHoverEvent(SwGraphicsSceneEventType type)
        : SwGraphicsSceneEvent(type) {}

    /**
     * @brief Returns the current pos.
     * @return The current pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF pos() const { return m_pos; }
    /**
     * @brief Sets the pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(const SwPointF& p) { m_pos = p; }

    /**
     * @brief Returns the current scene Pos.
     * @return The current scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF scenePos() const { return m_scenePos; }
    /**
     * @brief Sets the scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScenePos(const SwPointF& p) { m_scenePos = p; }

    /**
     * @brief Returns the current screen Pos.
     * @return The current screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint screenPos() const { return m_screenPos; }
    /**
     * @brief Sets the screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScreenPos(const SwPoint& p) { m_screenPos = p; }

    /**
     * @brief Returns the current last Pos.
     * @return The current last Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF lastPos() const { return m_lastPos; }
    /**
     * @brief Sets the last Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLastPos(const SwPointF& p) { m_lastPos = p; }

    /**
     * @brief Returns the current last Scene Pos.
     * @return The current last Scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF lastScenePos() const { return m_lastScenePos; }
    /**
     * @brief Sets the last Scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLastScenePos(const SwPointF& p) { m_lastScenePos = p; }

    /**
     * @brief Returns the current last Screen Pos.
     * @return The current last Screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint lastScreenPos() const { return m_lastScreenPos; }
    /**
     * @brief Sets the last Screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLastScreenPos(const SwPoint& p) { m_lastScreenPos = p; }

private:
    SwPointF m_pos{};
    SwPointF m_scenePos{};
    SwPoint m_screenPos{0, 0};
    SwPointF m_lastPos{};
    SwPointF m_lastScenePos{};
    SwPoint m_lastScreenPos{0, 0};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneContextMenuEvent
// ---------------------------------------------------------------------------
class SwGraphicsSceneContextMenuEvent : public SwGraphicsSceneEvent {
public:
    enum Reason { Mouse, Keyboard, Other };

    /**
     * @brief Constructs a `SwGraphicsSceneContextMenuEvent` instance.
     * @param GraphicsSceneContextMenu Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGraphicsSceneContextMenuEvent()
        : SwGraphicsSceneEvent(SwGraphicsSceneEventType::GraphicsSceneContextMenu) {}

    /**
     * @brief Returns the current pos.
     * @return The current pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF pos() const { return m_pos; }
    /**
     * @brief Sets the pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(const SwPointF& p) { m_pos = p; }

    /**
     * @brief Returns the current scene Pos.
     * @return The current scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF scenePos() const { return m_scenePos; }
    /**
     * @brief Sets the scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScenePos(const SwPointF& p) { m_scenePos = p; }

    /**
     * @brief Returns the current screen Pos.
     * @return The current screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint screenPos() const { return m_screenPos; }
    /**
     * @brief Sets the screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScreenPos(const SwPoint& p) { m_screenPos = p; }

    /**
     * @brief Returns the current modifiers.
     * @return The current modifiers.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int modifiers() const { return m_modifiers; }
    /**
     * @brief Sets the modifiers.
     * @param m Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModifiers(int m) { m_modifiers = m; }

    /**
     * @brief Returns the current reason.
     * @return The current reason.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Reason reason() const { return m_reason; }
    /**
     * @brief Sets the reason.
     * @param r Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setReason(Reason r) { m_reason = r; }

private:
    SwPointF m_pos{};
    SwPointF m_scenePos{};
    SwPoint m_screenPos{0, 0};
    int m_modifiers{0};
    Reason m_reason{Mouse};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneDragDropEvent
// ---------------------------------------------------------------------------
class SwGraphicsSceneDragDropEvent : public SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneDragDropEvent` instance.
     * @param type Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwGraphicsSceneDragDropEvent(SwGraphicsSceneEventType type)
        : SwGraphicsSceneEvent(type) {}

    /**
     * @brief Returns the current pos.
     * @return The current pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF pos() const { return m_pos; }
    /**
     * @brief Sets the pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPos(const SwPointF& p) { m_pos = p; }

    /**
     * @brief Returns the current scene Pos.
     * @return The current scene Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF scenePos() const { return m_scenePos; }
    /**
     * @brief Sets the scene Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScenePos(const SwPointF& p) { m_scenePos = p; }

    /**
     * @brief Returns the current screen Pos.
     * @return The current screen Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPoint screenPos() const { return m_screenPos; }
    /**
     * @brief Sets the screen Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScreenPos(const SwPoint& p) { m_screenPos = p; }

    /**
     * @brief Returns the current buttons.
     * @return The current buttons.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int buttons() const { return m_buttons; }
    /**
     * @brief Sets the buttons.
     * @param b Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setButtons(int b) { m_buttons = b; }

    /**
     * @brief Returns the current modifiers.
     * @return The current modifiers.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int modifiers() const { return m_modifiers; }
    /**
     * @brief Sets the modifiers.
     * @param m Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModifiers(int m) { m_modifiers = m; }

    /**
     * @brief Performs the `acceptProposedAction` operation.
     * @param m_proposedAccepted Value passed to the method.
     */
    void acceptProposedAction() { m_proposedAccepted = true; accept(); }
    /**
     * @brief Returns whether the object reports proposed Action Accepted.
     * @return `true` when the object reports proposed Action Accepted; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isProposedActionAccepted() const { return m_proposedAccepted; }

    enum DropAction { CopyAction, MoveAction, LinkAction, IgnoreAction };
    /**
     * @brief Returns the current proposed Action.
     * @return The current proposed Action.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    DropAction proposedAction() const { return m_proposedAction; }
    /**
     * @brief Sets the proposed Action.
     * @param a Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setProposedAction(DropAction a) { m_proposedAction = a; }
    /**
     * @brief Returns the current drop Action.
     * @return The current drop Action.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    DropAction dropAction() const { return m_dropAction; }
    /**
     * @brief Sets the drop Action.
     * @param a Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDropAction(DropAction a) { m_dropAction = a; }

private:
    SwPointF m_pos{};
    SwPointF m_scenePos{};
    SwPoint m_screenPos{0, 0};
    int m_buttons{0};
    int m_modifiers{0};
    bool m_proposedAccepted{false};
    DropAction m_proposedAction{CopyAction};
    DropAction m_dropAction{CopyAction};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneMoveEvent  (item pos changed)
// ---------------------------------------------------------------------------
class SwGraphicsSceneMoveEvent : public SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneMoveEvent` instance.
     * @param GraphicsSceneMove Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGraphicsSceneMoveEvent()
        : SwGraphicsSceneEvent(SwGraphicsSceneEventType::GraphicsSceneMove) {}

    /**
     * @brief Returns the current old Pos.
     * @return The current old Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF oldPos() const { return m_oldPos; }
    /**
     * @brief Sets the old Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOldPos(const SwPointF& p) { m_oldPos = p; }

    /**
     * @brief Returns the current new Pos.
     * @return The current new Pos.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF newPos() const { return m_newPos; }
    /**
     * @brief Sets the new Pos.
     * @param p Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setNewPos(const SwPointF& p) { m_newPos = p; }

private:
    SwPointF m_oldPos{};
    SwPointF m_newPos{};
};

// ---------------------------------------------------------------------------
// SwGraphicsSceneResizeEvent  (proxy widget resized)
// ---------------------------------------------------------------------------
class SwGraphicsSceneResizeEvent : public SwGraphicsSceneEvent {
public:
    /**
     * @brief Constructs a `SwGraphicsSceneResizeEvent` instance.
     * @param GraphicsSceneResize Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwGraphicsSceneResizeEvent()
        : SwGraphicsSceneEvent(SwGraphicsSceneEventType::GraphicsSceneResize) {}

    /**
     * @brief Returns the current old Size.
     * @return The current old Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSizeF oldSize() const { return m_oldSize; }
    /**
     * @brief Sets the old Size.
     * @param s Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOldSize(const SwSizeF& s) { m_oldSize = s; }

    /**
     * @brief Returns the current new Size.
     * @return The current new Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSizeF newSize() const { return m_newSize; }
    /**
     * @brief Sets the new Size.
     * @param s Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setNewSize(const SwSizeF& s) { m_newSize = s; }

private:
    SwSizeF m_oldSize{};
    SwSizeF m_newSize{};
};

// ---------------------------------------------------------------------------
// SwFocusEvent  (focus in / out)
// ---------------------------------------------------------------------------
class SwFocusEvent : public SwGraphicsSceneEvent {
public:
    enum FocusReason {
        MouseFocusReason,
        TabFocusReason,
        BacktabFocusReason,
        ActiveWindowFocusReason,
        PopupFocusReason,
        ShortcutFocusReason,
        MenuBarFocusReason,
        OtherFocusReason,
        NoFocusReason
    };

    /**
     * @brief Constructs a `SwFocusEvent` instance.
     * @param type Value passed to the method.
     * @param reason Value passed to the method.
     * @param reason Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwFocusEvent(SwGraphicsSceneEventType type, FocusReason reason = OtherFocusReason)
        : SwGraphicsSceneEvent(type), m_reason(reason) {}

    /**
     * @brief Returns the current reason.
     * @return The current reason.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    FocusReason reason() const { return m_reason; }

    /**
     * @brief Performs the `gotFocus` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool gotFocus() const { return type() == SwGraphicsSceneEventType::FocusIn; }
    /**
     * @brief Performs the `lostFocus` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool lostFocus() const { return type() == SwGraphicsSceneEventType::FocusOut; }

private:
    FocusReason m_reason{OtherFocusReason};
};
