#pragma once

/**
 * @file src/core/gui/SwSurfaceWidget.h
 * @ingroup core_gui
 * @brief GPU-only native surface host for zero-copy D3D11 video presentation.
 */

#include "SwGuiApplication.h"
#include "SwPainter.h"
#include "SwWidget.h"

#include "media/SwVideoFrame.h"
#include "media/SwVideoSink.h"
#include "media/SwVideoSource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

static constexpr const char* kSwLogCategory_SwSurfaceWidget = "sw.core.gui.swsurfacewidget";

#if defined(_WIN32)
#include "platform/win/SwD3D11VideoInterop.h"
#include "platform/win/SwWindows.h"
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

class SwSurfaceWidget : public SwWidget {
public:
    enum class PresentationMode {
        Fit,
        Fill,
        Stretch
    };

    enum class SurfaceState {
        Idle,
        AwaitingNativeSurface,
        Ready,
        Streaming,
        UnsupportedPlatform,
        UnsupportedFramePath,
        DeviceMismatch,
        SurfaceError
    };

    struct PresentStats {
        std::uint64_t framesSubmitted{0};
        std::uint64_t framesPresented{0};
        std::uint64_t framesDropped{0};
        std::uint64_t framesRejected{0};
        int surfaceWidth{0};
        int surfaceHeight{0};
        int frameWidth{0};
        int frameHeight{0};
        std::int64_t lastFrameTimestamp{-1};
        std::int64_t lastPresentTimeNs{0};
    };

    SwSurfaceWidget(SwWidget* parent = nullptr);
    ~SwSurfaceWidget() override;

    void setVideoSink(const std::shared_ptr<SwVideoSink>& sink);
    std::shared_ptr<SwVideoSink> videoSink() const;

    void setVideoSource(const std::shared_ptr<SwVideoSource>& source);
    std::shared_ptr<SwVideoSource> videoSource() const;

    void start();
    void stop();

    void setPresentationMode(PresentationMode mode);
    PresentationMode presentationMode() const;

    void setBackgroundColor(const SwColor& color);
    SwColor backgroundColor() const;

    SurfaceState surfaceState() const;
    SwString surfaceStateText() const;
    PresentStats lastPresentStats() const;

    bool submitNativeFrame(const SwVideoFrame& frame);

protected:
    void paintEvent(PaintEvent* event) override;
    void resizeEvent(ResizeEvent* event) override;
    void moveEvent(MoveEvent* event) override;
    void showEvent(Event* event) override;
    void hideEvent(Event* event) override;
    void newParentEvent(SwObject* parent) override;

private:
#if defined(_WIN32)
    class SwD3D11VideoSurfacePresenter;
    class SwD3D11ChildSurfaceHost;
#endif

    void attachVideoSink_(const std::shared_ptr<SwVideoSink>& sink);
    void detachVideoSink_(const std::shared_ptr<SwVideoSink>& sink);
    void setState_(SurfaceState state);

#if defined(_WIN32)
    bool submitNativeFrameWin_(const SwVideoFrame& frame);
    void syncNativeSurface_();
#endif

    std::shared_ptr<SwVideoSink> m_videoSink;
    std::shared_ptr<int> m_callbackGuard;
    mutable std::mutex m_stateMutex;
    PresentationMode m_presentationMode;
    SwColor m_backgroundColor;
    SurfaceState m_state;
#if defined(_WIN32)
    std::unique_ptr<SwD3D11ChildSurfaceHost> m_host;
    std::unique_ptr<SwD3D11VideoSurfacePresenter> m_presenter;
#endif
};

#if defined(_WIN32)

class SwSurfaceWidget::SwD3D11VideoSurfacePresenter {
public:
    enum class SubmitResult {
        Queued,
        UnsupportedFrame,
        DeviceMismatch,
        SurfaceUnavailable,
        SurfaceError
    };

    SwD3D11VideoSurfacePresenter();
    ~SwD3D11VideoSurfacePresenter();

    void setPresentationMode(PresentationMode mode);
    void setBackgroundColor(const SwColor& color);

    void attachHostWindow(HWND hwnd, int width, int height);
    void onHostResize(int width, int height);
    void detachHostWindow();

    void clearQueuedFrame();
    SubmitResult submitFrame(const SwVideoFrame& frame);
    void requestBlankPresent();
    void handlePresentMessage();

    PresentStats stats() const;

private:
    bool interopDeviceIsHardware_() const;
    bool schedulePresentLocked_();

    bool ensureFactory_();
    bool ensureSwapChain_(HWND hwnd, int width, int height);
    bool ensureProcessorResources_(const SwVideoFrame& frame);
    bool ensureInputView_(const SwVideoFrame& frame);
    bool ensureOutputView_();

    static void computeRects_(PresentationMode mode,
                              int inputWidth,
                              int inputHeight,
                              int outputWidth,
                              int outputHeight,
                              RECT& srcRect,
                              RECT& dstRect);

    void clearRenderTarget_(const SwColor& color);
    HRESULT clearAndPresent_(const SwColor& color);
    HRESULT presentFrame_(const SwVideoFrame& frame,
                          PresentationMode mode,
                          const SwColor& backgroundColor);
    void releaseDeviceResources_();

    mutable std::mutex m_mutex;
    HWND m_hostHwnd;
    SwVideoFrame m_latestFrame;
    PresentationMode m_mode;
    SwColor m_backgroundColor;
    PresentStats m_stats;
    bool m_presentQueued;
    bool m_swapChainDirty;
    int m_surfaceWidth;
    int m_surfaceHeight;
    std::uint64_t m_lastQueuedSequence;
    std::uint64_t m_lastPresentedSequence;

    Microsoft::WRL::ComPtr<IDXGIFactory2> m_factory;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_backBuffer;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_backBufferRtv;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_enumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_videoProcessor;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> m_inputView;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_outputView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_boundTexture;
    UINT m_boundSubresourceIndex;
    UINT m_inputWidth;
    UINT m_inputHeight;
    DXGI_FORMAT m_inputFormat;
    UINT m_outputWidth;
    UINT m_outputHeight;
};

class SwSurfaceWidget::SwD3D11ChildSurfaceHost {
public:
    SwD3D11ChildSurfaceHost();
    ~SwD3D11ChildSurfaceHost();

    static UINT presentMessageId();

    void setPresenter(SwD3D11VideoSurfacePresenter* presenter);
    bool ensure(const SwWidgetPlatformHandle& parentHandle, const SwRect& rect, bool visible);
    void destroy();
    void setVisible(bool visible);
    void postPresent();

    HWND hwnd() const;

private:
    static const wchar_t* className_();
    static void registerClass_();
    bool createWindow_();
    void syncGeometry_();

    static LRESULT CALLBACK wndProc_(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_parentHandle;
    HWND m_hwnd;
    SwRect m_rect;
    bool m_visible;
    SwD3D11VideoSurfacePresenter* m_presenter;
};

#endif

inline SwSurfaceWidget::SwSurfaceWidget(SwWidget* parent)
    : SwWidget(parent),
      m_videoSink(std::make_shared<SwVideoSink>()),
      m_callbackGuard(std::make_shared<int>(0)),
      m_presentationMode(PresentationMode::Fit),
      m_backgroundColor{0, 0, 0},
      m_state(SurfaceState::Idle)
#if defined(_WIN32)
      , m_host(new SwD3D11ChildSurfaceHost()),
      m_presenter(new SwD3D11VideoSurfacePresenter())
#endif
{
    attachVideoSink_(m_videoSink);
#if !defined(_WIN32)
    setState_(SurfaceState::UnsupportedPlatform);
#endif
}

inline SwSurfaceWidget::~SwSurfaceWidget() {
    detachVideoSink_(m_videoSink);
    stop();
#if defined(_WIN32)
    m_callbackGuard.reset();
    if (m_presenter) {
        m_presenter->detachHostWindow();
    }
    if (m_host) {
        m_host->destroy();
    }
#endif
    m_videoSink.reset();
}

inline void SwSurfaceWidget::setVideoSink(const std::shared_ptr<SwVideoSink>& sink) {
    if (m_videoSink == sink) {
        return;
    }
    detachVideoSink_(m_videoSink);
    m_videoSink = sink ? sink : std::make_shared<SwVideoSink>();
    attachVideoSink_(m_videoSink);
}

inline std::shared_ptr<SwVideoSink> SwSurfaceWidget::videoSink() const { return m_videoSink; }

inline void SwSurfaceWidget::setVideoSource(const std::shared_ptr<SwVideoSource>& source) {
    if (!m_videoSink) {
        m_videoSink = std::make_shared<SwVideoSink>();
        attachVideoSink_(m_videoSink);
    }
    m_videoSink->setVideoSource(source);
}

inline std::shared_ptr<SwVideoSource> SwSurfaceWidget::videoSource() const {
    return m_videoSink ? m_videoSink->videoSource() : std::shared_ptr<SwVideoSource>();
}

inline void SwSurfaceWidget::start() {
    if (m_videoSink) {
        m_videoSink->start();
    }
#if defined(_WIN32)
    syncNativeSurface_();
#endif
}

inline void SwSurfaceWidget::stop() {
    if (m_videoSink) {
        m_videoSink->stop();
    }
#if defined(_WIN32)
    if (m_presenter) {
        m_presenter->clearQueuedFrame();
        m_presenter->requestBlankPresent();
    }
#endif
    setState_(SurfaceState::Idle);
}

inline void SwSurfaceWidget::setPresentationMode(PresentationMode mode) {
    m_presentationMode = mode;
#if defined(_WIN32)
    if (m_presenter) {
        m_presenter->setPresentationMode(mode);
    }
#endif
}

inline SwSurfaceWidget::PresentationMode SwSurfaceWidget::presentationMode() const { return m_presentationMode; }

inline void SwSurfaceWidget::setBackgroundColor(const SwColor& color) {
    m_backgroundColor = color;
#if defined(_WIN32)
    if (m_presenter) {
        m_presenter->setBackgroundColor(color);
    }
#endif
}

inline SwColor SwSurfaceWidget::backgroundColor() const { return m_backgroundColor; }

inline SwSurfaceWidget::SurfaceState SwSurfaceWidget::surfaceState() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_state;
}

inline SwString SwSurfaceWidget::surfaceStateText() const {
    switch (surfaceState()) {
    case SurfaceState::Idle:
        return "Idle";
    case SurfaceState::AwaitingNativeSurface:
        return "AwaitingNativeSurface";
    case SurfaceState::Ready:
        return "Ready";
    case SurfaceState::Streaming:
        return "Streaming";
    case SurfaceState::UnsupportedPlatform:
        return "UnsupportedPlatform";
    case SurfaceState::UnsupportedFramePath:
        return "UnsupportedFramePath";
    case SurfaceState::DeviceMismatch:
        return "DeviceMismatch";
    case SurfaceState::SurfaceError:
    default:
        return "SurfaceError";
    }
}

inline SwSurfaceWidget::PresentStats SwSurfaceWidget::lastPresentStats() const {
#if defined(_WIN32)
    return m_presenter ? m_presenter->stats() : PresentStats{};
#else
    return PresentStats{};
#endif
}

inline bool SwSurfaceWidget::submitNativeFrame(const SwVideoFrame& frame) {
#if defined(_WIN32)
    return submitNativeFrameWin_(frame);
#else
    SW_UNUSED(frame);
    setState_(SurfaceState::UnsupportedPlatform);
    return false;
#endif
}

inline void SwSurfaceWidget::paintEvent(PaintEvent* event) { SW_UNUSED(event); }

inline void SwSurfaceWidget::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
#if defined(_WIN32)
    syncNativeSurface_();
#endif
}

inline void SwSurfaceWidget::moveEvent(MoveEvent* event) {
    SwWidget::moveEvent(event);
#if defined(_WIN32)
    syncNativeSurface_();
#endif
}

inline void SwSurfaceWidget::showEvent(Event* event) {
    SwWidget::showEvent(event);
#if defined(_WIN32)
    syncNativeSurface_();
#endif
}

inline void SwSurfaceWidget::hideEvent(Event* event) {
#if defined(_WIN32)
    if (m_host) {
        m_host->setVisible(false);
    }
#endif
    SwWidget::hideEvent(event);
}

inline void SwSurfaceWidget::newParentEvent(SwObject* parent) {
    SwWidget::newParentEvent(parent);
#if defined(_WIN32)
    syncNativeSurface_();
#else
    SW_UNUSED(parent);
#endif
}

inline void SwSurfaceWidget::attachVideoSink_(const std::shared_ptr<SwVideoSink>& sink) {
    if (!sink) {
        return;
    }
    std::weak_ptr<int> weakGuard = m_callbackGuard;
    sink->setFrameCallback([this, weakGuard](const SwVideoFrame& frame) {
        if (weakGuard.expired()) {
            return;
        }
        submitNativeFrame(frame);
    });
}

inline void SwSurfaceWidget::detachVideoSink_(const std::shared_ptr<SwVideoSink>& sink) {
    if (!sink) {
        return;
    }
    sink->setFrameCallback(SwVideoSink::FrameCallback());
}

inline void SwSurfaceWidget::setState_(SurfaceState state) {
    const char* stateName = "SurfaceError";
    switch (state) {
    case SurfaceState::Idle:
        stateName = "Idle";
        break;
    case SurfaceState::AwaitingNativeSurface:
        stateName = "AwaitingNativeSurface";
        break;
    case SurfaceState::Ready:
        stateName = "Ready";
        break;
    case SurfaceState::Streaming:
        stateName = "Streaming";
        break;
    case SurfaceState::UnsupportedPlatform:
        stateName = "UnsupportedPlatform";
        break;
    case SurfaceState::UnsupportedFramePath:
        stateName = "UnsupportedFramePath";
        break;
    case SurfaceState::DeviceMismatch:
        stateName = "DeviceMismatch";
        break;
    case SurfaceState::SurfaceError:
    default:
        break;
    }
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (m_state == state) {
        return;
    }
    m_state = state;
    swCWarning(kSwLogCategory_SwSurfaceWidget)
        << "[SwSurfaceWidget] State=" << stateName;
}

#if defined(_WIN32)

inline bool SwSurfaceWidget::submitNativeFrameWin_(const SwVideoFrame& frame) {
    if (!m_presenter) {
        setState_(SurfaceState::SurfaceError);
        return false;
    }
    const SwD3D11VideoSurfacePresenter::SubmitResult result = m_presenter->submitFrame(frame);
    switch (result) {
    case SwD3D11VideoSurfacePresenter::SubmitResult::Queued:
        setState_(SurfaceState::Streaming);
        return true;
    case SwD3D11VideoSurfacePresenter::SubmitResult::UnsupportedFrame:
        setState_(SurfaceState::UnsupportedFramePath);
        swCWarning(kSwLogCategory_SwSurfaceWidget)
            << "[SwSurfaceWidget] Rejected non-native frame width=" << frame.width()
            << " height=" << frame.height()
            << " fmt=" << static_cast<int>(frame.pixelFormat());
        return false;
    case SwD3D11VideoSurfacePresenter::SubmitResult::DeviceMismatch:
        setState_(SurfaceState::DeviceMismatch);
        swCWarning(kSwLogCategory_SwSurfaceWidget)
            << "[SwSurfaceWidget] Rejected frame from foreign D3D11 device";
        return false;
    case SwD3D11VideoSurfacePresenter::SubmitResult::SurfaceUnavailable:
        setState_(SurfaceState::SurfaceError);
        swCWarning(kSwLogCategory_SwSurfaceWidget)
            << "[SwSurfaceWidget] Surface unavailable for native frame presentation";
        return false;
    case SwD3D11VideoSurfacePresenter::SubmitResult::SurfaceError:
    default:
        setState_(SurfaceState::SurfaceError);
        return false;
    }
}

inline void SwSurfaceWidget::syncNativeSurface_() {
    if (!m_host || !m_presenter) {
        setState_(SurfaceState::SurfaceError);
        return;
    }
    m_host->setPresenter(m_presenter.get());
    m_presenter->setBackgroundColor(m_backgroundColor);
    m_presenter->setPresentationMode(m_presentationMode);

    const bool visible = isVisibleInHierarchy();
    const SwWidgetPlatformHandle handle = nativeWindowHandle();
    if (!handle) {
        m_host->destroy();
        if (visible) {
            setState_(SurfaceState::AwaitingNativeSurface);
        }
        return;
    }

    const SwRect rect = absoluteRect_();
    if (rect.width <= 0 || rect.height <= 0) {
        m_host->setVisible(false);
        setState_(SurfaceState::AwaitingNativeSurface);
        return;
    }

    if (!m_host->ensure(handle, rect, visible)) {
        setState_(SurfaceState::SurfaceError);
        return;
    }

    const SurfaceState current = surfaceState();
    if (current == SurfaceState::Idle || current == SurfaceState::AwaitingNativeSurface) {
        setState_(SurfaceState::Ready);
    }
}

inline SwSurfaceWidget::SwD3D11ChildSurfaceHost::SwD3D11ChildSurfaceHost()
    : m_parentHandle(nullptr),
      m_hwnd(nullptr),
      m_rect{0, 0, 0, 0},
      m_visible(false),
      m_presenter(nullptr) {}

inline SwSurfaceWidget::SwD3D11ChildSurfaceHost::~SwD3D11ChildSurfaceHost() {
    destroy();
}

inline UINT SwSurfaceWidget::SwD3D11ChildSurfaceHost::presentMessageId() {
    return WM_APP + 0x217;
}

inline void SwSurfaceWidget::SwD3D11ChildSurfaceHost::setPresenter(SwD3D11VideoSurfacePresenter* presenter) {
    m_presenter = presenter;
    if (m_presenter) {
        m_presenter->attachHostWindow(m_hwnd, m_rect.width, m_rect.height);
    }
}

inline bool SwSurfaceWidget::SwD3D11ChildSurfaceHost::ensure(const SwWidgetPlatformHandle& parentHandle,
                                                             const SwRect& rect,
                                                             bool visible) {
    m_parentHandle = SwWidgetPlatformAdapter::nativeHandleAs<HWND>(parentHandle);
    m_rect = rect;
    if (!m_parentHandle) {
        destroy();
        return false;
    }
    registerClass_();
    if (!m_hwnd) {
        if (!createWindow_()) {
            return false;
        }
    } else if (::GetParent(m_hwnd) != m_parentHandle) {
        destroy();
        if (!createWindow_()) {
            return false;
        }
    }
    syncGeometry_();
    setVisible(visible);
    return m_hwnd != nullptr;
}

inline void SwSurfaceWidget::SwD3D11ChildSurfaceHost::destroy() {
    if (m_presenter) {
        m_presenter->attachHostWindow(nullptr, 0, 0);
    }
    if (m_hwnd) {
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_parentHandle = nullptr;
    m_rect = SwRect{0, 0, 0, 0};
}

inline void SwSurfaceWidget::SwD3D11ChildSurfaceHost::setVisible(bool visible) {
    m_visible = visible;
    if (!m_hwnd) {
        return;
    }
    ShowWindow(m_hwnd, visible ? SW_SHOWNA : SW_HIDE);
}

inline void SwSurfaceWidget::SwD3D11ChildSurfaceHost::postPresent() {
    if (m_hwnd) {
        PostMessageW(m_hwnd, presentMessageId(), 0, 0);
    }
}

inline HWND SwSurfaceWidget::SwD3D11ChildSurfaceHost::hwnd() const { return m_hwnd; }

inline const wchar_t* SwSurfaceWidget::SwD3D11ChildSurfaceHost::className_() {
    return L"SwSurfaceWidgetChildWindow";
}

inline void SwSurfaceWidget::SwD3D11ChildSurfaceHost::registerClass_() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &SwD3D11ChildSurfaceHost::wndProc_;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className_();
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    registered = true;
}

inline bool SwSurfaceWidget::SwD3D11ChildSurfaceHost::createWindow_() {
    if (!m_parentHandle) {
        return false;
    }
    m_hwnd = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY,
        className_(),
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_DISABLED,
        std::max(0, m_rect.x),
        std::max(0, m_rect.y),
        std::max(1, m_rect.width),
        std::max(1, m_rect.height),
        m_parentHandle,
        nullptr,
        GetModuleHandle(nullptr),
        this);
    if (!m_hwnd) {
        return false;
    }
    if (m_presenter) {
        m_presenter->attachHostWindow(m_hwnd, std::max(1, m_rect.width), std::max(1, m_rect.height));
    }
    return true;
}

inline void SwSurfaceWidget::SwD3D11ChildSurfaceHost::syncGeometry_() {
    if (!m_hwnd) {
        return;
    }
    SetWindowPos(m_hwnd,
                 nullptr,
                 std::max(0, m_rect.x),
                 std::max(0, m_rect.y),
                 std::max(1, m_rect.width),
                 std::max(1, m_rect.height),
                 SWP_NOACTIVATE | SWP_NOZORDER);
    if (m_presenter) {
        m_presenter->onHostResize(std::max(1, m_rect.width), std::max(1, m_rect.height));
    }
}

inline LRESULT CALLBACK SwSurfaceWidget::SwD3D11ChildSurfaceHost::wndProc_(HWND hwnd,
                                                                          UINT msg,
                                                                          WPARAM wp,
                                                                          LPARAM lp) {
    auto* self = reinterpret_cast<SwD3D11ChildSurfaceHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<SwD3D11ChildSurfaceHost*>(cs ? cs->lpCreateParams : nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (!self) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (self->m_presenter) {
            self->m_presenter->onHostResize(
                std::max<int>(1, static_cast<int>(LOWORD(lp))),
                std::max<int>(1, static_cast<int>(HIWORD(lp))));
        }
        return 0;
    case WM_SHOWWINDOW:
        if (self->m_presenter) {
            self->m_presenter->requestBlankPresent();
        }
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, msg, wp, lp);
    default:
        if (msg == presentMessageId()) {
            if (self->m_presenter) {
                self->m_presenter->handlePresentMessage();
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

inline SwSurfaceWidget::SwD3D11VideoSurfacePresenter::SwD3D11VideoSurfacePresenter()
    : m_hostHwnd(nullptr),
      m_mode(PresentationMode::Fit),
      m_backgroundColor{0, 0, 0},
      m_presentQueued(false),
      m_swapChainDirty(true),
      m_surfaceWidth(0),
      m_surfaceHeight(0),
      m_lastQueuedSequence(0),
      m_lastPresentedSequence(0),
      m_boundSubresourceIndex(0),
      m_inputWidth(0),
      m_inputHeight(0),
      m_inputFormat(DXGI_FORMAT_UNKNOWN),
      m_outputWidth(0),
      m_outputHeight(0) {}

inline SwSurfaceWidget::SwD3D11VideoSurfacePresenter::~SwD3D11VideoSurfacePresenter() {
    detachHostWindow();
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::setPresentationMode(PresentationMode mode) {
    HWND host = nullptr;
    bool shouldPost = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mode = mode;
        shouldPost = schedulePresentLocked_();
        host = m_hostHwnd;
    }
    if (shouldPost && host) {
        PostMessageW(host, SwD3D11ChildSurfaceHost::presentMessageId(), 0, 0);
    }
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::setBackgroundColor(const SwColor& color) {
    HWND host = nullptr;
    bool shouldPost = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backgroundColor = color;
        shouldPost = schedulePresentLocked_();
        host = m_hostHwnd;
    }
    if (shouldPost && host) {
        PostMessageW(host, SwD3D11ChildSurfaceHost::presentMessageId(), 0, 0);
    }
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::attachHostWindow(HWND hwnd, int width, int height) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hostHwnd == hwnd && m_surfaceWidth == width && m_surfaceHeight == height) {
            return;
        }
        m_hostHwnd = hwnd;
        m_surfaceWidth = std::max(0, width);
        m_surfaceHeight = std::max(0, height);
        m_stats.surfaceWidth = m_surfaceWidth;
        m_stats.surfaceHeight = m_surfaceHeight;
        m_presentQueued = false;
        m_swapChainDirty = true;
    }
    if (!hwnd) {
        releaseDeviceResources_();
        return;
    }
    requestBlankPresent();
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::onHostResize(int width, int height) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_surfaceWidth = std::max(0, width);
        m_surfaceHeight = std::max(0, height);
        m_stats.surfaceWidth = m_surfaceWidth;
        m_stats.surfaceHeight = m_surfaceHeight;
        m_swapChainDirty = true;
    }
    requestBlankPresent();
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::detachHostWindow() {
    attachHostWindow(nullptr, 0, 0);
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::clearQueuedFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_latestFrame = SwVideoFrame();
    m_lastQueuedSequence = 0;
}

inline SwSurfaceWidget::SwD3D11VideoSurfacePresenter::SubmitResult
SwSurfaceWidget::SwD3D11VideoSurfacePresenter::submitFrame(const SwVideoFrame& frame) {
    if (!frame.isNativeD3D11()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_stats.framesRejected;
        return SubmitResult::UnsupportedFrame;
    }

    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    if (!interop.ensure() || !interop.device() || !interop.videoDevice() || !interop.videoContext()) {
        return SubmitResult::SurfaceUnavailable;
    }
    if (!interopDeviceIsHardware_()) {
        return SubmitResult::SurfaceUnavailable;
    }
    if (frame.d3d11Device() && frame.d3d11Device() != interop.device()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_stats.framesRejected;
        return SubmitResult::DeviceMismatch;
    }

    HWND host = nullptr;
    bool shouldPost = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestFrame = frame;
        ++m_stats.framesSubmitted;
        m_stats.frameWidth = frame.width();
        m_stats.frameHeight = frame.height();
        m_stats.lastFrameTimestamp = frame.timestamp();
        ++m_lastQueuedSequence;
        host = m_hostHwnd;
        shouldPost = schedulePresentLocked_();
    }
    if (shouldPost && host) {
        PostMessageW(host, SwD3D11ChildSurfaceHost::presentMessageId(), 0, 0);
    }
    return SubmitResult::Queued;
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::requestBlankPresent() {
    HWND host = nullptr;
    bool shouldPost = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        shouldPost = schedulePresentLocked_();
        host = m_hostHwnd;
    }
    if (shouldPost && host) {
        PostMessageW(host, SwD3D11ChildSurfaceHost::presentMessageId(), 0, 0);
    }
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::handlePresentMessage() {
    SwVideoFrame frame;
    SwColor backgroundColor{};
    PresentationMode mode = PresentationMode::Fit;
    std::uint64_t targetSequence = 0;
    HWND host = nullptr;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_presentQueued = false;
        host = m_hostHwnd;
        surfaceWidth = m_surfaceWidth;
        surfaceHeight = m_surfaceHeight;
        frame = m_latestFrame;
        backgroundColor = m_backgroundColor;
        mode = m_mode;
        targetSequence = m_lastQueuedSequence;
    }

    bool ok = false;
    bool dropped = false;
    if (host && surfaceWidth > 0 && surfaceHeight > 0) {
        ok = ensureSwapChain_(host, surfaceWidth, surfaceHeight);
        if (ok) {
            HRESULT hr = frame.isValid() ? presentFrame_(frame, mode, backgroundColor)
                                         : clearAndPresent_(backgroundColor);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
                dropped = true;
            } else {
                ok = SUCCEEDED(hr);
            }
        }
    }

    HWND repostHost = nullptr;
    bool repost = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (dropped) {
            ++m_stats.framesDropped;
        } else if (ok && frame.isValid()) {
            ++m_stats.framesPresented;
            m_lastPresentedSequence = targetSequence;
            m_stats.lastPresentTimeNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        if (m_lastQueuedSequence > targetSequence && !m_presentQueued && m_hostHwnd) {
            schedulePresentLocked_();
            repost = true;
            repostHost = m_hostHwnd;
        }
    }
    if (repost && repostHost) {
        PostMessageW(repostHost, SwD3D11ChildSurfaceHost::presentMessageId(), 0, 0);
    }
}

inline SwSurfaceWidget::PresentStats SwSurfaceWidget::SwD3D11VideoSurfacePresenter::stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::interopDeviceIsHardware_() const {
    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (!interop.device() ||
        FAILED(interop.device()->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))) ||
        !dxgiDevice) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) || !adapter) {
        return false;
    }
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) {
        return false;
    }
    return desc.VendorId != 0x1414u || desc.DeviceId != 0x008Cu;
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::schedulePresentLocked_() {
    if (m_presentQueued) {
        return false;
    }
    m_presentQueued = true;
    return true;
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::ensureFactory_() {
    if (m_factory) {
        return true;
    }
    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (!interop.device() ||
        FAILED(interop.device()->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()))) ||
        !dxgiDevice) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) || !adapter) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()))) || !factory) {
        return false;
    }
    m_factory = factory;
    return true;
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::ensureSwapChain_(HWND hwnd, int width, int height) {
    if (!hwnd || width <= 0 || height <= 0) {
        return false;
    }
    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    if (!interop.ensure() || !interop.device() || !ensureFactory_()) {
        return false;
    }

    const UINT targetWidth = static_cast<UINT>(std::max(1, width));
    const UINT targetHeight = static_cast<UINT>(std::max(1, height));
    if (!m_swapChain) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = targetWidth;
        desc.Height = targetHeight;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        HRESULT hr = m_factory->CreateSwapChainForHwnd(
            interop.device(),
            hwnd,
            &desc,
            nullptr,
            nullptr,
            swapChain.GetAddressOf());
        if (FAILED(hr) || !swapChain) {
            return false;
        }
        m_swapChain = swapChain;
        m_outputWidth = targetWidth;
        m_outputHeight = targetHeight;
        m_swapChainDirty = true;
    } else if (m_swapChainDirty || m_outputWidth != targetWidth || m_outputHeight != targetHeight) {
        m_outputView.Reset();
        m_backBufferRtv.Reset();
        m_backBuffer.Reset();
        HRESULT hr = m_swapChain->ResizeBuffers(0, targetWidth, targetHeight, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            return false;
        }
        m_outputWidth = targetWidth;
        m_outputHeight = targetHeight;
        m_swapChainDirty = true;
    }

    if (!m_backBuffer || !m_backBufferRtv) {
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.GetAddressOf()));
        if (FAILED(hr) || !m_backBuffer) {
            return false;
        }
        hr = interop.device()->CreateRenderTargetView(m_backBuffer.Get(), nullptr, m_backBufferRtv.GetAddressOf());
        if (FAILED(hr) || !m_backBufferRtv) {
            return false;
        }
        if (m_enumerator && !ensureOutputView_()) {
            return false;
        }
        m_swapChainDirty = false;
    }
    return true;
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::ensureProcessorResources_(const SwVideoFrame& frame) {
    if (!frame.d3d11Texture() || !m_backBuffer) {
        return false;
    }
    const UINT inputWidth = static_cast<UINT>(frame.width());
    const UINT inputHeight = static_cast<UINT>(frame.height());
    const DXGI_FORMAT inputFormat = frame.nativeDxgiFormat();
    const bool recreate =
        !m_enumerator ||
        !m_videoProcessor ||
        !m_outputView ||
        m_inputWidth != inputWidth ||
        m_inputHeight != inputHeight ||
        m_inputFormat != inputFormat;
    if (!recreate) {
        return true;
    }

    m_outputView.Reset();
    m_enumerator.Reset();
    m_videoProcessor.Reset();
    m_inputView.Reset();
    m_boundTexture.Reset();
    m_boundSubresourceIndex = 0;

    m_inputWidth = inputWidth;
    m_inputHeight = inputHeight;
    m_inputFormat = inputFormat;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = inputWidth;
    contentDesc.InputHeight = inputHeight;
    contentDesc.OutputWidth = m_outputWidth;
    contentDesc.OutputHeight = m_outputHeight;
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    HRESULT hr = interop.videoDevice()->CreateVideoProcessorEnumerator(
        &contentDesc,
        m_enumerator.GetAddressOf());
    if (FAILED(hr) || !m_enumerator) {
        return false;
    }
    hr = interop.videoDevice()->CreateVideoProcessor(m_enumerator.Get(), 0, m_videoProcessor.GetAddressOf());
    if (FAILED(hr) || !m_videoProcessor) {
        return false;
    }
    return ensureOutputView_();
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::ensureInputView_(const SwVideoFrame& frame) {
    if (!frame.d3d11Texture() || !m_enumerator) {
        return false;
    }
    if (m_inputView &&
        m_boundTexture.Get() == frame.d3d11Texture() &&
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
    return SUCCEEDED(hr) && m_inputView;
}

inline bool SwSurfaceWidget::SwD3D11VideoSurfacePresenter::ensureOutputView_() {
    if (m_outputView) {
        return true;
    }
    if (!m_backBuffer || !m_enumerator) {
        return false;
    }
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc{};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    HRESULT hr = SwD3D11VideoInterop::instance().videoDevice()->CreateVideoProcessorOutputView(
        m_backBuffer.Get(),
        m_enumerator.Get(),
        &outputDesc,
        m_outputView.GetAddressOf());
    return SUCCEEDED(hr) && m_outputView;
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::computeRects_(PresentationMode mode,
                                                                         int inputWidth,
                                                                         int inputHeight,
                                                                         int outputWidth,
                                                                         int outputHeight,
                                                                         RECT& srcRect,
                                                                         RECT& dstRect) {
    srcRect = RECT{0, 0, inputWidth, inputHeight};
    dstRect = RECT{0, 0, outputWidth, outputHeight};
    if (inputWidth <= 0 || inputHeight <= 0 || outputWidth <= 0 || outputHeight <= 0) {
        return;
    }
    const double scaleX = static_cast<double>(outputWidth) / static_cast<double>(inputWidth);
    const double scaleY = static_cast<double>(outputHeight) / static_cast<double>(inputHeight);
    if (mode == PresentationMode::Stretch) {
        return;
    }
    if (mode == PresentationMode::Fit) {
        const double scale = std::min(scaleX, scaleY);
        const int dstWidth = std::max(1, static_cast<int>(static_cast<double>(inputWidth) * scale + 0.5));
        const int dstHeight = std::max(1, static_cast<int>(static_cast<double>(inputHeight) * scale + 0.5));
        dstRect.left = (outputWidth - dstWidth) / 2;
        dstRect.top = (outputHeight - dstHeight) / 2;
        dstRect.right = dstRect.left + dstWidth;
        dstRect.bottom = dstRect.top + dstHeight;
        return;
    }
    const double scale = std::max(scaleX, scaleY);
    const int srcWidth = std::max(1, static_cast<int>(static_cast<double>(outputWidth) / scale + 0.5));
    const int srcHeight = std::max(1, static_cast<int>(static_cast<double>(outputHeight) / scale + 0.5));
    srcRect.left = std::max(0, (inputWidth - srcWidth) / 2);
    srcRect.top = std::max(0, (inputHeight - srcHeight) / 2);
    srcRect.right = std::min<int>(inputWidth, static_cast<int>(srcRect.left) + srcWidth);
    srcRect.bottom = std::min<int>(inputHeight, static_cast<int>(srcRect.top) + srcHeight);
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::clearRenderTarget_(const SwColor& color) {
    if (!m_backBufferRtv) {
        return;
    }
    float clearColor[4] = {
        static_cast<float>(std::max(0, std::min(255, color.r))) / 255.0f,
        static_cast<float>(std::max(0, std::min(255, color.g))) / 255.0f,
        static_cast<float>(std::max(0, std::min(255, color.b))) / 255.0f,
        1.0f
    };
    SwD3D11VideoInterop::instance().context()->ClearRenderTargetView(m_backBufferRtv.Get(), clearColor);
}

inline HRESULT SwSurfaceWidget::SwD3D11VideoSurfacePresenter::clearAndPresent_(const SwColor& color) {
    if (!m_swapChain || !m_backBufferRtv) {
        return E_FAIL;
    }
    clearRenderTarget_(color);
    SwD3D11VideoInterop::instance().flush();
    return m_swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
}

inline HRESULT SwSurfaceWidget::SwD3D11VideoSurfacePresenter::presentFrame_(const SwVideoFrame& frame,
                                                                            PresentationMode mode,
                                                                            const SwColor& backgroundColor) {
    if (!m_swapChain || !m_backBuffer || !m_backBufferRtv) {
        return E_FAIL;
    }
    clearRenderTarget_(backgroundColor);

    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    if (!ensureProcessorResources_(frame) || !ensureInputView_(frame)) {
        return E_FAIL;
    }

    RECT srcRect{};
    RECT dstRect{};
    computeRects_(mode, frame.width(), frame.height(), static_cast<int>(m_outputWidth), static_cast<int>(m_outputHeight), srcRect, dstRect);

    const bool fullCopy =
        frame.nativeDxgiFormat() == DXGI_FORMAT_B8G8R8A8_UNORM &&
        srcRect.left == 0 &&
        srcRect.top == 0 &&
        srcRect.right == frame.width() &&
        srcRect.bottom == frame.height() &&
        dstRect.left == 0 &&
        dstRect.top == 0 &&
        dstRect.right == static_cast<LONG>(m_outputWidth) &&
        dstRect.bottom == static_cast<LONG>(m_outputHeight);

    if (fullCopy) {
        interop.context()->CopySubresourceRegion(
            m_backBuffer.Get(),
            0,
            0,
            0,
            0,
            frame.d3d11Texture(),
            frame.nativeSubresourceIndex(),
            nullptr);
        interop.flush();
        return m_swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    }

    interop.videoContext()->VideoProcessorSetOutputTargetRect(m_videoProcessor.Get(), TRUE, &dstRect);
    interop.videoContext()->VideoProcessorSetStreamSourceRect(m_videoProcessor.Get(), 0, TRUE, &srcRect);
    interop.videoContext()->VideoProcessorSetStreamDestRect(m_videoProcessor.Get(), 0, TRUE, &dstRect);
    interop.videoContext()->VideoProcessorSetStreamFrameFormat(
        m_videoProcessor.Get(),
        0,
        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = m_inputView.Get();
    HRESULT hr = interop.videoContext()->VideoProcessorBlt(
        m_videoProcessor.Get(),
        m_outputView.Get(),
        0,
        1,
        &stream);
    if (FAILED(hr)) {
        return hr;
    }
    interop.flush();
    return m_swapChain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
}

inline void SwSurfaceWidget::SwD3D11VideoSurfacePresenter::releaseDeviceResources_() {
    m_outputView.Reset();
    m_inputView.Reset();
    m_videoProcessor.Reset();
    m_enumerator.Reset();
    m_backBufferRtv.Reset();
    m_backBuffer.Reset();
    m_swapChain.Reset();
    m_factory.Reset();
    m_boundTexture.Reset();
    m_boundSubresourceIndex = 0;
    m_inputWidth = 0;
    m_inputHeight = 0;
    m_inputFormat = DXGI_FORMAT_UNKNOWN;
    m_outputWidth = 0;
    m_outputHeight = 0;
}

#endif
