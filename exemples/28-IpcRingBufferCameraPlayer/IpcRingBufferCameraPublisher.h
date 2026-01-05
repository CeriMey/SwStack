#pragma once

#include "SwIpcNoCopyRingBuffer.h"
#include "SwString.h"

#include "IpcRingBufferFrameMeta.h"

#include <atomic>
#include <cstdint>
#include <memory>

class SwMediaFoundationVideoSource;
class SwVideoPacket;

class IpcRingBufferCameraPublisher {
public:
    struct Config {
        SwString domain{"demo"};
        SwString object{"camera"};
        SwString stream{"video"};
        uint32_t capacity{100};
        uint32_t maxBytes{10u * 1024u * 1024u};
        uint32_t deviceIndex{0};
        bool verbose{true};
    };

    explicit IpcRingBufferCameraPublisher(Config cfg);
    ~IpcRingBufferCameraPublisher();

    bool start();
    void stop();

    uint64_t publishedCount() const { return published_.load(); }
    uint64_t droppedCount() const { return dropped_.load(); }
    uint32_t width() const { return width_.load(); }
    uint32_t height() const { return height_.load(); }

private:
    using RB = sw::ipc::NoCopyRingBuffer<IpcRingBufferFrameMeta>;

    void onPacket_(const SwVideoPacket& packet);

    Config cfg_;
    sw::ipc::Registry reg_;
    RB rb_;
    std::shared_ptr<SwMediaFoundationVideoSource> source_;

    std::atomic<uint64_t> published_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint32_t> width_{0};
    std::atomic<uint32_t> height_{0};
};

