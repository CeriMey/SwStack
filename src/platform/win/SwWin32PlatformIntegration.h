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
 * @file src/platform/win/SwWin32PlatformIntegration.h
 * @ingroup platform_backends
 * @brief Declares the public interface exposed by SwWin32PlatformIntegration in the CoreSw Win32
 * platform integration layer.
 *
 * This header belongs to the CoreSw Win32 platform integration layer. It binds portable framework
 * abstractions to concrete Win32 windowing, painting, and input services.
 *
 * Within that layer, this file focuses on the win32 platform integration interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are SwVirtualGraphicsEngine, SwGdiPlusEngine,
 * SwWin32WindowCallbacks, and SwWin32PlatformIntegration.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Types here define the seam between portable APIs and the native event and rendering loop on
 * Windows.
 *
 */


/***************************************************************************************************
 * Windows backend for Sw platform integration.
 **************************************************************************************************/

#include "platform/SwPlatformIntegration.h"

#if defined(_WIN32)

#include "core/runtime/SwCoreApplication.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "platform/win/SwWindows.h"
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

class SwVirtualGraphicsEngine {
public:
    /**
     * @brief Destroys the `SwVirtualGraphicsEngine` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwVirtualGraphicsEngine() = default;
    /**
     * @brief Returns the current initialize.
     * @return The current initialize.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void initialize() = 0;
    /**
     * @brief Returns the current shutdown.
     * @return The current shutdown.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void shutdown() = 0;
    /**
     * @brief Returns the current render.
     * @return The current render.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void render() = 0;
};

class SwGdiPlusEngine : public SwVirtualGraphicsEngine {
public:
    /**
     * @brief Returns the current instance.
     * @return The current instance.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwGdiPlusEngine& instance() {
        static SwGdiPlusEngine instance;
        return instance;
    }

    /**
     * @brief Performs the `initialize` operation.
     */
    void initialize() override {
        if (initialized) {
            return;
        }
        gdiplusLib = LoadLibrary(TEXT("gdiplus.dll"));
        if (!gdiplusLib) {
            throw std::runtime_error("Failed to load gdiplus.dll");
        }

        auto startupProc = reinterpret_cast<decltype(&Gdiplus::GdiplusStartup)>(
            GetProcAddress(gdiplusLib, "GdiplusStartup"));
        auto shutdownProc = reinterpret_cast<decltype(&Gdiplus::GdiplusShutdown)>(
            GetProcAddress(gdiplusLib, "GdiplusShutdown"));

        if (!startupProc || !shutdownProc) {
            FreeLibrary(gdiplusLib);
            gdiplusLib = nullptr;
            throw std::runtime_error("Failed to retrieve GDI+ functions from gdiplus.dll");
        }

        gdiplusStartup = startupProc;
        gdiplusShutdown = shutdownProc;

        Gdiplus::GdiplusStartupInput gdiplusStartupInput = {};
        gdiplusStartupInput.GdiplusVersion = 1;

        Gdiplus::Status status = gdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
        if (status != Gdiplus::Ok) {
            FreeLibrary(gdiplusLib);
            gdiplusLib = nullptr;
            throw std::runtime_error("Failed to initialize GDI+");
        }

        initialized = true;
    }

    /**
     * @brief Performs the `shutdown` operation.
     */
    void shutdown() override {
        if (!initialized) {
            return;
        }
        try {
            if (gdiplusShutdown) {
                gdiplusShutdown(gdiplusToken);
            }
        } catch (...) {
        }

        if (gdiplusLib) {
            FreeLibrary(gdiplusLib);
            gdiplusLib = nullptr;
        }

        initialized = false;
    }

    /**
     * @brief Performs the `render` operation.
     */
    void render() override {}

private:
    SwGdiPlusEngine()
        : initialized(false), gdiplusLib(nullptr), gdiplusToken(0),
          gdiplusStartup(nullptr), gdiplusShutdown(nullptr) {}

    ~SwGdiPlusEngine() {
        if (initialized) {
            shutdown();
        }
    }

    SwGdiPlusEngine(const SwGdiPlusEngine&) = delete;
    SwGdiPlusEngine& operator=(const SwGdiPlusEngine&) = delete;

    bool initialized;
    HMODULE gdiplusLib;
    ULONG_PTR gdiplusToken;
    decltype(&Gdiplus::GdiplusStartup) gdiplusStartup;
    decltype(&Gdiplus::GdiplusShutdown) gdiplusShutdown;
};

struct SwWin32WindowCallbacks {
    /**
     * @brief Performs the `function<void` operation.
     * @param HDC Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(HDC, const RECT&)> paintHandler;
    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::function<void()> deleteHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @param SwMouseButton Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(int, int, SwMouseButton, bool, bool, bool)> mousePressHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @param SwMouseButton Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(int, int, SwMouseButton, bool, bool, bool)> mouseReleaseHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @param SwMouseButton Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(int, int, SwMouseButton, bool, bool, bool)> mouseDoubleClickHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(int, int, bool, bool, bool)> mouseMoveHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(int, int, int, bool, bool, bool)> mouseWheelHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param int Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param bool Value passed to the method.
     * @param wchar_t Value passed to the method.
     * @param bool Value passed to the method.
     * @return The requested function<void.
     */
    std::function<void(int, bool, bool, bool, wchar_t, bool)> keyPressHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @param width Width value.
     * @param height Height value.
     * @return The requested function<void.
     */
    std::function<void(int width, int height)> resizeHandler;
};

class SwWin32PlatformIntegration : public SwPlatformIntegration {
public:
    /**
     * @brief Constructs a `SwWin32PlatformIntegration` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwWin32PlatformIntegration() = default;
    /**
     * @brief Destroys the `SwWin32PlatformIntegration` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwWin32PlatformIntegration() override = default;

    /**
     * @brief Performs the `initialize` operation.
     * @param app Value passed to the method.
     */
    void initialize(SwGuiApplication* app) override {
        m_application = app;
        SwGdiPlusEngine::instance().initialize();
        setThreadPriorityHigh();
        m_mainThreadId = GetCurrentThreadId();
    }

    /**
     * @brief Performs the `shutdown` operation.
     */
    void shutdown() override {
        unregisterAllWindows();
        SwGdiPlusEngine::instance().shutdown();
    }

    /**
     * @brief Creates the requested window.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @return The resulting window.
     */
    std::unique_ptr<SwPlatformWindow> createWindow(const std::string&,
                                                   int,
                                                   int,
                                                   const SwWindowCallbacks&) override {
        return nullptr;
    }

    /**
     * @brief Returns the current painter.
     * @return The current painter.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::unique_ptr<SwPlatformPainter> createPainter() override { return nullptr; }

    /**
     * @brief Creates the requested image.
     * @param SwPixelFormat Value passed to the method.
     * @return The resulting image.
     */
    std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize&,
                                                 SwPixelFormat) override {
        return nullptr;
    }

    /**
     * @brief Performs the `processPlatformEvents` operation.
     */
    void processPlatformEvents() override {
        MSG msg = {};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                if (auto* app = SwCoreApplication::instance(false)) {
                    app->exit(static_cast<int>(msg.wParam));
                }
                return;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    /**
     * @brief Performs the `wakeUpGuiThread` operation.
     */
    void wakeUpGuiThread() override {
        if (m_mainThreadId != 0) {
            PostThreadMessage(m_mainThreadId, WM_NULL, 0, 0);
        }
    }

    /**
     * @brief Returns the current available Screens.
     * @return The current available Screens.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::vector<std::string> availableScreens() const override {
        std::vector<std::string> result;
        result.push_back("Primary Display");
        return result;
    }

    /**
     * @brief Returns the current clipboard Text.
     * @return The current clipboard Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString clipboardText() override {
        if (!OpenClipboard(nullptr)) {
            return {};
        }
        SwString result;
        HANDLE handle = GetClipboardData(CF_UNICODETEXT);
        if (handle) {
            wchar_t* data = static_cast<wchar_t*>(GlobalLock(handle));
            if (data) {
                result = SwString::fromWCharArray(data);
                GlobalUnlock(handle);
            }
        }
        CloseClipboard();
        return result;
    }

    /**
     * @brief Sets the clipboard Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setClipboardText(const SwString& text) override {
        if (!OpenClipboard(nullptr)) {
            return;
        }
        EmptyClipboard();
        std::wstring wideText = text.toStdWString();
        const size_t bytes = (wideText.length() + 1) * sizeof(wchar_t);
        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (handle) {
            void* dest = GlobalLock(handle);
            if (dest) {
                memcpy(dest, wideText.c_str(), bytes);
                GlobalUnlock(handle);
                SetClipboardData(CF_UNICODETEXT, handle);
                CloseClipboard();
                return;
            }
            GlobalFree(handle);
        }
        CloseClipboard();
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "win32"; }

    /**
     * @brief Performs the `registerWindow` operation.
     * @param hwnd Value passed to the method.
     * @param callbacks Value passed to the method.
     * @return The requested register Window.
     */
    static void registerWindow(HWND hwnd, const SwWin32WindowCallbacks& callbacks) {
        auto& registry = windowRegistry();
        std::lock_guard<std::mutex> lock(windowMutex());
        if (registry.empty()) {
            registerWindowClass();
        }
        registry[hwnd] = callbacks;
    }

    /**
     * @brief Performs the `deregisterWindow` operation.
     * @param hwnd Value passed to the method.
     * @return The requested deregister Window.
     */
    static void deregisterWindow(HWND hwnd) {
        auto& registry = windowRegistry();
        std::lock_guard<std::mutex> lock(windowMutex());
        auto it = registry.find(hwnd);
        if (it != registry.end()) {
            if (it->second.deleteHandler) {
                runDeleteHandler(it->second.deleteHandler);
            }
            registry.erase(it);
        }
        if (registry.empty()) {
            unregisterWindowClass();
        }
    }

    /**
     * @brief Performs the `WindowProc` operation.
     * @param hwnd Value passed to the method.
     * @param uMsg Value passed to the method.
     * @param wParam Value passed to the method.
     * @param lParam Value passed to the method.
     * @return The requested window Proc.
     */
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        SwWin32WindowCallbacks callbacks;
        {
            std::lock_guard<std::mutex> lock(windowMutex());
            auto& registry = windowRegistry();
            auto it = registry.find(hwnd);
            if (it != registry.end()) {
                callbacks = it->second;
            } else {
                return DefWindowProcW(hwnd, uMsg, wParam, lParam);
            }
        }

        switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (callbacks.paintHandler) {
                runPaintHandler(callbacks.paintHandler, hdc, ps.rcPaint);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: {
            return 1;
        }
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (callbacks.resizeHandler) {
                runResizeHandler(callbacks.resizeHandler, width, height);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            // Ensure we always receive move/up events during drags, even if the cursor leaves the window.
            if (GetCapture() != hwnd) {
                SetCapture(hwnd);
            }
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (callbacks.mousePressHandler) {
                SwMouseButton button = SwMouseButton::NoButton;
                if (uMsg == WM_LBUTTONDOWN) button = SwMouseButton::Left;
                else if (uMsg == WM_RBUTTONDOWN) button = SwMouseButton::Right;
                else if (uMsg == WM_MBUTTONDOWN) button = SwMouseButton::Middle;
                const UINT keyState = static_cast<UINT>(wParam);
                const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
                const bool shiftPressed = (keyState & MK_SHIFT) != 0;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                runMousePressHandler(callbacks.mousePressHandler, x, y, button, ctrlPressed, shiftPressed, altPressed);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (callbacks.mouseDoubleClickHandler) {
                SwMouseButton button = SwMouseButton::NoButton;
                if (uMsg == WM_LBUTTONDBLCLK) button = SwMouseButton::Left;
                else if (uMsg == WM_RBUTTONDBLCLK) button = SwMouseButton::Right;
                else if (uMsg == WM_MBUTTONDBLCLK) button = SwMouseButton::Middle;
                const UINT keyState = static_cast<UINT>(wParam);
                const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
                const bool shiftPressed = (keyState & MK_SHIFT) != 0;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                runMouseDoubleClickHandler(callbacks.mouseDoubleClickHandler,
                                           x,
                                           y,
                                           button,
                                           ctrlPressed,
                                           shiftPressed,
                                           altPressed);
            }
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (callbacks.mouseReleaseHandler) {
                SwMouseButton button = SwMouseButton::NoButton;
                if (uMsg == WM_LBUTTONUP) button = SwMouseButton::Left;
                else if (uMsg == WM_RBUTTONUP) button = SwMouseButton::Right;
                else if (uMsg == WM_MBUTTONUP) button = SwMouseButton::Middle;
                const UINT keyState = static_cast<UINT>(wParam);
                const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
                const bool shiftPressed = (keyState & MK_SHIFT) != 0;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                runMouseReleaseHandler(callbacks.mouseReleaseHandler, x, y, button, ctrlPressed, shiftPressed, altPressed);
            }
            // Release capture once all mouse buttons are up.
            const UINT keyState = static_cast<UINT>(wParam);
            if ((keyState & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0 && GetCapture() == hwnd) {
                ReleaseCapture();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            if (callbacks.mouseMoveHandler) {
                const UINT keyState = static_cast<UINT>(wParam);
                const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
                const bool shiftPressed = (keyState & MK_SHIFT) != 0;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                runMouseMoveHandler(callbacks.mouseMoveHandler, x, y, ctrlPressed, shiftPressed, altPressed);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (callbacks.mouseWheelHandler) {
                POINT pt;
                pt.x = static_cast<short>(LOWORD(lParam));
                pt.y = static_cast<short>(HIWORD(lParam));
                ScreenToClient(hwnd, &pt);

                const int delta = static_cast<short>(HIWORD(wParam));
                const UINT keyState = LOWORD(wParam);
                const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
                const bool shiftPressed = (keyState & MK_SHIFT) != 0;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

                runMouseWheelHandler(callbacks.mouseWheelHandler,
                                    pt.x,
                                    pt.y,
                                    delta,
                                    ctrlPressed,
                                    shiftPressed,
                                    altPressed);
            }
            return 0;
        }
        case WM_MOUSEHWHEEL: {
            if (callbacks.mouseWheelHandler) {
                POINT pt;
                pt.x = static_cast<short>(LOWORD(lParam));
                pt.y = static_cast<short>(HIWORD(lParam));
                ScreenToClient(hwnd, &pt);

                // The widget stack currently exposes a single wheel delta plus modifiers.
                // Map horizontal wheel gestures to Shift+wheel so existing horizontal
                // scrolling code paths (scroll areas, table/tree views, horizontal bars)
                // can consume trackpad left/right input without a wider event API change.
                const int delta = -static_cast<short>(HIWORD(wParam));
                const UINT keyState = LOWORD(wParam);
                const bool ctrlPressed = (keyState & MK_CONTROL) != 0;
                const bool shiftPressed = true;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

                runMouseWheelHandler(callbacks.mouseWheelHandler,
                                    pt.x,
                                    pt.y,
                                    delta,
                                    ctrlPressed,
                                    shiftPressed,
                                    altPressed);
            }
            return 0;
        }
        case WM_POINTERWHEEL: {
            if (callbacks.mouseWheelHandler) {
                POINT pt;
                pt.x = static_cast<short>(LOWORD(lParam));
                pt.y = static_cast<short>(HIWORD(lParam));
                ScreenToClient(hwnd, &pt);

                const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

                runMouseWheelHandler(callbacks.mouseWheelHandler,
                                    pt.x,
                                    pt.y,
                                    delta,
                                    ctrlPressed,
                                    shiftPressed,
                                    altPressed);
            }
            return 0;
        }
        case WM_POINTERHWHEEL: {
            if (callbacks.mouseWheelHandler) {
                POINT pt;
                pt.x = static_cast<short>(LOWORD(lParam));
                pt.y = static_cast<short>(HIWORD(lParam));
                ScreenToClient(hwnd, &pt);

                // Precision touchpads can emit horizontal wheel input through the
                // pointer-message path instead of WM_MOUSEHWHEEL.
                const int delta = -GET_WHEEL_DELTA_WPARAM(wParam);
                const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shiftPressed = true;
                const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

                runMouseWheelHandler(callbacks.mouseWheelHandler,
                                    pt.x,
                                    pt.y,
                                    delta,
                                    ctrlPressed,
                                    shiftPressed,
                                    altPressed);
            }
            return 0;
        }
        case WM_GESTURENOTIFY: {
            enableGestureInput_(hwnd);
            appendScrollInputLog_("win32", "WM_GESTURENOTIFY", 0, 0, 0, false, false, false, "enable gestures");
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
        case WM_GESTURE: {
            if (handlePanGesture_(hwnd, callbacks, lParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
        case WM_HSCROLL:
        case WM_VSCROLL: {
            if (handleLegacyScrollMessage_(hwnd, callbacks, uMsg, wParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
        case WM_KEYDOWN: {
            int keyCode = static_cast<int>(wParam);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

            // Peek at the WM_CHAR that TranslateMessage already posted for this keystroke.
            // wParam of WM_CHAR is already a Unicode codepoint (UTF-16 BMP).
            // textProvided=true always so widgets never fall back to translateCharacter()
            // (which would insert wrong chars for dead keys that yield no WM_CHAR).
            wchar_t textChar = L'\0';
            {
                MSG charMsg = {};
                if (PeekMessageW(&charMsg, hwnd, WM_CHAR, WM_CHAR, PM_REMOVE)) {
                    const wchar_t ch = static_cast<wchar_t>(charMsg.wParam);
                    if (ch >= 0x20) { // skip control chars (BS=8, CR=13, etc.)
                        textChar = ch;
                    }
                }
            }

            if (callbacks.keyPressHandler) {
                runKeyPressHandler(callbacks.keyPressHandler, keyCode, ctrlPressed, shiftPressed, altPressed, textChar, /*textProvided=*/true);
            }
            return 0;
        }
        case WM_DESTROY: {
            clearGesturePanState_(hwnd);
            if (callbacks.deleteHandler) {
                runDeleteHandler(callbacks.deleteHandler);
            }
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
    }

    /**
     * @brief Returns the current register Window Class.
     * @return The current register Window Class.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static void registerWindowClass() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SwWin32PlatformIntegration::WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"SwMainWindowClass";
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
    }

    /**
     * @brief Returns the current unregister Window Class.
     * @return The current unregister Window Class.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static void unregisterWindowClass() {
        UnregisterClassW(L"SwMainWindowClass", GetModuleHandle(nullptr));
    }

private:
    static void runPaintHandler(const std::function<void(HDC, const RECT&)>& handler,
                                HDC hdc,
                                const RECT& rect) {
        handler(hdc, rect);
    }

    static void runResizeHandler(const std::function<void(int, int)>& handler, int width, int height) {
            handler(width, height);
    }

    static void runMousePressHandler(const std::function<void(int, int, SwMouseButton, bool, bool, bool)>& handler,
                                     int x,
                                     int y,
                                     SwMouseButton button,
                                     bool ctrlPressed,
                                     bool shiftPressed,
                                     bool altPressed) {
        SwCoreApplication::instance()->postEvent([handler, x, y, button, ctrlPressed, shiftPressed, altPressed]() {
            handler(x, y, button, ctrlPressed, shiftPressed, altPressed);
        });
    }

    static void runMouseDoubleClickHandler(const std::function<void(int, int, SwMouseButton, bool, bool, bool)>& handler,
                                           int x,
                                           int y,
                                           SwMouseButton button,
                                           bool ctrlPressed,
                                           bool shiftPressed,
                                           bool altPressed) {
        SwCoreApplication::instance()->postEvent([handler, x, y, button, ctrlPressed, shiftPressed, altPressed]() {
            handler(x, y, button, ctrlPressed, shiftPressed, altPressed);
        });
    }

    static void runMouseReleaseHandler(const std::function<void(int, int, SwMouseButton, bool, bool, bool)>& handler,
                                       int x,
                                       int y,
                                       SwMouseButton button,
                                       bool ctrlPressed,
                                       bool shiftPressed,
                                       bool altPressed) {
        SwCoreApplication::instance()->postEvent([handler, x, y, button, ctrlPressed, shiftPressed, altPressed]() {
            handler(x, y, button, ctrlPressed, shiftPressed, altPressed);
        });
    }

    static void runMouseMoveHandler(const std::function<void(int, int, bool, bool, bool)>& handler,
                                    int x,
                                    int y,
                                    bool ctrlPressed,
                                    bool shiftPressed,
                                    bool altPressed) {
        SwCoreApplication::instance()->postEvent([handler, x, y, ctrlPressed, shiftPressed, altPressed]() {
            handler(x, y, ctrlPressed, shiftPressed, altPressed);
        });
    }

    static void runMouseWheelHandler(const std::function<void(int, int, int, bool, bool, bool)>& handler,
                                     int x,
                                     int y,
                                     int delta,
                                     bool ctrlPressed,
                                     bool shiftPressed,
                                     bool altPressed) {
        appendScrollInputLog_("dispatch",
                              shiftPressed ? "wheel-horizontal" : "wheel-vertical",
                              x,
                              y,
                              delta,
                              ctrlPressed,
                              shiftPressed,
                              altPressed,
                              "");
        SwCoreApplication::instance()->postEvent([handler, x, y, delta, ctrlPressed, shiftPressed, altPressed]() {
            handler(x, y, delta, ctrlPressed, shiftPressed, altPressed);
        });
    }

    static void runKeyPressHandler(const std::function<void(int, bool, bool, bool, wchar_t, bool)>& handler,
                                   int keyCode,
                                   bool ctrlPressed,
                                   bool shiftPressed,
                                   bool altPressed,
                                   wchar_t textChar,
                                   bool textProvided) {
        SwCoreApplication::instance()->postEvent([handler, keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided]() {
            handler(keyCode, ctrlPressed, shiftPressed, altPressed, textChar, textProvided);
        });
    }

    static void runDeleteHandler(const std::function<void()>& handler) {
        SwCoreApplication::instance()->postEvent([handler]() {
            handler();
        });
    }

    static void unregisterAllWindows() {
        auto& registry = windowRegistry();
        std::lock_guard<std::mutex> lock(windowMutex());
        for (auto& pair : registry) {
            if (pair.second.deleteHandler) {
                pair.second.deleteHandler();
            }
        }
        registry.clear();
    }

    static void setThreadPriorityHigh() {
        HANDLE thread = GetCurrentThread();
        // Boost process priority first (best effort).
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        // Try the highest thread priority available; fall back if it fails.
        if (!SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL)) {
            SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
        }
    }

    struct GesturePanState {
        POINT lastPoint{0, 0};
        bool hasLastPoint{false};
    };

    static bool handlePanGesture_(HWND hwnd, const SwWin32WindowCallbacks& callbacks, LPARAM lParam) {
        GESTUREINFO gestureInfo = {};
        gestureInfo.cbSize = sizeof(gestureInfo);
        if (!GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lParam), &gestureInfo)) {
            appendScrollInputLog_("win32", "WM_GESTURE", 0, 0, 0, false, false, false, "GetGestureInfo failed");
            return false;
        }

        if (gestureInfo.dwID != GID_PAN) {
            appendScrollInputLog_("win32",
                                  "WM_GESTURE",
                                  gestureInfo.ptsLocation.x,
                                  gestureInfo.ptsLocation.y,
                                  0,
                                  false,
                                  false,
                                  false,
                                  "non-pan");
            return false;
        }

        POINT clientPoint = {
            static_cast<LONG>(gestureInfo.ptsLocation.x),
            static_cast<LONG>(gestureInfo.ptsLocation.y)
        };
        ScreenToClient(hwnd, &clientPoint);

        int deltaX = 0;
        int deltaY = 0;
        bool hadLastPoint = false;
        {
            std::lock_guard<std::mutex> lock(gesturePanMutex_());
            GesturePanState& state = gesturePanRegistry_()[hwnd];
            hadLastPoint = state.hasLastPoint;
            if (hadLastPoint) {
                deltaX = clientPoint.x - state.lastPoint.x;
                deltaY = clientPoint.y - state.lastPoint.y;
            }
            if ((gestureInfo.dwFlags & GF_END) != 0) {
                gesturePanRegistry_().erase(hwnd);
            } else {
                state.lastPoint = clientPoint;
                state.hasLastPoint = true;
            }
        }

        std::ostringstream note;
        note << "pan flags=" << gestureInfo.dwFlags << " dx=" << deltaX << " dy=" << deltaY;
        appendScrollInputLog_("win32",
                              "WM_GESTURE/GID_PAN",
                              clientPoint.x,
                              clientPoint.y,
                              0,
                              false,
                              false,
                              false,
                              note.str().c_str());

        if (hadLastPoint && callbacks.mouseWheelHandler) {
            const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (absInt_(deltaX) >= absInt_(deltaY)) {
                if (deltaX != 0) {
                    runMouseWheelHandler(callbacks.mouseWheelHandler,
                                         clientPoint.x,
                                         clientPoint.y,
                                         deltaX,
                                         ctrlPressed,
                                         true,
                                         altPressed);
                }
            } else if (deltaY != 0) {
                runMouseWheelHandler(callbacks.mouseWheelHandler,
                                     clientPoint.x,
                                     clientPoint.y,
                                     deltaY,
                                     ctrlPressed,
                                     shiftPressed,
                                     altPressed);
            }
        }

        CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lParam));
        return true;
    }

    static bool handleLegacyScrollMessage_(HWND hwnd,
                                           const SwWin32WindowCallbacks& callbacks,
                                           UINT message,
                                           WPARAM wParam) {
        const bool horizontal = (message == WM_HSCROLL);
        const UINT request = LOWORD(wParam);
        int delta = 0;

        if (horizontal) {
            if (request == SB_LINELEFT) {
                delta = WHEEL_DELTA;
            } else if (request == SB_LINERIGHT) {
                delta = -WHEEL_DELTA;
            } else if (request == SB_PAGELEFT) {
                delta = 3 * WHEEL_DELTA;
            } else if (request == SB_PAGERIGHT) {
                delta = -3 * WHEEL_DELTA;
            }
        } else {
            if (request == SB_LINEUP) {
                delta = WHEEL_DELTA;
            } else if (request == SB_LINEDOWN) {
                delta = -WHEEL_DELTA;
            } else if (request == SB_PAGEUP) {
                delta = 3 * WHEEL_DELTA;
            } else if (request == SB_PAGEDOWN) {
                delta = -3 * WHEEL_DELTA;
            }
        }

        std::ostringstream note;
        note << "request=" << request;
        POINT clientPoint = {0, 0};
        GetCursorPos(&clientPoint);
        ScreenToClient(hwnd, &clientPoint);
        appendScrollInputLog_("win32",
                              horizontal ? "WM_HSCROLL" : "WM_VSCROLL",
                              clientPoint.x,
                              clientPoint.y,
                              delta,
                              false,
                              horizontal,
                              false,
                              note.str().c_str());

        if (delta == 0 || !callbacks.mouseWheelHandler) {
            return false;
        }

        const bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftPressed = horizontal ? true : ((GetKeyState(VK_SHIFT) & 0x8000) != 0);
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        runMouseWheelHandler(callbacks.mouseWheelHandler,
                             clientPoint.x,
                             clientPoint.y,
                             delta,
                             ctrlPressed,
                             shiftPressed,
                             altPressed);
        return true;
    }

    static void enableGestureInput_(HWND hwnd) {
        GESTURECONFIG gestureConfig = {};
        gestureConfig.dwID = 0;
        gestureConfig.dwWant = GC_ALLGESTURES;
        gestureConfig.dwBlock = 0;
        SetGestureConfig(hwnd, 0, 1, &gestureConfig, sizeof(gestureConfig));
    }

    static void clearGesturePanState_(HWND hwnd) {
        std::lock_guard<std::mutex> lock(gesturePanMutex_());
        gesturePanRegistry_().erase(hwnd);
    }

    static int absInt_(int value) {
        return value < 0 ? -value : value;
    }

    static bool scrollInputLogEnabled_() {
        static const bool enabled = []() {
            wchar_t buffer[4] = {};
            return GetEnvironmentVariableW(L"SW_DEBUG_SCROLL_INPUT", buffer, 4) > 0;
        }();
        return enabled;
    }

    static std::wstring scrollInputLogPath_() {
        wchar_t tempPath[MAX_PATH] = {};
        DWORD length = GetTempPathW(MAX_PATH, tempPath);
        if (length == 0 || length >= MAX_PATH) {
            return L"sw_scroll_input.log";
        }
        std::wstring path(tempPath);
        path += L"sw_scroll_input.log";
        return path;
    }

    static void appendScrollInputLog_(const char* source,
                                      const char* message,
                                      int x,
                                      int y,
                                      int delta,
                                      bool ctrlPressed,
                                      bool shiftPressed,
                                      bool altPressed,
                                      const char* note) {
        if (!scrollInputLogEnabled_()) {
            return;
        }

        SYSTEMTIME now = {};
        GetLocalTime(&now);

        std::ostringstream stream;
        stream << now.wHour << ":" << now.wMinute << ":" << now.wSecond << "." << now.wMilliseconds
               << " [" << (source ? source : "") << "]"
               << " " << (message ? message : "")
               << " x=" << x
               << " y=" << y
               << " delta=" << delta
               << " ctrl=" << (ctrlPressed ? 1 : 0)
               << " shift=" << (shiftPressed ? 1 : 0)
               << " alt=" << (altPressed ? 1 : 0);
        if (note && note[0] != '\0') {
            stream << " note=" << note;
        }
        stream << "\r\n";

        const std::string line = stream.str();
        std::lock_guard<std::mutex> lock(scrollInputLogMutex_());
        HANDLE handle = CreateFileW(scrollInputLogPath_().c_str(),
                                    FILE_APPEND_DATA,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr,
                                    OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD written = 0;
        WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(handle);
    }

    SwGuiApplication* m_application{nullptr};
    DWORD m_mainThreadId{0};

    static std::map<HWND, SwWin32WindowCallbacks>& windowRegistry() {
        static std::map<HWND, SwWin32WindowCallbacks> registry;
        return registry;
    }

    static std::mutex& windowMutex() {
        static std::mutex mutex;
        return mutex;
    }

    static std::map<HWND, GesturePanState>& gesturePanRegistry_() {
        static std::map<HWND, GesturePanState> registry;
        return registry;
    }

    static std::mutex& gesturePanMutex_() {
        static std::mutex mutex;
        return mutex;
    }

    static std::mutex& scrollInputLogMutex_() {
        static std::mutex mutex;
        return mutex;
    }
};

inline std::unique_ptr<SwPlatformIntegration> SwCreateWin32PlatformIntegration() {
    return std::unique_ptr<SwPlatformIntegration>(new SwWin32PlatformIntegration());
}

#else

inline std::unique_ptr<SwPlatformIntegration> SwCreateWin32PlatformIntegration() {
    return nullptr;
}

#endif
