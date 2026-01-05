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
 * SwDialog - Qt-like dialog widget (≈ QDialog).
 *
 * Focus:
 * - Native top-level window by default on Windows (Qt-like).
 * - Optional in-client modal overlay (snapshot-friendly, cross-platform-ready).
 * - Basic title + content area + button row.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwLabel.h"
#include "SwPushButton.h"
#include "SwTimer.h"
#include "SwWidgetPlatformAdapter.h"
#include "SwDragDrop.h"
#include "SwToolTip.h"
#include "SwShortcut.h"

#include "core/runtime/SwCoreApplication.h"
#include "core/runtime/SwEventLoop.h"

#include <algorithm>

#if defined(_WIN32)
#include "platform/win/SwWin32Painter.h"
#include "platform/win/SwWindows.h"
#endif

class SwDialog : public SwFrame {
    SW_OBJECT(SwDialog, SwFrame)

public:
    enum DialogCode {
        Rejected = 0,
        Accepted = 1
    };

    explicit SwDialog(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        buildChildren();
    }

    void setWindowTitle(const SwString& title) {
        m_title = title;
        if (m_titleLabel) {
            m_titleLabel->setText(m_title);
        }
#if defined(_WIN32)
        if (m_nativeHwnd) {
            const std::wstring wide = m_title.toStdWString();
            SetWindowTextW(m_nativeHwnd, wide.c_str());
        }
#endif
        update();
    }

    SwString windowTitle() const { return m_title; }

    void setModal(bool on) { m_modal = on; }
    bool isModal() const { return m_modal; }

    void setUseNativeWindow(bool on) { m_useNativeWindow = on; }
    bool useNativeWindow() const { return m_useNativeWindow; }

    SwWidget* contentWidget() const { return m_content; }
    SwWidget* buttonBarWidget() const { return m_buttonBar; }

    int result() const { return m_result; }

    void open() {
#if defined(_WIN32)
        if (m_useNativeWindow) {
            openNativeWindow();
            if (m_nativeHwnd) {
                return;
            }
        }
#endif
        ensureOverlay();
        if (m_modal && m_overlay) {
            m_overlay->show();
            m_overlay->update();
        }

        show();
        centerInRoot();
        update();
        if (m_modal) {
            setFocus(true);
        }
        shown();
    }

    int exec() {
        setModal(true);
        SwEventLoop loop;
        SwObject::connect(this, &SwDialog::finished, &loop, [&loop](int code) { loop.exit(code); });
        open();
        return loop.exec();
    }

    void done(int code) {
        m_result = code;
        hide();
#if defined(_WIN32)
        if (m_activePresentation == Presentation::NativeWindow) {
            closeNativeWindow();
        }
#endif
        if (m_overlay) {
            m_overlay->hide();
        }
        restorePresentationParent();
        finished(m_result);
        if (m_result == Accepted) {
            accepted();
        } else {
            rejected();
        }
    }

    void accept() { done(Accepted); }
    void reject() { done(Rejected); }

signals:
    DECLARE_SIGNAL_VOID(shown);
    DECLARE_SIGNAL(finished, int);
    DECLARE_SIGNAL_VOID(accepted);
    DECLARE_SIGNAL_VOID(rejected);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

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
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        if (!dialog) {
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
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
            SwMouseButton button = SwMouseButton::NoButton;
            if (uMsg == WM_LBUTTONDOWN) button = SwMouseButton::Left;
            else if (uMsg == WM_RBUTTONDOWN) button = SwMouseButton::Right;
            else if (uMsg == WM_MBUTTONDOWN) button = SwMouseButton::Middle;
            dialog->nativePostMousePress(x, y, button);
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            SwMouseButton button = SwMouseButton::NoButton;
            if (uMsg == WM_LBUTTONDBLCLK) button = SwMouseButton::Left;
            else if (uMsg == WM_RBUTTONDBLCLK) button = SwMouseButton::Right;
            else if (uMsg == WM_MBUTTONDBLCLK) button = SwMouseButton::Middle;
            dialog->nativePostMouseDoubleClick(x, y, button);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            SwMouseButton button = SwMouseButton::NoButton;
            if (uMsg == WM_LBUTTONUP) button = SwMouseButton::Left;
            else if (uMsg == WM_RBUTTONUP) button = SwMouseButton::Right;
            else if (uMsg == WM_MBUTTONUP) button = SwMouseButton::Middle;
            dialog->nativePostMouseRelease(x, y, button);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            dialog->nativePostMouseMove(x, y);
            return 0;
        }
        case WM_KEYDOWN: {
            int keyCode = static_cast<int>(wParam);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            dialog->nativePostKeyPress(keyCode, ctrlPressed, shiftPressed, altPressed);
            return 0;
        }
        case WM_CLOSE: {
            dialog->nativePostCloseRequested();
            return 0;
        }
        case WM_NCDESTROY: {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }

    void openNativeWindow() {
        SwWidget* root = findRootWidget(this);
        HWND owner = root ? SwWidgetPlatformAdapter::nativeHandleAs<HWND>(root->platformHandle()) : nullptr;

        if (m_activePresentation != Presentation::NativeWindow) {
            m_originalParent = parent();
            m_originalRect = getRect();
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

        ShowWindow(m_nativeHwnd, SW_SHOW);
        UpdateWindow(m_nativeHwnd);

        setFocus(true);
        shown();
    }

    void closeNativeWindow() {
        if (m_modal && m_nativeOwnerHwnd && m_nativeOwnerWasEnabled) {
            EnableWindow(m_nativeOwnerHwnd, TRUE);
        }
        m_nativeOwnerWasEnabled = false;
        m_nativeOwnerHwnd = nullptr;

        if (m_nativeHwnd) {
            HWND hwndToDestroy = m_nativeHwnd;
            m_nativeHwnd = nullptr;
            SetWindowLongPtr(hwndToDestroy, GWLP_USERDATA, 0);
            DestroyWindow(hwndToDestroy);
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

        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC) {
            return;
        }
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientWidth, clientHeight);
        if (!memBitmap) {
            DeleteDC(memDC);
            return;
        }

        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

        SwWin32Painter painter(memDC);
        painter.clear(SwColor{241, 245, 249});
        SwRect paintRect{0, 0, clientWidth, clientHeight};
        PaintEvent paintEvent(&painter, paintRect);
        this->paintEvent(&paintEvent);
        SwDragDrop::instance().paintOverlay(&painter);

        int copyWidth = rect.right - rect.left;
        int copyHeight = rect.bottom - rect.top;
        BitBlt(hdc, rect.left, rect.top, copyWidth, copyHeight, memDC, rect.left, rect.top, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
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

    void nativePostMousePress(int x, int y, SwMouseButton button) {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this, x, y, button]() { nativeDispatchMousePress(x, y, button); });
            return;
        }
        nativeDispatchMousePress(x, y, button);
    }

    void nativePostMouseDoubleClick(int x, int y, SwMouseButton button) {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this, x, y, button]() { nativeDispatchMouseDoubleClick(x, y, button); });
            return;
        }
        nativeDispatchMouseDoubleClick(x, y, button);
    }

    void nativePostMouseRelease(int x, int y, SwMouseButton button) {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this, x, y, button]() { nativeDispatchMouseRelease(x, y, button); });
            return;
        }
        nativeDispatchMouseRelease(x, y, button);
    }

    void nativePostMouseMove(int x, int y) {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this, x, y]() { nativeDispatchMouseMove(x, y); });
            return;
        }
        nativeDispatchMouseMove(x, y);
    }

    void nativePostKeyPress(int keyCode, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this, keyCode, ctrlPressed, shiftPressed, altPressed]() {
                nativeDispatchKeyPress(keyCode, ctrlPressed, shiftPressed, altPressed);
            });
            return;
        }
        nativeDispatchKeyPress(keyCode, ctrlPressed, shiftPressed, altPressed);
    }

    void nativePostCloseRequested() {
        if (auto* app = SwCoreApplication::instance(false)) {
            app->postEvent([this]() { reject(); });
            return;
        }
        reject();
    }

    void nativeDispatchMousePress(int x, int y, SwMouseButton button) {
        SwToolTip::handleMousePress();
        MouseEvent mouseEvent(EventType::MousePressEvent, x, y, button);
        SwWidget::mousePressEvent(&mouseEvent);
    }

    void nativeDispatchMouseDoubleClick(int x, int y, SwMouseButton button) {
        MouseEvent mouseEvent(EventType::MouseDoubleClickEvent, x, y, button);
        SwWidget::mouseDoubleClickEvent(&mouseEvent);
    }

    void nativeDispatchMouseRelease(int x, int y, SwMouseButton button) {
        MouseEvent mouseEvent(EventType::MouseReleaseEvent, x, y, button);
        SwWidget::mouseReleaseEvent(&mouseEvent);
    }

    void nativeDispatchMouseMove(int x, int y) {
        MouseEvent mouseEvent(EventType::MouseMoveEvent, x, y);
        SwWidgetPlatformAdapter::setCursor(CursorType::Arrow);
        SwWidget::mouseMoveEvent(&mouseEvent);
        SwToolTip::handleMouseMove(this, x, y);
    }

    void nativeDispatchKeyPress(int keyCode, bool ctrlPressed, bool shiftPressed, bool altPressed) {
        SwToolTip::handleKeyPress();
        KeyEvent keyEvent(keyCode, ctrlPressed, shiftPressed, altPressed);
        if (SwShortcut::dispatch(this, &keyEvent)) {
            return;
        }
        SwWidget::keyPressEvent(&keyEvent);
    }
#endif

    class ModalOverlay final : public SwWidget {
        SW_OBJECT(ModalOverlay, SwWidget)

    public:
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

        void mousePressEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            // Modal: swallow clicks outside the dialog.
            event->accept();
        }

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
        None,
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
        const SwRect r = getRect();
        const int pad = 18;
        const int titleH = 44;
        const int barH = 56;

        m_titleLabel->move(r.x + pad, r.y + 10);
        m_titleLabel->resize(std::max(0, r.width - 2 * pad), titleH);

        m_buttonBar->move(r.x + pad, r.y + r.height - barH);
        m_buttonBar->resize(std::max(0, r.width - 2 * pad), barH - 10);

        const int contentY = r.y + titleH;
        const int contentH = std::max(0, r.height - titleH - barH);
        m_content->move(r.x + pad, contentY);
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
            m_originalRect = getRect();
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

    void restorePresentationParent() {
        if (m_originalParent && m_activePresentation == Presentation::Overlay && parent() == m_overlay) {
            setParent(m_originalParent);
            move(m_originalRect.x, m_originalRect.y);
            resize(m_originalRect.width, m_originalRect.height);
            setNativeWindowHandleRecursive(nativeWindowHandle());
        }
#if defined(_WIN32)
        if (m_originalParent && m_activePresentation == Presentation::NativeWindow && parent() == nullptr) {
            setParent(m_originalParent);
            move(m_originalRect.x, m_originalRect.y);
            resize(m_originalRect.width, m_originalRect.height);
            setNativeWindowHandleRecursive(nativeWindowHandle());
        }
#endif
        m_activePresentation = Presentation::None;
        m_originalParent = nullptr;
        m_originalRect = SwRect{};
    }

    SwString m_title;
    bool m_modal{true};
#if defined(_WIN32)
    bool m_useNativeWindow{true};
#else
    bool m_useNativeWindow{false};
#endif
    int m_result{Rejected};

    SwWidget* m_root{nullptr};
    ModalOverlay* m_overlay{nullptr};
    SwWidget* m_overlayRootConnected{nullptr};
    SwObject* m_originalParent{nullptr};
    SwRect m_originalRect{};
    Presentation m_activePresentation{Presentation::None};

#if defined(_WIN32)
    HWND m_nativeHwnd{nullptr};
    HWND m_nativeOwnerHwnd{nullptr};
    bool m_nativeOwnerWasEnabled{false};
#endif

    SwLabel* m_titleLabel{nullptr};
    SwWidget* m_content{nullptr};
    SwWidget* m_buttonBar{nullptr};
};
