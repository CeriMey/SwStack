#pragma once

#if !SW_PLATFORM_WIN32
#error "SwQtBindingWin32WidgetHost is only available on Win32."
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>

#include "SwCoreApplication.h"
#include "SwShortcut.h"
#include "SwToolTip.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"
#include "platform/win/SwWin32Painter.h"
#include "platform/win/SwWindows.h"

class SwQtBindingWin32WidgetHost {
public:
    explicit SwQtBindingWin32WidgetHost(SwWidget* root = nullptr)
        : root_(root) {
    }

    void setRootWidget(SwWidget* root) {
        root_ = root;
        if (root_ && hostHandle_) {
            root_->bindToNativeWindowRecursive(hostHandle_);
        }
    }

    SwWidget* rootWidget() const {
        return root_;
    }

    void setHostHandle(const SwWidgetPlatformHandle& hostHandle) {
        hostHandle_ = hostHandle;
        if (root_ && hostHandle_) {
            root_->bindToNativeWindowRecursive(hostHandle_);
        }
    }

    void setHostWindowHandle(HWND hwnd) {
        setHostHandle(hwnd ? SwWidgetPlatformAdapter::fromNativeHandle(hwnd) : SwWidgetPlatformHandle{});
    }

    const SwWidgetPlatformHandle& hostHandle() const {
        return hostHandle_;
    }

    HWND hostWindowHandle() const {
        return nativeHostHandle_();
    }

    void attach() {
        if (!root_ || !hostHandle_) {
            return;
        }
        root_->bindToNativeWindowRecursive(hostHandle_);
    }

    void detach() {
        if (!root_) {
            return;
        }
        root_->unbindFromNativeWindowRecursive();
    }

    void syncRootGeometryToHostClientRect(int fallbackWidth = 0, int fallbackHeight = 0) {
        if (!root_) {
            return;
        }

        if (!hostHandle_) {
            root_->setGeometry(0, 0, std::max(1, fallbackWidth), std::max(1, fallbackHeight));
            return;
        }

        const SwRect clientRect = SwWidgetPlatformAdapter::clientRect(hostHandle_);
        root_->setGeometry(0,
                           0,
                           std::max(1, clientRect.width),
                           std::max(1, clientRect.height));
    }

    void shutdown() {
        SwToolTip::hideText();
        const HWND hwnd = nativeHostHandle_();
        if (hwnd && GetCapture() == hwnd) {
            ReleaseCapture();
        }
        detach();
        root_ = nullptr;
        hostHandle_ = SwWidgetPlatformHandle{};
        lastMoveTime_ = std::chrono::steady_clock::time_point{};
        lastMousePosition_ = SwPoint{0, 0};
    }

    bool handleMessage(const MSG* msg, intptr_t* result) {
        const HWND hwnd = nativeHostHandle_();
        if (!root_ || !hwnd || !msg || msg->hwnd != hwnd) {
            return false;
        }

        switch (msg->message) {
        case WM_ERASEBKGND:
            if (result) {
                *result = 1;
            }
            return true;
        case WM_PAINT:
            paintHost_();
            if (result) {
                *result = 0;
            }
            return true;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (GetCapture() != hwnd) {
                SetCapture(hwnd);
            }
            SetFocus(hwnd);
            SwToolTip::handleMousePress();
            dispatchMouseButton_(msg, EventType::MousePressEvent);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
            dispatchMouseButton_(msg, EventType::MouseDoubleClickEvent);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            dispatchMouseButton_(msg, EventType::MouseReleaseEvent);
            if ((static_cast<UINT>(msg->wParam) & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0 && GetCapture() == hwnd) {
                ReleaseCapture();
            }
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSEMOVE:
            trackMouseLeave_();
            dispatchMouseMove_(msg);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSELEAVE:
            dispatchMouseLeave_();
            lastMoveTime_ = std::chrono::steady_clock::time_point{};
            SwToolTip::hideText();
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSEWHEEL:
            dispatchMouseWheel_(msg, false);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_MOUSEHWHEEL:
            dispatchMouseWheel_(msg, true);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            dispatchKeyPress_(msg, L'\0', true);
            if (result) {
                *result = 0;
            }
            return true;
        case WM_CHAR:
        case WM_SYSCHAR: {
            const wchar_t ch = static_cast<wchar_t>(msg->wParam);
            if (ch >= 0x20) {
                dispatchKeyPress_(msg, ch, true);
            }
            if (result) {
                *result = 0;
            }
            return true;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            dispatchKeyRelease_(msg);
            if (result) {
                *result = 0;
            }
            return true;
        default:
            return false;
        }
    }

private:
    HWND nativeHostHandle_() const {
        return SwWidgetPlatformAdapter::nativeHandleAs<HWND>(hostHandle_);
    }

    void paintHost_() {
        const HWND hwnd = nativeHostHandle_();
        if (!hwnd) {
            return;
        }

        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) {
            return;
        }

        const SwRect clientRect = SwWidgetPlatformAdapter::clientRect(hostHandle_);
        SwPlatformPaintEvent platformPaintEvent;
        platformPaintEvent.nativePaintDevice = hdc;
        platformPaintEvent.nativeWindowHandle = hwnd;
        platformPaintEvent.surfaceSize = SwPlatformSize{std::max(1, clientRect.width), std::max(1, clientRect.height)};
        platformPaintEvent.dirtyRect = SwPlatformRect{
            ps.rcPaint.left,
            ps.rcPaint.top,
            std::max(0, static_cast<int>(ps.rcPaint.right - ps.rcPaint.left)),
            std::max(0, static_cast<int>(ps.rcPaint.bottom - ps.rcPaint.top))
        };

        SwWin32Painter painter;
        painter.begin(platformPaintEvent);
        PaintEvent widgetPaintEvent(&painter, SwRect{0, 0, platformPaintEvent.surfaceSize.width, platformPaintEvent.surfaceSize.height});
        SwCoreApplication::sendEvent(root_, &widgetPaintEvent);
        painter.finalize();
        painter.flush();
        EndPaint(hwnd, &ps);
    }

    void dispatchMouseButton_(const MSG* msg, EventType type) {
        const int x = GET_X_LPARAM(msg->lParam);
        const int y = GET_Y_LPARAM(msg->lParam);
        const UINT keyState = static_cast<UINT>(msg->wParam);
        const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
        const bool shiftPressed = (keyState & MK_SHIFT) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        SwMouseButton button = SwMouseButton::NoButton;
        if (msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP || msg->message == WM_LBUTTONDBLCLK) {
            button = SwMouseButton::Left;
        } else if (msg->message == WM_RBUTTONDOWN || msg->message == WM_RBUTTONUP || msg->message == WM_RBUTTONDBLCLK) {
            button = SwMouseButton::Right;
        } else if (msg->message == WM_MBUTTONDOWN || msg->message == WM_MBUTTONUP || msg->message == WM_MBUTTONDBLCLK) {
            button = SwMouseButton::Middle;
        }

        MouseEvent mouseEvent(type, x, y, button, ctrlPressed, shiftPressed, altPressed);
        mouseEvent.setGlobalPos(mapLocalToGlobal_(x, y));
        root_->dispatchMouseEventFromRoot(mouseEvent);
    }

    void dispatchMouseMove_(const MSG* msg) {
        swWidgetEnsureMouseLeaveTracking_(msg->hwnd);
        const int x = GET_X_LPARAM(msg->lParam);
        const int y = GET_Y_LPARAM(msg->lParam);
        const UINT keyState = static_cast<UINT>(msg->wParam);
        const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
        const bool shiftPressed = (keyState & MK_SHIFT) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        MouseEvent mouseEvent(EventType::MouseMoveEvent, x, y, SwMouseButton::NoButton, ctrlPressed, shiftPressed, altPressed);
        if (lastMoveTime_.time_since_epoch().count() != 0) {
            const auto now = std::chrono::steady_clock::now();
            const long long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveTime_).count();
            const int deltaX = x - lastMousePosition_.x;
            const int deltaY = y - lastMousePosition_.y;
            mouseEvent.setDeltaX(deltaX);
            mouseEvent.setDeltaY(deltaY);
            if (durationMs > 0) {
                mouseEvent.setSpeedX((static_cast<double>(deltaX) / static_cast<double>(durationMs)) * 1000.0);
                mouseEvent.setSpeedY((static_cast<double>(deltaY) / static_cast<double>(durationMs)) * 1000.0);
            }
            lastMoveTime_ = now;
        } else {
            lastMoveTime_ = std::chrono::steady_clock::now();
        }

        lastMousePosition_ = SwPoint{x, y};
        mouseEvent.setGlobalPos(mapLocalToGlobal_(x, y));
        const bool handled = root_->dispatchMouseEventFromRoot(mouseEvent);
        if (!handled) {
            SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
        }
        SwToolTip::handleMouseMove(root_, root_->hoveredWidgetFromRoot(), x, y);
    }

    void dispatchMouseLeave_() {
        swWidgetClearMouseLeaveTracking_(nativeHostHandle_());
        MouseEvent mouseEvent(EventType::MouseMoveEvent,
                              -100000,
                              -100000,
                              SwMouseButton::NoButton,
                              false,
                              false,
                              false);
        mouseEvent.setGlobalPos(SwPoint{-100000, -100000});
        root_->dispatchMouseEventFromRoot(mouseEvent);
    }

    void dispatchMouseWheel_(const MSG* msg, bool horizontal) {
        POINT point{};
        point.x = GET_X_LPARAM(msg->lParam);
        point.y = GET_Y_LPARAM(msg->lParam);
        ScreenToClient(msg->hwnd, &point);

        const int delta = horizontal ? -static_cast<short>(HIWORD(msg->wParam))
                                     : static_cast<short>(HIWORD(msg->wParam));
        const UINT keyState = LOWORD(msg->wParam);
        const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
        const bool shiftPressed = horizontal ? true : ((keyState & MK_SHIFT) != 0);
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        WheelEvent wheelEvent(point.x, point.y, delta, ctrlPressed, shiftPressed, altPressed);
        wheelEvent.setGlobalPos(mapLocalToGlobal_(point.x, point.y));
        root_->dispatchWheelEventFromRoot(wheelEvent);
    }

    void dispatchKeyPress_(const MSG* msg, wchar_t textChar, bool textProvided) {
        SwToolTip::handleKeyPress();
        const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const int keyCode = (msg->message == WM_CHAR || msg->message == WM_SYSCHAR) ? 0 : static_cast<int>(msg->wParam);

        KeyEvent keyEvent(keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided);
        if (!root_->dispatchKeyPressEventFromRoot(keyEvent)) {
            SwShortcut::dispatch(root_, &keyEvent);
        }
    }

    void dispatchKeyRelease_(const MSG* msg) {
        const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

        KeyEvent keyEvent(static_cast<int>(msg->wParam),
                          ctrlPressed,
                          shiftPressed,
                          altPressed,
                          L'\0',
                          false,
                          EventType::KeyReleaseEvent);
        root_->dispatchKeyReleaseEventFromRoot(keyEvent);
    }

    void trackMouseLeave_() {
        const HWND hwnd = nativeHostHandle_();
        if (!hwnd) {
            return;
        }

        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(TRACKMOUSEEVENT);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hwnd;
        TrackMouseEvent(&tracking);
    }

    SwPoint mapLocalToGlobal_(int x, int y) const {
        const HWND hwnd = nativeHostHandle_();
        if (!hwnd) {
            return SwPoint{x, y};
        }

        POINT point{};
        point.x = x;
        point.y = y;
        ClientToScreen(hwnd, &point);
        return SwPoint{point.x, point.y};
    }

    SwWidget* root_{nullptr};
    SwWidgetPlatformHandle hostHandle_{};
    std::chrono::steady_clock::time_point lastMoveTime_{};
    SwPoint lastMousePosition_{0, 0};
};
