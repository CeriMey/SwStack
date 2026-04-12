#pragma once

/**
 * @file src/media/SwUdpVideoSource.h
 * @ingroup media
 * @brief Declares a generic UDP video source for MPEG-TS and Annex-B payloads.
 */

#include "media/SwMediaOpenOptions.h"
#include "media/SwVideoSource.h"
#include "media/rtp/SwTsProgramDemux.h"
#include "core/io/SwUdpSocket.h"
#include "core/runtime/SwTimer.h"
#include "SwDebug.h"

#include <atomic>
#include <chrono>
#include <vector>
static constexpr const char* kSwLogCategory_SwUdpVideoSource = "sw.media.swudpvideosource";

class SwUdpVideoSource : public SwVideoSource {
public:
    explicit SwUdpVideoSource(const SwMediaOpenOptions& options, SwObject* parent = nullptr)
        : m_options(options) {
        SW_UNUSED(parent);
        m_callbackContext = new SwObject();
        m_socket = new SwUdpSocket();
        m_monitorTimer = new SwTimer(100);
        m_socket->setReceiveBufferSize(4 * 1024 * 1024);
        m_socket->setMaxDatagramSize(64 * 1024);
        m_socket->setMaxPendingDatagrams(256);
        m_tsDemux.setPacketCallback([this](const SwMediaPacket& packet) {
            emitProgramVideoPacket_(packet);
        });
        m_tsDemux.setTracksChangedCallback([this](const SwList<SwMediaTrack>& tracks) {
            setTracks(tracks);
        });
        SwObject::connect(m_socket, &SwUdpSocket::readyRead, m_callbackContext, [this]() {
            readPendingDatagrams_();
        });
        SwObject::connect(m_monitorTimer, &SwTimer::timeout, m_callbackContext, [this]() {
            checkTimeout_();
        });
    }

    ~SwUdpVideoSource() override {
        delete m_callbackContext;
        m_callbackContext = nullptr;
        stop();
        delete m_monitorTimer;
        delete m_socket;
    }

    SwString name() const override { return "SwUdpVideoSource"; }

    void start() override {
        if (isRunning()) {
            return;
        }
        const uint16_t listenPort = m_options.rtpPort != 0
                                        ? m_options.rtpPort
                                        : static_cast<uint16_t>(m_options.mediaUrl.port() > 0
                                                                     ? m_options.mediaUrl.port()
                                                                     : 5004);
        const SwString bindAddress = m_options.multicastGroup.isEmpty()
                                         ? m_options.bindAddress
                                         : wildcardBindAddressForGroup_(m_options.multicastGroup);
        const auto bindMode = static_cast<SwUdpSocket::BindMode>(SwUdpSocket::ShareAddress |
                                                                 SwUdpSocket::ReuseAddressHint);
        emitStatus(StreamState::Connecting, "Opening UDP source...");
        if (!m_socket->bind(bindAddress, listenPort, bindMode)) {
            swCError(kSwLogCategory_SwUdpVideoSource)
                << "[SwUdpVideoSource] Failed to bind UDP socket "
                << bindAddress << ":" << listenPort;
            emitStatus(StreamState::Recovering, "Failed to bind UDP socket");
            return;
        }
        if (!m_options.multicastGroup.isEmpty() &&
            !m_socket->joinMulticastGroup(m_options.multicastGroup, m_options.bindAddress)) {
            swCError(kSwLogCategory_SwUdpVideoSource)
                << "[SwUdpVideoSource] Failed to join multicast group "
                << m_options.multicastGroup;
            m_socket->close();
            emitStatus(StreamState::Recovering, "Failed to join multicast group");
            return;
        }
        setRunning(true);
        m_lastPacketTime = {};
        if (m_monitorTimer && !m_monitorTimer->isActive()) {
            m_monitorTimer->start();
        }
    }

    void stop() override {
        setRunning(false);
        if (m_monitorTimer) {
            m_monitorTimer->stop();
        }
        if (m_socket) {
            m_socket->close();
        }
        m_tsDemux.reset();
        emitStatus(StreamState::Stopped, "Stream stopped");
    }

private:
    static SwString wildcardBindAddressForGroup_(const SwString& group) {
        return group.contains(":") ? SwString("::") : SwString("0.0.0.0");
    }

    static bool hasStartCodeH264Idr_(const SwByteArray& payload) {
        std::vector<uint8_t> bytes(payload.begin(), payload.end());
        return SwTsProgramDemux::hasStartCodeH264Idr(bytes);
    }

    static bool hasStartCodeHevcIdr_(const SwByteArray& payload) {
        std::vector<uint8_t> bytes(payload.begin(), payload.end());
        return SwTsProgramDemux::hasStartCodeHevcIdr(bytes);
    }

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
        emitStatus(StreamState::Streaming, "Streaming");
        SwVideoPacket videoPacket(videoCodecFromName_(packet.codec()),
                                  packet.payload(),
                                  packet.pts(),
                                  packet.dts(),
                                  packet.isKeyFrame());
        videoPacket.setDiscontinuity(packet.isDiscontinuity());
        emitPacket(videoPacket);
    }

    void readPendingDatagrams_() {
        if (!isRunning() || !m_socket || !m_socket->isOpen()) {
            return;
        }
        while (m_socket->hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            SwByteArray datagram = m_socket->receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                break;
            }
            if (!m_options.sourceAddressFilter.isEmpty() &&
                sender != m_options.sourceAddressFilter) {
                continue;
            }
            m_lastPacketTime = std::chrono::steady_clock::now();
            if (m_options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::MpegTs ||
                m_options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::Auto) {
                m_tsDemux.feed(reinterpret_cast<const uint8_t*>(datagram.constData()),
                              datagram.size(),
                              ++m_timestampCounter);
                continue;
            }

            SwVideoPacket::Codec codec = m_options.codec;
            if (codec == SwVideoPacket::Codec::Unknown) {
                codec = (m_options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::AnnexBH265)
                            ? SwVideoPacket::Codec::H265
                            : SwVideoPacket::Codec::H264;
            }
            const bool keyFrame =
                (codec == SwVideoPacket::Codec::H265) ? hasStartCodeHevcIdr_(datagram)
                                                      : hasStartCodeH264Idr_(datagram);
            SwVideoPacket packet(codec,
                                 datagram,
                                 static_cast<std::int64_t>(++m_timestampCounter),
                                 static_cast<std::int64_t>(m_timestampCounter),
                                 keyFrame);
            emitStatus(StreamState::Streaming, "Streaming");
            emitPacket(packet);
        }
    }

    void checkTimeout_() {
        if (!isRunning() || m_lastPacketTime.time_since_epoch().count() == 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPacketTime).count();
        if (elapsed > 3) {
            emitStatus(StreamState::Recovering,
                       SwString("No UDP data received for ") +
                           SwString::number(static_cast<int>(elapsed)) + SwString(" s"));
        }
    }

    SwMediaOpenOptions m_options{};
    SwObject* m_callbackContext{nullptr};
    SwUdpSocket* m_socket{nullptr};
    SwTimer* m_monitorTimer{nullptr};
    SwTsProgramDemux m_tsDemux{};
    std::chrono::steady_clock::time_point m_lastPacketTime{};
    uint32_t m_timestampCounter{0};
};
