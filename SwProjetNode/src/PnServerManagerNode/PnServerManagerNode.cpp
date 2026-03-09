#include "PnServerManagerNode.h"

#include "PnUtils.h"

#include "SwDebug.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "media/SwMediaFoundationVideoSource.h"
#include "media/SwVideoPacket.h"
#include <cstring>

#if defined(_WIN32)
#include "media/SwHttpMjpegSource.h"
#include "platform/win/SwWindows.h"
#include <gdiplus.h>
#endif

namespace {

static const SwString kStreamBoundary = "swframeboundary";

#if defined(_WIN32)
class GdiPlusGuard_ {
public:
    GdiPlusGuard_() {
        Gdiplus::GdiplusStartupInput startupInput;
        m_ok = (Gdiplus::GdiplusStartup(&m_token, &startupInput, nullptr) == Gdiplus::Ok);
    }

    ~GdiPlusGuard_() {
        if (m_ok) {
            Gdiplus::GdiplusShutdown(m_token);
            m_ok = false;
            m_token = 0;
        }
    }

    bool ok() const {
        return m_ok;
    }

private:
    ULONG_PTR m_token{0};
    bool m_ok{false};
};

static bool ensureGdiPlus_() {
    static GdiPlusGuard_ guard;
    return guard.ok();
}

static bool findJpegEncoderClsid_(CLSID& outClsid) {
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok) {
        return false;
    }
    if (encoderCount == 0 || encoderBytes == 0) {
        return false;
    }

    SwByteArray buffer(static_cast<size_t>(encoderBytes), '\0');
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (!codecs) {
        return false;
    }

    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, codecs) != Gdiplus::Ok) {
        return false;
    }

    for (UINT i = 0; i < encoderCount; ++i) {
        if (codecs[i].MimeType && wcscmp(codecs[i].MimeType, L"image/jpeg") == 0) {
            outClsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}
#endif

} // namespace

PnServerManagerNode::PnServerManagerNode(const SwString& sysName,
                                         const SwString& nameSpace,
                                         const SwString& objectName,
                                         SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent),
      tcpServer_(new SwTcpServer(this)) {
    ipcRegisterConfig(int, httpPort_, "http_port", 8090, [this](const int&) { restartHttpServer_(); });
    ipcRegisterConfig(int, deviceIndex_, "device_index", 0, [this](const int&) { restartVideoSource_(); });
    ipcRegisterConfig(int, streamFps_, "stream_fps", 20, [this](const int&) { sanitizeConfig_(); });
    ipcRegisterConfig(int, jpegQuality_, "jpeg_quality", 80, [this](const int&) { sanitizeConfig_(); });
    ipcRegisterConfig(SwString, videoUrl_, "video_url", SwString("webcam://0"), [this](const SwString&) { restartVideoSource_(); });

    connect(tcpServer_, SIGNAL(newConnection), this, &PnServerManagerNode::onNewTcpConnection_);

    sanitizeConfig_();
    restartVideoSource_();
    restartHttpServer_();
}

PnServerManagerNode::~PnServerManagerNode() {
    while (!clients_.isEmpty()) {
        StreamClientState* client = clients_[0];
        if (!client) {
            clients_.removeAt(0);
            continue;
        }
        SwTcpSocket* socket = client->socket;
        removeClient_(socket, false);
        if (socket) {
            socket->close();
            delete socket;
        }
    }

    if (tcpServer_) {
        tcpServer_->close();
        httpListening_ = false;
    }

    if (webcamSource_) {
        webcamSource_->stop();
        delete webcamSource_;
        webcamSource_ = nullptr;
    }
#if defined(_WIN32)
    if (mjpegSource_) {
        mjpegSource_->stop();
        delete mjpegSource_;
        mjpegSource_ = nullptr;
    }
#endif
}

int PnServerManagerNode::clampInt_(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void PnServerManagerNode::sanitizeConfig_() {
    httpPort_ = clampInt_(httpPort_, 1, 65535);
    if (deviceIndex_ < 0) deviceIndex_ = 0;
    streamFps_ = clampInt_(streamFps_, 1, 60);
    jpegQuality_ = clampInt_(jpegQuality_, 1, 100);

    const int intervalMs = clampInt_(1000 / streamFps_, 10, 1000);
    for (size_t i = 0; i < clients_.size(); ++i) {
        StreamClientState* client = clients_[i];
        if (!client || !client->pumpTimer || !client->pumpTimer->isActive()) {
            continue;
        }
        client->pumpTimer->stop();
        client->pumpTimer->start(intervalMs);
    }
}

bool PnServerManagerNode::isWebcamUrl_(const SwString& url) {
    const SwString lowered = url.trimmed().toLower();
    return lowered.startsWith("webcam://", Sw::CaseInsensitive) ||
           lowered.startsWith("camera://", Sw::CaseInsensitive);
}

bool PnServerManagerNode::isDynamicSourcePath_(const SwString& path) {
    const SwString lowered = path.trimmed().toLower();
    return lowered.startsWith("/webcam://", Sw::CaseInsensitive) ||
           lowered.startsWith("/camera://", Sw::CaseInsensitive) ||
           lowered.startsWith("/http://", Sw::CaseInsensitive);
}

int PnServerManagerNode::parseWebcamIndexFromUrl_(const SwString& url, int fallbackIndex) {
    SwString lowered = url.trimmed().toLower();
    int schemeLen = 0;
    if (lowered.startsWith("webcam://", Sw::CaseInsensitive)) {
        schemeLen = 9;
    } else if (lowered.startsWith("camera://", Sw::CaseInsensitive)) {
        schemeLen = 9;
    } else {
        return fallbackIndex;
    }

    SwString tail = lowered.mid(schemeLen).trimmed();
    if (tail.isEmpty()) {
        return fallbackIndex;
    }

    const int slashPos = tail.indexOf('/');
    if (slashPos >= 0) {
        tail = tail.left(slashPos).trimmed();
    }

    bool ok = false;
    const int value = tail.toInt(&ok);
    if (!ok || value < 0) {
        return fallbackIndex;
    }
    return value;
}

void PnServerManagerNode::restartVideoSource_() {
    sanitizeConfig_();

    if (webcamSource_) {
        webcamSource_->stop();
        delete webcamSource_;
        webcamSource_ = nullptr;
    }
#if defined(_WIN32)
    if (mjpegSource_) {
        mjpegSource_->stop();
        delete mjpegSource_;
        mjpegSource_ = nullptr;
    }
#endif

    {
        SwMutexLocker locker(&frameMutex_);
        latestSeq_ = 0;
        latestWidth_ = 0;
        latestHeight_ = 0;
        latestJpeg_.clear();
    }

    cameraReady_ = false;

    SwString selectedUrl = videoUrl_.trimmed();
    if (selectedUrl.isEmpty()) {
        selectedUrl = "webcam://";
        selectedUrl += SwString::number(deviceIndex_);
    }

    if (isWebcamUrl_(selectedUrl)) {
        const int webcamIndex = parseWebcamIndexFromUrl_(selectedUrl, deviceIndex_);
        startWebcamSource_(webcamIndex);
        return;
    }

    if (selectedUrl.toLower().startsWith("http://", Sw::CaseInsensitive)) {
        startHttpMjpegSource_(selectedUrl);
        return;
    }

    swWarning() << "[PnServerManagerNode] unsupported video_url=" << selectedUrl
                << " expected webcam://<index> or http://<host>/stream";
}

void PnServerManagerNode::startWebcamSource_(int webcamIndex) {
    webcamIndex = clampInt_(webcamIndex, 0, 255);
    deviceIndex_ = webcamIndex;

    webcamSource_ = new SwMediaFoundationVideoSource(static_cast<unsigned int>(webcamIndex));
    webcamSource_->setPacketCallback([this](const SwVideoPacket& packet) {
        onVideoPacket_(packet);
    });

    if (!webcamSource_->initialize()) {
        swWarning() << "[PnServerManagerNode] webcam init failed for device_index=" << webcamIndex;
        return;
    }

    webcamSource_->start();
    cameraReady_ = true;
    swDebug() << "[PnServerManagerNode]" << PnUtils::banner(objectName())
              << "webcam started device_index=" << webcamIndex;
}

void PnServerManagerNode::startHttpMjpegSource_(const SwString& url) {
#if defined(_WIN32)
    mjpegSource_ = new SwHttpMjpegSource(url, nullptr);
    mjpegSource_->setPacketCallback([this](const SwVideoPacket& packet) {
        onVideoPacket_(packet);
    });
    mjpegSource_->start();
    cameraReady_ = true;
    swDebug() << "[PnServerManagerNode]" << PnUtils::banner(objectName())
              << "video source url=" << url;
#else
    (void)url;
    swWarning() << "[PnServerManagerNode] video_url http:// is supported on Windows only";
#endif
}

void PnServerManagerNode::restartHttpServer_() {
    sanitizeConfig_();

    while (!clients_.isEmpty()) {
        StreamClientState* client = clients_[0];
        if (!client) {
            clients_.removeAt(0);
            continue;
        }
        SwTcpSocket* socket = client->socket;
        closeClient_(socket);
    }

    if (tcpServer_) {
        tcpServer_->close();
    }
    httpListening_ = false;

    if (!tcpServer_) {
        return;
    }

    httpListening_ = tcpServer_->listen(static_cast<uint16_t>(httpPort_));
    if (!httpListening_) {
        swWarning() << "[PnServerManagerNode] failed to listen on port " << httpPort_;
        return;
    }

    swDebug() << "[PnServerManagerNode]" << PnUtils::banner(objectName())
              << "video stream ready on:"
              << "http://127.0.0.1:" << httpPort_ << "/frame.bmp";
}

void PnServerManagerNode::onVideoPacket_(const SwVideoPacket& packet) {
    if (packet.codec() != SwVideoPacket::Codec::RawBGRA) {
        return;
    }
    if (!packet.carriesRawFrame()) {
        return;
    }

    const SwVideoFormatInfo& info = packet.rawFormat();
    if (info.format != SwVideoPixelFormat::BGRA32 || info.width <= 0 || info.height <= 0) {
        return;
    }

    SwByteArray jpeg;
    if (!encodeBgraToJpeg_(packet.payload(), info.width, info.height, jpeg)) {
        return;
    }

    int seq = 0;
    {
        SwMutexLocker locker(&frameMutex_);
        ++latestSeq_;
        latestWidth_ = info.width;
        latestHeight_ = info.height;
        latestJpeg_ = jpeg;
        seq = latestSeq_;
    }

    if ((seq % 60) == 0) {
        swDebug() << "[PnServerManagerNode]" << PnUtils::banner(objectName())
                  << "camera seq=" << seq
                  << " size=" << info.width << "x" << info.height
                  << " jpeg_bytes=" << static_cast<long long>(jpeg.size());
    }
}

void PnServerManagerNode::onNewTcpConnection_() {
    if (!tcpServer_) {
        return;
    }

    while (SwTcpSocket* socket = tcpServer_->nextPendingConnection()) {
        StreamClientState* client = new StreamClientState();
        client->socket = socket;
        clients_.append(client);

        SwObject::connect(socket, SIGNAL(readyRead), [this, socket]() {
            onClientReadyRead_(socket);
        });

        SwObject::connect(socket, SIGNAL(disconnected), [this, socket]() {
            removeClient_(socket, true);
        });
    }
}

void PnServerManagerNode::onClientReadyRead_(SwTcpSocket* socket) {
    StreamClientState* client = findClient_(socket);
    if (!client || client->requestHandled) {
        return;
    }

    for (;;) {
        const SwString chunk = socket->read();
        if (chunk.isEmpty()) {
            break;
        }
        client->requestBuffer.append(chunk.toStdString());
        if (client->requestBuffer.size() > 16384u) {
            writeTextResponse_(socket, 413, statusReason_(413), "request headers too large");
            return;
        }
    }

    if (client->requestBuffer.indexOf("\r\n\r\n") < 0) {
        return;
    }

    const SwString method = extractRequestMethod_(client->requestBuffer);
    const SwString path = extractRequestPath_(client->requestBuffer);
    client->requestHandled = true;

    if (method.isEmpty() || path.isEmpty()) {
        writeTextResponse_(socket, 400, statusReason_(400), "invalid request");
        return;
    }

    if (method != "GET") {
        writeTextResponse_(socket, 405, statusReason_(405), "only GET is supported");
        return;
    }

    handleHttpRequest_(socket, method, path);
}

void PnServerManagerNode::handleHttpRequest_(SwTcpSocket* socket, const SwString& method, const SwString& path) {
    if (!socket) {
        return;
    }

    (void)method;

    if (isDynamicSourcePath_(path)) {
        // Example: GET /webcam://0 or GET /http://127.0.0.1:8080/stream.mjpg
        videoUrl_ = path.mid(1).trimmed();
        restartVideoSource_();
        SwString body = "video_url updated to ";
        body += videoUrl_;
        body += "\nstream: /frame.bmp";
        writeTextResponse_(socket, 200, statusReason_(200), body);
        return;
    }

    if (path == "/") {
        SwString body = "PnServerManagerNode";
        body += "\nGET /health";
        body += "\nGET /stats";
        body += "\nGET /frame.jpg";
        body += "\nGET /stream.mjpg";
        body += "\nGET /frame.bmp  (alias stream)";
        body += "\nconfig video_url=" + videoUrl_;
        writeTextResponse_(socket, 200, statusReason_(200), body);
        return;
    }

    if (path == "/health") {
        SwString body = cameraReady_ ? "ok" : "starting";
        body += " seq=" + SwString::number(latestSeq_);
        body += " port=" + SwString::number(httpPort_);
        body += " video_url=" + videoUrl_;
        writeTextResponse_(socket, 200, statusReason_(200), body);
        return;
    }

    if (path == "/stats") {
        int seq = 0;
        int width = 0;
        int height = 0;
        long long jpegBytes = 0;
        {
            SwMutexLocker locker(&frameMutex_);
            seq = latestSeq_;
            width = latestWidth_;
            height = latestHeight_;
            jpegBytes = static_cast<long long>(latestJpeg_.size());
        }

        SwString body;
        body += "camera_ready=";
        body += cameraReady_ ? "1" : "0";
        body += " seq=" + SwString::number(seq);
        body += " width=" + SwString::number(width);
        body += " height=" + SwString::number(height);
        body += " jpeg_bytes=" + SwString::number(jpegBytes);
        body += " clients=" + SwString::number(static_cast<long long>(clients_.size()));
        body += " stream_fps=" + SwString::number(streamFps_);
        body += " jpeg_quality=" + SwString::number(jpegQuality_);
        body += " video_url=" + videoUrl_;

        writeTextResponse_(socket, 200, statusReason_(200), body);
        return;
    }

    if (path == "/frame.jpg") {
        SwByteArray jpeg;
        {
            SwMutexLocker locker(&frameMutex_);
            jpeg = latestJpeg_;
        }
        if (jpeg.isEmpty()) {
            writeTextResponse_(socket, 503, statusReason_(503), "no camera frame available");
            return;
        }
        writeBinaryResponse_(socket, 200, statusReason_(200), "image/jpeg", jpeg);
        return;
    }

    if (path == "/stream.mjpg" || path == "/frame.bmp") {
        beginStream_(socket);
        return;
    }

    writeTextResponse_(socket, 404, statusReason_(404), "not found");
}

void PnServerManagerNode::beginStream_(SwTcpSocket* socket) {
    StreamClientState* client = findClient_(socket);
    if (!client || !socket) {
        return;
    }

    SwString header;
    header += "HTTP/1.1 200 OK\r\n";
    header += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    header += "Pragma: no-cache\r\n";
    header += "Connection: close\r\n";
    header += "Content-Type: multipart/x-mixed-replace; boundary=";
    header += kStreamBoundary;
    header += "\r\n\r\n";

    if (!socket->write(header)) {
        closeClient_(socket);
        return;
    }

    const int intervalMs = clampInt_(1000 / streamFps_, 10, 1000);
    if (!client->pumpTimer) {
        client->pumpTimer = new SwTimer(this);
        SwObject::connect(client->pumpTimer, &SwTimer::timeout, [this, socket]() {
            pumpStream_(socket);
        });
    }
    client->lastSentSeq = 0;
    client->pumpTimer->stop();
    client->pumpTimer->start(intervalMs);
    pumpStream_(socket);
}

void PnServerManagerNode::pumpStream_(SwTcpSocket* socket) {
    StreamClientState* client = findClient_(socket);
    if (!client || !socket) {
        return;
    }

    SwByteArray jpeg;
    int seq = 0;
    int width = 0;
    int height = 0;
    {
        SwMutexLocker locker(&frameMutex_);
        jpeg = latestJpeg_;
        seq = latestSeq_;
        width = latestWidth_;
        height = latestHeight_;
    }

    if (jpeg.isEmpty()) {
        return;
    }
    if (seq == client->lastSentSeq) {
        return;
    }

    SwString partHeader;
    partHeader += "--";
    partHeader += kStreamBoundary;
    partHeader += "\r\n";
    partHeader += "Content-Type: image/jpeg\r\n";
    partHeader += "Content-Length: " + SwString::number(static_cast<long long>(jpeg.size())) + "\r\n";
    partHeader += "X-Frame-Seq: " + SwString::number(seq) + "\r\n";
    partHeader += "X-Frame-Width: " + SwString::number(width) + "\r\n";
    partHeader += "X-Frame-Height: " + SwString::number(height) + "\r\n";
    partHeader += "\r\n";

    SwByteArray chunk(partHeader.toStdString());
    chunk.append(jpeg);
    chunk.append("\r\n");

    if (!socket->write(SwString(chunk))) {
        closeClient_(socket);
        return;
    }

    client->lastSentSeq = seq;
}

void PnServerManagerNode::writeTextResponse_(SwTcpSocket* socket,
                                             int status,
                                             const SwString& reason,
                                             const SwString& body) {
    writeBinaryResponse_(socket,
                         status,
                         reason,
                         "text/plain; charset=utf-8",
                         SwByteArray(body.toStdString()));
}

void PnServerManagerNode::writeBinaryResponse_(SwTcpSocket* socket,
                                               int status,
                                               const SwString& reason,
                                               const SwString& contentType,
                                               const SwByteArray& body) {
    if (!socket) {
        return;
    }

    SwString responseHeader;
    responseHeader += "HTTP/1.1 ";
    responseHeader += SwString::number(status);
    responseHeader += " ";
    responseHeader += reason;
    responseHeader += "\r\n";
    responseHeader += "Content-Type: ";
    responseHeader += contentType;
    responseHeader += "\r\n";
    responseHeader += "Content-Length: ";
    responseHeader += SwString::number(static_cast<long long>(body.size()));
    responseHeader += "\r\n";
    responseHeader += "Cache-Control: no-store\r\n";
    responseHeader += "Connection: close\r\n";
    responseHeader += "\r\n";

    SwByteArray payload(responseHeader.toStdString());
    payload.append(body);

    socket->write(SwString(payload));
    socket->waitForBytesWritten(1000);
    closeClient_(socket);
}

void PnServerManagerNode::closeClient_(SwTcpSocket* socket) {
    if (!socket) {
        return;
    }
    socket->close();
    if (findClient_(socket)) {
        removeClient_(socket, true);
    }
}

SwString PnServerManagerNode::statusReason_(int status) {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    default: return "Unknown";
    }
}

SwString PnServerManagerNode::extractRequestMethod_(const SwByteArray& request) {
    const int lineEnd = request.indexOf("\r\n");
    if (lineEnd <= 0) {
        return "";
    }

    const SwByteArray firstLine = request.left(lineEnd);
    const int firstSpace = firstLine.indexOf(' ');
    if (firstSpace <= 0) {
        return "";
    }

    return SwString(firstLine.left(firstSpace)).toUpper();
}

SwString PnServerManagerNode::extractRequestPath_(const SwByteArray& request) {
    const int lineEnd = request.indexOf("\r\n");
    if (lineEnd <= 0) {
        return "/";
    }

    const SwByteArray firstLine = request.left(lineEnd);
    const int firstSpace = firstLine.indexOf(' ');
    if (firstSpace < 0) {
        return "/";
    }
    const int secondSpace = firstLine.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0 || secondSpace <= firstSpace + 1) {
        return "/";
    }

    SwByteArray rawPath = firstLine.mid(firstSpace + 1, secondSpace - firstSpace - 1);
    const int qMark = rawPath.indexOf('?');
    if (qMark >= 0) {
        rawPath = rawPath.left(qMark);
    }
    if (rawPath.isEmpty()) {
        return "/";
    }
    return SwString(rawPath);
}

bool PnServerManagerNode::encodeBgraToJpeg_(const SwByteArray& bgra,
                                            int width,
                                            int height,
                                            SwByteArray& outJpeg) const {
    outJpeg.clear();

#if !defined(_WIN32)
    (void)bgra;
    (void)width;
    (void)height;
    return false;
#else
    if (width <= 0 || height <= 0) {
        return false;
    }

    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (bgra.size() < expectedSize) {
        return false;
    }

    if (!ensureGdiPlus_()) {
        return false;
    }

    static bool encoderReady = false;
    static CLSID jpegEncoderClsid{};
    if (!encoderReady) {
        if (!findJpegEncoderClsid_(jpegEncoderClsid)) {
            return false;
        }
        encoderReady = true;
    }

    Gdiplus::Bitmap bitmap(width,
                           height,
                           width * 4,
                           PixelFormat32bppARGB,
                           reinterpret_cast<BYTE*>(const_cast<char*>(bgra.constData())));

    if (bitmap.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK || !stream) {
        return false;
    }

    ULONG quality = static_cast<ULONG>(clampInt_(jpegQuality_, 1, 100));
    Gdiplus::EncoderParameters params;
    std::memset(&params, 0, sizeof(params));
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Value = &quality;

    bool ok = false;
    if (bitmap.Save(stream, &jpegEncoderClsid, &params) == Gdiplus::Ok) {
        HGLOBAL hGlobal = nullptr;
        if (GetHGlobalFromStream(stream, &hGlobal) == S_OK && hGlobal) {
            const SIZE_T byteCount = GlobalSize(hGlobal);
            if (byteCount > 0) {
                void* memory = GlobalLock(hGlobal);
                if (memory) {
                    outJpeg = SwByteArray(static_cast<const char*>(memory), static_cast<size_t>(byteCount));
                    GlobalUnlock(hGlobal);
                    ok = !outJpeg.isEmpty();
                }
            }
        }
    }

    stream->Release();
    return ok;
#endif
}

PnServerManagerNode::StreamClientState* PnServerManagerNode::findClient_(SwTcpSocket* socket) {
    if (!socket) {
        return nullptr;
    }
    for (size_t i = 0; i < clients_.size(); ++i) {
        StreamClientState* client = clients_[i];
        if (client && client->socket == socket) {
            return client;
        }
    }
    return nullptr;
}

void PnServerManagerNode::removeClient_(SwTcpSocket* socket, bool deleteSocket) {
    if (!socket) {
        return;
    }

    for (size_t i = 0; i < clients_.size(); ++i) {
        StreamClientState* client = clients_[i];
        if (!client || client->socket != socket) {
            continue;
        }

        clients_.removeAt(i);

        SwTimer* timer = client->pumpTimer;
        client->pumpTimer = nullptr;
        if (timer) {
            timer->stop();
            delete timer;
        }

        SwTcpSocket* ownedSocket = client->socket;
        client->socket = nullptr;
        delete client;

        if (deleteSocket && ownedSocket) {
            delete ownedSocket;
        }
        return;
    }
}
