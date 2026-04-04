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
 * @file src/core/gui/SwDialog.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwDialog in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the dialog interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwDialog.
 *
 * Dialog-oriented declarations here usually describe a bounded modal interaction: configuration
 * enters through setters or constructor state, the user edits the state through child widgets,
 * and the caller retrieves the accepted result through the public API.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwDialog - dialog widget.
 *
 * Focus:
 * - Native top-level window by default on Windows.
 * - Optional in-client modal overlay (snapshot-friendly, cross-platform-ready).
 * - Basic title + content area + button row.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwPushButton.h"
#include "SwTimer.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwDragDrop.h"
#include "SwToolTip.h"
#include "SwShortcut.h"

#include "core/object/SwPointer.h"
#include "core/runtime/SwCoreApplication.h"
#include "core/runtime/SwEventLoop.h"
#include "platform/SwPlatformTarget.h"

#include <algorithm>
#include <memory>
#include <utility>

#if SW_PLATFORM_WIN32
#include "platform/win/SwWindows.h"
#elif SW_PLATFORM_X11
#include "platform/x11/SwX11PlatformIntegration.h"
#endif

class SwDialog : public SwFrame {
    SW_OBJECT(SwDialog, SwFrame)

public:
    enum DialogCode {
        Rejected = 0,
        Accepted = 1
    };

    /**
     * @brief Constructs a `SwDialog` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwDialog(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        buildChildren();
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const SwString& title) {
        m_title = title;
        if (m_titleLabel) {
            m_titleLabel->setText(m_title);
        }
        if (m_nativePlatformWindow) {
            m_nativePlatformWindow->setTitle(toUtf8_(m_title));
        }
        update();
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const std::wstring& title) {
        setWindowTitle(SwString::fromWString(title));
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const wchar_t* title) {
        setWindowTitle(SwString::fromWCharArray(title));
    }

    /**
     * @brief Sets the window Title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWindowTitle(const char* title) {
        if (!title) {
            setWindowTitle(SwString());
            return;
        }
#if defined(_WIN32)
        // Defensive compatibility: accidental UTF-16LE pointer passed as char*.
        if (title[0] != '\0' && title[1] == '\0') {
            setWindowTitle(SwString::fromWCharArray(reinterpret_cast<const wchar_t*>(title)));
            return;
        }
#endif
        setWindowTitle(SwString::fromUtf8(title));
    }

    /**
     * @brief Returns the current window Title.
     * @return The current window Title.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString windowTitle() const { return m_title; }

#if defined(_WIN32)
    /**
     * @brief Sets the native Window Icon.
     * @param hIcon Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setNativeWindowIcon(HICON hIcon) {
        m_nativeIcon = hIcon;
        if (m_nativePlatformWindow && hIcon) {
            HWND hwnd = static_cast<HWND>(m_nativePlatformWindow->nativeHandle());
            if (hwnd) {
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
                SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            }
        }
    }
#endif

    /**
     * @brief Sets the modal.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setModal(bool on) { m_modal = on; }
    /**
     * @brief Returns whether the object reports modal.
     * @return `true` when the object reports modal; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isModal() const { return m_modal; }

    /**
     * @brief Sets the use Native Window.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUseNativeWindow(bool on) { m_useNativeWindow = on; }
    /**
     * @brief Returns the current use Native Window.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool useNativeWindow() const { return m_useNativeWindow; }

    /**
     * @brief Returns the current content Widget.
     * @return The current content Widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* contentWidget() const { return m_content; }
    /**
     * @brief Returns the current button Bar Widget.
     * @return The current button Bar Widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* buttonBarWidget() const { return m_buttonBar; }

    /**
     * @brief Returns the current result.
     * @return The current result.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int result() const { return m_result; }

    /**
     * @brief Opens the underlying resource managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void open() {
        if (m_useNativeWindow) {
            openNativePlatformWindow_();
            if (m_nativePlatformWindow) {
                return;
            }
        }
        capturePreviousFocusForActivation_();
        ensureOverlay();
        if (m_modal && m_overlay) {
            m_overlay->show();
            m_overlay->update();
        }

        show();
        centerInRoot();
        update();
        if (m_modal) {
            activateDialogFocus_();
        }
        shown();
    }

    /**
     * @brief Returns the current exec.
     * @return The current exec.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int exec() {
        setModal(true);
        SwEventLoop loop;
        SwObject::connect(this, &SwDialog::finished, &loop, [&loop](int code) { loop.exit(code); });
        open();
        return loop.exec();
    }

    /**
     * @brief Performs the `done` operation.
     * @param code Value passed to the method.
     */
    void done(int code) {
        m_result = code;
        hide();
        if (m_activePresentation == Presentation::NativeWindow) {
            closeNativePlatformWindow_();
        }
        if (m_overlay) {
            m_overlay->hide();
        }
        restorePresentationParent();
        restorePreviousFocusAfterClose_();
        finished(m_result);
        if (m_result == Accepted) {
            accepted();
        } else {
            rejected();
        }
    }

    /**
     * @brief Performs the `accept` operation.
     * @param Accepted Value passed to the method.
     */
    void accept() { done(Accepted); }
    /**
     * @brief Performs the `reject` operation.
     * @param Rejected Value passed to the method.
     */
    void reject() { done(Rejected); }

signals:
    DECLARE_SIGNAL_VOID(shown);
    DECLARE_SIGNAL(finished, int);
    DECLARE_SIGNAL_VOID(accepted);
    DECLARE_SIGNAL_VOID(rejected);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }
        SwFrame::keyPressEvent(event);
        if (event->isAccepted()) {
            return;
        }
        if (SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
            reject();
            event->accept();
        }
    }

private:
#if defined(_WIN32)
    static constexpr const wchar_t* nativeDialogWindowClassName() { return L"SwDialogWindowClass"; }

    static void ensureNativeDialogWindowClassRegistered() {
        static bool registered = false;
        if (registered) {
            return;
        }
        registered = true;

        WNDCLASSW wc = {};
        wc.lpfnWndProc = SwDialog::nativeDialogWindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = nativeDialogWindowClassName();
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
    }

    static LRESULT CALLBACK nativeDialogWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        SwDialog* dialog = reinterpret_cast<SwDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dialog = reinterpret_cast<SwDialog*>(cs ? cs->lpCreateParams : nullptr);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }

        if (!dialog) {
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }

        switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            dialog->nativeOnPaint(hdc, ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: {
            return 1;
        }
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            dialog->nativeOnResize(width, height);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            SwMouseButton button = SwMouseButton::NoButton;
            if (uMsg == WM_LBUTTONDOWN) button = SwMouseButton::Left;
            else if (uMsg == WM_RBUTTONDOWN) button = SwMouseButton::Right;
            else if (uMsg == WM_MBUTTONDOWN) button = SwMouseButton::Middle;
            dialog->nativePostMousePress(x, y, button, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            SwMouseButton button = SwMouseButton::NoButton;
            if (uMsg == WM_LBUTTONDBLCLK) button = SwMouseButton::Left;
            else if (uMsg == WM_RBUTTONDBLCLK) button = SwMouseButton::Right;
            else if (uMsg == WM_MBUTTONDBLCLK) button = SwMouseButton::Middle;
            dialog->nativePostMouseDoubleClick(x, y, button, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            SwMouseButton button = SwMouseButton::NoButton;
            if (uMsg == WM_LBUTTONUP) button = SwMouseButton::Left;
            else if (uMsg == WM_RBUTTONUP) button = SwMouseButton::Right;
            else if (uMsg == WM_MBUTTONUP) button = SwMouseButton::Middle;
            dialog->nativePostMouseRelease(x, y, button, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_MOUSEMOVE: {
            swWidgetEnsureMouseLeaveTracking_(hwnd);
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            dialog->nativePostMouseMove(x, y, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_MOUSELEAVE: {
            swWidgetClearMouseLeaveTracking_(hwnd);
            dialog->nativePostMouseLeave();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ScreenToClient(hwnd, &pt);
            dialog->nativePostMouseWheel(pt.x, pt.y, delta, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_MOUSEHWHEEL: {
            const int delta = -GET_WHEEL_DELTA_WPARAM(wParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = true;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            ScreenToClient(hwnd, &pt);
            dialog->nativePostMouseWheel(pt.x, pt.y, delta, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_POINTERWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            POINT pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            ScreenToClient(hwnd, &pt);
            dialog->nativePostMouseWheel(pt.x, pt.y, delta, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_POINTERHWHEEL: {
            const int delta = -GET_WHEEL_DELTA_WPARAM(wParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = true;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            POINT pt = { static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            ScreenToClient(hwnd, &pt);
            dialog->nativePostMouseWheel(pt.x, pt.y, delta, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int keyCode = static_cast<int>(wParam);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            dialog->nativePostKeyPress(keyCode, ctrlPressed, shiftPressed, altPressed, L'\0', true);
            return 0;
        }
        case WM_CHAR:
        case WM_SYSCHAR: {
            const wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch < 0x20) {
                return 0;
            }
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            dialog->nativePostKeyPress(0, ctrlPressed, shiftPressed, altPressed, ch, true);
            return 0;
        }
        case WM_DEADCHAR:
        case WM_SYSDEADCHAR:
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            const int keyCode = static_cast<int>(wParam);
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            dialog->nativePostKeyRelease(keyCode, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_CLOSE: {
            dialog->nativePostCloseRequested();
            return 0;
        }
        case WM_NCDESTROY: {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
    }

    void openNativeWindow() {
        capturePreviousFocusForActivation_();
        SwWidget* root = findRootWidget(this);
        HWND owner = root ? SwWidgetPlatformAdapter::nativeHandleAs<HWND>(root->platformHandle()) : nullptr;

        if (m_activePresentation != Presentation::NativeWindow) {
            m_originalParent = parent();
            m_originalRect = frameGeometry();
        }

        ensureNativeDialogWindowClassRegistered();

        const int desiredClientW = std::max(1, width());
        const int desiredClientH = std::max(1, height());

        DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_THICKFRAME;
        DWORD exStyle = owner ? 0 : WS_EX_TOOLWINDOW;

        RECT windowRect = {0, 0, desiredClientW, desiredClientH};
        AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
        const int windowW = std::max(1, static_cast<int>(windowRect.right - windowRect.left));
        const int windowH = std::max(1, static_cast<int>(windowRect.bottom - windowRect.top));

        std::wstring title = m_title.toStdWString();
        m_nativeHwnd = CreateWindowExW(exStyle,
                                       nativeDialogWindowClassName(),
                                       title.c_str(),
                                       style,
                                       CW_USEDEFAULT,
                                       CW_USEDEFAULT,
                                       windowW,
                                       windowH,
                                       owner,
                                       nullptr,
                                       GetModuleHandle(nullptr),
                                       this);

        if (!m_nativeHwnd) {
            return;
        }

        if (parent() != nullptr) {
            setParent(nullptr);
        }
        m_activePresentation = Presentation::NativeWindow;

        m_nativeOwnerHwnd = owner;
        m_nativeOwnerWasEnabled = false;
        if (m_modal && m_nativeOwnerHwnd) {
            m_nativeOwnerWasEnabled = IsWindowEnabled(m_nativeOwnerHwnd) != FALSE;
            EnableWindow(m_nativeOwnerHwnd, FALSE);
        }

        const auto handle = SwWidgetPlatformAdapter::fromNativeHandle(m_nativeHwnd);
        setNativeWindowHandleRecursive(handle);
        move(0, 0);

        RECT clientRect{};
        GetClientRect(m_nativeHwnd, &clientRect);
        const int clientW = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
        const int clientH = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
        SwWidget::resize(clientW, clientH);

        centerNativeWindow(m_nativeHwnd, owner, windowW, windowH);

        show();
        update();

        if (m_nativeIcon) {
            SendMessage(m_nativeHwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_nativeIcon));
            SendMessage(m_nativeHwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_nativeIcon));
        }

        ShowWindow(m_nativeHwnd, SW_SHOW);
        UpdateWindow(m_nativeHwnd);

        activateDialogFocus_();
        shown();
    }

    void closeNativeWindow() {
        HWND ownerToRestore = nullptr;
        if (m_modal && m_nativeOwnerHwnd && m_nativeOwnerWasEnabled) {
            EnableWindow(m_nativeOwnerHwnd, TRUE);
            ownerToRestore = m_nativeOwnerHwnd;
        }
        m_nativeOwnerWasEnabled = false;
        m_nativeOwnerHwnd = nullptr;

        if (m_nativeHwnd) {
            HWND hwndToDestroy = m_nativeHwnd;
            m_nativeHwnd = nullptr;
            SetWindowLongPtr(hwndToDestroy, GWLP_USERDATA, 0);
            DestroyWindow(hwndToDestroy);
        }

        if (ownerToRestore) {
            SetForegroundWindow(ownerToRestore);
        }
    }

    void nativeOnPaint(HDC hdc, const RECT& rect) {
        if (!hdc) {
            return;
        }
        if (!m_nativeHwnd) {
            return;
        }

        RECT clientRect{};
        GetClientRect(m_nativeHwnd, &clientRect);
        int clientWidth = clientRect.right - clientRect.left;
        int clientHeight = clientRect.bottom - clientRect.top;
        if (clientWidth <= 0 || clientHeight <= 0) {
            return;
        }
        paintPlatformSurface_(SwGuiApplication::instance(false) ? SwGuiApplication::instance(false)->platformIntegration() : nullptr,
                              SwMakePlatformPaintEvent(SwPlatformSize{clientWidth, clientHeight},
                                                       hdc,
                                                       m_nativeHwnd,
                                                       nullptr,
                                                       SwPlatformRect{
                                                           rect.left,
                                                           rect.top,
                                                           std::max(0, static_cast<int>(rect.right - rect.left)),
                                                           std::max(0, static_cast<int>(rect.bottom - rect.top))
                                                       }),
                              SwWidgetPlatformAdapter::fromNativeHandle(m_nativeHwnd),
                              clientWidth,
                              clientHeight,
                              true,
                              true,
                              [this](PaintEvent& paintEvent) {
                                  SwCoreApplication::sendEvent(this, &paintEvent);
                              });
    }

    void nativeOnResize(int width, int height) {
        move(0, 0);
        SwWidget::resize(width, height);
    }

    static void centerNativeWindow(HWND dialogHwnd, HWND owner, int windowW, int windowH) {
        if (!dialogHwnd || !windowW || !windowH) {
            return;
        }
        RECT targetRect{};
        if (owner && GetWindowRect(owner, &targetRect)) {
            const int ownerW = targetRect.right - targetRect.left;
            const int ownerH = targetRect.bottom - targetRect.top;
            const int x = targetRect.left + std::max(0, (ownerW - windowW) / 2);
            const int y = targetRect.top + std::max(0, (ownerH - windowH) / 2);
            SetWindowPos(dialogHwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
            return;
        }

        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int x = std::max(0, (screenW - windowW) / 2);
        const int y = std::max(0, (screenH - windowH) / 2);
        SetWindowPos(dialogHwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }

    void postGuardedNativeEvent_(std::function<void(SwDialog*)> fn) {
        if (!fn) {
            return;
        }
        if (auto* app = SwCoreApplication::instance(false)) {
            const SwPointer<SwDialog> self(this);
            std::function<void(SwDialog*)> fnCopy = fn;
            app->postEventOnLane([self, fnCopy]() mutable {
                if (!self) {
                    return;
                }
                SwDialog* liveSelf = self.data();
                if (!SwObject::isLive(liveSelf)) {
                    return;
                }
                fnCopy(liveSelf);
            }, SwFiberLane::Input);
            return;
        }
        fn(this);
    }

    void nativePostMousePress(int x, int y, SwMouseButton button, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        postGuardedNativeEvent_([x, y, button, ctrlPressed, shiftPressed, altPressed](SwDialog* self) {
            self->nativeDispatchMousePress(x, y, button, ctrlPressed, shiftPressed, altPressed);
        });
    }

    void nativePostMouseDoubleClick(int x, int y, SwMouseButton button, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        postGuardedNativeEvent_([x, y, button, ctrlPressed, shiftPressed, altPressed](SwDialog* self) {
            self->nativeDispatchMouseDoubleClick(x, y, button, ctrlPressed, shiftPressed, altPressed);
        });
    }

    void nativePostMouseRelease(int x, int y, SwMouseButton button, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        postGuardedNativeEvent_([x, y, button, ctrlPressed, shiftPressed, altPressed](SwDialog* self) {
            self->nativeDispatchMouseRelease(x, y, button, ctrlPressed, shiftPressed, altPressed);
        });
    }

    void nativePostMouseMove(int x, int y, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        postGuardedNativeEvent_([x, y, ctrlPressed, shiftPressed, altPressed](SwDialog* self) {
            self->nativeDispatchMouseMove(x, y, ctrlPressed, shiftPressed, altPressed);
        });
    }

    void nativePostMouseLeave() {
        postGuardedNativeEvent_([](SwDialog* self) {
            self->dispatchNativeMouseLeave_();
        });
    }

    void nativePostMouseWheel(int x, int y, int delta, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        postGuardedNativeEvent_([x, y, delta, ctrlPressed, shiftPressed, altPressed](SwDialog* self) {
            self->nativeDispatchMouseWheel(x, y, delta, ctrlPressed, shiftPressed, altPressed);
        });
    }

    void nativePostKeyPress(int keyCode,
                            bool ctrlPressed,
                            bool shiftPressed,
                            bool altPressed,
                            wchar_t textChar = L'\0',
                            bool textProvided = false) {
        postGuardedNativeEvent_([keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided](SwDialog* self) {
            self->nativeDispatchKeyPress(keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided);
        });
    }

    void nativePostKeyRelease(int keyCode,
                              bool ctrlPressed,
                              bool shiftPressed,
                              bool altPressed) {
        postGuardedNativeEvent_([keyCode, ctrlPressed, shiftPressed, altPressed](SwDialog* self) {
            self->nativeDispatchKeyRelease(keyCode, ctrlPressed, shiftPressed, altPressed);
        });
    }

    void nativePostCloseRequested() {
        postGuardedNativeEvent_([](SwDialog* self) { self->reject(); });
    }

    void nativeDispatchMousePress(int x, int y, SwMouseButton button, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        SwToolTip::handleMousePress();
        MouseEvent mouseEvent(EventType::MousePressEvent, x, y, button, ctrlPressed, shiftPressed, altPressed);
        mouseEvent.setGlobalPos(mapToGlobal(mouseEvent.pos()));
        dispatchMouseEventFromRoot_(mouseEvent);
    }

    void nativeDispatchMouseDoubleClick(int x,
                                        int y,
                                        SwMouseButton button,
                                        bool ctrlPressed,
                                        bool shiftPressed,
                                        bool altPressed) {
        MouseEvent mouseEvent(EventType::MouseDoubleClickEvent, x, y, button, ctrlPressed, shiftPressed, altPressed);
        mouseEvent.setGlobalPos(mapToGlobal(mouseEvent.pos()));
        dispatchMouseEventFromRoot_(mouseEvent);
    }

    void nativeDispatchMouseRelease(int x,
                                    int y,
                                    SwMouseButton button,
                                    bool ctrlPressed,
                                    bool shiftPressed,
                                    bool altPressed) {
        MouseEvent mouseEvent(EventType::MouseReleaseEvent, x, y, button, ctrlPressed, shiftPressed, altPressed);
        mouseEvent.setGlobalPos(mapToGlobal(mouseEvent.pos()));
        dispatchMouseEventFromRoot_(mouseEvent);
    }

    void nativeDispatchMouseMove(int x, int y, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        MouseEvent mouseEvent(EventType::MouseMoveEvent, x, y, SwMouseButton::NoButton, ctrlPressed, shiftPressed, altPressed);
        mouseEvent.setGlobalPos(mapToGlobal(mouseEvent.pos()));
        const bool handled = dispatchMouseEventFromRoot_(mouseEvent);
        if (!handled) {
            SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
        }
        SwToolTip::handleMouseMove(this, hoveredWidgetFromRoot(), x, y);
    }

    void nativeDispatchMouseWheel(int x, int y, int delta, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        WheelEvent wheelEvent(x, y, delta, ctrlPressed, shiftPressed, altPressed);
        wheelEvent.setGlobalPos(mapToGlobal(wheelEvent.pos()));
        dispatchWheelEventFromRoot_(wheelEvent);
    }

    void nativeDispatchKeyPress(int keyCode,
                                bool ctrlPressed,
                                bool shiftPressed,
                                bool altPressed,
                                wchar_t textChar = L'\0',
                                bool textProvided = false) {
        SwToolTip::handleKeyPress();
        KeyEvent keyEvent(keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided);
        if (!dispatchKeyPressEventFromRoot_(keyEvent)) {
            SwShortcut::dispatch(this, &keyEvent);
        }
    }

    void nativeDispatchKeyRelease(int keyCode,
                                  bool ctrlPressed,
                                  bool shiftPressed,
                                  bool altPressed) {
        KeyEvent keyEvent(keyCode,
                          ctrlPressed,
                          shiftPressed,
                          altPressed,
                          L'\0',
                          false,
                          EventType::KeyReleaseEvent);
        dispatchKeyReleaseEventFromRoot_(keyEvent);
    }
#endif

#if SW_PLATFORM_X11
    void openNativeWindowX11() {
        capturePreviousFocusForActivation_();
        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp) {
            return;
        }
        m_x11Integration = dynamic_cast<SwX11PlatformIntegration*>(guiApp->platformIntegration());
        if (!m_x11Integration) {
            return;
        }

        if (m_activePresentation != Presentation::NativeWindow) {
            m_originalParent = parent();
            m_originalRect = frameGeometry();
        }
        if (parent() != nullptr) {
            setParent(nullptr);
        }
        m_activePresentation = Presentation::NativeWindow;

        const int w = std::max(1, width());
        const int h = std::max(1, height());
        const std::string title = m_title.toStdString();

        SwWindowCallbacks callbacks;
        callbacks.paintRequestHandler = [this](const SwPlatformPaintEvent&) {
            x11HandlePaintRequest();
        };
        callbacks.deleteHandler = [this]() {
            postOrRunNativeEvent_([](SwDialog* self) { self->reject(); });
        };
        callbacks.resizeHandler = [this](const SwPlatformSize& size) {
            move(0, 0);
            SwWidget::resize(size.width, size.height);
        };
        callbacks.mousePressHandler = [this](const SwMouseEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeMousePress_(evt);
            });
        };
        callbacks.mouseDoubleClickHandler = [this](const SwMouseEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeMouseDoubleClick_(evt);
            });
        };
        callbacks.mouseReleaseHandler = [this](const SwMouseEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeMouseRelease_(evt);
            });
        };
        callbacks.mouseMoveHandler = [this](const SwMouseEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeMouseMove_(evt);
            });
        };
        callbacks.mouseLeaveHandler = [this]() {
            x11PostEvent([](SwDialog* self) {
                self->dispatchNativeMouseLeave_();
            });
        };
        callbacks.mouseWheelHandler = [this](const SwMouseEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeMouseWheel_(evt);
            });
        };
        callbacks.keyPressHandler = [this](const SwKeyEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeKeyPress_(evt);
            });
        };
        callbacks.keyReleaseHandler = [this](const SwKeyEvent& evt) {
            x11PostEvent([evt](SwDialog* self) {
                self->dispatchNativeKeyRelease_(evt);
            });
        };

        m_x11PlatformWindow = m_x11Integration->createWindow(title.empty() ? "Dialog" : title, w, h, callbacks);
        if (!m_x11PlatformWindow) {
            return;
        }

        auto* x11Window = dynamic_cast<SwX11PlatformWindow*>(m_x11PlatformWindow.get());
        if (!x11Window) {
            m_x11PlatformWindow.reset();
            return;
        }

        setNativeWindowHandleRecursive(SwWidgetPlatformAdapter::fromNativeHandle(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11Window->handle())),
            m_x11Integration->display()));

        move(0, 0);
        SwWidget::resize(w, h);
        show();
        update();
        m_x11PlatformWindow->show();
        activateDialogFocus_();
        shown();
    }

    void closeNativeWindowX11() {
        m_x11PlatformWindow.reset();
        m_x11Integration = nullptr;
    }

    void x11HandlePaintRequest() {
        if (!m_x11Integration || !m_x11PlatformWindow) {
            return;
        }
        auto* x11Window = dynamic_cast<SwX11PlatformWindow*>(m_x11PlatformWindow.get());
        if (!x11Window) {
            return;
        }
        const int w = std::max(1, width());
        const int h = std::max(1, height());
        paintPlatformSurface_(m_x11Integration,
                              SwMakePlatformPaintEvent(SwPlatformSize{w, h},
                                                       reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11Window->handle())),
                                                       reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11Window->handle())),
                                                       m_x11Integration->display(),
                                                       SwPlatformRect{0, 0, w, h}),
                              SwWidgetPlatformAdapter::fromNativeHandle(
                                  reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11Window->handle())),
                                  m_x11Integration->display()),
                              w,
                              h,
                              true,
                              false,
                              [this](PaintEvent& paintEvent) {
                                  SwCoreApplication::sendEvent(this, &paintEvent);
                              });
    }

    void x11PostEvent(std::function<void(SwDialog*)> fn) {
        postOrRunNativeEvent_(std::move(fn));
    }
#endif

    template<typename PaintDispatch>
    void paintPlatformSurface_(SwPlatformIntegration* integration,
                               const SwPlatformPaintEvent& baseEvent,
                               const SwWidgetPlatformHandle& handle,
                               int surfaceWidth,
                               int surfaceHeight,
                               bool clearBackground,
                               bool paintOverlay,
                               PaintDispatch&& dispatch) {
        if (!integration || surfaceWidth <= 0 || surfaceHeight <= 0) {
            return;
        }

        SwPlatformPaintEvent resolvedEvent = SwResolvePlatformPaintEvent(baseEvent,
                                                                         SwPlatformSize{surfaceWidth, surfaceHeight},
                                                                         baseEvent.nativePaintDevice
                                                                             ? baseEvent.nativePaintDevice
                                                                             : handle.nativeHandle,
                                                                         handle.nativeHandle,
                                                                         handle.nativeDisplay);
        SwScopedPlatformPainter painter(integration, resolvedEvent);
        if (!painter) {
            return;
        }

        if (clearBackground) {
            painter->clear(SwColor{241, 245, 249});
        }

        PaintEvent paintEvent(painter.asPainter(), SwRect{0, 0, surfaceWidth, surfaceHeight});
        dispatch(paintEvent);

        if (paintOverlay) {
            SwDragDrop::instance().paintOverlay(painter.asPainter());
        }

        painter->finalize();
        painter->flush();
    }

    static std::string toUtf8_(const SwString& value) {
        return value.toStdString();
    }

    void postOrRunNativeEvent_(std::function<void(SwDialog*)> fn) {
        if (!fn) {
            return;
        }
        if (auto* app = SwCoreApplication::instance(false)) {
            const SwPointer<SwDialog> self(this);
            std::function<void(SwDialog*)> fnCopy = fn;
            app->postEventOnLane([self, fnCopy]() mutable {
                if (!self) {
                    return;
                }
                SwDialog* liveSelf = self.data();
                if (!SwObject::isLive(liveSelf)) {
                    return;
                }
                fnCopy(liveSelf);
            }, SwFiberLane::Input);
            return;
        }
        fn(this);
    }

    SwWindowCallbacks nativePlatformCallbacks_() {
        SwWindowCallbacks callbacks;
        callbacks.paintRequestHandler = [this](const SwPlatformPaintEvent& event) {
            handleNativePaintRequest_(event);
        };
        callbacks.deleteHandler = [this]() {
            postOrRunNativeEvent_([](SwDialog* self) { self->reject(); });
        };
        callbacks.resizeHandler = [this](const SwPlatformSize& size) {
            move(0, 0);
            SwWidget::resize(size.width, size.height);
        };
        callbacks.mousePressHandler = [this](const SwMouseEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeMousePress_(evt); });
        };
        callbacks.mouseDoubleClickHandler = [this](const SwMouseEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeMouseDoubleClick_(evt); });
        };
        callbacks.mouseReleaseHandler = [this](const SwMouseEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeMouseRelease_(evt); });
        };
        callbacks.mouseMoveHandler = [this](const SwMouseEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeMouseMove_(evt); });
        };
        callbacks.mouseLeaveHandler = [this]() {
            postOrRunNativeEvent_([](SwDialog* self) { self->dispatchNativeMouseLeave_(); });
        };
        callbacks.mouseWheelHandler = [this](const SwMouseEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeMouseWheel_(evt); });
        };
        callbacks.keyPressHandler = [this](const SwKeyEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeKeyPress_(evt); });
        };
        callbacks.keyReleaseHandler = [this](const SwKeyEvent& evt) {
            postOrRunNativeEvent_([evt](SwDialog* self) { self->dispatchNativeKeyRelease_(evt); });
        };
        return callbacks;
    }

    void openNativePlatformWindow_() {
        capturePreviousFocusForActivation_();
        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp || !guiApp->platformIntegration()) {
            return;
        }

        SwWidget* root = findRootWidget(this);
        SwWidgetPlatformHandle ownerHandle = root ? root->platformHandle() : SwWidgetPlatformHandle{};
        if (m_activePresentation != Presentation::NativeWindow) {
            m_originalParent = parent();
            m_originalRect = frameGeometry();
        }

        if (parent() != nullptr) {
            setParent(nullptr);
        }

        SwPlatformWindowOptions options;
        options.role = SwPlatformWindowRole::Dialog;
        options.ownerHandle = ownerHandle.nativeHandle;
        options.resizable = false;
        options.minimizable = false;
        options.maximizable = false;
        options.showInTaskbar = false;

        const std::string title = toUtf8_(m_title);
        m_nativePlatformWindow = guiApp->platformIntegration()->createWindow(title.empty() ? "Dialog" : title,
                                                                             std::max(1, width()),
                                                                             std::max(1, height()),
                                                                             nativePlatformCallbacks_(),
                                                                             options);
        if (!m_nativePlatformWindow) {
            return;
        }

        m_activePresentation = Presentation::NativeWindow;
#if defined(_WIN32)
        m_nativeOwnerHwnd = ownerHandle
            ? SwWidgetPlatformAdapter::nativeHandleAs<HWND>(ownerHandle)
            : nullptr;
        m_nativeOwnerWasEnabled = false;
        if (m_modal && m_nativeOwnerHwnd) {
            m_nativeOwnerWasEnabled = IsWindowEnabled(m_nativeOwnerHwnd) != FALSE;
            EnableWindow(m_nativeOwnerHwnd, FALSE);
        }
#endif

        setNativeWindowHandleRecursive(SwWidgetPlatformAdapter::fromNativeHandle(
            m_nativePlatformWindow->nativeHandle(),
            m_nativePlatformWindow->nativeDisplay()));
        move(0, 0);

        SwRect clientRect = SwWidgetPlatformAdapter::clientRect(nativeWindowHandle());
        const int clientW = std::max(1, clientRect.width > 0 ? clientRect.width : width());
        const int clientH = std::max(1, clientRect.height > 0 ? clientRect.height : height());
        SwWidget::resize(clientW, clientH);

        centerNativePlatformWindow_(ownerHandle);
        show();
        update();
        m_nativePlatformWindow->show();
#if defined(_WIN32)
        if (m_nativeIcon) {
            HWND hwnd = static_cast<HWND>(m_nativePlatformWindow->nativeHandle());
            if (hwnd) {
                SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(m_nativeIcon));
                SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(m_nativeIcon));
            }
        }
#endif
        activateDialogFocus_();
        shown();
    }

    void closeNativePlatformWindow_() {
#if defined(_WIN32)
        HWND ownerToRestore = nullptr;
        if (m_modal && m_nativeOwnerHwnd && m_nativeOwnerWasEnabled) {
            EnableWindow(m_nativeOwnerHwnd, TRUE);
            ownerToRestore = m_nativeOwnerHwnd;
        }
        m_nativeOwnerWasEnabled = false;
        m_nativeOwnerHwnd = nullptr;
#endif
        m_nativePlatformWindow.reset();
        setNativeWindowHandleRecursive(SwWidgetPlatformHandle{});
#if defined(_WIN32)
        if (ownerToRestore) {
            SetForegroundWindow(ownerToRestore);
        }
#endif
    }

    void centerNativePlatformWindow_(const SwWidgetPlatformHandle& ownerHandle) {
        if (!m_nativePlatformWindow) {
            return;
        }

        const SwRect frameRect = SwWidgetPlatformAdapter::windowFrameRect(nativeWindowHandle());
        if (frameRect.width <= 0 || frameRect.height <= 0 || !ownerHandle) {
            return;
        }

        const SwRect ownerRect = SwWidgetPlatformAdapter::windowFrameRect(ownerHandle);
        if (ownerRect.width <= 0 || ownerRect.height <= 0) {
            return;
        }

        const int x = ownerRect.x + std::max(0, (ownerRect.width - frameRect.width) / 2);
        const int y = ownerRect.y + std::max(0, (ownerRect.height - frameRect.height) / 2);
        m_nativePlatformWindow->move(x, y);
    }

    void handleNativePaintRequest_(const SwPlatformPaintEvent& event) {
        if (!m_nativePlatformWindow) {
            return;
        }

        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp || !guiApp->platformIntegration()) {
            return;
        }

        SwRect clientRect = SwWidgetPlatformAdapter::clientRect(nativeWindowHandle());
        const int w = std::max(1, clientRect.width > 0 ? clientRect.width : width());
        const int h = std::max(1, clientRect.height > 0 ? clientRect.height : height());

        paintPlatformSurface_(guiApp->platformIntegration(),
                              event,
                              SwWidgetPlatformAdapter::fromNativeHandle(m_nativePlatformWindow->nativeHandle(),
                                                                        m_nativePlatformWindow->nativeDisplay()),
                              w,
                              h,
                              true,
                              true,
                              [this](PaintEvent& paintEvent) {
                                  SwCoreApplication::sendEvent(this, &paintEvent);
                              });
    }

    void dispatchNativeMousePress_(const SwMouseEvent& evt) {
        SwToolTip::handleMousePress();
        MouseEvent event(EventType::MousePressEvent,
                         evt.position.x,
                         evt.position.y,
                         evt.button,
                         evt.ctrl,
                         evt.shift,
                         evt.alt);
        event.setGlobalPos(mapToGlobal(event.pos()));
        dispatchMouseEventFromRoot_(event);
    }

    void dispatchNativeMouseDoubleClick_(const SwMouseEvent& evt) {
        MouseEvent event(EventType::MouseDoubleClickEvent,
                         evt.position.x,
                         evt.position.y,
                         evt.button,
                         evt.ctrl,
                         evt.shift,
                         evt.alt);
        event.setGlobalPos(mapToGlobal(event.pos()));
        dispatchMouseEventFromRoot_(event);
    }

    void dispatchNativeMouseRelease_(const SwMouseEvent& evt) {
        MouseEvent event(EventType::MouseReleaseEvent,
                         evt.position.x,
                         evt.position.y,
                         evt.button,
                         evt.ctrl,
                         evt.shift,
                         evt.alt);
        event.setGlobalPos(mapToGlobal(event.pos()));
        dispatchMouseEventFromRoot_(event);
    }

    void dispatchNativeMouseMove_(const SwMouseEvent& evt) {
        MouseEvent event(EventType::MouseMoveEvent,
                         evt.position.x,
                         evt.position.y,
                         SwMouseButton::NoButton,
                         evt.ctrl,
                         evt.shift,
                         evt.alt);
        event.setGlobalPos(mapToGlobal(event.pos()));
        const bool handled = dispatchMouseEventFromRoot_(event);
        if (!handled) {
            SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
        }
        SwToolTip::handleMouseMove(this, hoveredWidgetFromRoot(), evt.position.x, evt.position.y);
    }

    void dispatchNativeMouseLeave_() {
        clearHoverRecursive_();
        SwToolTip::hideText();
    }

    void dispatchNativeMouseWheel_(const SwMouseEvent& evt) {
        WheelEvent event(evt.position.x, evt.position.y, evt.wheelDelta, evt.ctrl, evt.shift, evt.alt);
        event.setGlobalPos(mapToGlobal(event.pos()));
        dispatchWheelEventFromRoot_(event);
    }

    void dispatchNativeKeyPress_(const SwKeyEvent& evt) {
        SwToolTip::handleKeyPress();
        KeyEvent event(evt.keyCode, evt.ctrl, evt.shift, evt.alt, evt.text, evt.textProvided);
        if (!dispatchKeyPressEventFromRoot_(event)) {
            SwShortcut::dispatch(this, &event);
        }
    }

    void dispatchNativeKeyRelease_(const SwKeyEvent& evt) {
        KeyEvent event(evt.keyCode,
                       evt.ctrl,
                       evt.shift,
                       evt.alt,
                       L'\0',
                       false,
                       EventType::KeyReleaseEvent);
        dispatchKeyReleaseEventFromRoot_(event);
    }

    class ModalOverlay final : public SwWidget {
        SW_OBJECT(ModalOverlay, SwWidget)

    public:
        /**
         * @brief Performs the `ModalOverlay` operation.
         * @param owner Value passed to the method.
         * @param root Value passed to the method.
         * @param parent Optional parent object that owns this instance.
         * @param root Value passed to the method.
         */
        ModalOverlay(SwDialog* owner, SwWidget* root, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner)
            , m_root(root) {
            setCursor(CursorType::Arrow);
            setFocusPolicy(FocusPolicyEnum::Strong);
            setStyleSheet(R"(
                SwWidget {
                    background-color: rgb(241, 245, 249);
                    border-width: 0px;
                }
            )");
        }

        /**
         * @brief Handles the mouse Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            // Modal: swallow clicks outside the dialog.
            event->accept();
        }

        /**
         * @brief Handles the key Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void keyPressEvent(KeyEvent* event) override {
            if (!event || !m_owner) {
                return;
            }
            if (SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
                m_owner->reject();
                event->accept();
                return;
            }
            SwWidget::keyPressEvent(event);
        }

    private:
        SwDialog* m_owner{nullptr};
        SwWidget* m_root{nullptr};
    };

    static SwWidget* findRootWidget(SwObject* start) {
        SwWidget* lastWidget = nullptr;
        for (SwObject* p = start; p; p = p->parent()) {
            if (auto* w = dynamic_cast<SwWidget*>(p)) {
                lastWidget = w;
            }
        }
        return lastWidget;
    }

    enum class Presentation {
        Embedded,
        Overlay,
        NativeWindow
    };

    void initDefaults() {
        resize(520, 260);
        setCursor(CursorType::Arrow);
        setFocusPolicy(FocusPolicyEnum::Strong);
        setFrameShape(Shape::NoFrame);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setStyleSheet(R"(
            SwDialog {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 16px;
            }
        )");
    }

    void buildChildren() {
        if (m_titleLabel) {
            return;
        }
        m_titleLabel = new SwLabel("", this);
        m_titleLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24, 28, 36); font-size: 16px; }
        )");
        m_titleLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
        m_titleLabel->setVisible(false);

        m_content = new SwWidget(this);
        m_content->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        m_buttonBar = new SwWidget(this);
        m_buttonBar->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        updateLayout();
    }

    void updateLayout() {
        if (!m_titleLabel || !m_content || !m_buttonBar) {
            return;
        }
        const SwRect r = rect();
        const int pad = 18;
        const bool showTitle = m_titleLabel->getVisible();
        const int titleH = showTitle ? 44 : 0;
        const int barH = 56;

        if (showTitle) {
            m_titleLabel->move(pad, 10);
            m_titleLabel->resize(std::max(0, r.width - 2 * pad), titleH);
        }

        m_buttonBar->move(pad, r.height - barH);
        m_buttonBar->resize(std::max(0, r.width - 2 * pad), barH - 10);

        const int contentY = showTitle ? titleH : pad;
        const int contentH = std::max(0, r.height - (showTitle ? titleH : pad) - barH);
        m_content->move(pad, contentY);
        m_content->resize(std::max(0, r.width - 2 * pad), contentH);
    }

    void ensureOverlay() {
        if (!m_modal) {
            return;
        }
        m_root = findRootWidget(this);
        if (!m_root) {
            return;
        }
        if (!m_overlay) {
            m_overlay = new ModalOverlay(this, m_root, m_root);
            m_overlay->move(0, 0);
            m_overlay->resize(m_root->width(), m_root->height());
        }
        if (parent() != m_overlay) {
            m_originalParent = parent();
            m_originalRect = frameGeometry();
            setParent(m_overlay);
            m_activePresentation = Presentation::Overlay;
        }

        if (m_overlayRootConnected != m_root) {
            m_overlayRootConnected = m_root;
            SwWidget* expectedRoot = m_root;
            SwObject::connect(m_root, &SwWidget::resized, this, [this, expectedRoot](int w, int h) {
                if (m_root != expectedRoot) {
                    return;
                }
                if (m_overlay) {
                    m_overlay->move(0, 0);
                    m_overlay->resize(w, h);
                }
                centerInRoot();
            });
        }
    }

    void centerInRoot() {
        if (!m_root) {
            m_root = findRootWidget(this);
        }
        if (!m_root) {
            return;
        }
        const int w = width();
        const int h = height();
        const int x = std::max(0, (m_root->width() - w) / 2);
        const int y = std::max(0, (m_root->height() - h) / 2);
        move(x, y);
    }

    void capturePreviousFocusForActivation_() {
        m_previousFocus = nullptr;
        m_focusRestoreRoot = nullptr;
        if (!m_modal) {
            return;
        }
        SwWidget* focusRoot = m_root ? m_root : findRootWidget(this);
        if (!focusRoot) {
            return;
        }
        m_focusRestoreRoot = focusRoot;
        SwWidget* focused = focusRoot->focusedWidgetInHierarchy();
        if (focused && focused != this) {
            m_previousFocus = focused;
        }
    }

    void activateDialogFocus_() {
        setFocus(true);
    }

    void restorePreviousFocusAfterClose_() {
        SwPointer<SwWidget> previous = m_previousFocus;
        SwPointer<SwWidget> focusRoot = m_focusRestoreRoot;
        m_previousFocus = nullptr;
        m_focusRestoreRoot = nullptr;

        auto canRestore = [](SwWidget* widget) -> bool {
            return widget &&
                   widget->isVisibleInHierarchy() &&
                   widget->getEnable() &&
                   widget->getFocusPolicy() != FocusPolicyEnum::NoFocus;
        };

        if (canRestore(previous.data()) && previous.data() != this) {
            previous.data()->setFocus(true);
            return;
        }

        if (canRestore(focusRoot.data()) && focusRoot.data() != this) {
            focusRoot.data()->setFocus(true);
        }
    }

    void restorePresentationParent() {
        if (m_originalParent && m_activePresentation == Presentation::Overlay && parent() == m_overlay) {
            setParent(m_originalParent);
            move(m_originalRect.x, m_originalRect.y);
            resize(m_originalRect.width, m_originalRect.height);
            setNativeWindowHandleRecursive(nativeWindowHandle());
        }
#if SW_PLATFORM_WIN32 || SW_PLATFORM_X11
        if (m_originalParent && m_activePresentation == Presentation::NativeWindow && parent() == nullptr) {
            setParent(m_originalParent);
            move(m_originalRect.x, m_originalRect.y);
            resize(m_originalRect.width, m_originalRect.height);
            setNativeWindowHandleRecursive(nativeWindowHandle());
        }
#endif
        m_activePresentation = Presentation::Embedded;
        m_originalParent = nullptr;
        m_originalRect = SwRect{};
    }

    SwString m_title;
    bool m_modal{true};
#if SW_PLATFORM_WIN32 || SW_PLATFORM_X11
    bool m_useNativeWindow{true};
#else
    bool m_useNativeWindow{false};
#endif
    int m_result{Rejected};

    SwWidget* m_root{nullptr};
    ModalOverlay* m_overlay{nullptr};
    SwWidget* m_overlayRootConnected{nullptr};
    SwPointer<SwWidget> m_previousFocus;
    SwPointer<SwWidget> m_focusRestoreRoot;
    SwObject* m_originalParent{nullptr};
    SwRect m_originalRect{};
    Presentation m_activePresentation{Presentation::Embedded};
    std::unique_ptr<SwPlatformWindow> m_nativePlatformWindow;

#if SW_PLATFORM_WIN32
    HWND m_nativeHwnd{nullptr};
    HWND m_nativeOwnerHwnd{nullptr};
    bool m_nativeOwnerWasEnabled{false};
    HICON m_nativeIcon{nullptr};
#elif SW_PLATFORM_X11
    std::unique_ptr<SwPlatformWindow> m_x11PlatformWindow;
    SwX11PlatformIntegration* m_x11Integration{nullptr};
#endif

    SwLabel* m_titleLabel{nullptr};
    SwWidget* m_content{nullptr};
    SwWidget* m_buttonBar{nullptr};
};

