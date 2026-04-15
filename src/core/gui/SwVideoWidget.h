#pragma once

/**
 * @file src/core/gui/SwVideoWidget.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwVideoWidget in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the video widget interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwVideoRenderer, SwFallbackVideoRenderer,
 * SwWin32VideoRenderer, SwD2DVideoRenderer, and SwVideoWidget.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */

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
#include "graphics/SwImage.h"
#include "core/object/SwPointer.h"

#include "media/SwVideoFrame.h"
#include "media/SwVideoSink.h"
#if defined(_WIN32)
#include "media/SwPlatformVideoDecoder.h"
#include "platform/win/SwD3D11VideoInterop.h"
#endif
#include "SwGuiApplication.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
static constexpr const char* kSwLogCategory_SwVideoWidget = "sw.core.gui.swvideowidget";

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <wrl/client.h>
#include <d3d11.h>
#include <d2d1.h>
#include <d2d1helper.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#endif

class SwVideoRenderer {
public:
    /**
     * @brief Destroys the `SwVideoRenderer` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwVideoRenderer() = default;
    /**
     * @brief Performs the `render` operation.
     * @param painter Value passed to the method.
     * @param frame Value passed to the method.
     * @param targetRect Value passed to the method.
     * @return The requested render.
     */
    virtual bool render(SwPainter* painter,
                        const SwVideoFrame& frame,
                        const SwRect& targetRect) = 0;
};

class SwFallbackVideoRenderer : public SwVideoRenderer {
public:
    /**
     * @brief Performs the `render` operation.
     * @param painter Value passed to the method.
     * @param frame Value passed to the method.
     * @param targetRect Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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

class SwImageVideoRenderer : public SwVideoRenderer {
public:
    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter || !frame.isValid() || frame.pixelFormat() != SwVideoPixelFormat::BGRA32 ||
            !frame.planeData(0) || frame.planeStride(0) < frame.width() * 4) {
            return false;
        }

        SwImage image(frame.width(), frame.height(), SwImage::Format_ARGB32);
        if (image.isNull()) {
            return false;
        }

        const uint8_t* src = frame.planeData(0);
        const int srcStride = frame.planeStride(0);
        const std::size_t copyBytes = static_cast<std::size_t>(frame.width()) * 4;
        for (int y = 0; y < frame.height(); ++y) {
            uint8_t* dstRow = reinterpret_cast<uint8_t*>(image.scanLine(y));
            if (!dstRow) {
                return false;
            }
            const uint8_t* srcRow = src + static_cast<std::size_t>(y) * srcStride;
            std::memcpy(dstRow, srcRow, copyBytes);
        }

        painter->drawImage(targetRect, image, nullptr);
        return true;
    }
};

#if !defined(_WIN32)
class SwNativeFrameVideoRenderer : public SwVideoRenderer {
public:
    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter || !frame.isValid() || !frame.isNative()) {
            return false;
        }
        return painter->drawNativeVideoFrame(targetRect, frame);
    }
};

class SwNativeVideoUploadRenderer : public SwVideoRenderer {
public:
    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter || !frame.isValid() || frame.pixelFormat() != SwVideoPixelFormat::BGRA32 ||
            !frame.planeData(0) || frame.planeStride(0) < frame.width() * 4) {
            return false;
        }

        return painter->drawBgra32(targetRect,
                                   frame.planeData(0),
                                   frame.width(),
                                   frame.height(),
                                   frame.planeStride(0));
    }
};
#endif

#if defined(_WIN32)
class SwD3D11VideoRenderer : public SwVideoRenderer {
public:
    SwD3D11VideoRenderer() {
        m_supported = SwD3D11VideoInterop::instance().ensure();
    }

    bool isSupported() const {
        return m_supported && SwD3D11VideoInterop::instance().isReady();
    }

    bool render(SwPainter* painter,
                const SwVideoFrame& frame,
                const SwRect& targetRect) override {
        if (!painter || !frame.isNativeD3D11() || !isSupported()) {
            return false;
        }
        void* native = painter->nativeHandle();
        HDC hdc = native ? reinterpret_cast<HDC>(native) : nullptr;
        if (!hdc || !frame.d3d11Texture()) {
            return false;
        }
        SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
        if (!interop.device() || !interop.context() || !interop.videoDevice() || !interop.videoContext()) {
            return false;
        }
        if (frame.d3d11Device() && frame.d3d11Device() != interop.device()) {
            return false;
        }

        const SwRect deviceRect = painter->mapToDevice(targetRect);
        if (deviceRect.width <= 0 || deviceRect.height <= 0) {
            return false;
        }

        if (!ensureOutputResources(frame, deviceRect)) {
            return false;
        }
        if (!ensureInputView(frame)) {
            return false;
        }
        if (!ensureOutputView()) {
            return false;
        }
        if (!renderToOutput(frame)) {
            return false;
        }
        return blitOutput(hdc, deviceRect);
    }

private:
    bool ensureOutputResources(const SwVideoFrame& frame, const SwRect& deviceRect) {
        const UINT width = static_cast<UINT>(deviceRect.width);
        const UINT height = static_cast<UINT>(deviceRect.height);
        const bool sizeChanged = (m_outputWidth != width || m_outputHeight != height);
        const bool formatChanged =
            (m_inputWidth != static_cast<UINT>(frame.width()) ||
             m_inputHeight != static_cast<UINT>(frame.height()) ||
             m_inputFormat != frame.nativeDxgiFormat());
        if (!sizeChanged && !formatChanged && m_outputTexture && m_enumerator && m_videoProcessor) {
            return true;
        }

        m_outputView.Reset();
        m_outputSurface.Reset();
        m_outputTexture.Reset();
        m_enumerator.Reset();
        m_videoProcessor.Reset();
        m_inputWidth = static_cast<UINT>(frame.width());
        m_inputHeight = static_cast<UINT>(frame.height());
        m_inputFormat = frame.nativeDxgiFormat();
        m_outputWidth = width;
        m_outputHeight = height;

        SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();

        D3D11_TEXTURE2D_DESC outputDesc{};
        outputDesc.Width = width;
        outputDesc.Height = height;
        outputDesc.MipLevels = 1;
        outputDesc.ArraySize = 1;
        outputDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        outputDesc.SampleDesc.Count = 1;
        outputDesc.Usage = D3D11_USAGE_DEFAULT;
        outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        outputDesc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
        HRESULT hr = interop.device()->CreateTexture2D(&outputDesc, nullptr, m_outputTexture.GetAddressOf());
        if (FAILED(hr) || !m_outputTexture) {
            return false;
        }
        if (FAILED(m_outputTexture.As(&m_outputSurface)) || !m_outputSurface) {
            m_outputTexture.Reset();
            return false;
        }

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputWidth = m_inputWidth;
        contentDesc.InputHeight = m_inputHeight;
        contentDesc.OutputWidth = width;
        contentDesc.OutputHeight = height;
        contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        hr = interop.videoDevice()->CreateVideoProcessorEnumerator(&contentDesc, m_enumerator.GetAddressOf());
        if (FAILED(hr) || !m_enumerator) {
            m_outputSurface.Reset();
            m_outputTexture.Reset();
            return false;
        }
        hr = interop.videoDevice()->CreateVideoProcessor(m_enumerator.Get(), 0, m_videoProcessor.GetAddressOf());
        if (FAILED(hr) || !m_videoProcessor) {
            m_enumerator.Reset();
            m_outputSurface.Reset();
            m_outputTexture.Reset();
            return false;
        }
        return true;
    }

    bool ensureInputView(const SwVideoFrame& frame) {
        if (!frame.d3d11Texture()) {
            return false;
        }
        if (m_inputView && m_boundTexture.Get() == frame.d3d11Texture() &&
            m_boundSubresourceIndex == frame.nativeSubresourceIndex()) {
            return true;
        }
        m_inputView.Reset();
        m_boundTexture = frame.d3d11Texture();
        m_boundSubresourceIndex = frame.nativeSubresourceIndex();

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        inputDesc.Texture2D.ArraySlice = m_boundSubresourceIndex;
        HRESULT hr = SwD3D11VideoInterop::instance().videoDevice()->CreateVideoProcessorInputView(
            frame.d3d11Texture(),
            m_enumerator.Get(),
            &inputDesc,
            m_inputView.GetAddressOf());
        if (FAILED(hr)) {
            m_inputView.Reset();
            return false;
        }
        return true;
    }

    bool ensureOutputView() {
        if (m_outputView) {
            return true;
        }
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
        outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        HRESULT hr = SwD3D11VideoInterop::instance().videoDevice()->CreateVideoProcessorOutputView(
            m_outputTexture.Get(),
            m_enumerator.Get(),
            &outputDesc,
            m_outputView.GetAddressOf());
        if (FAILED(hr)) {
            m_outputView.Reset();
            return false;
        }
        return true;
    }

    bool renderToOutput(const SwVideoFrame& frame) {
        SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
        if (frame.nativeDxgiFormat() == DXGI_FORMAT_B8G8R8A8_UNORM &&
            frame.width() == static_cast<int>(m_outputWidth) &&
            frame.height() == static_cast<int>(m_outputHeight)) {
            interop.context()->CopySubresourceRegion(m_outputTexture.Get(),
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     frame.d3d11Texture(),
                                                     frame.nativeSubresourceIndex(),
                                                     nullptr);
            interop.flush();
            return true;
        }

        RECT srcRect{0, 0, frame.width(), frame.height()};
        RECT dstRect{0, 0, static_cast<LONG>(m_outputWidth), static_cast<LONG>(m_outputHeight)};
        interop.videoContext()->VideoProcessorSetOutputTargetRect(m_videoProcessor.Get(), TRUE, &dstRect);
        interop.videoContext()->VideoProcessorSetStreamSourceRect(m_videoProcessor.Get(), 0, TRUE, &srcRect);
        interop.videoContext()->VideoProcessorSetStreamDestRect(m_videoProcessor.Get(), 0, TRUE, &dstRect);
        interop.videoContext()->VideoProcessorSetStreamFrameFormat(m_videoProcessor.Get(),
                                                                   0,
                                                                   D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = m_inputView.Get();
        HRESULT hr = interop.videoContext()->VideoProcessorBlt(m_videoProcessor.Get(),
                                                               m_outputView.Get(),
                                                               0,
                                                               1,
                                                               &stream);
        interop.flush();
        return SUCCEEDED(hr);
    }

    bool blitOutput(HDC targetHdc, const SwRect& deviceRect) {
        if (!m_outputSurface || !targetHdc) {
            return false;
        }
        HDC surfaceDc = nullptr;
        HRESULT hr = m_outputSurface->GetDC(FALSE, &surfaceDc);
        if (FAILED(hr) || !surfaceDc) {
            return false;
        }
        int previousMode = SetStretchBltMode(targetHdc, COLORONCOLOR);
        BOOL copied = BitBlt(targetHdc,
                             deviceRect.x,
                             deviceRect.y,
                             deviceRect.width,
                             deviceRect.height,
                             surfaceDc,
                             0,
                             0,
                             SRCCOPY);
        if (previousMode != 0) {
            SetStretchBltMode(targetHdc, previousMode);
        }
        m_outputSurface->ReleaseDC(nullptr);
        return copied == TRUE;
    }

    bool m_supported{false};
    UINT m_inputWidth{0};
    UINT m_inputHeight{0};
    UINT m_outputWidth{0};
    UINT m_outputHeight{0};
    UINT m_boundSubresourceIndex{0};
    DXGI_FORMAT m_inputFormat{DXGI_FORMAT_UNKNOWN};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_boundTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_outputTexture;
    Microsoft::WRL::ComPtr<IDXGISurface1> m_outputSurface;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_videoProcessor;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> m_inputView;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_outputView;
};

class SwWin32VideoRenderer : public SwVideoRenderer {
public:
    /**
     * @brief Performs the `render` operation.
     * @param painter Value passed to the method.
     * @param frame Value passed to the method.
     * @param targetRect Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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
        const SwRect deviceRect = painter->mapToDevice(targetRect);
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
        int previousMode = SetStretchBltMode(hdc, COLORONCOLOR);
        POINT prevOrg{};
        SetBrushOrgEx(hdc, 0, 0, &prevOrg);

        int result = StretchDIBits(hdc,
                                   deviceRect.x,
                                   deviceRect.y,
                                   deviceRect.width,
                                   deviceRect.height,
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
                                              deviceRect.x,
                                              deviceRect.y,
                                              deviceRect.width,
                                              deviceRect.height,
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
    /**
     * @brief Constructs a `SwD2DVideoRenderer` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwD2DVideoRenderer() {
        ensureFactory();
    }

    /**
     * @brief Returns whether the object reports supported.
     * @return `true` when the object reports supported; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isSupported() const {
        return sharedFactory().Get() != nullptr;
    }

    /**
     * @brief Performs the `render` operation.
     * @param painter Value passed to the method.
     * @param frame Value passed to the method.
     * @param targetRect Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
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
        const SwRect deviceRect = painter->mapToDevice(targetRect);
        if (!ensureRenderTarget()) {
            return false;
        }
        RECT rc{deviceRect.x,
                deviceRect.y,
                deviceRect.x + deviceRect.width,
                deviceRect.y + deviceRect.height};
        HRESULT hr = m_renderTarget->BindDC(hdc, &rc);
        if (FAILED(hr)) {
            return false;
        }

        if (!updateBitmap(frame)) {
            return false;
        }

        m_renderTarget->BeginDraw();
        D2D1_RECT_F dest = D2D1::RectF(static_cast<FLOAT>(deviceRect.x),
                                       static_cast<FLOAT>(deviceRect.y),
                                       static_cast<FLOAT>(deviceRect.x + deviceRect.width),
                                       static_cast<FLOAT>(deviceRect.y + deviceRect.height));
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
 * widget->setVideoDecoder(myDecoder); // optional, defaults to the decoder factory
 * widget->start();
 * @endcode
 */
class SwVideoWidget : public SwWidget {
public:
    using FrameCallback = std::function<void(const SwVideoFrame&)>;

#ifdef None
#undef None
#endif

    enum class ScalingMode {
        Fit,
        Fill,
        Stretch,
        Center
    };

    enum class RenderPath {
        None,
        GPUZeroCopy,
        NativePlatformSurface,
        NativePlatformUpload,
        CpuPainterImage,
        CpuD2DUpload,
        CpuGdiFallback,
        Placeholder
    };

    /**
     * @brief Constructs a `SwVideoWidget` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwVideoWidget(SwWidget* parent = nullptr)
        : SwWidget(parent),
          m_videoSink(std::make_shared<SwVideoSink>()),
          m_renderer(createDefaultRenderer()) {
#if defined(_WIN32)
        boostGuiThreadPriority();
#endif
        attachVideoSink(m_videoSink);
    }

    /**
     * @brief Destroys the `SwVideoWidget` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwVideoWidget() override {
        m_callbackGuard.reset();
        clearPendingFrameDispatch_();
        setRealtimePresentationActive_(false);
        detachVideoSink(m_videoSink);
        m_videoSink.reset();
        stop();
    }

    void setVideoSink(const std::shared_ptr<SwVideoSink>& sink) {
        if (m_videoSink == sink) {
            return;
        }
        setRealtimePresentationActive_(false);
        detachVideoSink(m_videoSink);
        m_videoSink = sink ? sink : std::make_shared<SwVideoSink>();
        attachVideoSink(m_videoSink);
    }

    std::shared_ptr<SwVideoSink> videoSink() const { return m_videoSink; }

    /**
     * @brief Sets the video Source.
     * @param source Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setVideoSource(const std::shared_ptr<SwVideoSource>& source) {
        if (!m_videoSink) {
            m_videoSink = std::make_shared<SwVideoSink>();
            attachVideoSink(m_videoSink);
        }
        m_videoSink->setVideoSource(source);
    }

    /**
     * @brief Returns the current video Source.
     * @return The current video Source.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::shared_ptr<SwVideoSource> videoSource() const {
        return m_videoSink ? m_videoSink->videoSource() : std::shared_ptr<SwVideoSource>();
    }

    /**
     * @brief Sets the video Decoder.
     * @param decoder Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setVideoDecoder(const std::shared_ptr<SwVideoDecoder>& decoder) {
        if (!decoder) {
            return;
        }
        m_decoder = decoder;
        if (m_videoSink) {
            m_videoSink->setVideoDecoder(decoder);
        }
    }

    /**
     * @brief Returns the registered decoder backends for the given codec.
     * @param codec Value passed to the method.
     * @return The registered backends ordered by priority.
     */
    static SwList<SwVideoDecoderDescriptor> availableVideoDecoders(SwVideoPacket::Codec codec) {
        return SwVideoSink::availableVideoDecoders(codec);
    }

    /**
     * @brief Selects a registered decoder backend for the given codec.
     * @param codec Value passed to the method.
     * @param decoderId Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool setPreferredVideoDecoder(SwVideoPacket::Codec codec, const SwString& decoderId) {
        if (!m_videoSink) {
            return false;
        }
        bool ok = m_videoSink->setPreferredVideoDecoder(codec, decoderId);
        if (ok) {
            m_decoder.reset();
        }
        return ok;
    }

    /**
     * @brief Clears the codec-specific decoder backend preference.
     * @param codec Value passed to the method.
     */
    void clearPreferredVideoDecoder(SwVideoPacket::Codec codec) {
        if (!m_videoSink) {
            return;
        }
        m_videoSink->clearPreferredVideoDecoder(codec);
    }

    /**
     * @brief Returns the preferred decoder backend id for the given codec.
     * @param codec Value passed to the method.
     * @return The preferred backend id, if any.
     */
    SwString preferredVideoDecoder(SwVideoPacket::Codec codec) const {
        if (!m_videoSink) {
            return SwString();
        }
        return m_videoSink->preferredVideoDecoder(codec);
    }

    /**
     * @brief Returns the current video Decoder.
     * @return The current video Decoder.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::shared_ptr<SwVideoDecoder> videoDecoder() const { return m_decoder; }

    /**
     * @brief Sets the renderer.
     * @param renderer Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRenderer(const std::shared_ptr<SwVideoRenderer>& renderer) {
        m_renderer = renderer ? renderer : createDefaultRenderer();
        m_useAutomaticRenderer = !renderer;
    }

    /**
     * @brief Returns the current renderer.
     * @return The current renderer.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::shared_ptr<SwVideoRenderer> renderer() const { return m_renderer; }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() {
        if (!m_videoSink) {
            return;
        }
        m_videoSink->start();
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() {
        if (m_videoSink) {
            m_videoSink->stop();
        }
        setRealtimePresentationActive_(false);
    }

    /**
     * @brief Sets the scaling Mode.
     * @param mode Mode value that controls the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setScalingMode(ScalingMode mode) { m_scalingMode = mode; }
    /**
     * @brief Returns the current scaling Mode.
     * @return The current scaling Mode.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ScalingMode scalingMode() const { return m_scalingMode; }

    /**
     * @brief Sets the background Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBackgroundColor(const SwColor& color) { m_backgroundColor = color; }
    /**
     * @brief Returns the current background Color.
     * @return The current background Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor backgroundColor() const { return m_backgroundColor; }

    /**
     * @brief Sets the frame Arrived Callback.
     * @param callback Callback invoked by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFrameArrivedCallback(std::function<void(const SwVideoFrame&)> callback) {
        m_frameArrived = std::move(callback);
    }

    /**
     * @brief Returns whether the object reports frame.
     * @return `true` when the object reports frame; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasFrame() const {
        return m_videoSink ? m_videoSink->hasFrame() : false;
    }

    /**
     * @brief Returns the current current Frame.
     * @return The current current Frame.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwVideoFrame currentFrame() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_currentFrame;
    }

    /**
     * @brief Returns the current last Frame Time.
     * @return The current last Frame Time.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::chrono::steady_clock::time_point lastFrameTime() const {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        return m_lastFrameTime;
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!event) {
            return;
        }
        SwPainter* painter = event->painter();
        if (!painter) {
            return;
        }
        m_repaintPending.store(false);

        const SwRect rect = this->rect();
        painter->fillRect(rect, m_backgroundColor, m_backgroundColor, 0);

        SwVideoFrame frame = currentFrame();
        bool rendered = false;
        if (frame.isValid()) {
            const SwRect target = computeTargetRect(rect, frame);
            if (!m_useAutomaticRenderer && m_renderer) {
                rendered = m_renderer->render(painter, frame, target);
            }
#if defined(_WIN32)
            if (!rendered && frame.isNativeD3D11()) {
                auto gpuRenderer = ensureGpuRenderer_();
                if (gpuRenderer) {
                    rendered = gpuRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = gpuRenderer;
                        }
                        recordRenderPath_(RenderPath::GPUZeroCopy);
                    }
                }
            }
#endif
#if !defined(_WIN32)
            if (!rendered && frame.isNative()) {
                auto nativeFrameRenderer = ensureNativeFrameRenderer_();
                if (nativeFrameRenderer) {
                    rendered = nativeFrameRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = nativeFrameRenderer;
                        }
                        recordRenderPath_(RenderPath::NativePlatformSurface);
                    }
                }
            }
            if (!rendered && isTightBGRAFrame_(frame)) {
                auto nativeRenderer = ensureNativeUploadRenderer_();
                if (nativeRenderer) {
                    rendered = nativeRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = nativeRenderer;
                        }
                        recordRenderPath_(RenderPath::NativePlatformUpload);
                    }
                }
            }
            if (!rendered && isTightBGRAFrame_(frame)) {
                auto imageRenderer = ensureImageRenderer_();
                if (imageRenderer) {
                    rendered = imageRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = imageRenderer;
                        }
                        recordRenderPath_(RenderPath::CpuPainterImage);
                    }
                }
            }
#endif
#if defined(_WIN32)
            if (!rendered && isTightBGRAFrame_(frame)) {
                auto d2dRenderer = ensureD2DRenderer_();
                if (d2dRenderer) {
                    rendered = d2dRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = d2dRenderer;
                        }
                        recordRenderPath_(RenderPath::CpuD2DUpload);
                    }
                }
            }
            if (!rendered) {
                auto gdiRenderer = ensureGdiRenderer_();
                if (gdiRenderer) {
                    rendered = gdiRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = gdiRenderer;
                        }
                        recordRenderPath_(RenderPath::CpuGdiFallback);
                    }
                }
            }
#endif
            if (!rendered) {
                auto fallbackRenderer = ensureFallbackRenderer_();
                if (fallbackRenderer) {
                    rendered = fallbackRenderer->render(painter, frame, target);
                    if (rendered) {
                        if (m_useAutomaticRenderer) {
                            m_renderer = fallbackRenderer;
                        }
                        recordRenderPath_(RenderPath::Placeholder);
                    }
                }
            }
        }

        if (!rendered) {
            recordRenderPath_(RenderPath::Placeholder);
            drawPlaceholder(painter, rect);
            return;
        }

        const auto status = currentStreamStatus();
        if (status.state != SwVideoSource::StreamState::Streaming) {
            drawStatusOverlay(painter, rect, status);
        }
    }

    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
    }

private:
    void attachVideoSink(const std::shared_ptr<SwVideoSink>& sink) {
        if (!sink) {
            return;
        }
        std::weak_ptr<int> weakGuard = m_callbackGuard;
        sink->setFrameCallback([this, weakGuard](const SwVideoFrame& frame) {
            if (weakGuard.expired()) {
                return;
            }
            queueIncomingFrame_(frame);
        });
        sink->setStatusCallback([this, weakGuard](const SwVideoSource::StreamStatus& status) {
            if (weakGuard.expired()) {
                return;
            }
            postGuardedUiAction_([status](SwVideoWidget* self) {
                self->setStreamStatus(status);
            });
        });
        setStreamStatus(sink->streamStatus());
    }

    void detachVideoSink(const std::shared_ptr<SwVideoSink>& sink) {
        if (!sink) {
            return;
        }
        sink->setFrameCallback(FrameCallback());
        sink->setStatusCallback(SwVideoSink::StatusCallback());
        clearPendingFrameDispatch_();
    }

    void postGuardedUiAction_(std::function<void(SwVideoWidget*)> fn) {
        if (!fn) {
            return;
        }
        if (auto* app = SwCoreApplication::instance(false)) {
            const SwPointer<SwVideoWidget> self(this);
            std::weak_ptr<int> weakGuard = m_callbackGuard;
            std::function<void(SwVideoWidget*)> fnCopy = std::move(fn);
            app->postEventOnLane([self, weakGuard, fnCopy]() mutable {
                if (weakGuard.expired() || !self) {
                    return;
                }
                SwVideoWidget* liveSelf = self.data();
                if (!SwObject::isLive(liveSelf)) {
                    return;
                }
                fnCopy(liveSelf);
            }, SwFiberLane::Input);
            return;
        }
        fn(this);
    }

    void queueIncomingFrame_(const SwVideoFrame& frame) {
        bool shouldPost = false;
        {
            std::lock_guard<std::mutex> lock(m_pendingFrameMutex);
            m_pendingFrame = frame;
            m_hasPendingFrame = frame.isValid();
            if (!m_frameDispatchPending) {
                m_frameDispatchPending = true;
                shouldPost = true;
            }
        }
        if (!shouldPost) {
            return;
        }
        postGuardedUiAction_([](SwVideoWidget* self) {
            self->dispatchPendingFrameOnUiThread_();
        });
    }

    void dispatchPendingFrameOnUiThread_() {
        SwVideoFrame frame;
        {
            std::lock_guard<std::mutex> lock(m_pendingFrameMutex);
            if (!m_hasPendingFrame) {
                m_frameDispatchPending = false;
                return;
            }
            frame = m_pendingFrame;
            m_pendingFrame = SwVideoFrame();
            m_hasPendingFrame = false;
            m_frameDispatchPending = false;
        }
        if (frame.isValid()) {
            handleIncomingFrame(frame);
        }
    }

    void clearPendingFrameDispatch_() {
        std::lock_guard<std::mutex> lock(m_pendingFrameMutex);
        m_pendingFrame = SwVideoFrame();
        m_hasPendingFrame = false;
        m_frameDispatchPending = false;
    }

    void setStreamStatus(const SwVideoSource::StreamStatus& status) {
        {
            std::lock_guard<std::mutex> lock(m_streamStatusMutex);
            m_streamStatus = status;
        }
        setRealtimePresentationActive_(status.state == SwVideoSource::StreamState::Streaming);
        if (!m_repaintPending.exchange(true)) {
            update();
        }
    }

    SwVideoSource::StreamStatus currentStreamStatus() const {
        std::lock_guard<std::mutex> lock(m_streamStatusMutex);
        return m_streamStatus;
    }

    SwString statusTextForDisplay(const SwVideoSource::StreamStatus& status) const {
        if (!status.reason.isEmpty()) {
            return status.reason;
        }
        switch (status.state) {
        case SwVideoSource::StreamState::Connecting:
            return "Connecting stream...";
        case SwVideoSource::StreamState::Recovering:
            return "Recovering stream...";
        case SwVideoSource::StreamState::Streaming:
            return "Streaming";
        case SwVideoSource::StreamState::Stopped:
        default:
            break;
        }
        return (m_videoSink && m_videoSink->videoSource()) ? SwString("Waiting stream...")
                                                           : SwString("No source");
    }

    void drawStatusOverlay(SwPainter* painter,
                           const SwRect& rect,
                           const SwVideoSource::StreamStatus& status) {
        if (!painter) {
            return;
        }
        const SwString text = statusTextForDisplay(status);
        if (text.isEmpty()) {
            return;
        }
        SwRect banner{rect.x + 12, rect.y + 12, std::max(160, rect.width / 3), 32};
        painter->fillRect(banner, SwColor{0, 0, 0}, SwColor{0, 0, 0}, 0);
        painter->drawText(banner,
                          text,
                          DrawTextFormat::VCenter,
                          {235, 235, 235},
                          SwFont());
    }

    static std::shared_ptr<SwVideoRenderer> createDefaultRenderer() {
        return std::make_shared<SwFallbackVideoRenderer>();
    }

    void handleIncomingFrame(const SwVideoFrame& frame) {
        if (frame.isNative()) {
            presentFrame(frame);
            return;
        }
        if (frame.isValid() &&
            frame.pixelFormat() == SwVideoPixelFormat::BGRA32 &&
            frame.planeData(0) &&
            frame.planeStride(0) == frame.width() * 4) {
            presentFrame(frame);
            return;
        }
        SwVideoFrame readyFrame = convertToBGRA(frame);
        if (!readyFrame.isValid()) {
            return;
        }
        presentFrame(readyFrame);
    }

    void drawPlaceholder(SwPainter* painter, const SwRect& rect) {
        if (!painter) {
            return;
        }
        painter->fillRect(rect, m_backgroundColor, {80, 80, 80}, 1);
        SwString text = statusTextForDisplay(currentStreamStatus());
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

    static bool isTightBGRAFrame_(const SwVideoFrame& frame) {
        return frame.isValid() &&
               frame.pixelFormat() == SwVideoPixelFormat::BGRA32 &&
               frame.planeData(0) &&
               frame.planeStride(0) == frame.width() * 4;
    }

    std::shared_ptr<SwVideoRenderer> ensureFallbackRenderer_() {
        if (!m_fallbackRenderer) {
            m_fallbackRenderer = std::make_shared<SwFallbackVideoRenderer>();
        }
        return m_fallbackRenderer;
    }

    std::shared_ptr<SwVideoRenderer> ensureImageRenderer_() {
        if (!m_imageRenderer) {
            m_imageRenderer = std::make_shared<SwImageVideoRenderer>();
        }
        return m_imageRenderer;
    }

#if !defined(_WIN32)
    std::shared_ptr<SwVideoRenderer> ensureNativeFrameRenderer_() {
        if (!m_nativeFrameRenderer) {
            m_nativeFrameRenderer = std::make_shared<SwNativeFrameVideoRenderer>();
        }
        return m_nativeFrameRenderer;
    }

    std::shared_ptr<SwVideoRenderer> ensureNativeUploadRenderer_() {
        if (!m_nativeUploadRenderer) {
            m_nativeUploadRenderer = std::make_shared<SwNativeVideoUploadRenderer>();
        }
        return m_nativeUploadRenderer;
    }
#endif

#if defined(_WIN32)
    std::shared_ptr<SwVideoRenderer> ensureGpuRenderer_() {
        if (!m_gpuRenderer) {
            auto renderer = std::make_shared<SwD3D11VideoRenderer>();
            if (renderer->isSupported()) {
                m_gpuRenderer = renderer;
            }
        }
        return m_gpuRenderer;
    }

    std::shared_ptr<SwVideoRenderer> ensureD2DRenderer_() {
        if (!m_d2dRenderer) {
            auto renderer = std::make_shared<SwD2DVideoRenderer>();
            if (renderer->isSupported()) {
                m_d2dRenderer = renderer;
            }
        }
        return m_d2dRenderer;
    }

    std::shared_ptr<SwVideoRenderer> ensureGdiRenderer_() {
        if (!m_gdiRenderer) {
            m_gdiRenderer = std::make_shared<SwWin32VideoRenderer>();
        }
        return m_gdiRenderer;
    }
#endif

    static const char* renderPathName_(RenderPath path) {
        switch (path) {
        case RenderPath::GPUZeroCopy:
            return "GPU zero-copy";
        case RenderPath::NativePlatformSurface:
            return "native platform surface";
        case RenderPath::NativePlatformUpload:
            return "native platform upload";
        case RenderPath::CpuPainterImage:
            return "CPU painter image";
        case RenderPath::CpuD2DUpload:
            return "CPU D2D upload";
        case RenderPath::CpuGdiFallback:
            return "CPU GDI fallback";
        case RenderPath::Placeholder:
            return "placeholder";
        case RenderPath::None:
        default:
            return "none";
        }
    }

    void recordRenderPath_(RenderPath path) {
        RenderPath previous = m_renderPath.load();
        if (previous == path) {
            return;
        }
        m_renderPath.store(path);
        swCWarning(kSwLogCategory_SwVideoWidget)
            << "[SwVideoWidget] Render path=" << renderPathName_(path);
    }

    SwVideoFrame convertToBGRA(const SwVideoFrame& frame) {
        if (!frame.isValid()) {
            return {};
        }

        SwVideoFrame buffer = SwVideoFrame::allocate(frame.width(),
                                                     frame.height(),
                                                     SwVideoPixelFormat::BGRA32);
        uint8_t* dst = buffer.planeData(0);
        if (!dst) {
            return {};
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
        case SwVideoPixelFormat::P010:
        case SwVideoPixelFormat::P016: {
            const uint16_t* yPlane = reinterpret_cast<const uint16_t*>(frame.planeData(0));
            const uint16_t* uvPlane = reinterpret_cast<const uint16_t*>(frame.planeData(1));
            const int yStride = frame.planeStride(0) / static_cast<int>(sizeof(uint16_t));
            const int uvStride = frame.planeStride(1) / static_cast<int>(sizeof(uint16_t));
            for (int y = 0; y < frame.height(); ++y) {
                const uint16_t* yRow = yPlane + static_cast<std::size_t>(y) * yStride;
                const uint16_t* uvRow = uvPlane + static_cast<std::size_t>(y / 2) * uvStride;
                uint8_t* dstRow = dst + y * dstStride;
                for (int x = 0; x < frame.width(); ++x) {
                    const uint8_t Y = static_cast<uint8_t>(yRow[x] >> 8);
                    const int chromaIndex = (x / 2) * 2;
                    const int U = static_cast<int>(static_cast<uint8_t>(uvRow[chromaIndex] >> 8)) - 128;
                    const int V = static_cast<int>(static_cast<uint8_t>(uvRow[chromaIndex + 1] >> 8)) - 128;
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
        case SwVideoPixelFormat::YUY2: {
            const uint8_t* src = frame.planeData(0);
            const int srcStride = frame.planeStride(0);
            for (int y = 0; y < frame.height(); ++y) {
                const uint8_t* srcRow = src + static_cast<std::size_t>(y) * srcStride;
                uint8_t* dstRow = dst + static_cast<std::size_t>(y) * dstStride;
                for (int x = 0; x < frame.width(); x += 2) {
                    const int idx = x * 2;
                    const int y0 = srcRow[idx + 0];
                    const int u = srcRow[idx + 1] - 128;
                    const int y1 = srcRow[idx + 2];
                    const int v = srcRow[idx + 3] - 128;
                    const int c0 = std::max(0, y0 - 16);
                    const int c1 = std::max(0, y1 - 16);
                    auto convertPixel = [&](int C, uint8_t* dstPixel) {
                        int r = (298 * C + 409 * v + 128) >> 8;
                        int g = (298 * C - 100 * u - 208 * v + 128) >> 8;
                        int b = (298 * C + 516 * u + 128) >> 8;
                        dstPixel[0] = clamp(b);
                        dstPixel[1] = clamp(g);
                        dstPixel[2] = clamp(r);
                        dstPixel[3] = 255;
                    };
                    convertPixel(c0, dstRow + x * 4);
                    if (x + 1 < frame.width()) {
                        convertPixel(c1, dstRow + (x + 1) * 4);
                    }
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
            return {};
        }

        return buffer;
    }

private:
    std::shared_ptr<SwVideoSink> m_videoSink;
    std::shared_ptr<SwVideoDecoder> m_decoder;
    std::shared_ptr<SwVideoRenderer> m_renderer;
#if defined(_WIN32)
    std::shared_ptr<SwVideoRenderer> m_gpuRenderer;
    std::shared_ptr<SwVideoRenderer> m_d2dRenderer;
    std::shared_ptr<SwVideoRenderer> m_gdiRenderer;
#endif
#if !defined(_WIN32)
    std::shared_ptr<SwVideoRenderer> m_nativeFrameRenderer;
    std::shared_ptr<SwVideoRenderer> m_nativeUploadRenderer;
#endif
    std::shared_ptr<SwVideoRenderer> m_imageRenderer;
    std::shared_ptr<SwVideoRenderer> m_fallbackRenderer;
    ScalingMode m_scalingMode{ScalingMode::Fit};
    SwColor m_backgroundColor{0, 0, 0};
    std::atomic<RenderPath> m_renderPath{RenderPath::None};
    bool m_useAutomaticRenderer{true};

    mutable std::mutex m_frameMutex;
    SwVideoFrame m_currentFrame;
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    std::function<void(const SwVideoFrame&)> m_frameArrived;
    mutable std::mutex m_pendingFrameMutex;
    SwVideoFrame m_pendingFrame;
    bool m_hasPendingFrame{false};
    bool m_frameDispatchPending{false};
    mutable std::mutex m_streamStatusMutex;
    SwVideoSource::StreamStatus m_streamStatus{};
    std::shared_ptr<int> m_callbackGuard{std::make_shared<int>(0)};
    std::atomic<bool> m_repaintPending{false};
    std::atomic<bool> m_realtimePresentationActive{false};
    std::atomic<bool> m_loggedFirstPresentedFrame{false};
    std::atomic<uint64_t> m_presentedFrameCount{0};
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
        auto presented = ++m_presentedFrameCount;
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_currentFrame = frame;
            m_lastFrameTime = std::chrono::steady_clock::now();
        }
        setRealtimePresentationActive_(true);
        if (!m_loggedFirstPresentedFrame.exchange(true)) {
            swCWarning(kSwLogCategory_SwVideoWidget) << "[SwVideoWidget] First frame presented "
                        << frame.width() << "x" << frame.height()
                        << " ts=" << frame.timestamp()
                        << " fmt=" << static_cast<int>(frame.pixelFormat());
        } else if ((presented % 25) == 0) {
            swCWarning(kSwLogCategory_SwVideoWidget) << "[SwVideoWidget] Presented frame count="
                        << presented
                        << " ts=" << frame.timestamp()
                        << " fmt=" << static_cast<int>(frame.pixelFormat());
        }
        if (m_frameArrived) {
            m_frameArrived(frame);
        }
        if (!m_repaintPending.exchange(true)) {
            update();
        }
    }

    void setRealtimePresentationActive_(bool active) {
        const bool previous = m_realtimePresentationActive.exchange(active);
        if (previous == active) {
            return;
        }
        SwWidgetPlatformAdapter::setDamageThrottleSuppressed(nativeWindowHandle(), active);
    }

};

