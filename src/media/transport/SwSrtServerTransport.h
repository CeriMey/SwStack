#pragma once

/**
 * @file src/media/transport/SwSrtServerTransport.h
 * @brief SRT (Secure Reliable Transport) implementation of the common media server interface.
 *
 * Publishes the video stream as MPEG-TS over SRT (`SwTsMuxer`), the interop format
 * consumed by OBS, ffmpeg, VLC and hardware decoders. The transport runs an SRT listener:
 * receivers connect as callers (e.g. `ffplay 'srt://host:port'` or `SwSrtVideoSource`).
 * libsrt is loaded at runtime through `SwSrtLibrary` — zero link-time dependency; start()
 * fails gracefully when the library is absent.
 *
 * Reliability, pacing and retransmission are handled by SRT itself, so there is no
 * NACK/feedback plumbing here (unlike SwVTP).
 */

#include "media/SwSrtLibrary.h"
#include "media/rtp/SwTsMuxer.h"
#include "media/server/SwVideoTransportServer.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static constexpr const char* kSwLogCategory_SwSrtServerTransport =
    "sw.media.swsrtservertransport";

class SwSrtServerTransport : public SwVideoTransportServer {
public:
    SwSrtServerTransport() = default;

    ~SwSrtServerTransport() override {
        stop();
    }

    SwString protocolName() const override { return "srt"; }

    bool addStream(const SwVideoPublishStream& stream) override {
        if (!stream.isValid()) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            if (it->id == stream.id) {
                *it = stream;
                return true;
            }
        }
        m_streams.append(stream);
        if (m_metrics.targetBitrateKbps == 0U) {
            m_metrics.targetBitrateKbps = stream.startBitrateKbps;
            m_metrics.encoderBitrateKbps = stream.startBitrateKbps;
        }
        return true;
    }

    bool start() override {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        if (!srt.ensureStarted()) {
            swCWarning(kSwLogCategory_SwSrtServerTransport)
                << "[SwSrtServerTransport] libsrt is not available.";
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running.load()) {
            return true;
        }
        if (m_streams.isEmpty() || config().endpoint.port == 0U) {
            return false;
        }

        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE;
        const SwString bindHost = config().endpoint.bindAddress.isEmpty() ||
                                          config().endpoint.bindAddress == "0.0.0.0"
                                      ? SwString()
                                      : config().endpoint.bindAddress;
        const std::string bindText = bindHost.toStdString();
        const std::string portText = std::to_string(config().endpoint.port);
        struct addrinfo* resolved = nullptr;
        if (::getaddrinfo(bindText.empty() ? nullptr : bindText.c_str(),
                          portText.c_str(),
                          &hints,
                          &resolved) != 0 ||
            !resolved) {
            swCWarning(kSwLogCategory_SwSrtServerTransport)
                << "[SwSrtServerTransport] cannot resolve bind address.";
            return false;
        }

        m_listenSocket = srt.createSocket();
        bool ok = m_listenSocket != SwSrtLibrary::kInvalidSocket;
        if (ok && srt.bind(m_listenSocket, resolved->ai_addr,
                           static_cast<int>(resolved->ai_addrlen)) == SwSrtLibrary::kError) {
            logError_("srt_bind failed");
            ok = false;
        }
        if (ok && srt.listen(m_listenSocket, 5) == SwSrtLibrary::kError) {
            logError_("srt_listen failed");
            ok = false;
        }
        ::freeaddrinfo(resolved);
        if (!ok) {
            if (m_listenSocket != SwSrtLibrary::kInvalidSocket) {
                srt.close(m_listenSocket);
                m_listenSocket = SwSrtLibrary::kInvalidSocket;
            }
            return false;
        }
        const int nonBlocking = 0;
        srt.setSockFlag(m_listenSocket, SwSrtLibrary::kOptRcvSyn, &nonBlocking,
                        sizeof(nonBlocking));

        m_muxer.reset();
        m_muxer.setVideoCodec(streamCodecLocked_());
        m_running.store(true);
        m_acceptWorker = std::thread([this]() { acceptLoop_(); });
        return true;
    }

    void stop() override {
        m_running.store(false);
        if (m_acceptWorker.joinable()) {
            m_acceptWorker.join();
        }
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::size_t i = 0; i < m_clients.size(); ++i) {
            srt.close(m_clients[i]);
        }
        m_clients.clear();
        if (m_listenSocket != SwSrtLibrary::kInvalidSocket) {
            srt.close(m_listenSocket);
            m_listenSocket = SwSrtLibrary::kInvalidSocket;
        }
    }

    bool isRunning() const override {
        return m_running.load();
    }

    std::size_t clientCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_clients.size();
    }

    bool publishVideoPacket(const SwString& streamId,
                            const SwVideoPacket& packet) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        const SwVideoPublishStream* stream = findStreamLocked_(streamId);
        if (!m_running.load() || !stream || packet.payload().isEmpty()) {
            ++m_metrics.framesDropped;
            return false;
        }
        ++m_metrics.framesAccepted;
        m_metrics.videoBytesAccepted += packet.payload().size();

        // Packet pts is in clockRate units (RTP 90 kHz by default); the muxer wants ms.
        const int clockRate = packet.clockRate() > 0 ? packet.clockRate() : 90000;
        const std::int64_t ptsMs =
            packet.pts() >= 0 ? (packet.pts() * 1000LL) / clockRate : 0;

        m_muxBuffer.clear();
        m_muxer.muxAccessUnit(packet.payload(), ptsMs, packet.isKeyFrame(), m_muxBuffer);
        if (m_muxBuffer.empty()) {
            ++m_metrics.framesDropped;
            return false;
        }
        if (m_clients.empty()) {
            // Nothing connected yet: accepted but not sent (matches live semantics).
            ++m_metrics.framesSent;
            return true;
        }

        SwSrtLibrary& srt = SwSrtLibrary::instance();
        static const std::size_t kChunk = 1316; // 7 x 188, the SRT/TS convention
        bool anySent = false;
        for (std::size_t clientIndex = 0; clientIndex < m_clients.size();) {
            const int client = m_clients[clientIndex];
            bool clientOk = true;
            for (std::size_t offset = 0; offset < m_muxBuffer.size(); offset += kChunk) {
                const std::size_t size = (m_muxBuffer.size() - offset) < kChunk
                                             ? (m_muxBuffer.size() - offset)
                                             : kChunk;
                if (srt.sendMsg(client, m_muxBuffer.data() + offset,
                                static_cast<int>(size), -1, 1) == SwSrtLibrary::kError) {
                    clientOk = false;
                    break;
                }
                m_metrics.transport.bytesSent += size;
                ++m_metrics.transport.datagramsSent;
            }
            if (!clientOk) {
                ++m_metrics.transport.sendFailures;
                srt.close(client);
                m_clients.erase(m_clients.begin() +
                                static_cast<std::ptrdiff_t>(clientIndex));
                swCWarning(kSwLogCategory_SwSrtServerTransport)
                    << "[SwSrtServerTransport] client dropped (send failed), "
                    << m_clients.size() << " remaining";
                continue;
            }
            anySent = true;
            ++clientIndex;
        }
        if (anySent) {
            ++m_metrics.framesSent;
            m_metrics.videoBytesSent += packet.payload().size();
        } else {
            ++m_metrics.framesDropped;
        }
        return anySent;
    }

    SwVideoServerMetrics metrics() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_metrics;
    }

private:
    SwTsMuxer::VideoCodec streamCodecLocked_() const {
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            const SwString codec = it->codecName().toLower();
            if (codec == "h265" || codec == "hevc") {
                return SwTsMuxer::VideoCodec::H265;
            }
        }
        return SwTsMuxer::VideoCodec::H264;
    }

    const SwVideoPublishStream* findStreamLocked_(const SwString& streamId) const {
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            if (it->id == streamId || it->trackId == streamId) {
                return &(*it);
            }
        }
        return nullptr;
    }

    void acceptLoop_() {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        while (m_running.load()) {
            const int accepted = srt.accept(m_listenSocket, nullptr, nullptr);
            if (accepted != SwSrtLibrary::kInvalidSocket) {
                const int sendTimeoutMs = 1000;
                srt.setSockFlag(accepted, SwSrtLibrary::kOptSndTimeout, &sendTimeoutMs,
                                sizeof(sendTimeoutMs));
                std::lock_guard<std::mutex> lock(m_mutex);
                m_clients.push_back(accepted);
                // Ensure the next frames re-emit PAT/PMT + a key frame prefix quickly.
                m_muxer.reset();
                swCDebug(kSwLogCategory_SwSrtServerTransport)
                    << "[SwSrtServerTransport] client connected, total "
                    << m_clients.size();
                continue;
            }
            if (!SwSrtLibrary::isRetryableError(srt.lastErrorCode())) {
                swCWarning(kSwLogCategory_SwSrtServerTransport)
                    << "[SwSrtServerTransport] accept failed; stopping accept loop.";
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void logError_(const char* label) const {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        const char* detail = srt.getLastErrorStr ? srt.getLastErrorStr() : "";
        swCWarning(kSwLogCategory_SwSrtServerTransport)
            << "[SwSrtServerTransport] " << label
            << (detail && detail[0] ? ": " : "") << (detail ? detail : "");
    }

    mutable std::mutex m_mutex;
    SwList<SwVideoPublishStream> m_streams{};
    SwVideoServerMetrics m_metrics{};
    std::atomic<bool> m_running{false};
    int m_listenSocket{SwSrtLibrary::kInvalidSocket};
    std::vector<int> m_clients{};
    SwTsMuxer m_muxer{};
    std::vector<char> m_muxBuffer{};
    std::thread m_acceptWorker;
};
