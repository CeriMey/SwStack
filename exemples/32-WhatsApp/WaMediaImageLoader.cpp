#include "WaMediaImageLoader.h"

#include <algorithm>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <combaseapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {
struct ScopedCoInit {
    HRESULT hr{E_FAIL};
    bool shouldUninit{false};

    ScopedCoInit() {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (hr == RPC_E_CHANGED_MODE) {
            hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        }
        shouldUninit = SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE;
    }

    ~ScopedCoInit() {
        if (shouldUninit) {
            CoUninitialize();
        }
    }
};

static Microsoft::WRL::ComPtr<IWICImagingFactory> createWicFactory_() {
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    IWICImagingFactory* raw = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IWICImagingFactory),
                                  reinterpret_cast<void**>(&raw));
    if (FAILED(hr) || !raw) {
        hr = CoCreateInstance(CLSID_WICImagingFactory,
                              nullptr,
                              CLSCTX_INPROC_SERVER,
                              __uuidof(IWICImagingFactory),
                              reinterpret_cast<void**>(&raw));
    }
    if (SUCCEEDED(hr) && raw) {
        factory.Attach(raw);
    }
    return factory;
}

static std::wstring toWide_(const SwString& s) {
    const std::wstring w = s.toStdWString();
    return w;
}

static SwImage loadWicScaled_(const SwString& absPath, int targetW, int targetH) {
    ScopedCoInit co;
    if (FAILED(co.hr)) {
        return SwImage();
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory = createWicFactory_();
    if (!factory) {
        return SwImage();
    }

    const std::wstring wide = toWide_(absPath);

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = factory->CreateDecoderFromFilename(wide.c_str(),
                                                    nullptr,
                                                    GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad,
                                                    decoder.GetAddressOf());
    if (FAILED(hr) || !decoder) {
        return SwImage();
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr) || !frame) {
        return SwImage();
    }

    UINT srcW = 0;
    UINT srcH = 0;
    frame->GetSize(&srcW, &srcH);
    if (srcW == 0 || srcH == 0) {
        return SwImage();
    }

    UINT outW = srcW;
    UINT outH = srcH;
    if (targetW > 0 && targetH > 0) {
        outW = static_cast<UINT>(std::max(1, targetW));
        outH = static_cast<UINT>(std::max(1, targetH));
    }

    Microsoft::WRL::ComPtr<IWICBitmapSource> source = frame;
    if (outW != srcW || outH != srcH) {
        Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
        if (FAILED(hr) || !scaler) {
            return SwImage();
        }
        hr = scaler->Initialize(frame.Get(), outW, outH, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            return SwImage();
        }
        source = scaler;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr) || !converter) {
        return SwImage();
    }
    hr = converter->Initialize(source.Get(),
                               GUID_WICPixelFormat32bppBGRA,
                               WICBitmapDitherTypeNone,
                               nullptr,
                               0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return SwImage();
    }

    UINT w = 0;
    UINT h = 0;
    converter->GetSize(&w, &h);
    if (w == 0 || h == 0) {
        return SwImage();
    }

    SwImage img(static_cast<int>(w), static_cast<int>(h), SwImage::Format_ARGB32);
    if (img.isNull()) {
        return SwImage();
    }

    const UINT stride = static_cast<UINT>(img.bytesPerLine());
    const UINT size = stride * h;
    hr = converter->CopyPixels(nullptr, stride, size, reinterpret_cast<BYTE*>(img.bits()));
    if (FAILED(hr)) {
        return SwImage();
    }

    return img;
}
} // namespace
#endif

SwImage WaMediaImageLoader::loadThumbnail(const SwString& absoluteFilePath, int targetW, int targetH) {
    if (absoluteFilePath.isEmpty() || targetW <= 0 || targetH <= 0) {
        return SwImage();
    }

#if defined(_WIN32)
    SwImage img = loadWicScaled_(absoluteFilePath, targetW, targetH);
    if (!img.isNull()) {
        return img;
    }
#endif

    // Fallback (BMP only).
    SwImage bmp;
    if (bmp.load(absoluteFilePath)) {
        return bmp;
    }
    return SwImage();
}

SwImage WaMediaImageLoader::loadImageFit(const SwString& absoluteFilePath, int maxW, int maxH) {
    if (absoluteFilePath.isEmpty()) {
        return SwImage();
    }

#if defined(_WIN32)
    // Decode once to read dimensions.
    SwImage decoded = loadWicScaled_(absoluteFilePath, 0, 0);
    if (!decoded.isNull()) {
        if (maxW <= 0 || maxH <= 0) {
            return decoded;
        }

        const int srcW = decoded.width();
        const int srcH = decoded.height();
        if (srcW <= 0 || srcH <= 0) {
            return decoded;
        }

        const float sx = static_cast<float>(maxW) / static_cast<float>(srcW);
        const float sy = static_cast<float>(maxH) / static_cast<float>(srcH);
        const float s = std::min(1.0f, std::min(sx, sy));

        const int outW = std::max(1, static_cast<int>(srcW * s));
        const int outH = std::max(1, static_cast<int>(srcH * s));
        if (outW == srcW && outH == srcH) {
            return decoded;
        }

        // Re-decode directly at target size to keep memory low.
        SwImage scaled = loadWicScaled_(absoluteFilePath, outW, outH);
        return scaled.isNull() ? decoded : scaled;
    }
#endif

    SwImage bmp;
    if (bmp.load(absoluteFilePath)) {
        return bmp;
    }
    return SwImage();
}

