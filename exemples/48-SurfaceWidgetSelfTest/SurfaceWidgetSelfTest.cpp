#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwSurfaceWidget.h"
#include "SwTimer.h"

#if defined(_WIN32)
#include "platform/win/SwD3D11VideoInterop.h"
#include <d3d11.h>
#include <wrl/client.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>

namespace {

#if defined(_WIN32)
static bool createColorFrame_(int width,
                              int height,
                              std::uint8_t red,
                              std::uint8_t green,
                              std::uint8_t blue,
                              ID3D11Device* device,
                              SwVideoFrame& frameOut) {
    if (!device || width <= 0 || height <= 0) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    const std::uint32_t pixel =
        (0xFFu << 24) |
        (static_cast<std::uint32_t>(red) << 16) |
        (static_cast<std::uint32_t>(green) << 8) |
        static_cast<std::uint32_t>(blue);
    std::unique_ptr<std::uint32_t[]> pixels(
        new std::uint32_t[static_cast<std::size_t>(width) * static_cast<std::size_t>(height)]);
    for (int i = 0; i < width * height; ++i) {
        pixels[i] = pixel;
    }

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = pixels.get();
    initData.SysMemPitch = width * static_cast<UINT>(sizeof(std::uint32_t));

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, texture.GetAddressOf());
    if (FAILED(hr) || !texture) {
        return false;
    }

    const SwVideoFormatInfo info = SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, width, height);
    frameOut = SwVideoFrame::wrapNativeD3D11(
        info,
        device,
        texture.Get(),
        DXGI_FORMAT_B8G8R8A8_UNORM,
        0);
    frameOut.setTimestamp(static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()));
    return frameOut.isValid();
}

static bool createInteropColorFrame_(int width,
                                     int height,
                                     std::uint8_t red,
                                     std::uint8_t green,
                                     std::uint8_t blue,
                                     SwVideoFrame& frameOut) {
    SwD3D11VideoInterop& interop = SwD3D11VideoInterop::instance();
    if (!interop.ensure() || !interop.device()) {
        return false;
    }
    return createColorFrame_(width, height, red, green, blue, interop.device(), frameOut);
}

static bool createMismatchedDeviceFrame_(int width, int height, SwVideoFrame& frameOut) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr,
                                   creationFlags,
                                   featureLevels,
                                   ARRAYSIZE(featureLevels),
                                   D3D11_SDK_VERSION,
                                   device.GetAddressOf(),
                                   &featureLevel,
                                   context.GetAddressOf());
    if (FAILED(hr) || !device || !context) {
        hr = D3D11CreateDevice(nullptr,
                               D3D_DRIVER_TYPE_WARP,
                               nullptr,
                               creationFlags,
                               featureLevels,
                               ARRAYSIZE(featureLevels),
                               D3D11_SDK_VERSION,
                               device.GetAddressOf(),
                               &featureLevel,
                               context.GetAddressOf());
    }
    if (FAILED(hr) || !device) {
        return false;
    }

    return createColorFrame_(width, height, 255, 180, 32, device.Get(), frameOut);
}
#endif

} // namespace

int main() {
    SwGuiApplication app;

    SwMainWindow window("SwSurfaceWidget SelfTest", 1080, 720);
    window.setStyleSheet(
        "SwWidget { background-color: rgb(27, 27, 29); color: rgb(220, 220, 220); }"
        "SwFrame { background-color: rgb(37, 37, 38); border-color: rgb(62, 62, 66); border-width: 1px; border-radius: 14px; }"
        "SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(220, 220, 220); }");

    auto* root = window.centralWidget();
    auto* rootLayout = new SwHorizontalLayout(root);
    rootLayout->setMargin(12);
    rootLayout->setSpacing(12);
    root->setLayout(rootLayout);

    auto* leftPane = new SwFrame(root);
    auto* rightPane = new SwFrame(root);
    leftPane->setMinimumSize(320, 0);
    rightPane->setMinimumSize(320, 0);

    auto* leftLayout = new SwVerticalLayout(leftPane);
    leftLayout->setMargin(12);
    leftLayout->setSpacing(8);
    leftPane->setLayout(leftLayout);

    auto* rightLayout = new SwVerticalLayout(rightPane);
    rightLayout->setMargin(12);
    rightLayout->setSpacing(8);
    rightPane->setLayout(rightLayout);

    auto* title = new SwLabel("SwSurfaceWidget synthetic GPU self-test", leftPane);
    auto* status = new SwLabel("Awaiting first frame", leftPane);
    auto* stats = new SwLabel("No stats yet", leftPane);
    auto* negativeChecks = new SwLabel("Negative checks pending", leftPane);
    leftLayout->addWidget(title, 0, 24);
    leftLayout->addWidget(status, 0, 20);
    leftLayout->addWidget(stats, 0, 20);
    leftLayout->addWidget(negativeChecks, 0, 20);

    auto* surface = new SwSurfaceWidget(leftPane);
    surface->setMinimumSize(0, 420);
    surface->setBackgroundColor({8, 8, 8});
    surface->setPresentationMode(SwSurfaceWidget::PresentationMode::Fit);
    leftLayout->addWidget(surface, 1);

    auto* instructions = new SwLabel(
        "Sequence automatique: resize, hide/show, puis reparent de la surface entre les deux panneaux.",
        rightPane);
    auto* hostInfo = new SwLabel("Le panneau droit recoit la surface pendant la phase de reparent.", rightPane);
    rightLayout->addWidget(instructions, 0, 20);
    rightLayout->addWidget(hostInfo, 0, 20);
    rightLayout->addStretch(1);

    rootLayout->addWidget(leftPane, 1);
    rootLayout->addWidget(rightPane, 1);

    SwTimer frameTimer(&window);
    frameTimer.setInterval(33);
    SwObject::connect(&frameTimer, &SwTimer::timeout, &window, [surface, status, stats]() {
#if defined(_WIN32)
        static int tick = 0;
        ++tick;
        const double phase = static_cast<double>(tick) * 0.08;
        const std::uint8_t red = static_cast<std::uint8_t>(127 + std::sin(phase) * 110.0);
        const std::uint8_t green = static_cast<std::uint8_t>(127 + std::sin(phase + 2.1) * 110.0);
        const std::uint8_t blue = static_cast<std::uint8_t>(127 + std::sin(phase + 4.2) * 110.0);
        SwVideoFrame frame;
        if (createInteropColorFrame_(1280, 720, red, green, blue, frame) && surface->submitNativeFrame(frame)) {
            status->setText("Streaming | state " + surface->surfaceStateText());
        } else {
            status->setText("Submit failed | state " + surface->surfaceStateText());
        }
        const SwSurfaceWidget::PresentStats presentStats = surface->lastPresentStats();
        stats->setText(
            "submitted " + SwString::number(static_cast<unsigned long long>(presentStats.framesSubmitted)) +
            " | presented " + SwString::number(static_cast<unsigned long long>(presentStats.framesPresented)) +
            " | dropped " + SwString::number(static_cast<unsigned long long>(presentStats.framesDropped)) +
            " | rejected " + SwString::number(static_cast<unsigned long long>(presentStats.framesRejected)));
#else
        status->setText("Unsupported platform");
        stats->setText("SwSurfaceWidget is Windows/D3D11 only in this self-test.");
#endif
    });
    frameTimer.start();

    SwTimer negativeTimer(&window);
    negativeTimer.setSingleShot(true);
    negativeTimer.setInterval(900);
    SwObject::connect(&negativeTimer, &SwTimer::timeout, &window, [surface, negativeChecks]() {
#if defined(_WIN32)
        bool cpuRejected = false;
        SwVideoFrame cpuFrame = SwVideoFrame::allocate(320, 180, SwVideoPixelFormat::BGRA32);
        if (cpuFrame.isValid()) {
            cpuFrame.setTimestamp(static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()));
            cpuRejected = !surface->submitNativeFrame(cpuFrame) &&
                          surface->surfaceState() == SwSurfaceWidget::SurfaceState::UnsupportedFramePath;
        }

        bool mismatchRejected = false;
        SwVideoFrame mismatchedFrame;
        if (createMismatchedDeviceFrame_(320, 180, mismatchedFrame)) {
            mismatchRejected = !surface->submitNativeFrame(mismatchedFrame) &&
                               surface->surfaceState() == SwSurfaceWidget::SurfaceState::DeviceMismatch;
        }

        negativeChecks->setText(
            "negative cpu=" + SwString(cpuRejected ? "rejected" : "failed") +
            " | mismatch=" + SwString(mismatchRejected ? "rejected" : "failed"));
#else
        negativeChecks->setText("Negative checks unavailable on this platform.");
#endif
    });
    negativeTimer.start();

    SwTimer scenarioTimer(&window);
    scenarioTimer.setInterval(2200);
    SwObject::connect(&scenarioTimer, &SwTimer::timeout, &window, [surface, leftPane, rightPane, leftLayout, rightLayout]() {
        static int step = 0;
        ++step;
        switch (step % 5) {
        case 0:
            surface->setParent(rightPane);
            rightLayout->addWidget(surface, 1);
            surface->show();
            break;
        case 1:
            surface->hide();
            break;
        case 2:
            surface->show();
            surface->resize(std::max(240, surface->width() - 140), std::max(180, surface->height() - 100));
            break;
        case 3:
            surface->resize(surface->width() + 220, surface->height() + 140);
            break;
        default:
            surface->setParent(leftPane);
            leftLayout->addWidget(surface, 1);
            surface->show();
            break;
        }
    });
    scenarioTimer.start();

    window.show();
    return app.exec();
}
