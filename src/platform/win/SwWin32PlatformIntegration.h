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
#include <string>
#include <vector>
#include <stdexcept>

class SwVirtualGraphicsEngine {
public:
    virtual ~SwVirtualGraphicsEngine() = default;
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    virtual void render() = 0;
};

class SwGdiPlusEngine : public SwVirtualGraphicsEngine {
public:
    static SwGdiPlusEngine& instance() {
        static SwGdiPlusEngine instance;
        return instance;
    }

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
    std::function<void(HDC, const RECT&)> paintHandler;
    std::function<void()> deleteHandler;
    std::function<void(int, int, SwMouseButton, bool, bool, bool)> mousePressHandler;
    std::function<void(int, int, SwMouseButton, bool, bool, bool)> mouseReleaseHandler;
    std::function<void(int, int, SwMouseButton, bool, bool, bool)> mouseDoubleClickHandler;
    std::function<void(int, int, bool, bool, bool)> mouseMoveHandler;
    std::function<void(int, int, int, bool, bool, bool)> mouseWheelHandler;
    std::function<void(int, bool, bool, bool)> keyPressHandler;
    std::function<void(int width, int height)> resizeHandler;
};

class SwWin32PlatformIntegration : public SwPlatformIntegration {
public:
    SwWin32PlatformIntegration() = default;
    ~SwWin32PlatformIntegration() override = default;

    void initialize(SwGuiApplication* app) override {
        m_application = app;
        SwGdiPlusEngine::instance().initialize();
        setThreadPriorityHigh();
        m_mainThreadId = GetCurrentThreadId();
    }

    void shutdown() override {
        unregisterAllWindows();
        SwGdiPlusEngine::instance().shutdown();
    }

    std::unique_ptr<SwPlatformWindow> createWindow(const std::string&,
                                                   int,
                                                   int,
                                                   const SwWindowCallbacks&) override {
        return nullptr;
    }

    std::unique_ptr<SwPlatformPainter> createPainter() override { return nullptr; }

    std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize&,
                                                 SwPixelFormat) override {
        return nullptr;
    }

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

    void wakeUpGuiThread() override {
        if (m_mainThreadId != 0) {
            PostThreadMessage(m_mainThreadId, WM_NULL, 0, 0);
        }
    }

    std::vector<std::string> availableScreens() const override {
        std::vector<std::string> result;
        result.push_back("Primary Display");
        return result;
    }

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

    const char* name() const override { return "win32"; }

    static void registerWindow(HWND hwnd, const SwWin32WindowCallbacks& callbacks) {
        auto& registry = windowRegistry();
        std::lock_guard<std::mutex> lock(windowMutex());
        if (registry.empty()) {
            registerWindowClass();
        }
        registry[hwnd] = callbacks;
    }

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

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        SwWin32WindowCallbacks callbacks;
        {
            std::lock_guard<std::mutex> lock(windowMutex());
            auto& registry = windowRegistry();
            auto it = registry.find(hwnd);
            if (it != registry.end()) {
                callbacks = it->second;
            } else {
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
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
        case WM_KEYDOWN: {
            int keyCode = static_cast<int>(wParam);
            bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (callbacks.keyPressHandler) {
                runKeyPressHandler(callbacks.keyPressHandler, keyCode, ctrlPressed, shiftPressed, altPressed);
            }
            return 0;
        }
        case WM_DESTROY: {
            if (callbacks.deleteHandler) {
                runDeleteHandler(callbacks.deleteHandler);
            }
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }

    static void registerWindowClass() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SwWin32PlatformIntegration::WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"SwMainWindowClass";
        wc.style = CS_DBLCLKS;
        RegisterClassW(&wc);
    }

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
        SwCoreApplication::instance()->postEvent([handler, x, y, delta, ctrlPressed, shiftPressed, altPressed]() {
            handler(x, y, delta, ctrlPressed, shiftPressed, altPressed);
        });
    }

    static void runKeyPressHandler(const std::function<void(int, bool, bool, bool)>& handler,
                                   int keyCode,
                                   bool ctrlPressed,
                                   bool shiftPressed,
                                   bool altPressed) {
        SwCoreApplication::instance()->postEvent([handler, keyCode, ctrlPressed, shiftPressed, altPressed]() {
            handler(keyCode, ctrlPressed, shiftPressed, altPressed);
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
};

inline std::unique_ptr<SwPlatformIntegration> SwCreateWin32PlatformIntegration() {
    return std::unique_ptr<SwPlatformIntegration>(new SwWin32PlatformIntegration());
}

#else

inline std::unique_ptr<SwPlatformIntegration> SwCreateWin32PlatformIntegration() {
    return nullptr;
}

#endif
