#pragma once

/**
 * @file src/core/object/SwEvent.h
 * @ingroup core_object
 * @brief Declares the event base types shared by the SwStack object and widget layers.
 */

class SwObject;

enum class EventType : int {
    None = 0,
    Timer,
    DeferredDelete,
    ChildAdded,
    ChildRemoved,
    Paint,
    Resize,
    Move,
    KeyPressEvent,
    KeyReleaseEvent,
    MousePressEvent,
    MouseDoubleClickEvent,
    MouseMoveEvent,
    MouseReleaseEvent,
    WheelEvent,
    Show,
    Hide,
    Close,
    GraphicsSceneDispatch = 900,
    User = 1000
};

class SwEvent {
public:
    explicit SwEvent(EventType type, bool spontaneous = false)
        : m_type(type)
        , m_accepted(false)
        , m_spontaneous(spontaneous)
        , m_kernelDispatched(false) {}

    virtual ~SwEvent() = default;

    EventType type() const { return m_type; }

    void accept() { m_accepted = true; }
    void ignore() { m_accepted = false; }
    bool isAccepted() const { return m_accepted; }
    void setAccepted(bool accepted) { m_accepted = accepted; }

    bool spontaneous() const { return m_spontaneous; }
    void setSpontaneous(bool spontaneous) { m_spontaneous = spontaneous; }

    bool isKernelDispatched() const { return m_kernelDispatched; }
    void setKernelDispatched(bool dispatched) { m_kernelDispatched = dispatched; }

    virtual SwEvent* clone() const { return new SwEvent(*this); }

private:
    EventType m_type;
    bool m_accepted;
    bool m_spontaneous;
    bool m_kernelDispatched;
};

class SwDeferredDeleteEvent : public SwEvent {
public:
    SwDeferredDeleteEvent()
        : SwEvent(EventType::DeferredDelete) {}

    SwDeferredDeleteEvent* clone() const override { return new SwDeferredDeleteEvent(*this); }
};

class SwChildEvent : public SwEvent {
public:
    SwChildEvent(EventType type, SwObject* child)
        : SwEvent(type)
        , m_child(child) {}

    SwObject* child() const { return m_child; }

    SwChildEvent* clone() const override { return new SwChildEvent(*this); }

private:
    SwObject* m_child{nullptr};
};
