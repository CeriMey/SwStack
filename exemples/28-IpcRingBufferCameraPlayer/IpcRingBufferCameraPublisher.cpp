#include "IpcRingBufferCameraPublisher.h"

#include "media/SwMediaFoundationVideoSource.h"
#include "media/SwVideoPacket.h"
#include "media/SwVideoTypes.h"

#include <cstring>
#include <iostream>

IpcRingBufferCameraPublisher::IpcRingBufferCameraPublisher(Config cfg)
    : cfg_(std::move(cfg))
    , reg_(cfg_.domain, cfg_.object) {}

IpcRingBufferCameraPublisher::~IpcRingBufferCameraPublisher() {
    stop();
}

bool IpcRingBufferCameraPublisher::start() {
#if !defined(_WIN32)
    std::cerr << "[IpcRingBufferCamera] This example requires Windows Media Foundation and is only available on Windows.\n";
    return false;
#else
    try {
        rb_ = RB::create(reg_, cfg_.stream, cfg_.capacity, cfg_.maxBytes);
    } catch (const std::exception& e) {
        std::cerr << "[IpcRingBufferCamera] Failed to create ring buffer: " << e.what() << "\n";
        return false;
    }

    source_ = std::make_shared<SwMediaFoundationVideoSource>(cfg_.deviceIndex);
    if (!source_->initialize()) {
        std::cerr << "[IpcRingBufferCamera] Failed to initialize webcam (deviceIndex=" << cfg_.deviceIndex << ")\n";
        source_.reset();
        return false;
    }

    source_->setPacketCallback([this](const SwVideoPacket& packet) {
        onPacket_(packet);
    });

    if (cfg_.verbose) {
        std::cout << "[IpcRingBufferCamera] publishing: target=" << (cfg_.domain + "/" + cfg_.object).toStdString()
                  << " stream=" << cfg_.stream.toStdString()
                  << " cap=" << rb_.capacity()
                  << " max=" << rb_.maxBytesPerItem()
                  << " shm=" << rb_.shmName().toStdString()
                  << " notify=" << rb_.notifySignalName().toStdString()
                  << "\n";
    }

    source_->start();
    return true;
#endif
}

void IpcRingBufferCameraPublisher::stop() {
#if defined(_WIN32)
    if (source_) {
        source_->stop();
        source_->setPacketCallback(SwVideoSource::PacketCallback());
        source_.reset();
    }
#endif
}

void IpcRingBufferCameraPublisher::onPacket_(const SwVideoPacket& packet) {
#if !defined(_WIN32)
    (void)packet;
#else
    if (!rb_.isValid()) return;
    if (!packet.carriesRawFrame() || packet.payload().isEmpty()) return;

    const SwVideoFormatInfo& fmt = packet.rawFormat();
    if (!fmt.isValid()) return;
    if (fmt.format != SwVideoPixelFormat::BGRA32) return;

    const uint64_t bytesU64 = static_cast<uint64_t>(packet.payload().size());
    if (bytesU64 == 0 || bytesU64 > static_cast<uint64_t>(rb_.maxBytesPerItem())) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t bytes = static_cast<uint32_t>(bytesU64);

    RB::WriteLease w = rb_.beginWrite();
    if (!w.isValid()) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    std::memcpy(w.data(), packet.payload().constData(), bytes);

    IpcRingBufferFrameMeta& meta = w.meta();
    meta.width = static_cast<uint32_t>((std::max)(0, fmt.width));
    meta.height = static_cast<uint32_t>((std::max)(0, fmt.height));
    meta.pixelFormat = static_cast<uint32_t>(fmt.format);
    meta.stride = static_cast<int32_t>(fmt.stride[0]);
    meta.pts = packet.pts();

    if (!w.commit(bytes)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    width_.store(meta.width, std::memory_order_relaxed);
    height_.store(meta.height, std::memory_order_relaxed);
    published_.fetch_add(1, std::memory_order_relaxed);
#endif
}

