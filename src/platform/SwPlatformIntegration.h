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
 * @file src/platform/SwPlatformIntegration.h
 * @ingroup platform_backends
 * @brief Declares the public interface exposed by SwPlatformIntegration in the CoreSw platform
 * integration layer.
 *
 * This header belongs to the CoreSw platform integration layer. It exposes top-level platform
 * integration contracts shared by the GUI and rendering backends.
 *
 * Within that layer, this file focuses on the platform integration interface. The declarations
 * exposed here define the stable surface that adjacent code can rely on while the implementation
 * remains free to evolve behind the header.
 *
 * The main declarations in this header are SwMouseButton, SwPixelFormat, SwPlatformPoint,
 * SwPlatformSize, SwPlatformRect, SwKeyEvent, SwMouseEvent, and SwWindowCallbacks, plus related
 * helper declarations.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * These declarations define how platform-specific backends plug into otherwise portable framework
 * code.
 *
 */


#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SwGuiApplication;
#include "core/types/SwString.h"

enum class SwMouseButton {
    NoButton,
    Left,
    Right,
    Middle,
    Other
};

enum class SwPixelFormat {
    Unknown,
    RGB24,
    BGR24,
    ARGB32,
    ABGR32
};

struct SwPlatformPoint {
    int x{0};
    int y{0};

    /**
     * @brief Constructs a `SwPlatformPoint` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPlatformPoint() = default;
    /**
     * @brief Constructs a `SwPlatformPoint` instance.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPlatformPoint(int px, int py) : x(px), y(py) {}
};

struct SwPlatformSize {
    int width{0};
    int height{0};

    /**
     * @brief Constructs a `SwPlatformSize` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPlatformSize() = default;
    /**
     * @brief Constructs a `SwPlatformSize` instance.
     * @param w Width value.
     * @param h Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPlatformSize(int w, int h) : width(w), height(h) {}
};

struct SwPlatformRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    /**
     * @brief Constructs a `SwPlatformRect` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPlatformRect() = default;
    /**
     * @brief Constructs a `SwPlatformRect` instance.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     * @param w Width value.
     * @param h Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPlatformRect(int px, int py, int w, int h)
        : x(px), y(py), width(w), height(h) {}
};

struct SwKeyEvent {
    int keyCode{0};
    bool ctrl{false};
    bool shift{false};
    bool alt{false};
    bool system{false};
    wchar_t text{L'\0'};      // translated character from the active keyboard layout (Unicode)
    bool textProvided{false}; // true when the platform performed key-to-char translation
                              // (e.g. Win32 via WM_CHAR); widgets skip translateCharacter() when true
};

struct SwMouseEvent {
    SwPlatformPoint position{};
    SwMouseButton button{SwMouseButton::NoButton};
    bool ctrl{false};
    bool shift{false};
    bool alt{false};
    int wheelDelta{0};
    int clickCount{1};
};

struct SwWindowCallbacks {
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwPlatformSize&)> resizeHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwMouseEvent&)> mousePressHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwMouseEvent&)> mouseDoubleClickHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwMouseEvent&)> mouseReleaseHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwMouseEvent&)> mouseMoveHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwMouseEvent&)> mouseWheelHandler;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    std::function<void(const SwKeyEvent&)> keyPressHandler;
    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::function<void()> deleteHandler;
    /**
     * @brief Returns the current function<void.
     * @return The current function<void.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::function<void()> paintRequestHandler;
};

class SwPlatformImage {
public:
    /**
     * @brief Destroys the `SwPlatformImage` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwPlatformImage() = default;

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwPlatformSize size() const = 0;
    /**
     * @brief Returns the current format.
     * @return The current format.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwPixelFormat format() const = 0;
    /**
     * @brief Returns the current pitch.
     * @return The current pitch.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual int pitch() const = 0;
    /**
     * @brief Returns the current pixels.
     * @return The current pixels.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual std::uint8_t* pixels() = 0;
    /**
     * @brief Returns the current pixels.
     * @return The current pixels.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual const std::uint8_t* pixels() const = 0;
    /**
     * @brief Clears the current object state.
     * @param argb Value passed to the method.
     * @return The requested clear.
     */
    virtual void clear(std::uint32_t argb) = 0;
};

class SwPlatformPainter {
public:
    /**
     * @brief Destroys the `SwPlatformPainter` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwPlatformPainter() = default;

    /**
     * @brief Performs the `begin` operation.
     * @param surface Value passed to the method.
     * @return The requested begin.
     */
    virtual void begin(void* surface) = 0;
    /**
     * @brief Returns the current end.
     * @return The current end.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void end() = 0;
    /**
     * @brief Returns the current flush.
     * @return The current flush.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void flush() = 0;

    /**
     * @brief Performs the `drawImage` operation.
     * @return The requested draw Image.
     */
    virtual void drawImage(const SwPlatformImage&,
                           const SwPlatformRect&,
                           const SwPlatformPoint&) {}

    /**
     * @brief Performs the `fillRect` operation.
     * @param uint32_t Value passed to the method.
     * @return The requested fill Rect.
     */
    virtual void fillRect(const SwPlatformRect&, std::uint32_t /*argb*/) {}
};

class SwPlatformWindow {
public:
    /**
     * @brief Destroys the `SwPlatformWindow` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwPlatformWindow() = default;

    /**
     * @brief Returns the current show.
     * @return The current show.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void show() = 0;
    /**
     * @brief Returns the current hide.
     * @return The current hide.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void hide() = 0;
    /**
     * @brief Sets the title.
     * @param title Title text applied by the operation.
     * @return The requested title.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual void setTitle(const std::string& title) = 0;
    /**
     * @brief Performs the `resize` operation.
     * @param width Width value.
     * @param height Height value.
     * @return The requested resize.
     */
    virtual void resize(int width, int height) = 0;
    /**
     * @brief Performs the `move` operation.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @return The requested move.
     */
    virtual void move(int x, int y) = 0;
    /**
     * @brief Returns the current request Update.
     * @return The current request Update.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void requestUpdate() = 0;
    /**
     * @brief Returns the current native Handle.
     * @return The current native Handle.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void* nativeHandle() const = 0;
};

class SwPlatformIntegration {
public:
    /**
     * @brief Destroys the `SwPlatformIntegration` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwPlatformIntegration() = default;

    /**
     * @brief Performs the `initialize` operation.
     * @param app Value passed to the method.
     * @return The requested initialize.
     */
    virtual void initialize(SwGuiApplication* app) = 0;
    /**
     * @brief Returns the current shutdown.
     * @return The current shutdown.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Creates the requested window.
     * @param title Title text applied by the operation.
     * @param width Width value.
     * @param height Height value.
     * @param callbacks Value passed to the method.
     * @return The resulting window.
     */
    virtual std::unique_ptr<SwPlatformWindow> createWindow(const std::string& title,
                                                           int width,
                                                           int height,
                                                           const SwWindowCallbacks& callbacks) = 0;

    /**
     * @brief Returns the current painter.
     * @return The current painter.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual std::unique_ptr<SwPlatformPainter> createPainter() = 0;
    /**
     * @brief Creates the requested image.
     * @param size Size value used by the operation.
     * @param format Value passed to the method.
     * @return The resulting image.
     */
    virtual std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize& size,
                                                         SwPixelFormat format) = 0;

    /**
     * @brief Returns the current process Platform Events.
     * @return The current process Platform Events.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void processPlatformEvents() = 0;
    /**
     * @brief Returns the current wake Up Gui Thread.
     * @return The current wake Up Gui Thread.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void wakeUpGuiThread() = 0;
    /**
     * @brief Returns the current available Screens.
     * @return The current available Screens.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual std::vector<std::string> availableScreens() const = 0;
    /**
     * @brief Returns the current clipboard Text.
     * @return The current clipboard Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwString clipboardText() = 0;
    /**
     * @brief Sets the clipboard Text.
     * @param text Value passed to the method.
     * @return The requested clipboard Text.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    virtual void setClipboardText(const SwString& text) = 0;
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual const char* name() const = 0;
};
