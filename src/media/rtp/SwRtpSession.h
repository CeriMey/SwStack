#pragma once

/**
 * @file src/media/rtp/SwRtpSession.h
 * @ingroup media
 * @brief Declares a reusable low-latency RTP/RTCP UDP session helper for media sources.
 */

#include "core/io/SwUdpSocket.h"
#include "core/runtime/SwTimer.h"
#include "core/types/SwByteArray.h"
#include "core/types/SwString.h"
#include "SwDebug.h"
#include "media/rtp/SwRtpJitterBuffer.h"
#include "media/rtp/SwRtpSessionDescriptor.h"
#include "media/rtp/SwRtpStats.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
static constexpr const char* kSwLogCategory_SwRtpSession = "sw.media.swrtpsession";

class SwRtpSession {
public:
    struct Packet {
        SwByteArray payload{};
        SwString senderAddress{};
        uint16_t senderPort{0};
        uint8_t payloadType{0};
        uint16_t sequenceNumber{0};
        uint32_t timestamp{0};
        uint32_t ssrc{0};
        bool marker{false};
    };

    using PacketCallback = std::function<void(const Packet&)>;
    using GapCallback = std::function<void(uint16_t expected, uint16_t actual)>;
    using TimeoutCallback = std::function<void(int secondsWithoutData)>;

    explicit SwRtpSession(const SwRtpSessionDescriptor& descriptor)
        : m_descriptor(descriptor) {
        m_callbackContext = new SwObject();
        m_rtpSocket = new SwUdpSocket();
        m_rtcpSocket = new SwUdpSocket();
        m_monitorTimer = new SwTimer(100);
        m_rtpSocket->setReceiveBufferSize(8 * 1024 * 1024);
        m_rtpSocket->setMaxDatagramSize(64 * 1024);
        m_rtpSocket->setMaxPendingDatagrams(1024);
        m_rtcpSocket->setReceiveBufferSize(1 * 1024 * 1024);
        m_rtcpSocket->setMaxDatagramSize(4096);
        m_rtcpSocket->setMaxPendingDatagrams(128);
        if (m_descriptor.lowLatency) {
            m_jitterBuffer.setLimits(64, 30);
        } else {
            m_jitterBuffer.setLimits(128, 60);
        }

        SwObject::connect(m_rtpSocket, &SwUdpSocket::readyRead, m_callbackContext, [this]() {
            readRtpPackets_();
        });
        SwObject::connect(m_rtcpSocket, &SwUdpSocket::readyRead, m_callbackContext, [this]() {
            readRtcpPackets_();
        });
        SwObject::connect(m_monitorTimer, &SwTimer::timeout, m_callbackContext, [this]() {
            drainBufferedPackets_(true);
            maybeSendReceiverReport_();
            checkTimeout_();
            maybeLogTransportStats_();
        });
    }

    ~SwRtpSession() {
        delete m_callbackContext;
        m_callbackContext = nullptr;
        stop();
        delete m_monitorTimer;
        delete m_rtpSocket;
        delete m_rtcpSocket;
    }

    void setPacketCallback(PacketCallback callback) { m_packetCallback = std::move(callback); }
    void setGapCallback(GapCallback callback) { m_gapCallback = std::move(callback); }
    void setTimeoutCallback(TimeoutCallback callback) { m_timeoutCallback = std::move(callback); }

    bool start() {
        if (m_running.load()) {
            return true;
        }
        resetRuntimeState_();
        if (!bindSockets_()) {
            return false;
        }
        m_running.store(true);
        if (m_monitorTimer && !m_monitorTimer->isActive()) {
            m_monitorTimer->start();
        }
        return true;
    }

    void stop() {
        m_running.store(false);
        if (m_monitorTimer) {
            m_monitorTimer->stop();
        }
        if (m_rtpSocket) {
            m_rtpSocket->close();
        }
        if (m_rtcpSocket) {
            m_rtcpSocket->close();
        }
        resetRuntimeState_();
    }

    bool isRunning() const { return m_running.load(); }

    SwRtpStatsSnapshot stats() const { return m_stats.snapshot(); }

    void sendUdpPunch(const SwString& host, uint16_t rtpPort, uint16_t rtcpPort) {
        if (!m_running.load()) {
            return;
        }
        static const char payload[] = "ping";
        if (m_rtpSocket && rtpPort != 0) {
            m_rtpSocket->writeDatagram(payload, sizeof(payload) - 1, host, rtpPort);
        }
        if (m_rtcpSocket && rtcpPort != 0) {
            m_rtcpSocket->writeDatagram(payload, sizeof(payload) - 1, host, rtcpPort);
        }
    }

    void requestKeyFrame(const SwString& reason = SwString()) {
        if (!m_running.load() || !m_descriptor.allowKeyFrameRequests || m_remoteSsrc == 0 ||
            !m_descriptor.multicastGroup.isEmpty()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastPliTime.time_since_epoch().count() != 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPliTime).count();
            if (elapsed < 1000) {
                return;
            }
        }
        const SwString targetAddress = remoteControlAddress_();
        const uint16_t targetPort = remoteControlPort_();
        if (targetAddress.isEmpty() || targetPort == 0) {
            return;
        }

        uint8_t packet[12] = {0};
        packet[0] = 0x81;
        packet[1] = 206;
        packet[2] = 0x00;
        packet[3] = 0x02;
        const uint32_t mySsrc = localSsrc_();
        packet[4] = static_cast<uint8_t>((mySsrc >> 24) & 0xFF);
        packet[5] = static_cast<uint8_t>((mySsrc >> 16) & 0xFF);
        packet[6] = static_cast<uint8_t>((mySsrc >> 8) & 0xFF);
        packet[7] = static_cast<uint8_t>(mySsrc & 0xFF);
        packet[8] = static_cast<uint8_t>((m_remoteSsrc >> 24) & 0xFF);
        packet[9] = static_cast<uint8_t>((m_remoteSsrc >> 16) & 0xFF);
        packet[10] = static_cast<uint8_t>((m_remoteSsrc >> 8) & 0xFF);
        packet[11] = static_cast<uint8_t>(m_remoteSsrc & 0xFF);
        if (m_rtcpSocket->writeDatagram(reinterpret_cast<const char*>(packet),
                                        12,
                                        targetAddress,
                                        targetPort) > 0) {
            m_lastPliTime = now;
            ++m_stats.m_pliSent;
            swCWarning(kSwLogCategory_SwRtpSession)
                << "[SwRtpSession] Sent RTCP PLI"
                << (reason.isEmpty() ? SwString() : SwString(" (") + reason + SwString(")"))
                << " remote SSRC=0x" << std::hex << m_remoteSsrc << std::dec
                << " port=" << targetPort;
        }
    }

private:
    static SwString wildcardBindAddressForGroup_(const SwString& group) {
        return group.contains(":") ? SwString("::") : SwString("0.0.0.0");
    }

    static bool parseSequenceNumber_(const SwByteArray& datagram, uint16_t& sequenceNumber) {
        if (datagram.size() < 4) {
            return false;
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(datagram.constData());
        sequenceNumber = static_cast<uint16_t>((static_cast<uint16_t>(data[2]) << 8) |
                                               static_cast<uint16_t>(data[3]));
        return true;
    }

    static const char* advanceReasonToString_(SwRtpJitterBuffer::AdvanceReason reason) {
        switch (reason) {
        case SwRtpJitterBuffer::AdvanceReason::AgeLimit:
            return "age";
        case SwRtpJitterBuffer::AdvanceReason::BufferLimit:
            return "size";
        default:
            return "none";
        }
    }

    static bool shouldLogCounter_(uint64_t count) {
        return count <= 4 || (count != 0 && (count & (count - 1)) == 0);
    }

    static int waitBucketMs_(int waitMs) {
        if (waitMs >= 800) return 800;
        if (waitMs >= 400) return 400;
        if (waitMs >= 200) return 200;
        if (waitMs >= 100) return 100;
        if (waitMs >= 50) return 50;
        if (waitMs >= 20) return 20;
        return 0;
    }

    static int gapBucket_(int gap) {
        if (gap >= 256) return 256;
        if (gap >= 128) return 128;
        if (gap >= 64) return 64;
        if (gap >= 32) return 32;
        if (gap >= 16) return 16;
        if (gap >= 8) return 8;
        if (gap >= 4) return 4;
        return 0;
    }

    void resetRuntimeState_() {
        m_jitterBuffer.reset();
        m_detectedRtpSenderAddress.clear();
        m_detectedRtcpSenderAddress.clear();
        m_detectedRtpSenderPort = 0;
        m_detectedRtcpSenderPort = 0;
        m_remoteSsrc = 0;
        m_sentStartupPli = false;
        m_lastRtpTime = {};
        m_lastPliTime = {};
        m_lastReceiverReportTime = {};
        m_lastLoggedTimeoutSec = -1;
        m_lastTransportLogTime = {};
        m_lastTransportSnapshot = {};
        m_lastLoggedRtpSocketRx = 0;
        m_lastLoggedRtpSocketDrops = 0;
        m_lastLoggedRtcpSocketRx = 0;
        m_lastLoggedRtcpSocketDrops = 0;
        m_hasLoggedBlockedSequence = false;
        m_lastBlockedExpectedSequence = 0;
        m_lastBlockedWaitBucketMs = 0;
        m_lastBlockedGapBucket = 0;
    }

    bool bindSockets_() {
        if (!m_rtpSocket || !m_rtcpSocket) {
            return false;
        }
        const SwString bindAddress =
            m_descriptor.multicastGroup.isEmpty()
                ? m_descriptor.bindAddress
                : wildcardBindAddressForGroup_(m_descriptor.multicastGroup);
        const auto bindMode = static_cast<SwUdpSocket::BindMode>(SwUdpSocket::ShareAddress |
                                                                 SwUdpSocket::ReuseAddressHint);
        if (!m_rtpSocket->bind(bindAddress, m_descriptor.localRtpPort, bindMode)) {
            swCError(kSwLogCategory_SwRtpSession)
                << "[SwRtpSession] Failed to bind RTP socket "
                << bindAddress << ":" << m_descriptor.localRtpPort;
            m_rtpSocket->close();
            m_rtcpSocket->close();
            return false;
        }
        if (!m_rtcpSocket->bind(bindAddress, m_descriptor.localRtcpPort, bindMode)) {
            swCError(kSwLogCategory_SwRtpSession)
                << "[SwRtpSession] Failed to bind RTCP socket "
                << bindAddress << ":" << m_descriptor.localRtcpPort;
            m_rtpSocket->close();
            m_rtcpSocket->close();
            return false;
        }
        if (!m_descriptor.multicastGroup.isEmpty()) {
            if (!m_rtpSocket->joinMulticastGroup(m_descriptor.multicastGroup, m_descriptor.bindAddress) ||
                !m_rtcpSocket->joinMulticastGroup(m_descriptor.multicastGroup, m_descriptor.bindAddress)) {
                swCError(kSwLogCategory_SwRtpSession)
                    << "[SwRtpSession] Failed to join multicast group "
                    << m_descriptor.multicastGroup;
                m_rtpSocket->close();
                m_rtcpSocket->close();
                return false;
            }
        }
        swCWarning(kSwLogCategory_SwRtpSession)
            << "[SwRtpSession] Listening on "
            << bindAddress << ":" << m_descriptor.localRtpPort
            << " (rtcp=" << m_descriptor.localRtcpPort
            << ", codec=" << SwMediaOpenOptions::codecToString(m_descriptor.codec)
            << ", pt=" << m_descriptor.payloadType
            << ", clock=" << m_descriptor.clockRate << ")";
        return true;
    }

    void readRtpPackets_() {
        if (!m_running.load() || !m_rtpSocket || !m_rtpSocket->isOpen()) {
            return;
        }
        while (m_rtpSocket->hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            SwByteArray datagram = m_rtpSocket->receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                break;
            }
            if (!acceptsSender_(sender, senderPort, false)) {
                continue;
            }
            if (m_detectedRtpSenderAddress.isEmpty()) {
                m_detectedRtpSenderAddress = sender;
            }
            if (senderPort != 0) {
                m_detectedRtpSenderPort = senderPort;
            }
            ++m_stats.m_receivedDatagrams;
            uint16_t sequenceNumber = 0;
            if (!parseSequenceNumber_(datagram, sequenceNumber)) {
                continue;
            }
            const auto insertResult = m_jitterBuffer.enqueue(sequenceNumber,
                                                             datagram,
                                                             std::chrono::steady_clock::now());
            if (insertResult == SwRtpJitterBuffer::InsertResult::Late) {
                const uint64_t lateCount = ++m_stats.m_latePackets;
                maybeLogLatePacket_(sequenceNumber, lateCount);
                continue;
            }
            if (insertResult == SwRtpJitterBuffer::InsertResult::Duplicate) {
                const uint64_t duplicateCount = ++m_stats.m_duplicatePackets;
                maybeLogDuplicatePacket_(sequenceNumber, duplicateCount);
            } else if (insertResult == SwRtpJitterBuffer::InsertResult::AcceptedOutOfOrder) {
                const uint64_t outOfOrderCount = ++m_stats.m_outOfOrderPackets;
                maybeLogOutOfOrderPacket_(sequenceNumber, outOfOrderCount);
            }
            drainBufferedPackets_(false);
        }
    }

    void drainBufferedPackets_(bool allowGapAdvance) {
        while (m_running.load()) {
            SwRtpJitterBuffer::PopResult result = m_jitterBuffer.popReady(allowGapAdvance);
            if (!result.ready) {
                maybeLogBlockedReorder_();
                break;
            }
            Packet packet;
            if (!parseRtpPacket_(result.datagram, packet)) {
                continue;
            }
            if (result.gapAdvanced) {
                ++m_stats.m_gapEvents;
                if (result.advanceReason == SwRtpJitterBuffer::AdvanceReason::BufferLimit) {
                    ++m_stats.m_gapAdvanceBySize;
                } else if (result.advanceReason == SwRtpJitterBuffer::AdvanceReason::AgeLimit) {
                    ++m_stats.m_gapAdvanceByAge;
                }
                if (m_gapCallback) {
                    m_gapCallback(result.expectedSequence, result.actualSequence);
                }
                swCWarning(kSwLogCategory_SwRtpSession)
                    << "[SwRtpSession] RTP gap advance reason="
                    << advanceReasonToString_(result.advanceReason)
                    << " expected="
                    << result.expectedSequence
                    << " got=" << result.actualSequence
                    << " gap=" << result.gapDistance
                    << " waitMs=" << result.waitAgeMs
                    << " jbQueued=" << result.queuedPackets;
            }
            m_lastRtpTime = std::chrono::steady_clock::now();
            if (m_remoteSsrc == 0 && packet.ssrc != 0) {
                m_remoteSsrc = packet.ssrc;
                if (!m_sentStartupPli) {
                    m_sentStartupPli = true;
                    requestKeyFrame("startup");
                }
            }
            ++m_stats.m_emittedPackets;
            if (m_packetCallback) {
                m_packetCallback(packet);
            }
        }
    }

    void maybeLogLatePacket_(uint16_t sequenceNumber, uint64_t lateCount) {
        if (!shouldLogCounter_(lateCount)) {
            return;
        }
        const auto snapshot = m_jitterBuffer.snapshot();
        swCWarning(kSwLogCategory_SwRtpSession)
            << "[SwRtpSession] RTP late packet dropped"
            << " seq=" << sequenceNumber
            << " expected=" << (snapshot.expectedValid ? snapshot.expectedSequence : 0)
            << " jbQueued=" << snapshot.queuedPackets
            << " lateCount=" << lateCount;
    }

    void maybeLogDuplicatePacket_(uint16_t sequenceNumber, uint64_t duplicateCount) {
        if (!shouldLogCounter_(duplicateCount)) {
            return;
        }
        const auto snapshot = m_jitterBuffer.snapshot();
        swCWarning(kSwLogCategory_SwRtpSession)
            << "[SwRtpSession] RTP duplicate packet"
            << " seq=" << sequenceNumber
            << " expected=" << (snapshot.expectedValid ? snapshot.expectedSequence : 0)
            << " jbQueued=" << snapshot.queuedPackets
            << " duplicateCount=" << duplicateCount;
    }

    void maybeLogOutOfOrderPacket_(uint16_t sequenceNumber, uint64_t outOfOrderCount) {
        const auto snapshot = m_jitterBuffer.snapshot();
        const bool stressed = snapshot.blocked || snapshot.queuedPackets >= 16;
        if (!stressed && !shouldLogCounter_(outOfOrderCount)) {
            return;
        }
        swCWarning(kSwLogCategory_SwRtpSession)
            << "[SwRtpSession] RTP out-of-order packet buffered"
            << " seq=" << sequenceNumber
            << " expected=" << (snapshot.expectedValid ? snapshot.expectedSequence : 0)
            << " jbQueued=" << snapshot.queuedPackets
            << " blockedGap=" << snapshot.blockedGap
            << " blockedWaitMs=" << snapshot.blockedAgeMs
            << " outOfOrderCount=" << outOfOrderCount;
    }

    void maybeLogBlockedReorder_() {
        const auto snapshot = m_jitterBuffer.snapshot();
        if (!snapshot.blocked) {
            m_hasLoggedBlockedSequence = false;
            m_lastBlockedExpectedSequence = 0;
            m_lastBlockedWaitBucketMs = 0;
            m_lastBlockedGapBucket = 0;
            return;
        }

        if (!m_hasLoggedBlockedSequence ||
            snapshot.blockedExpectedSequence != m_lastBlockedExpectedSequence) {
            m_hasLoggedBlockedSequence = true;
            m_lastBlockedExpectedSequence = snapshot.blockedExpectedSequence;
            m_lastBlockedWaitBucketMs = 0;
            m_lastBlockedGapBucket = 0;
        }

        const int waitBucket = waitBucketMs_(snapshot.blockedAgeMs);
        const int gapBucket = gapBucket_(snapshot.blockedGap);
        if (waitBucket <= m_lastBlockedWaitBucketMs &&
            gapBucket <= m_lastBlockedGapBucket) {
            return;
        }

        m_lastBlockedWaitBucketMs = waitBucket > m_lastBlockedWaitBucketMs
                                        ? waitBucket
                                        : m_lastBlockedWaitBucketMs;
        m_lastBlockedGapBucket = gapBucket > m_lastBlockedGapBucket
                                     ? gapBucket
                                     : m_lastBlockedGapBucket;

        swCWarning(kSwLogCategory_SwRtpSession)
            << "[SwRtpSession] RTP reorder waiting for missing sequence"
            << " expected=" << snapshot.blockedExpectedSequence
            << " next=" << snapshot.blockedNextSequence
            << " gap=" << snapshot.blockedGap
            << " waitMs=" << snapshot.blockedAgeMs
            << " jbQueued=" << snapshot.queuedPackets
            << " jbHw=" << snapshot.queueHighWatermark;
    }

    bool parseRtpPacket_(const SwByteArray& datagram, Packet& outPacket) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(datagram.constData());
        const size_t len = datagram.size();
        if (!data || len < 12) {
            return false;
        }
        const uint8_t version = data[0] >> 6;
        if (version != 2) {
            return false;
        }
        const bool padding = (data[0] & 0x20) != 0;
        const bool extension = (data[0] & 0x10) != 0;
        const uint8_t csrcCount = data[0] & 0x0F;
        const bool marker = (data[1] & 0x80) != 0;
        const uint8_t payloadType = data[1] & 0x7F;
        if (m_descriptor.payloadType >= 0 &&
            payloadType != static_cast<uint8_t>(m_descriptor.payloadType)) {
            ++m_stats.m_payloadMismatches;
            return false;
        }
        size_t offset = 12 + static_cast<size_t>(csrcCount) * 4;
        if (offset > len) {
            return false;
        }
        if (extension) {
            if (offset + 4 > len) {
                return false;
            }
            const uint16_t extLength = static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset + 2]) << 8) |
                static_cast<uint16_t>(data[offset + 3]));
            offset += 4 + static_cast<size_t>(extLength) * 4;
        }
        if (offset >= len) {
            return false;
        }
        size_t payloadSize = len - offset;
        if (padding && payloadSize > 0) {
            const uint8_t padCount = data[len - 1];
            if (padCount < payloadSize) {
                payloadSize -= padCount;
            }
        }
        if (payloadSize == 0) {
            return false;
        }
        outPacket.payload = SwByteArray(reinterpret_cast<const char*>(data + offset), payloadSize);
        outPacket.payloadType = payloadType;
        outPacket.marker = marker;
        outPacket.sequenceNumber = static_cast<uint16_t>(
            (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]));
        outPacket.timestamp = (static_cast<uint32_t>(data[4]) << 24) |
                              (static_cast<uint32_t>(data[5]) << 16) |
                              (static_cast<uint32_t>(data[6]) << 8) |
                              static_cast<uint32_t>(data[7]);
        outPacket.ssrc = (static_cast<uint32_t>(data[8]) << 24) |
                         (static_cast<uint32_t>(data[9]) << 16) |
                         (static_cast<uint32_t>(data[10]) << 8) |
                         static_cast<uint32_t>(data[11]);
        outPacket.senderAddress = m_detectedRtpSenderAddress;
        outPacket.senderPort = m_detectedRtpSenderPort;
        return true;
    }

    void readRtcpPackets_() {
        if (!m_running.load() || !m_rtcpSocket || !m_rtcpSocket->isOpen()) {
            return;
        }
        while (m_rtcpSocket->hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            SwByteArray datagram = m_rtcpSocket->receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                break;
            }
            if (!acceptsSender_(sender, senderPort, true)) {
                continue;
            }
            if (m_detectedRtcpSenderAddress.isEmpty()) {
                m_detectedRtcpSenderAddress = sender;
            }
            if (senderPort != 0) {
                m_detectedRtcpSenderPort = senderPort;
            }
            handleRtcpPacket_(datagram);
        }
    }

    void handleRtcpPacket_(const SwByteArray& datagram) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(datagram.constData());
        const size_t len = datagram.size();
        if (!data || len < 8) {
            return;
        }
        const uint8_t version = data[0] >> 6;
        const uint8_t packetType = data[1];
        if (version != 2) {
            return;
        }
        if (packetType == 200 && len >= 28) {
            m_remoteSsrc = (static_cast<uint32_t>(data[4]) << 24) |
                           (static_cast<uint32_t>(data[5]) << 16) |
                           (static_cast<uint32_t>(data[6]) << 8) |
                           static_cast<uint32_t>(data[7]);
            sendReceiverReport_();
        }
    }

    void maybeSendReceiverReport_() {
        if (!m_running.load() || m_remoteSsrc == 0 || !m_descriptor.multicastGroup.isEmpty()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastReceiverReportTime.time_since_epoch().count() != 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastReceiverReportTime).count();
            if (elapsed < 1000) {
                return;
            }
        }
        sendReceiverReport_();
    }

    void sendReceiverReport_() {
        if (!m_descriptor.multicastGroup.isEmpty()) {
            return;
        }
        const SwString targetAddress = remoteControlAddress_();
        const uint16_t targetPort = remoteControlPort_();
        if (targetAddress.isEmpty() || targetPort == 0) {
            return;
        }
        uint8_t packet[8] = {0};
        packet[0] = 0x80;
        packet[1] = 201;
        packet[2] = 0x00;
        packet[3] = 0x01;
        const uint32_t mySsrc = localSsrc_();
        packet[4] = static_cast<uint8_t>((mySsrc >> 24) & 0xFF);
        packet[5] = static_cast<uint8_t>((mySsrc >> 16) & 0xFF);
        packet[6] = static_cast<uint8_t>((mySsrc >> 8) & 0xFF);
        packet[7] = static_cast<uint8_t>(mySsrc & 0xFF);
        if (m_rtcpSocket->writeDatagram(reinterpret_cast<const char*>(packet),
                                        8,
                                        targetAddress,
                                        targetPort) > 0) {
            m_lastReceiverReportTime = std::chrono::steady_clock::now();
            ++m_stats.m_receiverReportsSent;
        }
    }

    void checkTimeout_() {
        if (!m_running.load() || m_lastRtpTime.time_since_epoch().count() == 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = static_cast<int>(
            std::chrono::duration_cast<std::chrono::seconds>(now - m_lastRtpTime).count());
        if (elapsed <= 3) {
            m_lastLoggedTimeoutSec = -1;
            return;
        }
        if (elapsed == m_lastLoggedTimeoutSec) {
            return;
        }
        m_lastLoggedTimeoutSec = elapsed;
        if (m_timeoutCallback) {
            m_timeoutCallback(elapsed);
        }
    }

    void maybeLogTransportStats_() {
        if (!m_running.load()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastTransportLogTime.time_since_epoch().count() != 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTransportLogTime).count();
            if (elapsed < 5000) {
                return;
            }
        }

        const SwRtpJitterBuffer::Snapshot jitterSnapshot = m_jitterBuffer.snapshot();
        m_stats.m_trimmedLatePackets.store(jitterSnapshot.trimmedLatePackets);
        const SwRtpStatsSnapshot snapshot = m_stats.snapshot();
        const uint64_t rtpSocketRx = m_rtpSocket ? m_rtpSocket->totalReceivedDatagrams() : 0;
        const uint64_t rtpSocketDrops = m_rtpSocket ? m_rtpSocket->droppedDatagrams() : 0;
        const uint64_t rtpSocketQueueHw = m_rtpSocket ? m_rtpSocket->queueHighWatermark() : 0;
        const size_t rtpSocketPending = m_rtpSocket ? m_rtpSocket->pendingDatagramCount() : 0;
        const uint64_t rtcpSocketRx = m_rtcpSocket ? m_rtcpSocket->totalReceivedDatagrams() : 0;
        const uint64_t rtcpSocketDrops = m_rtcpSocket ? m_rtcpSocket->droppedDatagrams() : 0;

        swCWarning(kSwLogCategory_SwRtpSession)
            << "[SwRtpSession] Transport stats"
            << " local=" << m_descriptor.localRtpPort
            << " rtpRx=" << rtpSocketRx
            << " (+" << (rtpSocketRx - m_lastLoggedRtpSocketRx) << ")"
            << " emitted=" << snapshot.emittedPackets
            << " (+" << (snapshot.emittedPackets - m_lastTransportSnapshot.emittedPackets) << ")"
            << " ooo=" << snapshot.outOfOrderPackets
            << " (+" << (snapshot.outOfOrderPackets - m_lastTransportSnapshot.outOfOrderPackets) << ")"
            << " dup=" << snapshot.duplicatePackets
            << " (+" << (snapshot.duplicatePackets - m_lastTransportSnapshot.duplicatePackets) << ")"
            << " gaps=" << snapshot.gapEvents
            << " (+" << (snapshot.gapEvents - m_lastTransportSnapshot.gapEvents) << ")"
            << " gapAge=" << snapshot.gapAdvanceByAge
            << " (+" << (snapshot.gapAdvanceByAge - m_lastTransportSnapshot.gapAdvanceByAge) << ")"
            << " gapSize=" << snapshot.gapAdvanceBySize
            << " (+" << (snapshot.gapAdvanceBySize - m_lastTransportSnapshot.gapAdvanceBySize) << ")"
            << " late=" << snapshot.latePackets
            << " (+" << (snapshot.latePackets - m_lastTransportSnapshot.latePackets) << ")"
            << " trimmed=" << snapshot.trimmedLatePackets
            << " (+" << (snapshot.trimmedLatePackets - m_lastTransportSnapshot.trimmedLatePackets) << ")"
            << " pli=" << snapshot.pliSent
            << " (+" << (snapshot.pliSent - m_lastTransportSnapshot.pliSent) << ")"
            << " udpPending=" << rtpSocketPending
            << " udpDrops=" << rtpSocketDrops
            << " (+" << (rtpSocketDrops - m_lastLoggedRtpSocketDrops) << ")"
            << " udpQueueHw=" << rtpSocketQueueHw
            << " jbQueued=" << jitterSnapshot.queuedPackets
            << " jbHw=" << jitterSnapshot.queueHighWatermark
            << " jbBlocked=" << (jitterSnapshot.blocked ? 1 : 0)
            << " jbGap=" << jitterSnapshot.blockedGap
            << " jbWaitMs=" << jitterSnapshot.blockedAgeMs
            << " rtcpRx=" << rtcpSocketRx
            << " (+" << (rtcpSocketRx - m_lastLoggedRtcpSocketRx) << ")"
            << " rtcpDrops=" << rtcpSocketDrops
            << " (+" << (rtcpSocketDrops - m_lastLoggedRtcpSocketDrops) << ")";

        m_lastTransportLogTime = now;
        m_lastTransportSnapshot = snapshot;
        m_lastLoggedRtpSocketRx = rtpSocketRx;
        m_lastLoggedRtpSocketDrops = rtpSocketDrops;
        m_lastLoggedRtcpSocketRx = rtcpSocketRx;
        m_lastLoggedRtcpSocketDrops = rtcpSocketDrops;
    }

    bool acceptsSender_(const SwString& sender, uint16_t senderPort, bool rtcp) const {
        if (!m_descriptor.sourceAddressFilter.isEmpty() &&
            sender != m_descriptor.sourceAddressFilter) {
            return false;
        }
        if (rtcp && m_descriptor.sourceRtcpPort != 0 && senderPort != m_descriptor.sourceRtcpPort) {
            return false;
        }
        return true;
    }

    SwString remoteControlAddress_() const {
        if (!m_descriptor.multicastGroup.isEmpty()) {
            return SwString();
        }
        if (!m_detectedRtcpSenderAddress.isEmpty()) {
            return m_detectedRtcpSenderAddress;
        }
        if (!m_detectedRtpSenderAddress.isEmpty()) {
            return m_detectedRtpSenderAddress;
        }
        return m_descriptor.sourceAddressFilter;
    }

    uint16_t remoteControlPort_() const {
        if (!m_descriptor.multicastGroup.isEmpty()) {
            return 0;
        }
        if (m_detectedRtcpSenderPort != 0) {
            return m_detectedRtcpSenderPort;
        }
        if (m_descriptor.sourceRtcpPort != 0) {
            return m_descriptor.sourceRtcpPort;
        }
        if (m_detectedRtpSenderPort != 0) {
            return static_cast<uint16_t>(m_detectedRtpSenderPort + 1);
        }
        return 0;
    }

    uint32_t localSsrc_() {
        if (m_localSsrc == 0) {
            m_localSsrc = static_cast<uint32_t>(
                std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
            if (m_localSsrc == 0) {
                m_localSsrc = 1;
            }
        }
        return m_localSsrc;
    }

    SwRtpSessionDescriptor m_descriptor{};
    SwObject* m_callbackContext{nullptr};
    SwUdpSocket* m_rtpSocket{nullptr};
    SwUdpSocket* m_rtcpSocket{nullptr};
    SwTimer* m_monitorTimer{nullptr};
    SwRtpJitterBuffer m_jitterBuffer{};
    SwRtpStats m_stats{};
    PacketCallback m_packetCallback{};
    GapCallback m_gapCallback{};
    TimeoutCallback m_timeoutCallback{};
    std::atomic<bool> m_running{false};
    SwString m_detectedRtpSenderAddress{};
    SwString m_detectedRtcpSenderAddress{};
    uint16_t m_detectedRtpSenderPort{0};
    uint16_t m_detectedRtcpSenderPort{0};
    uint32_t m_remoteSsrc{0};
    uint32_t m_localSsrc{0};
    bool m_sentStartupPli{false};
    std::chrono::steady_clock::time_point m_lastRtpTime{};
    std::chrono::steady_clock::time_point m_lastPliTime{};
    std::chrono::steady_clock::time_point m_lastReceiverReportTime{};
    std::chrono::steady_clock::time_point m_lastTransportLogTime{};
    SwRtpStatsSnapshot m_lastTransportSnapshot{};
    uint64_t m_lastLoggedRtpSocketRx{0};
    uint64_t m_lastLoggedRtpSocketDrops{0};
    uint64_t m_lastLoggedRtcpSocketRx{0};
    uint64_t m_lastLoggedRtcpSocketDrops{0};
    int m_lastLoggedTimeoutSec{-1};
    bool m_hasLoggedBlockedSequence{false};
    uint16_t m_lastBlockedExpectedSequence{0};
    int m_lastBlockedWaitBucketMs{0};
    int m_lastBlockedGapBucket{0};
};
