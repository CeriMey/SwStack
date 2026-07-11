#pragma once

/**
 * @file src/media/SwSrtVideoSource.h
 * @ingroup media
 * @brief Declares the SRT (Secure Reliable Transport) receive source.
 *
 * Receives an MPEG-TS stream over SRT (the interop payload used by OBS, ffmpeg, hardware
 * encoders and CDN ingest) and demuxes it with `SwTsProgramDemux`, emitting the same
 * compressed `SwVideoPacket`/`SwMediaPacket` stream as the UDP/RTP sources — decode is
 * left to the regular pipeline decoders.
 *
 * libsrt is loaded at runtime via `SwSrtLibrary` (dlopen/LoadLibrary, zero link-time
 * dependency). URL forms handled by `SwMediaSourceFactory`:
 *
 *   srt://host:port                      caller (connects out; OBS/ffmpeg in listener mode)
 *   srt://:port  or  ?mode=listener     listener (waits for the sender to connect)
 *   ?streamid=...&passphrase=...        SRT stream id and encryption passphrase
 *   ?latency=<ms>                       SRT receiver latency (SRTO_RCVLATENCY)
 *
 * The worker thread owns the SRT socket entirely (create/connect/recv/close) and
 * reconnects with `StreamState::Recovering` between attempts, like the RTSP source.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwSrtLibrary.h"
#include "media/SwVideoSource.h"
#include "media/rtp/SwTsProgramDemux.h"
#include "SwDebug.h"

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

static constexpr const char* kSwLogCategory_SwSrtVideoSource = "sw.media.swsrtvideosource";

class SwSrtVideoSource : public SwVideoSource {
public:
    explicit SwSrtVideoSource(const SwMediaOpenOptions& options)
        : m_options(options) {
        m_host = options.mediaUrl.host().toStdString();
        m_port = options.mediaUrl.port() > 0 ? options.mediaUrl.port() : 0;
        m_streamId = options.mediaUrl.queryValue("streamid").toStdString();
        m_passphrase = options.mediaUrl.queryValue("passphrase").toStdString();
        m_latencyMs = options.latencyTargetMs;
        const SwString mode = options.mediaUrl.queryValue("mode").toLower();
        m_listener = (mode == "listener") ||
                     (mode.isEmpty() && (m_host.empty() || m_host == "0.0.0.0"));

        m_tsDemux.setPacketCallback([this](const SwMediaPacket& packet) {
            emitProgramVideoPacket_(packet);
        });
        m_tsDemux.setTracksChangedCallback([this](const SwList<SwMediaTrack>& tracks) {
            setTracks(tracks);
        });
    }

    ~SwSrtVideoSource() override {
        stop();
    }

    SwString name() const override { return "SwSrtVideoSource"; }

    /**
     * @brief Returns whether libsrt is loadable on this machine.
     */
    static bool runtimeAvailable() {
        return SwSrtLibrary::instance().available();
    }

    bool isListener() const { return m_listener; }
    int latencyMs() const { return m_latencyMs; }
    const std::string& streamId() const { return m_streamId; }

    void start() override {
        if (isRunning()) {
            return;
        }
        if (m_worker.joinable()) {
            m_worker.join();
        }
        if (m_port <= 0) {
            swCError(kSwLogCategory_SwSrtVideoSource)
                << "[SwSrtVideoSource] missing port in " << m_options.mediaUrl.toString();
            emitStatus(StreamState::Recovering, "Missing SRT port");
            return;
        }
        if (!SwSrtLibrary::instance().ensureStarted()) {
            swCError(kSwLogCategory_SwSrtVideoSource)
                << "[SwSrtVideoSource] libsrt is not available; cannot open "
                << m_options.mediaUrl.toString();
            emitStatus(StreamState::Recovering, "libsrt not available");
            return;
        }
        emitStatus(StreamState::Connecting, "Opening SRT stream...");
        setRunning(true);
        m_worker = std::thread([this]() { workerLoop_(); });
    }

    void stop() override {
        setRunning(false);
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

private:
    static SwVideoPacket::Codec videoCodecFromName_(const SwString& codec) {
        if (codec == "h265" || codec == "hevc") {
            return SwVideoPacket::Codec::H265;
        }
        return SwVideoPacket::Codec::H264;
    }

    void emitProgramVideoPacket_(const SwMediaPacket& packet) {
        if (packet.type() != SwMediaPacket::Type::Video) {
            emitMediaPacket(packet);
            return;
        }
        if (!m_streamingReported) {
            m_streamingReported = true;
            emitStatus(StreamState::Streaming, "Streaming");
        }
        SwVideoPacket videoPacket(videoCodecFromName_(packet.codec()),
                                  packet.payload(),
                                  packet.pts(),
                                  packet.dts(),
                                  packet.isKeyFrame());
        videoPacket.setDiscontinuity(packet.isDiscontinuity());
        emitPacket(videoPacket);
    }

    void workerLoop_() {
        bool firstAttempt = true;
        while (isRunning()) {
            if (!firstAttempt) {
                emitStatus(StreamState::Recovering, "Reconnecting SRT stream...");
                emitRecovery(RecoveryEvent::Kind::Reconnect, "SRT reconnect");
                m_tsDemux.reset();
                m_streamingReported = false;
                if (!sleepWhileRunning_(1000)) {
                    break;
                }
            }
            firstAttempt = false;

            const int socket = openSocket_();
            if (socket == SwSrtLibrary::kInvalidSocket) {
                continue;
            }
            receiveLoop_(socket);
            SwSrtLibrary::instance().close(socket);
        }
        setRunning(false);
        emitStatus(StreamState::Stopped, "Stream stopped");
    }

    /**
     * @brief Creates, configures and connects (or accepts) the SRT socket.
     * @return A connected socket, or kInvalidSocket to trigger a retry.
     */
    int openSocket_() {
        SwSrtLibrary& srt = SwSrtLibrary::instance();

        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        if (m_listener) {
            hints.ai_flags = AI_PASSIVE;
        }
        const std::string host = m_listener && m_host.empty() ? std::string() : m_host;
        const std::string portText = std::to_string(m_port);
        struct addrinfo* resolved = nullptr;
        const int resolveResult = ::getaddrinfo(host.empty() ? nullptr : host.c_str(),
                                                portText.c_str(),
                                                &hints,
                                                &resolved);
        if (resolveResult != 0 || !resolved) {
            logError_("getaddrinfo failed");
            sleepWhileRunning_(1000);
            return SwSrtLibrary::kInvalidSocket;
        }

        int socket = srt.createSocket();
        if (socket == SwSrtLibrary::kInvalidSocket) {
            ::freeaddrinfo(resolved);
            logError_("srt_create_socket failed");
            return SwSrtLibrary::kInvalidSocket;
        }
        applySocketOptions_(socket);

        bool connected = false;
        if (m_listener) {
            connected = acceptPeer_(socket, resolved->ai_addr,
                                    static_cast<int>(resolved->ai_addrlen));
        } else {
            const int connectTimeoutMs = 3000;
            srt.setSockFlag(socket, SwSrtLibrary::kOptConnTimeout, &connectTimeoutMs,
                            sizeof(connectTimeoutMs));
            connected = srt.connect(socket, resolved->ai_addr,
                                    static_cast<int>(resolved->ai_addrlen)) !=
                        SwSrtLibrary::kError;
            if (!connected) {
                logError_("srt_connect failed");
            } else {
                socket = configureAcceptedSocket_(socket);
            }
        }
        ::freeaddrinfo(resolved);
        if (!connected || socket == SwSrtLibrary::kInvalidSocket) {
            if (socket != SwSrtLibrary::kInvalidSocket) {
                srt.close(socket);
            }
            return SwSrtLibrary::kInvalidSocket;
        }
        swCDebug(kSwLogCategory_SwSrtVideoSource)
            << "[SwSrtVideoSource] connected (" << (m_listener ? "listener" : "caller")
            << ", latency=" << m_latencyMs << "ms)";
        return socket;
    }

    void applySocketOptions_(int socket) {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        if (m_latencyMs > 0) {
            srt.setSockFlag(socket, SwSrtLibrary::kOptRcvLatency, &m_latencyMs,
                            sizeof(m_latencyMs));
        }
        if (!m_streamId.empty()) {
            srt.setSockFlag(socket, SwSrtLibrary::kOptStreamId, m_streamId.c_str(),
                            static_cast<int>(m_streamId.size()));
        }
        if (!m_passphrase.empty()) {
            srt.setSockFlag(socket, SwSrtLibrary::kOptPassphrase, m_passphrase.c_str(),
                            static_cast<int>(m_passphrase.size()));
        }
    }

    /**
     * @brief Binds + listens, then polls a non-blocking accept until a sender connects.
     * @return true with `socket` replaced by the accepted connection.
     */
    bool acceptPeer_(int& socket, const void* addr, int addrLen) {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        if (srt.bind(socket, addr, addrLen) == SwSrtLibrary::kError) {
            logError_("srt_bind failed");
            sleepWhileRunning_(1000);
            return false;
        }
        if (srt.listen(socket, 1) == SwSrtLibrary::kError) {
            logError_("srt_listen failed");
            return false;
        }
        const int nonBlocking = 0;
        srt.setSockFlag(socket, SwSrtLibrary::kOptRcvSyn, &nonBlocking, sizeof(nonBlocking));
        while (isRunning()) {
            const int accepted = srt.accept(socket, nullptr, nullptr);
            if (accepted != SwSrtLibrary::kInvalidSocket) {
                srt.close(socket);
                socket = configureAcceptedSocket_(accepted);
                return socket != SwSrtLibrary::kInvalidSocket;
            }
            if (!SwSrtLibrary::isRetryableError(srt.lastErrorCode())) {
                logError_("srt_accept failed");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    int configureAcceptedSocket_(int socket) {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        const int blocking = 1;
        srt.setSockFlag(socket, SwSrtLibrary::kOptRcvSyn, &blocking, sizeof(blocking));
        const int receiveTimeoutMs = 500;
        srt.setSockFlag(socket, SwSrtLibrary::kOptRcvTimeout, &receiveTimeoutMs,
                        sizeof(receiveTimeoutMs));
        return socket;
    }

    void receiveLoop_(int socket) {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        char buffer[1500]; // SRT live-mode payload is at most 1456 bytes
        while (isRunning()) {
            const int received = srt.recvMsg(socket, buffer, sizeof(buffer));
            if (received > 0) {
                m_tsDemux.feed(reinterpret_cast<const uint8_t*>(buffer),
                               static_cast<std::size_t>(received),
                               ++m_timestampCounter);
                continue;
            }
            if (received == SwSrtLibrary::kError &&
                SwSrtLibrary::isRetryableError(srt.lastErrorCode())) {
                continue; // receive timeout: re-check isRunning()
            }
            logError_("srt_recvmsg failed (connection lost)");
            return; // reconnect
        }
    }

    bool sleepWhileRunning_(int totalMs) {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(totalMs);
        while (isRunning() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return isRunning();
    }

    void logError_(const char* label) const {
        SwSrtLibrary& srt = SwSrtLibrary::instance();
        const char* detail = srt.getLastErrorStr ? srt.getLastErrorStr() : "";
        swCWarning(kSwLogCategory_SwSrtVideoSource)
            << "[SwSrtVideoSource] " << label << " for " << m_options.mediaUrl.toString()
            << (detail && detail[0] ? ": " : "") << (detail ? detail : "");
    }

    SwMediaOpenOptions m_options{};
    std::string m_host{};
    int m_port{0};
    std::string m_streamId{};
    std::string m_passphrase{};
    int m_latencyMs{0};
    bool m_listener{false};
    SwTsProgramDemux m_tsDemux{};
    std::atomic<bool> m_streamingReported{false};
    uint32_t m_timestampCounter{0};
    std::thread m_worker;
};
