#pragma once

/**
 * @file src/media/SwLinuxVideoSource.h
 * @ingroup media
 * @brief Declares the Linux V4L2 capture source exposed through the platform-neutral APIs.
 *
 * The source opens `/dev/videoN`, negotiates a capture format (YUYV, RGB24, BGR24 or MJPEG),
 * streams frames through memory-mapped kernel buffers and emits `SwVideoPacket` instances.
 * Uncompressed formats are converted to BGRA32 (same contract as the Windows MediaFoundation
 * capture source); MJPEG frames are forwarded as `Codec::MotionJPEG` packets.
 *
 * V4L2 is a pure kernel UAPI (`<linux/videodev2.h>` + ioctls): no external library is required
 * at build time or at runtime.
 */

#include "media/SwVideoSource.h"
#include "SwDebug.h"

#if defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "core/fs/SwMutex.h"

static constexpr const char* kSwLogCategory_SwLinuxVideoSource = "sw.media.swlinuxvideosource";

class SwLinuxVideoSource : public SwVideoSource {
public:
    explicit SwLinuxVideoSource(unsigned int deviceIndex = 0)
        : m_devicePath("/dev/video" + std::to_string(deviceIndex)) {}

    explicit SwLinuxVideoSource(const std::string& devicePath)
        : m_devicePath(devicePath) {}

    ~SwLinuxVideoSource() override {
        stop();
        releaseResources();
    }

    SwString name() const override { return "SwLinuxVideoSource"; }

    bool initialize() {
        SwMutexLocker lock(m_stateMutex);
        return initializeLocked_();
    }

    void start() override {
        if (isRunning()) {
            return;
        }
        // Reap a worker that exited on its own (device unplugged, V4L2 error): assigning a
        // new std::thread over a still-joinable one would call std::terminate.
        if (m_worker.joinable()) {
            m_worker.join();
        }
        emitStatus(StreamState::Connecting, "Opening V4L2 device...");
        {
            SwMutexLocker lock(m_stateMutex);
            if (!initializeLocked_() || !startStreamingLocked_()) {
                emitStatus(StreamState::Recovering, "Failed to open V4L2 device");
                return;
            }
        }
        setRunning(true);
        m_worker = std::thread([this]() { captureLoop_(); });
    }

    void stop() override {
        setRunning(false);
        if (m_worker.joinable()) {
            m_worker.join();
        }
        SwMutexLocker lock(m_stateMutex);
        stopStreamingLocked_();
    }

    int frameWidth() const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }

    /**
     * @brief Returns the negotiated V4L2 pixel format (V4L2_PIX_FMT_*), 0 before initialize().
     */
    uint32_t pixelFormat() const { return m_pixelFormat; }

private:
    struct MappedBuffer {
        void* data{nullptr};
        std::size_t length{0};
    };

    static int xioctl_(int fd, unsigned long request, void* arg) {
        int result;
        do {
            result = ::ioctl(fd, request, arg);
        } while (result == -1 && errno == EINTR);
        return result;
    }

    void logErrno_(const char* label) const {
        swCError(kSwLogCategory_SwLinuxVideoSource)
            << "[SwLinuxVideoSource] " << label << " failed on " << m_devicePath
            << ": " << ::strerror(errno);
    }

    bool initializeLocked_() {
        if (m_initialized) {
            return true;
        }

        m_fd = ::open(m_devicePath.c_str(), O_RDWR | O_NONBLOCK, 0);
        if (m_fd < 0) {
            logErrno_("open");
            return false;
        }

        v4l2_capability capability;
        ::memset(&capability, 0, sizeof(capability));
        if (xioctl_(m_fd, VIDIOC_QUERYCAP, &capability) < 0) {
            logErrno_("VIDIOC_QUERYCAP");
            releaseLocked_();
            return false;
        }
        const uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
                                  ? capability.device_caps
                                  : capability.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
            swCError(kSwLogCategory_SwLinuxVideoSource)
                << "[SwLinuxVideoSource] " << m_devicePath
                << " does not support streaming video capture.";
            releaseLocked_();
            return false;
        }

        if (!negotiateFormatLocked_()) {
            releaseLocked_();
            return false;
        }
        if (!mapBuffersLocked_()) {
            releaseLocked_();
            return false;
        }

        publishTracksLocked_();
        m_initialized = true;
        swCDebug(kSwLogCategory_SwLinuxVideoSource)
            << "[SwLinuxVideoSource] " << m_devicePath << " ready: " << fourccName_(m_pixelFormat)
            << " " << m_frameWidth << "x" << m_frameHeight << " (" << m_buffers.size()
            << " mmap buffers)";
        return true;
    }

    bool negotiateFormatLocked_() {
        v4l2_format format;
        ::memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl_(m_fd, VIDIOC_G_FMT, &format) < 0) {
            logErrno_("VIDIOC_G_FMT");
            return false;
        }

        // Keep the device's current resolution; only steer the pixel format towards
        // something the downstream pipeline handles without a decoder.
        static const uint32_t kPreferredFormats[] = {
            V4L2_PIX_FMT_YUYV,
            V4L2_PIX_FMT_RGB24,
            V4L2_PIX_FMT_BGR24,
            V4L2_PIX_FMT_MJPEG,
            V4L2_PIX_FMT_JPEG
        };

        bool negotiated = false;
        for (std::size_t i = 0; i < sizeof(kPreferredFormats) / sizeof(kPreferredFormats[0]); ++i) {
            v4l2_format attempt = format;
            attempt.fmt.pix.pixelformat = kPreferredFormats[i];
            attempt.fmt.pix.field = V4L2_FIELD_NONE;
            if (xioctl_(m_fd, VIDIOC_S_FMT, &attempt) < 0) {
                continue;
            }
            if (attempt.fmt.pix.pixelformat != kPreferredFormats[i]) {
                continue;
            }
            format = attempt;
            negotiated = true;
            break;
        }
        if (!negotiated) {
            // Fall back to whatever the driver currently exposes if it is convertible.
            if (!isSupportedFormat_(format.fmt.pix.pixelformat)) {
                swCError(kSwLogCategory_SwLinuxVideoSource)
                    << "[SwLinuxVideoSource] no supported pixel format on " << m_devicePath
                    << " (driver format " << fourccName_(format.fmt.pix.pixelformat) << ")";
                return false;
            }
        }

        m_pixelFormat = format.fmt.pix.pixelformat;
        m_frameWidth = static_cast<int>(format.fmt.pix.width);
        m_frameHeight = static_cast<int>(format.fmt.pix.height);
        m_bytesPerLine = static_cast<int>(format.fmt.pix.bytesperline);
        if (m_frameWidth <= 0 || m_frameHeight <= 0) {
            swCError(kSwLogCategory_SwLinuxVideoSource)
                << "[SwLinuxVideoSource] invalid negotiated size on " << m_devicePath;
            return false;
        }
        return true;
    }

    bool mapBuffersLocked_() {
        v4l2_requestbuffers request;
        ::memset(&request, 0, sizeof(request));
        request.count = 4;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        if (xioctl_(m_fd, VIDIOC_REQBUFS, &request) < 0) {
            logErrno_("VIDIOC_REQBUFS");
            return false;
        }
        if (request.count < 2) {
            swCError(kSwLogCategory_SwLinuxVideoSource)
                << "[SwLinuxVideoSource] insufficient buffer memory on " << m_devicePath;
            return false;
        }

        m_buffers.resize(request.count);
        for (uint32_t i = 0; i < request.count; ++i) {
            v4l2_buffer buffer;
            ::memset(&buffer, 0, sizeof(buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;
            if (xioctl_(m_fd, VIDIOC_QUERYBUF, &buffer) < 0) {
                logErrno_("VIDIOC_QUERYBUF");
                return false;
            }
            void* mapped = ::mmap(nullptr,
                                  buffer.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED,
                                  m_fd,
                                  buffer.m.offset);
            if (mapped == MAP_FAILED) {
                logErrno_("mmap");
                return false;
            }
            m_buffers[i].data = mapped;
            m_buffers[i].length = buffer.length;
        }
        return true;
    }

    bool startStreamingLocked_() {
        if (m_streaming) {
            return true;
        }
        for (std::size_t i = 0; i < m_buffers.size(); ++i) {
            v4l2_buffer buffer;
            ::memset(&buffer, 0, sizeof(buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = static_cast<uint32_t>(i);
            if (xioctl_(m_fd, VIDIOC_QBUF, &buffer) < 0) {
                logErrno_("VIDIOC_QBUF");
                return false;
            }
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl_(m_fd, VIDIOC_STREAMON, &type) < 0) {
            logErrno_("VIDIOC_STREAMON");
            return false;
        }
        m_streaming = true;
        return true;
    }

    void stopStreamingLocked_() {
        if (!m_streaming || m_fd < 0) {
            m_streaming = false;
            return;
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl_(m_fd, VIDIOC_STREAMOFF, &type) < 0) {
            logErrno_("VIDIOC_STREAMOFF");
        }
        m_streaming = false;
    }

    void releaseLocked_() {
        for (std::size_t i = 0; i < m_buffers.size(); ++i) {
            if (m_buffers[i].data && m_buffers[i].data != MAP_FAILED) {
                ::munmap(m_buffers[i].data, m_buffers[i].length);
            }
        }
        m_buffers.clear();
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        m_initialized = false;
    }

    void releaseResources() {
        SwMutexLocker lock(m_stateMutex);
        stopStreamingLocked_();
        releaseLocked_();
    }

    void publishTracksLocked_() {
        SwMediaTrack track;
        track.id = "v4l2-video-0";
        track.type = SwMediaTrack::Type::Video;
        track.codec = isCompressedFormat_(m_pixelFormat) ? "mjpeg" : "raw-bgra";
        track.selected = true;
        track.availability = SwMediaTrack::Availability::Available;
        SwList<SwMediaTrack> tracks;
        tracks.append(track);
        setTracks(tracks);
    }

    void captureLoop_() {
        bool firstFrame = true;
        while (isRunning()) {
            pollfd descriptor;
            descriptor.fd = m_fd;
            descriptor.events = POLLIN;
            descriptor.revents = 0;
            const int ready = ::poll(&descriptor, 1, 500);
            if (!isRunning()) {
                break;
            }
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                logErrno_("poll");
                emitStatus(StreamState::Recovering, "V4L2 poll failed");
                break;
            }
            if (ready == 0) {
                continue;
            }

            v4l2_buffer buffer;
            ::memset(&buffer, 0, sizeof(buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (xioctl_(m_fd, VIDIOC_DQBUF, &buffer) < 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                logErrno_("VIDIOC_DQBUF");
                emitStatus(StreamState::Recovering, "V4L2 dequeue failed");
                break;
            }

            if (buffer.index < m_buffers.size() && buffer.bytesused > 0) {
                const std::int64_t timestampMs =
                    static_cast<std::int64_t>(buffer.timestamp.tv_sec) * 1000 +
                    static_cast<std::int64_t>(buffer.timestamp.tv_usec) / 1000;
                SwVideoPacket packet;
                if (buildPacket_(m_buffers[buffer.index],
                                 static_cast<std::size_t>(buffer.bytesused),
                                 timestampMs,
                                 packet)) {
                    if (firstFrame) {
                        emitStatus(StreamState::Streaming, "Streaming");
                        firstFrame = false;
                    }
                    emitPacket(packet);
                }
            }

            if (xioctl_(m_fd, VIDIOC_QBUF, &buffer) < 0) {
                logErrno_("VIDIOC_QBUF(requeue)");
                emitStatus(StreamState::Recovering, "V4L2 requeue failed");
                break;
            }
        }
        setRunning(false);
        emitStatus(StreamState::Stopped, "Stream stopped");
    }

    bool buildPacket_(const MappedBuffer& buffer,
                      std::size_t bytesUsed,
                      std::int64_t timestampMs,
                      SwVideoPacket& packet) const {
        const uint8_t* src = static_cast<const uint8_t*>(buffer.data);
        const std::size_t available = bytesUsed < buffer.length ? bytesUsed : buffer.length;

        if (isCompressedFormat_(m_pixelFormat)) {
            packet = SwVideoPacket(SwVideoPacket::Codec::MotionJPEG,
                                   SwByteArray(reinterpret_cast<const char*>(src), available),
                                   timestampMs,
                                   timestampMs,
                                   true);
            packet.setClockRate(1000);
            return true;
        }

        SwByteArray payload;
        bool converted = false;
        switch (m_pixelFormat) {
        case V4L2_PIX_FMT_YUYV:
            converted = convertYuyvToBgra_(src, available, payload);
            break;
        case V4L2_PIX_FMT_RGB24:
            converted = convertRgb24ToBgra_(src, available, payload, false);
            break;
        case V4L2_PIX_FMT_BGR24:
            converted = convertRgb24ToBgra_(src, available, payload, true);
            break;
        default:
            break;
        }
        if (!converted) {
            return false;
        }

        SwVideoFormatInfo format =
            SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, m_frameWidth, m_frameHeight);
        packet = SwVideoPacket(SwVideoPacket::Codec::RawBGRA,
                               std::move(payload),
                               timestampMs,
                               timestampMs,
                               true);
        packet.setRawFormat(format);
        packet.setClockRate(1000);
        return true;
    }

    bool convertYuyvToBgra_(const uint8_t* src, std::size_t available, SwByteArray& out) const {
        const int width = m_frameWidth;
        const int height = m_frameHeight;
        const int stride = (m_bytesPerLine > 0) ? m_bytesPerLine : width * 2;
        if (available < static_cast<std::size_t>(stride) * height) {
            return false;
        }
        out = SwByteArray(static_cast<std::size_t>(width) * height * 4, '\0');
        char* dstBase = out.data();
        if (!dstBase) {
            return false;
        }
        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        };
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = src + static_cast<std::size_t>(y) * stride;
            uint8_t* dstRow =
                reinterpret_cast<uint8_t*>(dstBase + static_cast<std::size_t>(y) * width * 4);
            for (int x = 0; x + 1 < width; x += 2) {
                const int c0 = std::max(static_cast<int>(row[x * 2 + 0]) - 16, 0);
                const int u = static_cast<int>(row[x * 2 + 1]) - 128;
                const int c1 = std::max(static_cast<int>(row[x * 2 + 2]) - 16, 0);
                const int v = static_cast<int>(row[x * 2 + 3]) - 128;
                // Shared chroma terms, computed once per YUYV pixel pair.
                const int rTerm = 409 * v + 128;
                const int gTerm = 100 * u + 208 * v - 128;
                const int bTerm = 516 * u + 128;
                uint8_t* pixel = dstRow + x * 4;
                for (int k = 0; k < 2; ++k, pixel += 4) {
                    const int luma = 298 * (k == 0 ? c0 : c1);
                    pixel[0] = clamp((luma + bTerm) >> 8);
                    pixel[1] = clamp((luma - gTerm) >> 8);
                    pixel[2] = clamp((luma + rTerm) >> 8);
                    pixel[3] = 255;
                }
            }
        }
        return true;
    }

    bool convertRgb24ToBgra_(const uint8_t* src,
                             std::size_t available,
                             SwByteArray& out,
                             bool sourceIsBgr) const {
        const int width = m_frameWidth;
        const int height = m_frameHeight;
        const int stride = (m_bytesPerLine > 0) ? m_bytesPerLine : width * 3;
        if (available < static_cast<std::size_t>(stride) * height) {
            return false;
        }
        out = SwByteArray(static_cast<std::size_t>(width) * height * 4, '\0');
        char* dstBase = out.data();
        if (!dstBase) {
            return false;
        }
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = src + static_cast<std::size_t>(y) * stride;
            uint8_t* dstRow =
                reinterpret_cast<uint8_t*>(dstBase + static_cast<std::size_t>(y) * width * 4);
            for (int x = 0; x < width; ++x) {
                const uint8_t c0 = row[x * 3 + 0];
                const uint8_t c1 = row[x * 3 + 1];
                const uint8_t c2 = row[x * 3 + 2];
                uint8_t* pixel = dstRow + x * 4;
                pixel[0] = sourceIsBgr ? c0 : c2;
                pixel[1] = c1;
                pixel[2] = sourceIsBgr ? c2 : c0;
                pixel[3] = 255;
            }
        }
        return true;
    }

    static bool isCompressedFormat_(uint32_t pixelFormat) {
        return pixelFormat == V4L2_PIX_FMT_MJPEG || pixelFormat == V4L2_PIX_FMT_JPEG;
    }

    static bool isSupportedFormat_(uint32_t pixelFormat) {
        return pixelFormat == V4L2_PIX_FMT_YUYV ||
               pixelFormat == V4L2_PIX_FMT_RGB24 ||
               pixelFormat == V4L2_PIX_FMT_BGR24 ||
               isCompressedFormat_(pixelFormat);
    }

    static std::string fourccName_(uint32_t fourcc) {
        std::string name(4, '?');
        for (int i = 0; i < 4; ++i) {
            const char c = static_cast<char>((fourcc >> (8 * i)) & 0xFF);
            name[static_cast<std::size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
        }
        return name;
    }

    std::string m_devicePath;
    mutable SwMutex m_stateMutex;
    int m_fd{-1};
    bool m_initialized{false};
    bool m_streaming{false};
    uint32_t m_pixelFormat{0};
    int m_frameWidth{0};
    int m_frameHeight{0};
    int m_bytesPerLine{0};
    std::vector<MappedBuffer> m_buffers;
    std::thread m_worker;
};

#endif
