#include "SwCoreApplication.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "SwString.h"

#include "media/SwMediaFoundationVideoSource.h"
#include "media/SwVideoDecoder.h"
#include "media/SwVideoSource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <gdiplus.h>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {

class GdiPlusGuard {
public:
    GdiPlusGuard() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&m_token, &input, nullptr) != Gdiplus::Ok) {
            m_token = 0;
        }
    }
    ~GdiPlusGuard() {
        if (m_token != 0) {
            Gdiplus::GdiplusShutdown(m_token);
            m_token = 0;
        }
    }
    bool ok() const { return m_token != 0; }

private:
    ULONG_PTR m_token{0};
};

bool findEncoderClsid(const wchar_t* mimeType, CLSID& clsid) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) {
        return false;
    }
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mimeType) == 0) {
            clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

bool encodeFrameToJpeg(const SwVideoFrame& frame, std::string& outJpeg, ULONG quality = 80) {
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        return false;
    }
    const uint8_t* data = frame.planeData(0);
    if (!data) {
        return false;
    }
    const int stride = frame.planeStride(0);
    const int width = frame.width();
    const int height = frame.height();

    Gdiplus::Bitmap bitmap(width, height, stride, PixelFormat32bppARGB, const_cast<BYTE*>(data));

    CLSID encoderClsid{};
    if (!findEncoderClsid(L"image/jpeg", encoderClsid)) {
        return false;
    }

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) {
        return false;
    }

    Gdiplus::EncoderParameters params{};
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;

    bool ok = false;
    if (bitmap.Save(stream, &encoderClsid, &params) == Gdiplus::Ok) {
        HGLOBAL hMem = nullptr;
        if (GetHGlobalFromStream(stream, &hMem) == S_OK) {
            SIZE_T len = GlobalSize(hMem);
            if (len > 0) {
                void* mem = GlobalLock(hMem);
                if (mem) {
                    outJpeg.assign(static_cast<char*>(mem), static_cast<char*>(mem) + len);
                    GlobalUnlock(hMem);
                    ok = true;
                }
            }
        }
    }
    stream->Release();
    return ok;
}

} // namespace

int main() {
#if !defined(_WIN32)
    std::cerr << "MjpegServer is Windows-only (Media Foundation + GDI+)." << std::endl;
    return 1;
#else
    GdiPlusGuard gdip;
    if (!gdip.ok()) {
        std::cerr << "[MjpegServer] Failed to initialize GDI+." << std::endl;
        return 1;
    }

    SwCoreApplication app;

    auto source = std::make_shared<SwMediaFoundationVideoSource>();
    if (!source->initialize()) {
        std::cerr << "[MjpegServer] Failed to initialize webcam source." << std::endl;
        return 1;
    }

    std::mutex jpegMutex;
    std::string latestJpeg;

    auto decoder = std::make_shared<SwPassthroughVideoDecoder>();
    decoder->setFrameCallback([&](const SwVideoFrame& frame) {
        std::string jpeg;
        if (encodeFrameToJpeg(frame, jpeg)) {
            std::lock_guard<std::mutex> lock(jpegMutex);
            latestJpeg = std::move(jpeg);
        }
    });

    auto pipeline = std::make_shared<SwVideoPipeline>();
    pipeline->setDecoder(decoder);
    pipeline->setSource(source);
    pipeline->start();

    SwTcpServer server;
    const uint16_t port = 8080;
    if (!server.listen(port)) {
        std::cerr << "[MjpegServer] Failed to listen on port " << port << std::endl;
        return 1;
    }
    std::cout << "[MjpegServer] Serving MJPEG on http://localhost:" << port << "/ (multipart/x-mixed-replace)\n";

    SwObject::connect(&server, SIGNAL(newConnection), [&]() {
        SwTcpSocket* client = server.nextPendingConnection();
        if (!client) {
            return;
        }

        // Minimal HTTP handshake; ignore request content for simplicity.
        client->read();
        const std::string header =
            "HTTP/1.1 200 OK\r\n"
            "Cache-Control: no-cache\r\n"
            "Pragma: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "\r\n";
        client->write(SwString(header));

        // Per-client timer to push frames.
        auto* pump = new SwTimer(50, client);
        SwObject::connect(pump, &SwTimer::timeout, [client, &jpegMutex, &latestJpeg]() {
            std::string jpegCopy;
            {
                std::lock_guard<std::mutex> lock(jpegMutex);
                jpegCopy = latestJpeg;
            }
            if (jpegCopy.empty()) {
                return;
            }

            std::ostringstream oss;
            oss << "--frame\r\n"
                << "Content-Type: image/jpeg\r\n"
                << "Content-Length: " << jpegCopy.size() << "\r\n\r\n";
            SwString chunk(oss.str());
            chunk += SwString(jpegCopy);
            chunk += "\r\n";
            client->write(chunk);
        });
        pump->start(50);

        SwObject::connect(client, SIGNAL(disconnected), [client, pump]() {
            pump->stop();
            delete pump;
            delete client;
        });
    });

    source->start();
    int code = app.exec();
    source->stop();
    return code;
#endif
}
