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

    SwPlatformPoint() = default;
    SwPlatformPoint(int px, int py) : x(px), y(py) {}
};

struct SwPlatformSize {
    int width{0};
    int height{0};

    SwPlatformSize() = default;
    SwPlatformSize(int w, int h) : width(w), height(h) {}
};

struct SwPlatformRect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    SwPlatformRect() = default;
    SwPlatformRect(int px, int py, int w, int h)
        : x(px), y(py), width(w), height(h) {}
};

struct SwKeyEvent {
    int keyCode{0};
    bool ctrl{false};
    bool shift{false};
    bool alt{false};
    bool system{false};
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
    std::function<void(const SwPlatformSize&)> resizeHandler;
    std::function<void(const SwMouseEvent&)> mousePressHandler;
    std::function<void(const SwMouseEvent&)> mouseDoubleClickHandler;
    std::function<void(const SwMouseEvent&)> mouseReleaseHandler;
    std::function<void(const SwMouseEvent&)> mouseMoveHandler;
    std::function<void(const SwMouseEvent&)> mouseWheelHandler;
    std::function<void(const SwKeyEvent&)> keyPressHandler;
    std::function<void()> deleteHandler;
    std::function<void()> paintRequestHandler;
};

class SwPlatformImage {
public:
    virtual ~SwPlatformImage() = default;

    virtual SwPlatformSize size() const = 0;
    virtual SwPixelFormat format() const = 0;
    virtual int pitch() const = 0;
    virtual std::uint8_t* pixels() = 0;
    virtual const std::uint8_t* pixels() const = 0;
    virtual void clear(std::uint32_t argb) = 0;
};

class SwPlatformPainter {
public:
    virtual ~SwPlatformPainter() = default;

    virtual void begin(void* surface) = 0;
    virtual void end() = 0;
    virtual void flush() = 0;

    virtual void drawImage(const SwPlatformImage&,
                           const SwPlatformRect&,
                           const SwPlatformPoint&) {}

    virtual void fillRect(const SwPlatformRect&, std::uint32_t /*argb*/) {}
};

class SwPlatformWindow {
public:
    virtual ~SwPlatformWindow() = default;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void setTitle(const std::string& title) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void move(int x, int y) = 0;
    virtual void requestUpdate() = 0;
    virtual void* nativeHandle() const = 0;
};

class SwPlatformIntegration {
public:
    virtual ~SwPlatformIntegration() = default;

    virtual void initialize(SwGuiApplication* app) = 0;
    virtual void shutdown() = 0;

    virtual std::unique_ptr<SwPlatformWindow> createWindow(const std::string& title,
                                                           int width,
                                                           int height,
                                                           const SwWindowCallbacks& callbacks) = 0;

    virtual std::unique_ptr<SwPlatformPainter> createPainter() = 0;
    virtual std::unique_ptr<SwPlatformImage> createImage(const SwPlatformSize& size,
                                                         SwPixelFormat format) = 0;

    virtual void processPlatformEvents() = 0;
    virtual void wakeUpGuiThread() = 0;
    virtual std::vector<std::string> availableScreens() const = 0;
    virtual SwString clipboardText() = 0;
    virtual void setClipboardText(const SwString& text) = 0;
    virtual const char* name() const = 0;
};
