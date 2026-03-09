#include "PnCaptureManagerNode.h"

#include "PnUtils.h"

#include "SwDebug.h"
#include "SwTimer.h"
#include "media/SwMediaFoundationVideoSource.h"
#include "media/SwVideoPacket.h"

static int clampInt_(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

PnCaptureManagerNode::PnCaptureManagerNode(const SwString& sysName,
                                           const SwString& nameSpace,
                                           const SwString& objectName,
                                           SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent),
      timer_(new SwTimer(this)) {
    ipcRegisterConfig(int, periodMs_, "period_ms", 100, [this](const int&) { retime_(); });
    ipcRegisterConfig(int, width_, "width", 32, [this](const int&) { sanitizeGeometry_(); });
    ipcRegisterConfig(int, height_, "height", 32, [this](const int&) { sanitizeGeometry_(); });
    ipcRegisterConfig(int, deviceIndex_, "device_index", -1, [this](const int&) { restartCamera_(); });

    SwObject::connect(timer_, &SwTimer::timeout, [this]() { publishFrame_(); });
    sanitizeGeometry_();
    restartCamera_();
    retime_();
}

PnCaptureManagerNode::~PnCaptureManagerNode() {
    if (cameraSource_) {
        cameraSource_->stop();
        delete cameraSource_;
        cameraSource_ = nullptr;
    }
}

void PnCaptureManagerNode::retime_() {
    if (!timer_) return;
    periodMs_ = clampInt_(periodMs_, 10, 5000);
    timer_->stop();
    timer_->start(periodMs_);
}

void PnCaptureManagerNode::sanitizeGeometry_() {
    // SW_REGISTER_SHM_SIGNAL payload is bounded (~4KB), keep RGB frames compact.
    width_ = clampInt_(width_, 8, 32);
    height_ = clampInt_(height_, 8, 32);
}

void PnCaptureManagerNode::restartCamera_() {
    if (cameraSource_) {
        cameraSource_->stop();
        delete cameraSource_;
        cameraSource_ = nullptr;
    }

    hasLatestFrame_ = false;
    cameraReady_ = false;

    if (deviceIndex_ < 0) {
        swDebug() << "[PnCaptureManagerNode] webcam disabled (device_index < 0), synthetic fallback active";
        return;
    }

    cameraSource_ = new SwMediaFoundationVideoSource(static_cast<unsigned int>(deviceIndex_));
    cameraSource_->setPacketCallback([this](const SwVideoPacket& packet) {
        onCameraPacket_(packet);
    });

    if (!cameraSource_->initialize()) {
        swWarning() << "[PnCaptureManagerNode] webcam init failed for device_index=" << deviceIndex_;
        return;
    }

    cameraSource_->start();
    cameraReady_ = true;
    swDebug() << "[PnCaptureManagerNode] webcam started device_index=" << deviceIndex_;
}

void PnCaptureManagerNode::onCameraPacket_(const SwVideoPacket& packet) {
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

    int targetWidth = 0;
    int targetHeight = 0;
    {
        SwMutexLocker locker(&frameMutex_);
        targetWidth = width_;
        targetHeight = height_;
    }

    const SwByteArray rgb = downscaleBgraToRgb_(packet.payload(), info.width, info.height, targetWidth, targetHeight);
    if (rgb.isEmpty()) {
        return;
    }

    {
        SwMutexLocker locker(&frameMutex_);
        latestFrameRgb_ = rgb;
        latestFrameWidth_ = targetWidth;
        latestFrameHeight_ = targetHeight;
        hasLatestFrame_ = true;
    }
}

SwByteArray PnCaptureManagerNode::downscaleBgraToRgb_(const SwByteArray& bgra,
                                                      int srcWidth,
                                                      int srcHeight,
                                                      int dstWidth,
                                                      int dstHeight) {
    if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
        return SwByteArray();
    }

    const size_t srcSize = static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight) * 4u;
    if (bgra.size() < srcSize) {
        return SwByteArray();
    }

    const size_t dstSize = static_cast<size_t>(dstWidth) * static_cast<size_t>(dstHeight) * 3u;
    SwByteArray rgb(dstSize, '\0');

    for (int y = 0; y < dstHeight; ++y) {
        const int sy = (y * srcHeight) / dstHeight;
        for (int x = 0; x < dstWidth; ++x) {
            const int sx = (x * srcWidth) / dstWidth;

            const size_t s = (static_cast<size_t>(sy) * static_cast<size_t>(srcWidth) + static_cast<size_t>(sx)) * 4u;
            const size_t d = (static_cast<size_t>(y) * static_cast<size_t>(dstWidth) + static_cast<size_t>(x)) * 3u;

            const unsigned char b = static_cast<unsigned char>(bgra[s + 0]);
            const unsigned char g = static_cast<unsigned char>(bgra[s + 1]);
            const unsigned char r = static_cast<unsigned char>(bgra[s + 2]);

            rgb[d + 0] = static_cast<char>(r);
            rgb[d + 1] = static_cast<char>(g);
            rgb[d + 2] = static_cast<char>(b);
        }
    }

    return rgb;
}

SwByteArray PnCaptureManagerNode::makeSyntheticRgb_(int seq, int width, int height) {
    if (width <= 0 || height <= 0) {
        return SwByteArray();
    }

    SwByteArray rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u, '\0');
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int i = (y * width + x) * 3;
            const int fx = (x + seq * 2) & 0xFF;
            const int fy = (y + seq * 3) & 0xFF;
            const int mix = ((x + y) / 2 + seq * 4) & 0xFF;
            rgb[static_cast<size_t>(i + 0)] = static_cast<char>(fx);
            rgb[static_cast<size_t>(i + 1)] = static_cast<char>(fy);
            rgb[static_cast<size_t>(i + 2)] = static_cast<char>(mix);
        }
    }
    return rgb;
}

void PnCaptureManagerNode::publishFrame_() {
    sanitizeGeometry_();

    SwByteArray rgb;
    int width = 0;
    int height = 0;
    bool hasFrame = false;
    {
        SwMutexLocker locker(&frameMutex_);
        width = width_;
        height = height_;
        if (hasLatestFrame_ && latestFrameWidth_ == width && latestFrameHeight_ == height) {
            rgb = latestFrameRgb_;
            hasFrame = !rgb.isEmpty();
        }
    }

    if (!hasFrame) {
        if (deviceIndex_ < 0) {
            rgb = makeSyntheticRgb_(seq_ + 1, width, height);
            hasFrame = !rgb.isEmpty();
        }
    }

    if (!hasFrame) {
        if (!cameraReady_) {
            swWarning() << "[PnCaptureManagerNode] waiting for webcam...";
        }
        return;
    }

    ++seq_;
    const bool ok = emit frame(seq_, width, height, rgb);
    if (!ok) {
        swWarning() << "[PnCaptureManagerNode] publish failed (frame)";
        return;
    }

    if ((seq_ % 30) == 0) {
        swDebug() << "[PnCaptureManagerNode]" << PnUtils::banner(objectName())
                  << "frame seq=" << seq_ << " size=" << width_ << "x" << height_;
    }
}
