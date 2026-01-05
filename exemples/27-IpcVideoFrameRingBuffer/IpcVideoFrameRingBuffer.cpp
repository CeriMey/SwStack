#include "SwCoreApplication.h"
#include "SwIpcNoCopyRingBuffer.h"
#include "SwRegularExpression.h"
#include "SwString.h"

#include "media/SwVideoFrame.h"
#include "media/SwVideoTypes.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

static bool splitTarget(const SwString& fqn, SwString& domainOut, SwString& objectOut) {
    SwString x = fqn.trimmed();
    x.replace('\\', '/');
    x.remove(SwRegularExpression("^/+|/+$"));
    while (x.contains("//")) x.replace("//", "/");

    const int slash = x.indexOf('/');
    if (slash <= 0 || slash >= x.size() - 1) return false;
    domainOut = x.left(slash);
    objectOut = x.mid(slash + 1);
    return !domainOut.isEmpty() && !objectOut.isEmpty();
}

static uint32_t checksum32(const uint8_t* data, size_t n) {
    if (!data || n == 0) return 0;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint32_t>(data[i]);
        h *= 16777619u;
    }
    return h;
}

static void fillPattern(uint8_t* p, size_t n, uint8_t seed) {
    if (!p) return;
    for (size_t i = 0; i < n; i += 4) {
        p[i + 0] = seed;        // B
        p[i + 1] = 64;          // G
        p[i + 2] = 255 - seed;  // R
        p[i + 3] = 255;         // A
    }
}

static void usage() {
    std::cout << "Usage:\n"
              << "  IpcVideoFrameRingBuffer --mode=pub --target=domain/object [--w=640 --h=480 --fps=30 --cap=100 --max=10485760]\n"
              << "  IpcVideoFrameRingBuffer --mode=sub --target=domain/object [--count=0]\n";
}

} // namespace

struct VideoFrameMeta {
    static constexpr const char* kTypeName = "sw::ipc::VideoFrameMetaV1";

    uint32_t width{0};
    uint32_t height{0};
    uint32_t planeCount{0};
    uint32_t pixelFormat{0};

    int32_t stride[4]{};
    int32_t planeHeights[4]{};
    uint64_t planeOffsets[4]{};
    uint64_t dataSize{0};

    int64_t pts{0};
    uint32_t colorSpace{0};
    uint32_t rotation{0};
    uint32_t reserved0{0};
    double aspectRatio{0.0};
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    const SwString mode = app.getArgument("mode", "pub");
    const SwString target = app.getArgument("target", "demo/camera");

    SwString domain;
    SwString object;
    if (!splitTarget(target, domain, object)) {
        std::cerr << "[IpcVideoFrameRingBuffer] invalid --target (expected domain/object)\n";
        usage();
        return 2;
    }

    const int cap = (std::max)(1, app.getArgument("cap", "100").toInt());
    const int maxBytes = (std::max)(1, app.getArgument("max", "10485760").toInt()); // default: 10MB

    const SwString streamName = app.getArgument("stream", "video");

    sw::ipc::Registry reg(domain, object);
    using VideoRb = sw::ipc::NoCopyRingBuffer<VideoFrameMeta>;

    if (mode == "pub") {
        const int width = (std::max)(1, app.getArgument("w", "640").toInt());
        const int height = (std::max)(1, app.getArgument("h", "480").toInt());
        const int fps = (std::max)(1, app.getArgument("fps", "30").toInt());
        const int count = (std::max)(0, app.getArgument("count", "0").toInt());

        VideoRb rb = VideoRb::create(reg, streamName, static_cast<uint32_t>(cap), static_cast<uint32_t>(maxBytes));

        const uint64_t bytesU64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
        if (bytesU64 > static_cast<uint64_t>(rb.maxBytesPerItem())) {
            std::cerr << "[IpcVideoFrameRingBuffer] frame too large for --max (bytes=" << bytesU64
                      << " max=" << rb.maxBytesPerItem() << ")\n";
            return 2;
        }
        const uint32_t bytes = static_cast<uint32_t>(bytesU64);

        std::cout << "[IpcVideoFrameRingBuffer] publisher online: target=" << target.toStdString()
                  << " stream=" << streamName.toStdString()
                  << " cap=" << rb.capacity()
                  << " max=" << rb.maxBytesPerItem()
                  << " fps=" << fps
                  << " size=" << width << "x" << height << "\n";

        const auto period = std::chrono::milliseconds(static_cast<int>(1000.0 / static_cast<double>(fps)));

        uint64_t i = 0;
        while (count == 0 || static_cast<int>(i) < count) {
            auto w = rb.beginWrite();
            if (!w.isValid()) {
                std::cerr << "[IpcVideoFrameRingBuffer] drop (dropped=" << rb.droppedCount() << ")\n";
            } else {
                fillPattern(w.data(), bytes, static_cast<uint8_t>(i & 0xFFu));

                VideoFrameMeta& m = w.meta();
                m.width = static_cast<uint32_t>(width);
                m.height = static_cast<uint32_t>(height);
                m.planeCount = 1;
                m.pixelFormat = static_cast<uint32_t>(SwVideoPixelFormat::BGRA32);
                m.stride[0] = width * 4;
                m.planeHeights[0] = height;
                m.planeOffsets[0] = 0;
                m.dataSize = bytes;
                m.pts = static_cast<int64_t>(i);

                const bool ok = w.commit(bytes);
                if (!ok) {
                    std::cerr << "[IpcVideoFrameRingBuffer] drop (dropped=" << rb.droppedCount() << ")\n";
                }
            }

            ++i;
            std::this_thread::sleep_for(period);
        }
        return 0;
    }

    if (mode == "sub") {
        const int count = (std::max)(0, app.getArgument("count", "0").toInt());
        std::atomic<int> got{0};

        VideoRb rb = VideoRb::open(reg, streamName);
        VideoRb::Consumer consumer = rb.consumer();

        std::cout << "[IpcVideoFrameRingBuffer] subscriber online: target=" << target.toStdString()
                  << " stream=" << streamName.toStdString()
                  << " cap=" << rb.capacity()
                  << " max=" << rb.maxBytesPerItem()
                  << " lastSeq=" << rb.lastSeq() << "\n";

        auto sub = rb.connect(consumer, [&](uint64_t seq, VideoRb::ReadLease lease) {
            const int n = got.fetch_add(1, std::memory_order_acq_rel) + 1;
            const uint8_t* p = lease.data();
            const size_t bytes = lease.bytes();
            const uint32_t c = checksum32(p, (std::min<size_t>)(bytes, 256));

            const VideoFrameMeta& m = lease.meta();

            std::cout << "[sub] seq=" << seq
                      << " w=" << static_cast<int>(m.width)
                      << " h=" << static_cast<int>(m.height)
                      << " fmt=" << static_cast<int>(m.pixelFormat)
                      << " bytes=" << bytes
                      << " checksum=" << c << "\n";

            if (count > 0 && n >= count) {
                app.quit();
            }
        }, /*fireInitial=*/true);

        return app.exec();
    }

    std::cerr << "[IpcVideoFrameRingBuffer] unknown --mode (expected pub/sub)\n";
    usage();
    return 2;
}
