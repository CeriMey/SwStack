#pragma once
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

#include "SwWidget.h"
#include "SwPainter.h"

#include "media/SwVideoDecoder.h"
#include "media/SwVideoFrame.h"
#include "media/SwVideoSource.h"
#if defined(_WIN32)
#include "media/SwMediaFoundationH264Decoder.h"
#endif
#include "SwGuiApplication.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <wrl/client.h>
#include <d2d1.h>
#include <d2d1helper.h>
#pragma comment(lib, "d2d1.lib")
#endif

class SwVideoRenderer {
public:
    virtual ~SwVideoRenderer() = default;
    virtual bool render(SwPainter* painter,
                        const SwVideoFrame& frame,
                        const SwRect& targetRect) = 0;
};

class SwFallbackVideoRenderer : public SwVideoRenderer {
public:
    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter) {
            return false;
        }
        painter->fillRect(targetRect, {30, 30, 30}, {80, 80, 80}, 1);
        SwString description = SwString("%1x%2 @%3")
                                   .arg(SwString(std::to_string(frame.width())))
                                   .arg(SwString(std::to_string(frame.height())))
                                   .arg(SwString(std::to_string(frame.timestamp())));
        painter->drawText(targetRect,
                          description,
                          DrawTextFormat::Center | DrawTextFormat::VCenter,
                          {220, 220, 220}, SwFont());
        return true;
    }
};

#if defined(_WIN32)
class SwWin32VideoRenderer : public SwVideoRenderer {
public:
    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter) {
            return false;
        }
        void* native = painter->nativeHandle();
        if (!native) {
            return false;
        }
        HDC hdc = reinterpret_cast<HDC>(native);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = frame.width();
        bmi.bmiHeader.biHeight = -frame.height();
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = static_cast<DWORD>(frame.width() * frame.height() * 4);
        const uint8_t* data = frame.planeData(0);
        if (!data) {
            return false;
        }
        int previousMode = SetStretchBltMode(hdc, HALFTONE);
        POINT prevOrg{};
        SetBrushOrgEx(hdc, 0, 0, &prevOrg);

        int result = StretchDIBits(hdc,
                                   targetRect.x,
                                   targetRect.y,
                                   targetRect.width,
                                   targetRect.height,
                                   0,
                                   0,
                                   frame.width(),
                                   frame.height(),
                                   data,
                                   &bmi,
                                   DIB_RGB_COLORS,
                                   SRCCOPY);
        if (previousMode != 0) {
            SetStretchBltMode(hdc, previousMode);
        }
        SetBrushOrgEx(hdc, prevOrg.x, prevOrg.y, nullptr);
        if (result == 0 || result == GDI_ERROR) {
            int setResult = SetDIBitsToDevice(hdc,
                                              targetRect.x,
                                              targetRect.y,
                                              targetRect.width,
                                              targetRect.height,
                                              0,
                                              0,
                                              0,
                                              frame.height(),
                                              data,
                                              &bmi,
                                              DIB_RGB_COLORS);
            if (setResult == 0 || setResult == GDI_ERROR) {
                return false;
            }
        }
        return true;
    }
};

class SwD2DVideoRenderer : public SwVideoRenderer {
public:
    SwD2DVideoRenderer() {
        ensureFactory();
    }

    bool isSupported() const {
        return sharedFactory().Get() != nullptr;
    }

    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter || !isSupported()) {
            return false;
        }
        void* native = painter->nativeHandle();
        if (!native) {
            return false;
        }
        HDC hdc = reinterpret_cast<HDC>(native);
        if (!ensureRenderTarget()) {
            return false;
        }
        RECT rc{targetRect.x,
                targetRect.y,
                targetRect.x + targetRect.width,
                targetRect.y + targetRect.height};
        HRESULT hr = m_renderTarget->BindDC(hdc, &rc);
        if (FAILED(hr)) {
            return false;
        }

        if (!updateBitmap(frame)) {
            return false;
        }

        m_renderTarget->BeginDraw();
        D2D1_RECT_F dest = D2D1::RectF(static_cast<FLOAT>(targetRect.x),
                                       static_cast<FLOAT>(targetRect.y),
                                       static_cast<FLOAT>(targetRect.x + targetRect.width),
                                       static_cast<FLOAT>(targetRect.y + targetRect.height));
        D2D1_RECT_F src = D2D1::RectF(0.0f,
                                      0.0f,
                                      static_cast<FLOAT>(frame.width()),
                                      static_cast<FLOAT>(frame.height()));
        m_renderTarget->DrawBitmap(m_bitmap.Get(),
                                   dest,
                                   1.0f,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                   src);
        hr = m_renderTarget->EndDraw();
        if (FAILED(hr)) {
            m_bitmap.Reset();
            return false;
        }
        return true;
    }

private:
    bool ensureRenderTarget() {
        if (!m_renderTarget) {
            ID2D1Factory* factory = sharedFactory().Get();
            if (!factory) {
                return false;
            }
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
            HRESULT hr = factory->CreateDCRenderTarget(&props, m_renderTarget.GetAddressOf());
            if (FAILED(hr)) {
                m_renderTarget.Reset();
                return false;
            }
        }
        return true;
    }

    bool updateBitmap(const SwVideoFrame& frame) {
        if (!frame.isValid()) {
            return false;
        }
        const UINT width = static_cast<UINT>(frame.width());
        const UINT height = static_cast<UINT>(frame.height());
        const UINT stride = static_cast<UINT>(frame.planeStride(0));
        D2D1_SIZE_U size = D2D1::SizeU(width, height);

        if (!m_bitmap ||
            m_bitmapSize.width != size.width ||
            m_bitmapSize.height != size.height) {
            m_bitmap.Reset();
            D2D1_BITMAP_PROPERTIES props =
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                         D2D1_ALPHA_MODE_IGNORE));
            HRESULT hr = m_renderTarget->CreateBitmap(size,
                                                      frame.planeData(0),
                                                      stride,
                                                      &props,
                                                      m_bitmap.GetAddressOf());
            if (FAILED(hr)) {
                m_bitmap.Reset();
                return false;
            }
            m_bitmapSize = size;
        } else {
            HRESULT hr = m_bitmap->CopyFromMemory(nullptr,
                                                  frame.planeData(0),
                                                  stride);
            if (FAILED(hr)) {
                m_bitmap.Reset();
                return false;
            }
        }
        return true;
    }

    static void ensureFactory() {
        if (sharedFactory().Get()) {
            return;
        }
        D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        ID2D1Factory* rawFactory = nullptr;
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       __uuidof(ID2D1Factory),
                                       &options,
                                       reinterpret_cast<void**>(&rawFactory));
        if (FAILED(hr)) {
            return;
        }
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        factory.Attach(rawFactory);
        sharedFactory() = factory;
    }

    static Microsoft::WRL::ComPtr<ID2D1Factory>& sharedFactory() {
        static Microsoft::WRL::ComPtr<ID2D1Factory> s_factory;
        return s_factory;
    }

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_bitmap;
    D2D1_SIZE_U m_bitmapSize{0, 0};
};
#endif

/**
 * @brief Widget that hosts a media pipeline and paints decoded video frames.
 *
 * Typical usage:
 * @code
 * auto widget = new SwVideoWidget(parent);
 * widget->setVideoSource(mySource);
 * widget->setVideoDecoder(myDecoder); // optional, defaults to passthrough
 * widget->start();
 * @endcode
 */
class SwVideoWidget : public SwWidget {
public:
    enum class ScalingMode {
        Fit,
        Fill,
        Stretch,
        Center
    };

    SwVideoWidget(SwWidget* parent = nullptr)
        : SwWidget(parent),
          m_pipeline(std::make_shared<SwVideoPipeline>()),
          m_renderer(createDefaultRenderer()) {
#if defined(_WIN32)
        boostGuiThreadPriority();
#endif
        m_pipeline->setFrameCallback([this](const SwVideoFrame& frame) {
            handleIncomingFrame(frame);
        });
        m_pipeline->useDecoderFactory(true);
#if defined(_WIN32)
        m_pipeline->setDecoderHint(SwVideoPacket::Codec::H264,
                                   std::make_shared<SwMediaFoundationH264Decoder>());
#endif
    }

    ~SwVideoWidget() override {
        stop();
    }

    void setVideoSource(const std::shared_ptr<SwVideoSource>& source) {
        m_source = source;
        if (m_pipeline) {
            m_pipeline->setSource(source);
        }
    }

    std::shared_ptr<SwVideoSource> videoSource() const { return m_source; }

    void setVideoDecoder(const std::shared_ptr<SwVideoDecoder>& decoder) {
        if (!decoder) {
            return;
        }
        m_decoder = decoder;
        if (m_pipeline) {
            m_pipeline->setDecoder(decoder);
            m_pipeline->setFrameCallback([this](const SwVideoFrame& frame) {
                handleIncomingFrame(frame);
            });
        }
    }

    std::shared_ptr<SwVideoDecoder> videoDecoder() const { return m_decoder; }

    void setRenderer(const std::shared_ptr<SwVideoRenderer>& renderer) {
        m_renderer = renderer ? renderer : createDefaultRenderer();
    }

    std::shared_ptr<SwVideoRenderer> renderer() const { return m_renderer; }

    void start() {
        if (!m_pipeline || !m_source) {
            return;
        }
        m_pipeline->start();
    }

    void stop() {
        if (m_pipeline) {
            m_pipeline->stop();
        }
    }

    void setScalingMode(ScalingMode mode) { m_scalingMode = mode; }
    ScalingMode scalingMode() const { return m_scalingMode; }

    void setBackgroundColor(const SwColor& color) { m_backgroundColor = color; }
    SwColor backgroundColor() const { return m_backgroundColor; }

    void setFrameArrivedCallback(std::function<void(const SwVideoFrame&)> callback) {
        m_frameArrived = std::move(callback);
    }

    bool hasFrame() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame.isValid();
    }

    SwVideoFrame currentFrame() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame;
    }

    std::chrono::steady_clock::time_point lastFrameTime() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_lastFrameTime;
    }

    void paintEvent(PaintEvent* event) override {
        if (!event) {
            return;
        }
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }

        const SwRect rect = getRect();
        painter->fillRect(rect, m_backgroundColor, m_backgroundColor, 0);

        SwVideoFrame frame = currentFrame();
        bool rendered = false;
        if (frame.isValid() && m_renderer) {
            const SwRect target = computeTargetRect(rect, frame);
            rendered = m_renderer->render(painter, frame, target);
#if defined(_WIN32)
            if (!rendered && !std::dynamic_pointer_cast<SwWin32VideoRenderer>(m_renderer)) {
                auto gdiRenderer = std::make_shared<SwWin32VideoRenderer>();
                rendered = gdiRenderer->render(painter, frame, target);
                if (rendered) {
                    m_renderer = gdiRenderer;
                }
            }
#endif
            if (!rendered && !std::dynamic_pointer_cast<SwFallbackVideoRenderer>(m_renderer)) {
                auto fallbackRenderer = std::make_shared<SwFallbackVideoRenderer>();
                rendered = fallbackRenderer->render(painter, frame, target);
                if (rendered) {
                    m_renderer = fallbackRenderer;
                }
            }
        }

        if (!rendered) {
            drawPlaceholder(painter, rect);
        }
    }

    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
    }

private:
    static std::shared_ptr<SwVideoRenderer> createDefaultRenderer() {
#if defined(_WIN32)
        auto d2dRenderer = std::make_shared<SwD2DVideoRenderer>();
        if (d2dRenderer->isSupported()) {
            return d2dRenderer;
        }
        return std::make_shared<SwWin32VideoRenderer>();
#else
        return std::make_shared<SwFallbackVideoRenderer>();
#endif
    }

    void handleIncomingFrame(const SwVideoFrame& frame) {
        SwVideoFrame converted = frame;
        if (!ensureBGRA(converted)) {
            return;
        }
        SwVideoFrame readyFrame = SwVideoFrame::allocate(converted.width(),
                                                         converted.height(),
                                                         SwVideoPixelFormat::BGRA32);
        if (!readyFrame.isValid()) {
            return;
        }

        const uint8_t* src = converted.planeData(0);
        uint8_t* dst = readyFrame.planeData(0);
        if (!src || !dst) {
            return;
        }
        const int srcStride = converted.planeStride(0);
        const int dstStride = readyFrame.planeStride(0);
        const int rowBytes = converted.width() * 4;
        const int absSrcStride = srcStride >= 0 ? srcStride : -srcStride;
        if (srcStride == dstStride && srcStride == rowBytes) {
            std::memcpy(dst,
                        src,
                        static_cast<std::size_t>(rowBytes) * converted.height());
        } else {
            for (int y = 0; y < converted.height(); ++y) {
                const uint8_t* srcRow =
                    (srcStride >= 0)
                        ? src + static_cast<std::size_t>(y) * absSrcStride
                        : src + static_cast<std::size_t>(converted.height() - 1 - y) * absSrcStride;
                uint8_t* dstRow = dst + static_cast<std::size_t>(y) * dstStride;
                std::memcpy(dstRow, srcRow, rowBytes);
            }
        }

        readyFrame.setTimestamp(converted.timestamp());
        readyFrame.setAspectRatio(converted.aspectRatio());
        readyFrame.setColorSpace(converted.colorSpace());
        readyFrame.setRotation(converted.rotation());
        presentFrame(readyFrame);
    }

    void drawPlaceholder(SwPainter* painter, const SwRect& rect) {
        if (!painter) {
            return;
        }
        painter->fillRect(rect, m_backgroundColor, {80, 80, 80}, 1);
        SwString text = m_source ? SwString("Waiting stream...") : SwString("No source");
        painter->drawText(rect,
                          text,
                          DrawTextFormat::Center | DrawTextFormat::VCenter,
                          {200, 200, 200},
                          SwFont());
    }

    SwRect computeTargetRect(const SwRect& bounds, const SwVideoFrame& frame) const {
        SwRect target = bounds;
        if (m_scalingMode == ScalingMode::Stretch) {
            return target;
        }

        double scale = 1.0;
        if (m_scalingMode == ScalingMode::Fit || m_scalingMode == ScalingMode::Center) {
            scale = std::min(static_cast<double>(bounds.width) / frame.width(),
                             static_cast<double>(bounds.height) / frame.height());
            if (m_scalingMode == ScalingMode::Center) {
                scale = std::min(1.0, scale);
            }
        } else if (m_scalingMode == ScalingMode::Fill) {
            scale = std::max(static_cast<double>(bounds.width) / frame.width(),
                             static_cast<double>(bounds.height) / frame.height());
        }

        const int scaledWidth = static_cast<int>(frame.width() * scale);
        const int scaledHeight = static_cast<int>(frame.height() * scale);
        target.width = std::min(bounds.width, scaledWidth);
        target.height = std::min(bounds.height, scaledHeight);
        target.x = bounds.x + (bounds.width - target.width) / 2;
        target.y = bounds.y + (bounds.height - target.height) / 2;
        return target;
    }

    bool ensureBGRA(SwVideoFrame& frame) {
        if (!frame.isValid()) {
            return false;
        }
        if (frame.pixelFormat() == SwVideoPixelFormat::BGRA32) {
            return true;
        }

        SwVideoFrame& buffer = conversionBufferFor(frame.width(), frame.height());
        uint8_t* dst = buffer.planeData(0);
        if (!dst) {
            return false;
        }

        const int dstStride = buffer.planeStride(0);
        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>(std::max(0, std::min(255, v)));
        };

        auto copyMetadata = [&]() {
            buffer.setTimestamp(frame.timestamp());
            buffer.setAspectRatio(frame.aspectRatio());
            buffer.setColorSpace(frame.colorSpace());
            buffer.setRotation(frame.rotation());
        };

        switch (frame.pixelFormat()) {
        case SwVideoPixelFormat::RGBA32:
        case SwVideoPixelFormat::BGRA32: {
            const uint8_t* src = frame.planeData(0);
            const int srcStride = frame.planeStride(0);
            for (int y = 0; y < frame.height(); ++y) {
                const uint8_t* srcRow = src + y * srcStride;
                uint8_t* dstRow = dst + y * dstStride;
                for (int x = 0; x < frame.width(); ++x) {
                    const std::size_t idx = static_cast<std::size_t>(x) * 4;
                    uint8_t r = srcRow[idx + 0];
                    uint8_t g = srcRow[idx + 1];
                    uint8_t b = srcRow[idx + 2];
                    uint8_t a = srcRow[idx + 3];
                    dstRow[idx + 0] = b;
                    dstRow[idx + 1] = g;
                    dstRow[idx + 2] = r;
                    dstRow[idx + 3] = a;
                }
            }
            copyMetadata();
            break;
        }
        case SwVideoPixelFormat::RGB24:
        case SwVideoPixelFormat::BGR24: {
            const bool srcBGR = frame.pixelFormat() == SwVideoPixelFormat::BGR24;
            const uint8_t* src = frame.planeData(0);
            const int srcStride = frame.planeStride(0);
            for (int y = 0; y < frame.height(); ++y) {
                const uint8_t* srcRow = src + y * srcStride;
                uint8_t* dstRow = dst + y * dstStride;
                for (int x = 0; x < frame.width(); ++x) {
                    const std::size_t idx = static_cast<std::size_t>(x) * 3;
                    uint8_t r = srcBGR ? srcRow[idx + 2] : srcRow[idx + 0];
                    uint8_t g = srcRow[idx + 1];
                    uint8_t b = srcBGR ? srcRow[idx + 0] : srcRow[idx + 2];
                    dstRow[x * 4 + 0] = b;
                    dstRow[x * 4 + 1] = g;
                    dstRow[x * 4 + 2] = r;
                    dstRow[x * 4 + 3] = 255;
                }
            }
            copyMetadata();
            break;
        }
        case SwVideoPixelFormat::Gray8: {
            const uint8_t* src = frame.planeData(0);
            const int srcStride = frame.planeStride(0);
            for (int y = 0; y < frame.height(); ++y) {
                const uint8_t* srcRow = src + y * srcStride;
                uint8_t* dstRow = dst + y * dstStride;
                for (int x = 0; x < frame.width(); ++x) {
                    const uint8_t v = srcRow[x];
                    dstRow[x * 4 + 0] = v;
                    dstRow[x * 4 + 1] = v;
                    dstRow[x * 4 + 2] = v;
                    dstRow[x * 4 + 3] = 255;
                }
            }
            copyMetadata();
            break;
        }
        case SwVideoPixelFormat::NV12: {
            const uint8_t* yPlane = frame.planeData(0);
            const uint8_t* uvPlane = frame.planeData(1);
            const int yStride = frame.planeStride(0);
            const int uvStride = frame.planeStride(1);
            for (int y = 0; y < frame.height(); ++y) {
                const uint8_t* yRow = yPlane + y * yStride;
                const uint8_t* uvRow = uvPlane + (y / 2) * uvStride;
                uint8_t* dstRow = dst + y * dstStride;
                for (int x = 0; x < frame.width(); ++x) {
                    const uint8_t Y = yRow[x];
                    const int chromaIndex = (x / 2) * 2;
                    const int U = static_cast<int>(uvRow[chromaIndex]) - 128;
                    const int V = static_cast<int>(uvRow[chromaIndex + 1]) - 128;
                    const int C = std::max(0, static_cast<int>(Y) - 16);
                    int r = (298 * C + 409 * V + 128) >> 8;
                    int g = (298 * C - 100 * U - 208 * V + 128) >> 8;
                    int b = (298 * C + 516 * U + 128) >> 8;
                    dstRow[x * 4 + 0] = clamp(b);
                    dstRow[x * 4 + 1] = clamp(g);
                    dstRow[x * 4 + 2] = clamp(r);
                    dstRow[x * 4 + 3] = 255;
                }
            }
            copyMetadata();
            break;
        }
        case SwVideoPixelFormat::YUV420P: {
            const uint8_t* yPlane = frame.planeData(0);
            const uint8_t* uPlane = frame.planeData(1);
            const uint8_t* vPlane = frame.planeData(2);
            const int yStride = frame.planeStride(0);
            const int uStride = frame.planeStride(1);
            const int vStride = frame.planeStride(2);
            for (int y = 0; y < frame.height(); ++y) {
                const uint8_t* yRow = yPlane + y * yStride;
                uint8_t* dstRow = dst + y * dstStride;
                const uint8_t* uRow = uPlane + (y / 2) * uStride;
                const uint8_t* vRow = vPlane + (y / 2) * vStride;
                for (int x = 0; x < frame.width(); ++x) {
                    const uint8_t Y = yRow[x];
                    const uint8_t Usample = uRow[x / 2];
                    const uint8_t Vsample = vRow[x / 2];
                    const int U = static_cast<int>(Usample) - 128;
                    const int V = static_cast<int>(Vsample) - 128;
                    const int C = std::max(0, static_cast<int>(Y) - 16);
                    int r = (298 * C + 409 * V + 128) >> 8;
                    int g = (298 * C - 100 * U - 208 * V + 128) >> 8;
                    int b = (298 * C + 516 * U + 128) >> 8;
                    dstRow[x * 4 + 0] = clamp(b);
                    dstRow[x * 4 + 1] = clamp(g);
                    dstRow[x * 4 + 2] = clamp(r);
                    dstRow[x * 4 + 3] = 255;
                }
            }
            copyMetadata();
            break;
        }
        default:
            return false;
        }

        frame = buffer;
        return true;
    }

    SwVideoFrame& conversionBufferFor(int width, int height) {
        if (!m_conversionFrame.isValid() ||
            m_conversionFrame.width() != width ||
            m_conversionFrame.height() != height) {
            m_conversionFrame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        }
        return m_conversionFrame;
    }

private:
    std::shared_ptr<SwVideoPipeline> m_pipeline;
    std::shared_ptr<SwVideoSource> m_source;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    std::shared_ptr<SwVideoRenderer> m_renderer;
    ScalingMode m_scalingMode{ScalingMode::Fit};
    SwColor m_backgroundColor{0, 0, 0};

    mutable std::mutex m_frameMutex;
    SwVideoFrame m_currentFrame;
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    std::function<void(const SwVideoFrame&)> m_frameArrived;

    SwVideoFrame m_conversionFrame;
#if defined(_WIN32)
    void boostGuiThreadPriority() {
        static bool boosted = false;
        if (boosted) {
            return;
        }
        if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
            boosted = true;
        }
    }
#endif

    void presentFrame(const SwVideoFrame& frame) {
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_currentFrame = frame;
            m_lastFrameTime = std::chrono::steady_clock::now();
        }
        if (m_frameArrived) {
            m_frameArrived(frame);
        }
        update();
        if (auto* app = SwGuiApplication::instance(false)) {
            if (auto* integration = app->platformIntegration()) {
                integration->wakeUpGuiThread();
            }
        }
    }

};
