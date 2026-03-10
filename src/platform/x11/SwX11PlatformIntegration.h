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
 * @file src/platform/x11/SwX11PlatformIntegration.h
 * @ingroup platform_backends
 * @brief Declares the public interface exposed by SwX11PlatformIntegration in the CoreSw X11
 * platform integration layer.
 *
 * This header belongs to the CoreSw X11 platform integration layer. It binds portable framework
 * abstractions to concrete X11 windowing, painting, and input services.
 *
 * Within that layer, this file focuses on the X11 platform integration interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are SwX11PlatformImage, SwX11PlatformPainter,
 * SwX11PlatformWindow, and SwX11PlatformIntegration.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Types here define the seam between portable APIs and the native event and rendering loop on X11
 * systems.
 *
 */


/***************************************************************************************************
 * This header introduces the Linux/X11 backend skeleton for the Sw platform abstraction layer.
 * The implementation purposefully focuses on structure and extensibility rather than feature
 * completeness so that future iterations can progressively plug rendering, input, and widget code.
 **************************************************************************************************/

#include "platform/SwPlatformIntegration.h"
#include "core/gui/SwWidgetPlatformAdapter.h"
#include "core/runtime/SwCoreApplication.h"
#include "platform/x11/SwX11Painter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <cmath>

#if defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

class SwX11PlatformIntegration;

#if defined(__linux__)

class SwX11PlatformImage : public SwPlatformImage {
public:
    /**
     * @brief Constructs a `SwX11PlatformImage` instance.
     * @param size Size value used by the operation.
     * @param format Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwX11PlatformImage(const SwPlatformSize& size, SwPixelFormat format)
        : m_size(size), m_format(format) {
        const int bytesPerPixel = (format == SwPixelFormat::RGB24 || format == SwPixelFormat::BGR24) ? 3 : 4;
        m_pitch = size.width * bytesPerPixel;
        m_pixels.resize(static_cast<std::size_t>(m_pitch) * size.height);
    }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPlatformSize size() const override { return m_size; }
    /**
     * @brief Returns the current format.
     * @return The current format.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPixelFormat format() const override { return m_format; }
    /**
     * @brief Returns the current pitch.
     * @return The current pitch.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int pitch() const override { return m_pitch; }
    /**
     * @brief Performs the `pixels` operation.
     * @return The requested pixels.
     */
    std::uint8_t* pixels() override { return m_pixels.empty() ? nullptr : m_pixels.data(); }
    /**
     * @brief Performs the `pixels` operation.
     * @return The requested pixels.
     */
    const std::uint8_t* pixels() const override { return m_pixels.empty() ? nullptr : m_pixels.data(); }

    /**
     * @brief Clears the current object state.
     * @param argb Value passed to the method.
     */
    void clear(std::uint32_t argb) override {
        if (m_pixels.empty()) {
            return;
        }

        const std::uint8_t a = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
        const std::uint8_t r = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
        const std::uint8_t g = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
        const std::uint8_t b = static_cast<std::uint8_t>(argb & 0xFF);

        for (int y = 0; y < m_size.height; ++y) {
            std::uint8_t* row = m_pixels.data() + y * m_pitch;
            for (int x = 0; x < m_size.width; ++x) {
                std::uint8_t* pixel = row + x * ((m_format == SwPixelFormat::RGB24 || m_format == SwPixelFormat::BGR24) ? 3 : 4);
                if (m_format == SwPixelFormat::RGB24) {
                    pixel[0] = r;
                    pixel[1] = g;
                    pixel[2] = b;
                } else if (m_format == SwPixelFormat::BGR24) {
                    pixel[0] = b;
                    pixel[1] = g;
                    pixel[2] = r;
                } else if (m_format == SwPixelFormat::ABGR32) {
                    pixel[0] = a;
                    pixel[1] = b;
                    pixel[2] = g;
                    pixel[3] = r;
                } else {
                    pixel[0] = b;
                    pixel[1] = g;
                    pixel[2] = r;
                    pixel[3] = a;
                }
            }
        }
    }

private:
    SwPlatformSize m_size{};
    SwPixelFormat m_format{SwPixelFormat::Unknown};
    int m_pitch{0};
    std::vector<std::uint8_t> m_pixels;
};

class SwX11PlatformWindow : public SwPlatformWindow {
public:
    /**
     * @brief Constructs a `SwX11PlatformWindow` instance.
     * @param integration Value passed to the method.
     * @param display Value passed to the method.
     * @param screen Value passed to the method.
     * @param title Title text applied by the operation.
     * @param width Width value.
     * @param height Height value.
     * @param callbacks Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwX11PlatformWindow(SwX11PlatformIntegration* integration,
                        Display* display,
                        int screen,
                        const std::string& title,
                        int width,
                        int height,
                        const SwWindowCallbacks& callbacks);

    /**
     * @brief Destroys the `SwX11PlatformWindow` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwX11PlatformWindow() override;

    /**
     * @brief Performs the `show` operation.
     */
    void show() override;
    /**
     * @brief Performs the `hide` operation.
     */
    void hide() override;
    /**
     * @brief Sets the title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitle(const std::string& title) override;
    /**
     * @brief Performs the `resize` operation.
     * @param width Width value.
     * @param height Height value.
     */
    void resize(int width, int height) override;
    /**
     * @brief Performs the `move` operation.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     */
    void move(int x, int y) override;
    /**
     * @brief Performs the `requestUpdate` operation.
     */
    void requestUpdate() override;
    /**
     * @brief Returns the current native Handle.
     * @return The current native Handle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void* nativeHandle() const override;
    /**
     * @brief Returns the native X11 display associated with this window.
     * @return The current native display.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void* nativeDisplay() const override;

    ::Window handle() const { return m_window; }
    /**
     * @brief Returns the current display.
     * @return The current display.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Display* display() const { return m_display; }
    /**
     * @brief Returns the current callbacks.
     * @return The current callbacks.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwWindowCallbacks& callbacks() const { return m_callbacks; }
    /**
     * @brief Sets the callbacks.
     * @param callbacks Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCallbacks(const SwWindowCallbacks& callbacks) { m_callbacks = callbacks; }

    /**
     * @brief Updates the size managed by the object.
     * @param width Width value.
     * @param height Height value.
     */
    void updateSize(int width, int height) { m_size = {width, height}; }

private:
    friend class SwX11PlatformIntegration;
    void releaseNativeResources();

    SwX11PlatformIntegration* m_integration{nullptr};
    Display* m_display{nullptr};
    ::Window m_window{0};
    GC m_gc{0};
    SwWindowCallbacks m_callbacks;
    SwPlatformSize m_size{};
};

class SwX11PlatformIntegration : public SwPlatformIntegration {
public:
    /**
     * @brief Constructs a `SwX11PlatformIntegration` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwX11PlatformIntegration() = default;
    /**
     * @brief Destroys the `SwX11PlatformIntegration` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwX11PlatformIntegration() override { shutdown(); }

    /**
     * @brief Performs the `initialize` operation.
     * @param app Value passed to the method.
     */
    void initialize(SwGuiApplication* app) override;
    /**
     * @brief Performs the `shutdown` operation.
     */
    void shutdown() override;

    /**
     * @brief Creates the requested window.
     * @param title Title text applied by the operation.
     * @param width Width value.
     * @param height Height value.
     * @param callbacks Value passed to the method.
     * @return The resulting window.
     */
    std::unique_ptr<SwPlatformWindow> createWindow(const std::string& title,
                                                   int width,
                                                   int height,
                                                   const SwWindowCallbacks& callbacks,
                                                   const SwPlatformWindowOptions& options = {}) override;

    /**
     * @brief Returns the current painter.
     * @return The current painter.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::unique_ptr<SwPlatformPainter> createPainter() override {
        if (!m_display) {
            throw std::runtime_error("X11 backend must be initialized before creating a painter.");
        }
        return std::unique_ptr<SwPlatformPainter>(new SwX11Painter());
    }

    /**
     * @brief Creates the requested image.
     * @param size Size value used by the operation.
     * @param format Value passed to the method.
     * @return The resulting image.
     */
    std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize& size,
                                                 SwPixelFormat format) override {
        SwPixelFormat resolved = (format == SwPixelFormat::Unknown) ? SwPixelFormat::ARGB32 : format;
        if (resolved != SwPixelFormat::ARGB32 &&
            resolved != SwPixelFormat::ABGR32 &&
            resolved != SwPixelFormat::RGB24 &&
            resolved != SwPixelFormat::BGR24) {
            throw std::runtime_error("Unsupported pixel format for X11 images.");
        }
        return std::unique_ptr<SwPlatformImage>(new SwX11PlatformImage(size, resolved));
    }

    /**
     * @brief Performs the `processPlatformEvents` operation.
     */
    void processPlatformEvents() override;
    /**
     * @brief Performs the `wakeUpGuiThread` operation.
     */
    void wakeUpGuiThread() override {
        if (m_display) {
            XFlush(m_display);
        }
    }

    /**
     * @brief Returns the current available Screens.
     * @return The current available Screens.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::vector<std::string> availableScreens() const override {
        std::vector<std::string> screens;
        if (!m_display) {
            return screens;
        }

        const int count = ScreenCount(m_display);
        screens.reserve(count);
        for (int index = 0; index < count; ++index) {
            std::ostringstream stream;
            stream << "Screen " << index
                   << " (" << DisplayWidth(m_display, index)
                   << "x" << DisplayHeight(m_display, index) << ")";
            screens.push_back(stream.str());
        }
        return screens;
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "x11"; }

    /**
     * @brief Returns the current clipboard Text.
     * @return The current clipboard Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString clipboardText() override;

    /**
     * @brief Sets the clipboard Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setClipboardText(const SwString& text) override;

    /**
     * @brief Returns the current display.
     * @return The current display.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Display* display() const { return m_display; }
    /**
     * @brief Returns the current delete Window Atom.
     * @return The current delete Window Atom.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Atom deleteWindowAtom() const { return m_deleteWindowAtom; }

    /**
     * @brief Performs the `registerWindow` operation.
     * @param windowId Value passed to the method.
     * @param window Value passed to the method.
     */
    void registerWindow(::Window windowId, SwX11PlatformWindow* window);
    /**
     * @brief Performs the `unregisterWindow` operation.
     * @param windowId Value passed to the method.
     */
    void unregisterWindow(::Window windowId);
    /**
     * @brief Performs the `findWindow` operation.
     * @param windowId Value passed to the method.
     * @return The requested find Window.
     */
    SwX11PlatformWindow* findWindow(::Window windowId) const;

private:
    void dispatchEvent(const XEvent& event);
    void handleMouseEvent(const XEvent& event, bool pressed);
    void handleMotionEvent(const XEvent& event);
    void handleLeaveEvent(const XEvent& event);
    void handleKeyEvent(const XEvent& event, bool pressed);
    void handleConfigureEvent(const XConfigureEvent& event);
    void handleClientMessage(const XClientMessageEvent& event);
    void handleSelectionRequest(const XSelectionRequestEvent& event);
    void handleSelectionClear(const XSelectionClearEvent& event);

    SwMouseEvent toMouseEvent(const XButtonEvent& event, SwMouseButton button, int clickCount);
    SwMouseEvent toMouseMoveEvent(const XMotionEvent& event);
    SwKeyEvent toKeyEvent(const XKeyEvent& event);
    bool isDoubleClick(const XButtonEvent& event, SwMouseButton button);
    SwMouseButton translateButton(unsigned int xButton) const;
    void ensureClipboardWindow();
    void destroyClipboardWindow();
    SwString requestClipboardText(Atom selection);

    struct SelectionWaitContext {
        Atom selection{None};
        Atom property{None};

        /**
         * @brief Constructs a `SelectionWaitContext` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        SelectionWaitContext() = default;
        /**
         * @brief Constructs a `SelectionWaitContext` instance.
         * @param sel Value passed to the method.
         * @param prop Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        SelectionWaitContext(Atom sel, Atom prop)
            : selection(sel), property(prop) {}
    };

    static Bool selectionPredicate(Display*, XEvent*, XPointer);

    SwGuiApplication* m_application{nullptr};
    Display* m_display{nullptr};
    int m_screen{0};
    Atom m_deleteWindowAtom{0};
    ::Window m_clipboardWindow{0};
    Atom m_clipboardAtom{0};
    Atom m_targetsAtom{0};
    Atom m_utf8StringAtom{0};
    Atom m_textAtom{0};
    Atom m_clipboardProperty{0};

    mutable std::mutex m_windowMutex;
    std::unordered_map<::Window, SwX11PlatformWindow*> m_windows;

    unsigned long m_lastClickTime{0};
    SwMouseButton m_lastClickButton{SwMouseButton::NoButton};
    SwPlatformPoint m_lastClickPosition{};

    SwString m_clipboard;
    mutable std::mutex m_clipboardMutex;
    size_t m_eventFdWaitableId{0};
};

inline SwX11PlatformWindow::SwX11PlatformWindow(SwX11PlatformIntegration* integration,
                                                Display* display,
                                                int screen,
                                                const std::string& title,
                                                int width,
                                                int height,
                                                const SwWindowCallbacks& callbacks)
    : m_integration(integration), m_display(display), m_callbacks(callbacks), m_size{width, height} {
    if (!m_display) {
        throw std::runtime_error("Cannot create X11 window without a display.");
    }

    const unsigned int clampedWidth = static_cast<unsigned int>(std::max(1, width));
    const unsigned int clampedHeight = static_cast<unsigned int>(std::max(1, height));

    XSetWindowAttributes attributes{};
    // Windows 11 default surface: rgb(249, 249, 249)
    attributes.background_pixel = (249u << 16) | (249u << 8) | 249u;
    attributes.border_pixel = BlackPixel(m_display, screen);
    attributes.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask | LeaveWindowMask | StructureNotifyMask;

    m_window = XCreateWindow(m_display,
                             RootWindow(m_display, screen),
                             0,
                             0,
                             clampedWidth,
                             clampedHeight,
                             0,
                             DefaultDepth(m_display, screen),
                             InputOutput,
                             DefaultVisual(m_display, screen),
                             CWBackPixel | CWBorderPixel | CWEventMask,
                             &attributes);

    if (!m_window) {
        throw std::runtime_error("Failed to create X11 window.");
    }

    XSelectInput(m_display,
                 m_window,
                 ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | LeaveWindowMask | StructureNotifyMask);

    Atom protocols[1] = { integration->deleteWindowAtom() };
    XSetWMProtocols(m_display, m_window, protocols, 1);

    m_gc = XCreateGC(m_display, m_window, 0, nullptr);
    setTitle(title);
}

inline SwX11PlatformWindow::~SwX11PlatformWindow() {
    if (m_integration && m_window) {
        m_integration->unregisterWindow(m_window);
    }
    releaseNativeResources();
}

inline void SwX11PlatformWindow::releaseNativeResources() {
    if (m_display && m_gc) {
        XFreeGC(m_display, m_gc);
        m_gc = 0;
    }
    if (m_display && m_window) {
        SwWidgetPlatformAdapter::finishSyntheticExpose(m_window);
        XDestroyWindow(m_display, m_window);
        m_window = 0;
    }
    m_display = nullptr;
}

inline void SwX11PlatformWindow::show() {
    if (m_display && m_window) {
        XMapRaised(m_display, m_window);
        XFlush(m_display);
    }
}

inline void SwX11PlatformWindow::hide() {
    if (m_display && m_window) {
        XUnmapWindow(m_display, m_window);
        XFlush(m_display);
    }
}

inline void SwX11PlatformWindow::setTitle(const std::string& title) {
    if (m_display && m_window) {
        XStoreName(m_display, m_window, title.c_str());
    }
}

inline void SwX11PlatformWindow::resize(int width, int height) {
    if (m_display && m_window) {
        XResizeWindow(m_display, m_window,
                      static_cast<unsigned int>(std::max(1, width)),
                      static_cast<unsigned int>(std::max(1, height)));
    }
}

inline void SwX11PlatformWindow::move(int x, int y) {
    if (m_display && m_window) {
        XMoveWindow(m_display, m_window, x, y);
    }
}

inline void SwX11PlatformWindow::requestUpdate() {
    if (m_display && m_window) {
        if (!SwWidgetPlatformAdapter::requestSyntheticExpose(m_window)) {
            return;
        }
        XEvent exposeEvent;
        std::memset(&exposeEvent, 0, sizeof(exposeEvent));
        exposeEvent.type = Expose;
        exposeEvent.xexpose.window = m_window;
        XSendEvent(m_display, m_window, False, ExposureMask, &exposeEvent);
        XFlush(m_display);
    }
}

inline void* SwX11PlatformWindow::nativeHandle() const {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_window));
}

inline void* SwX11PlatformWindow::nativeDisplay() const {
    return m_display;
}

inline void SwX11PlatformIntegration::initialize(SwGuiApplication* app) {
    if (m_display) {
        return;
    }

    XInitThreads();
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        throw std::runtime_error("Unable to connect to the X server.");
    }

    m_application = app;
    m_screen = DefaultScreen(m_display);
    m_deleteWindowAtom = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    m_clipboardAtom = XInternAtom(m_display, "CLIPBOARD", False);
    m_targetsAtom = XInternAtom(m_display, "TARGETS", False);
    m_utf8StringAtom = XInternAtom(m_display, "UTF8_STRING", False);
    m_textAtom = XInternAtom(m_display, "TEXT", False);
    m_clipboardProperty = XInternAtom(m_display, "SW_INTERNAL_CLIPBOARD", False);
    ensureClipboardWindow();
    if (SwCoreApplication* coreApp = SwCoreApplication::instance(false)) {
        const int x11Fd = ConnectionNumber(m_display);
        m_eventFdWaitableId = coreApp->addWaitFd(x11Fd, [this]() {
            processPlatformEvents();
        });
    }
}

inline void SwX11PlatformIntegration::shutdown() {
    if (m_eventFdWaitableId) {
        if (SwCoreApplication* coreApp = SwCoreApplication::instance(false)) {
            coreApp->removeWaitable(m_eventFdWaitableId);
        }
        m_eventFdWaitableId = 0;
    }

    {
        std::lock_guard<std::mutex> lock(m_windowMutex);
        for (auto& entry : m_windows) {
            if (entry.second) {
                entry.second->releaseNativeResources();
            }
        }
        m_windows.clear();
    }

    destroyClipboardWindow();
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
    m_application = nullptr;
    m_deleteWindowAtom = 0;
    m_clipboardAtom = 0;
    m_targetsAtom = 0;
    m_utf8StringAtom = 0;
    m_textAtom = 0;
    m_clipboardProperty = 0;
    m_eventFdWaitableId = 0;
}

inline std::unique_ptr<SwPlatformWindow> SwX11PlatformIntegration::createWindow(
    const std::string& title,
    int width,
    int height,
    const SwWindowCallbacks& callbacks,
    const SwPlatformWindowOptions& options) {
    if (!m_display) {
        throw std::runtime_error("X11 backend is not initialized.");
    }

    (void)options;

    std::unique_ptr<SwX11PlatformWindow> window(
        new SwX11PlatformWindow(this, m_display, m_screen, title, width, height, callbacks));

    registerWindow(window->handle(), window.get());
    return std::unique_ptr<SwPlatformWindow>(window.release());
}

inline void SwX11PlatformIntegration::registerWindow(::Window windowId, SwX11PlatformWindow* window) {
    std::lock_guard<std::mutex> lock(m_windowMutex);
    m_windows[windowId] = window;
}

inline void SwX11PlatformIntegration::unregisterWindow(::Window windowId) {
    std::lock_guard<std::mutex> lock(m_windowMutex);
    m_windows.erase(windowId);
}

inline void SwX11PlatformIntegration::processPlatformEvents() {
    if (!m_display) {
        return;
    }

    while (XPending(m_display)) {
        XEvent event;
        XNextEvent(m_display, &event);
        dispatchEvent(event);
    }
}

inline void SwX11PlatformIntegration::dispatchEvent(const XEvent& event) {
    switch (event.type) {
    case Expose:
        SwWidgetPlatformAdapter::finishSyntheticExpose(event.xexpose.window);
        if (auto* window = findWindow(event.xexpose.window)) {
            if (window->callbacks().paintRequestHandler) {
                SwPlatformPaintEvent paintEvent;
                paintEvent.dirtyRect = SwPlatformRect{
                    event.xexpose.x,
                    event.xexpose.y,
                    event.xexpose.width,
                    event.xexpose.height
                };
                paintEvent.surfaceSize = window->m_size;
                paintEvent.nativePaintDevice = window->nativeHandle();
                paintEvent.nativeWindowHandle = window->nativeHandle();
                paintEvent.nativeDisplay = window->nativeDisplay();
                window->callbacks().paintRequestHandler(paintEvent);
            }
        }
        break;
    case ConfigureNotify:
        handleConfigureEvent(event.xconfigure);
        break;
    case ButtonPress:
        handleMouseEvent(event, true);
        break;
    case ButtonRelease:
        handleMouseEvent(event, false);
        break;
    case MotionNotify:
        handleMotionEvent(event);
        break;
    case LeaveNotify:
        handleLeaveEvent(event);
        break;
    case KeyPress:
        handleKeyEvent(event, true);
        break;
    case KeyRelease:
        handleKeyEvent(event, false);
        break;
    case ClientMessage:
        handleClientMessage(event.xclient);
        break;
    case SelectionRequest:
        handleSelectionRequest(event.xselectionrequest);
        break;
    case SelectionClear:
        handleSelectionClear(event.xselectionclear);
        break;
    default:
        break;
    }
}

inline SwX11PlatformWindow* SwX11PlatformIntegration::findWindow(::Window windowId) const {
    std::lock_guard<std::mutex> lock(m_windowMutex);
    auto it = m_windows.find(windowId);
    return (it != m_windows.end()) ? it->second : nullptr;
}

inline void SwX11PlatformIntegration::handleMouseEvent(const XEvent& event, bool pressed) {
    auto* window = findWindow(event.xbutton.window);
    if (!window) {
        return;
    }

    if (pressed) {
        if ((event.xbutton.button == Button4) || (event.xbutton.button == Button5)) {
            if (window->callbacks().mouseWheelHandler) {
                SwMouseEvent mouseEvent = toMouseEvent(event.xbutton, SwMouseButton::NoButton, 0);
                mouseEvent.wheelDelta = (event.xbutton.button == Button4) ? 120 : -120;
                window->callbacks().mouseWheelHandler(mouseEvent);
            }
            return;
        }
    }

    const SwMouseButton button = translateButton(event.xbutton.button);
    const bool doubleClick = pressed && button != SwMouseButton::NoButton &&
                             isDoubleClick(event.xbutton, button);
    SwMouseEvent mouseEvent = toMouseEvent(event.xbutton, button, doubleClick ? 2 : 1);

    if (doubleClick && window->callbacks().mouseDoubleClickHandler) {
        window->callbacks().mouseDoubleClickHandler(mouseEvent);
    }

    if (pressed) {
        if (window->callbacks().mousePressHandler) {
            window->callbacks().mousePressHandler(mouseEvent);
        }
    } else {
        if (window->callbacks().mouseReleaseHandler) {
            window->callbacks().mouseReleaseHandler(mouseEvent);
        }
    }
}

inline void SwX11PlatformIntegration::handleMotionEvent(const XEvent& event) {
    auto* window = findWindow(event.xmotion.window);
    if (!window || !window->callbacks().mouseMoveHandler) {
        return;
    }
    window->callbacks().mouseMoveHandler(toMouseMoveEvent(event.xmotion));
}

inline void SwX11PlatformIntegration::handleLeaveEvent(const XEvent& event) {
    if (event.xcrossing.mode != NotifyNormal) {
        return;
    }
    if (event.xcrossing.detail == NotifyInferior) {
        return;
    }
    auto* window = findWindow(event.xcrossing.window);
    if (!window || !window->callbacks().mouseLeaveHandler) {
        return;
    }
    window->callbacks().mouseLeaveHandler();
}

inline void SwX11PlatformIntegration::handleKeyEvent(const XEvent& event, bool pressed) {
    auto* window = findWindow(event.xkey.window);
    if (!window) {
        return;
    }

    SwKeyEvent keyEvent = toKeyEvent(event.xkey);
    if (pressed) {
        if (window->callbacks().keyPressHandler) {
            window->callbacks().keyPressHandler(keyEvent);
        }
        return;
    }

    if (window->callbacks().keyReleaseHandler) {
        window->callbacks().keyReleaseHandler(keyEvent);
    }
}

inline void SwX11PlatformIntegration::handleConfigureEvent(const XConfigureEvent& event) {
    auto* window = findWindow(event.window);
    if (!window) {
        return;
    }

    window->updateSize(event.width, event.height);
    if (window->callbacks().resizeHandler) {
        window->callbacks().resizeHandler(SwPlatformSize{event.width, event.height});
    }
    if (window->callbacks().paintRequestHandler) {
        SwPlatformPaintEvent paintEvent;
        paintEvent.dirtyRect = SwPlatformRect{0, 0, event.width, event.height};
        paintEvent.surfaceSize = SwPlatformSize{event.width, event.height};
        paintEvent.nativePaintDevice = window->nativeHandle();
        paintEvent.nativeWindowHandle = window->nativeHandle();
        paintEvent.nativeDisplay = window->nativeDisplay();
        window->callbacks().paintRequestHandler(paintEvent);
    }
}

inline void SwX11PlatformIntegration::handleClientMessage(const XClientMessageEvent& event) {
    if (static_cast<Atom>(event.data.l[0]) != m_deleteWindowAtom) {
        return;
    }

    auto* window = findWindow(event.window);
    if (!window || !window->callbacks().deleteHandler) {
        return;
    }
    window->callbacks().deleteHandler();
}

inline SwString SwX11PlatformIntegration::clipboardText() {
    if (!m_display) {
        return {};
    }
    ensureClipboardWindow();

    Atom selection = m_clipboardAtom ? m_clipboardAtom : XA_PRIMARY;
    ::Window owner = XGetSelectionOwner(m_display, selection);
    if (owner == None || owner == m_clipboardWindow) {
        std::lock_guard<std::mutex> lock(m_clipboardMutex);
        return m_clipboard;
    }

    SwString text = requestClipboardText(selection);
    if (!text.isEmpty()) {
        return text;
    }

    if (selection != XA_PRIMARY) {
        return requestClipboardText(XA_PRIMARY);
    }
    return {};
}

inline void SwX11PlatformIntegration::setClipboardText(const SwString& text) {
    if (!m_display) {
        return;
    }
    ensureClipboardWindow();

    {
        std::lock_guard<std::mutex> lock(m_clipboardMutex);
        m_clipboard = text;
    }

    if (m_clipboardWindow) {
        XSetSelectionOwner(m_display, XA_PRIMARY, m_clipboardWindow, CurrentTime);
        if (m_clipboardAtom) {
            XSetSelectionOwner(m_display, m_clipboardAtom, m_clipboardWindow, CurrentTime);
        }
        XFlush(m_display);
    }
}

inline void SwX11PlatformIntegration::ensureClipboardWindow() {
    if (!m_display || m_clipboardWindow) {
        return;
    }
    m_clipboardWindow = XCreateSimpleWindow(m_display,
                                            DefaultRootWindow(m_display),
                                            0,
                                            0,
                                            1,
                                            1,
                                            0,
                                            BlackPixel(m_display, m_screen),
                                            BlackPixel(m_display, m_screen));
    XSelectInput(m_display, m_clipboardWindow, PropertyChangeMask | StructureNotifyMask);
}

inline void SwX11PlatformIntegration::destroyClipboardWindow() {
    if (m_display && m_clipboardWindow) {
        XDestroyWindow(m_display, m_clipboardWindow);
        m_clipboardWindow = 0;
    }
}

inline SwString SwX11PlatformIntegration::requestClipboardText(Atom selection) {
    if (!m_display || !m_clipboardWindow || selection == None) {
        return {};
    }

    Atom property = m_clipboardProperty ? m_clipboardProperty : selection;
    Atom target = m_utf8StringAtom ? m_utf8StringAtom : XA_STRING;

    XConvertSelection(m_display,
                      selection,
                      target,
                      property,
                      m_clipboardWindow,
                      CurrentTime);
    XFlush(m_display);

    SelectionWaitContext context{selection, property};
    XEvent event;
    constexpr int MaxAttempts = 40;
    for (int attempt = 0; attempt < MaxAttempts; ++attempt) {
        if (XCheckIfEvent(m_display,
                          &event,
                          &SwX11PlatformIntegration::selectionPredicate,
                          reinterpret_cast<XPointer>(&context))) {
            if (event.xselection.property == None) {
                return {};
            }

            Atom actualType;
            int actualFormat;
            unsigned long itemsCount;
            unsigned long bytesAfter;
            unsigned char* data = nullptr;
            SwString text;
            if (XGetWindowProperty(m_display,
                                   m_clipboardWindow,
                                   property,
                                   0,
                                   (~0L),
                                   False,
                                   AnyPropertyType,
                                   &actualType,
                                   &actualFormat,
                                   &itemsCount,
                                   &bytesAfter,
                                   &data) == Success && data) {
                size_t byteCount = static_cast<size_t>(itemsCount) * static_cast<size_t>(actualFormat) / 8;
                if (byteCount > 0 && data[byteCount - 1] == '\0') {
                    --byteCount;
                }
                text = SwString(std::string(reinterpret_cast<char*>(data), byteCount));
                XFree(data);
            }
            XDeleteProperty(m_display, m_clipboardWindow, property);
            return text;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {};
}

inline Bool SwX11PlatformIntegration::selectionPredicate(Display*, XEvent* event, XPointer userData) {
    if (event->type != SelectionNotify) {
        return False;
    }
    auto* context = reinterpret_cast<SelectionWaitContext*>(userData);
    if (event->xselection.selection != context->selection) {
        return False;
    }
    if (event->xselection.property == None) {
        return True;
    }
    return event->xselection.property == context->property;
}

inline void SwX11PlatformIntegration::handleSelectionRequest(const XSelectionRequestEvent& event) {
    if (!m_display) {
        return;
    }

    Atom property = event.property;
    if (property == None) {
        property = event.target;
    }

    XEvent response;
    std::memset(&response, 0, sizeof(response));
    response.xselection.type = SelectionNotify;
    response.xselection.display = event.display;
    response.xselection.requestor = event.requestor;
    response.xselection.selection = event.selection;
    response.xselection.target = event.target;
    response.xselection.time = event.time;
    response.xselection.property = None;

    bool handled = false;
    if (event.target == m_targetsAtom) {
        std::vector<Atom> targets;
        targets.push_back(m_targetsAtom);
        if (m_utf8StringAtom) {
            targets.push_back(m_utf8StringAtom);
        }
        targets.push_back(XA_STRING);
        XChangeProperty(m_display,
                        event.requestor,
                        property,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char*>(targets.data()),
                        static_cast<int>(targets.size()));
        response.xselection.property = property;
        handled = true;
    } else if (event.target == m_utf8StringAtom ||
               event.target == m_textAtom ||
               event.target == XA_STRING) {
        std::string clipboardUtf8;
        {
            std::lock_guard<std::mutex> lock(m_clipboardMutex);
            clipboardUtf8 = m_clipboard.toStdString();
        }
        if (clipboardUtf8.empty() || clipboardUtf8.back() != '\0') {
            clipboardUtf8.push_back('\0');
        }
        Atom propertyType = (event.target == XA_STRING)
            ? XA_STRING
            : (m_utf8StringAtom ? m_utf8StringAtom : XA_STRING);
        XChangeProperty(m_display,
                        event.requestor,
                        property,
                        propertyType,
                        8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char*>(clipboardUtf8.data()),
                        static_cast<int>(clipboardUtf8.size()));
        response.xselection.property = property;
        handled = true;
    }

    if (!handled) {
        response.xselection.property = None;
    }

    XSendEvent(m_display, event.requestor, False, 0, &response);
    XFlush(m_display);
}

inline void SwX11PlatformIntegration::handleSelectionClear(const XSelectionClearEvent& event) {
    if (event.window != m_clipboardWindow) {
        return;
    }
    if (event.selection == XA_PRIMARY || (m_clipboardAtom && event.selection == m_clipboardAtom)) {
        std::lock_guard<std::mutex> lock(m_clipboardMutex);
        m_clipboard.clear();
    }
}

inline SwMouseEvent SwX11PlatformIntegration::toMouseEvent(const XButtonEvent& event,
                                                           SwMouseButton button,
                                                           int clickCount) {
    SwMouseEvent mouseEvent;
    mouseEvent.position = {event.x, event.y};
    mouseEvent.button = button;
    mouseEvent.ctrl = (event.state & ControlMask) != 0;
    mouseEvent.shift = (event.state & ShiftMask) != 0;
    mouseEvent.alt = (event.state & Mod1Mask) != 0;
    mouseEvent.wheelDelta = 0;
    mouseEvent.clickCount = clickCount;
    return mouseEvent;
}

inline SwMouseEvent SwX11PlatformIntegration::toMouseMoveEvent(const XMotionEvent& event) {
    SwMouseEvent mouseEvent;
    mouseEvent.position = {event.x, event.y};
    mouseEvent.button = SwMouseButton::NoButton;
    mouseEvent.ctrl = (event.state & ControlMask) != 0;
    mouseEvent.shift = (event.state & ShiftMask) != 0;
    mouseEvent.alt = (event.state & Mod1Mask) != 0;
    mouseEvent.clickCount = 0;
    return mouseEvent;
}

inline SwKeyEvent SwX11PlatformIntegration::toKeyEvent(const XKeyEvent& event) {
    SwKeyEvent keyEvent;
    keyEvent.ctrl = (event.state & ControlMask) != 0;
    keyEvent.shift = (event.state & ShiftMask) != 0;
    keyEvent.alt = (event.state & Mod1Mask) != 0;
    keyEvent.system = false;

    KeySym sym = XLookupKeysym(const_cast<XKeyEvent*>(&event), 0);
    keyEvent.keyCode = static_cast<int>(sym);
    return keyEvent;
}

inline bool SwX11PlatformIntegration::isDoubleClick(const XButtonEvent& event, SwMouseButton button) {
    constexpr unsigned long DoubleClickThresholdMs = 250;

    SwPlatformPoint point{event.x, event.y};
    const unsigned long currentTime = event.time;
    const bool isWithinThreshold = (button == m_lastClickButton) &&
                                   (currentTime - m_lastClickTime <= DoubleClickThresholdMs) &&
                                   (std::abs(point.x - m_lastClickPosition.x) <= 2) &&
                                   (std::abs(point.y - m_lastClickPosition.y) <= 2);

    m_lastClickTime = currentTime;
    m_lastClickButton = button;
    m_lastClickPosition = point;
    return isWithinThreshold;
}

inline SwMouseButton SwX11PlatformIntegration::translateButton(unsigned int xButton) const {
    switch (xButton) {
    case Button1:
        return SwMouseButton::Left;
    case Button2:
        return SwMouseButton::Middle;
    case Button3:
        return SwMouseButton::Right;
    default:
        return SwMouseButton::Other;
    }
}

#else

class SwX11PlatformIntegration : public SwPlatformIntegration {
public:
    /**
     * @brief Performs the `initialize` operation.
     */
    void initialize(SwGuiApplication*) override {
        throw std::runtime_error("X11 backend is only available on Linux.");
    }

    /**
     * @brief Performs the `shutdown` operation.
     */
    void shutdown() override {}

    /**
     * @brief Creates the requested window.
     * @param int Value passed to the method.
     * @param int Value passed to the method.
     * @return The resulting window.
     */
    std::unique_ptr<SwPlatformWindow> createWindow(const std::string&,
                                                   int,
                                                   int,
                                                   const SwWindowCallbacks&,
                                                   const SwPlatformWindowOptions& = {}) override {
        throw std::runtime_error("X11 backend is not available on this platform.");
    }

    /**
     * @brief Returns the current painter.
     * @return The current painter.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::unique_ptr<SwPlatformPainter> createPainter() override {
        throw std::runtime_error("X11 backend is not available on this platform.");
    }

    /**
     * @brief Creates the requested image.
     * @param SwPixelFormat Value passed to the method.
     * @return The resulting image.
     */
    std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize&, SwPixelFormat) override {
        throw std::runtime_error("X11 backend is not available on this platform.");
    }

    /**
     * @brief Performs the `processPlatformEvents` operation.
     */
    void processPlatformEvents() override {}
    /**
     * @brief Performs the `wakeUpGuiThread` operation.
     */
    void wakeUpGuiThread() override {}
    /**
     * @brief Returns the current available Screens.
     * @return The current available Screens.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::vector<std::string> availableScreens() const override { return {}; }
    /**
     * @brief Returns the current clipboard Text.
     * @return The current clipboard Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString clipboardText() override { return {}; }
    /**
     * @brief Sets the clipboard Text.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setClipboardText(const SwString&) override {}
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const char* name() const override { return "x11"; }
};

#endif

inline std::unique_ptr<SwPlatformIntegration> SwCreateX11PlatformIntegration() {
    return std::unique_ptr<SwPlatformIntegration>(new SwX11PlatformIntegration());
}
