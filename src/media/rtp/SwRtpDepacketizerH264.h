#pragma once

/**
 * @file src/media/rtp/SwRtpDepacketizerH264.h
 * @ingroup media
 * @brief Declares a low-latency RTP/H264 depacketizer emitting Annex-B access units.
 */

#include "media/SwVideoPacket.h"
#include "media/rtp/SwRtpSession.h"
#include "SwDebug.h"

#include <cctype>
#include <functional>
#include <string>
#include <utility>
static constexpr const char* kSwLogCategory_SwRtpDepacketizerH264 = "sw.media.swrtpdepacketizerh264";

class SwRtpDepacketizerH264 {
public:
    using PacketCallback = std::function<void(const SwVideoPacket&)>;

    void setPacketCallback(PacketCallback callback) { m_packetCallback = std::move(callback); }

    bool isWaitingForKeyFrame() const { return m_waitingForKeyFrame; }

    void reset() {
        m_accessUnit.clear();
        m_haveTimestamp = false;
        m_currentTimestamp = 0;
        m_currentKeyFrame = false;
        m_waitingForKeyFrame = true;
        m_currentAccessUnitHasHeaders = false;
        m_currentAccessUnitInjectedHeaders = false;
        m_headersInserted = false;
        m_dropCurrentAccessUnit = false;
        m_emitDiscontinuityOnNextFrame = true;
        m_loggedWaitingForKeyFrame = false;
        m_loggedUnsupportedPacketizationType = false;
    }

    void setFmtp(const SwString& fmtp) {
        m_sps.clear();
        m_pps.clear();
        const std::string fmtpText = fmtp.toStdString();
        std::size_t start = 0;
        while (start <= fmtpText.size()) {
            const std::size_t end = fmtpText.find(';', start);
            std::string part = fmtpText.substr(start,
                                               end == std::string::npos ? std::string::npos
                                                                        : end - start);
            trim_(part);
            const std::string lower = toLower_(part);
            if (lower.rfind("sprop-parameter-sets=", 0) == 0) {
                const std::size_t equalPos = part.find('=');
                const std::string values =
                    (equalPos == std::string::npos) ? std::string() : part.substr(equalPos + 1);
                const std::size_t commaPos = values.find(',');
                if (commaPos == std::string::npos) {
                    m_sps = SwByteArray::fromBase64(SwByteArray(values));
                } else {
                    m_sps = SwByteArray::fromBase64(SwByteArray(values.substr(0, commaPos)));
                    m_pps = SwByteArray::fromBase64(SwByteArray(values.substr(commaPos + 1)));
                }
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    void onSequenceGap(bool forceWaitForKeyFrame = false) {
        const bool hadDecoderSync = !m_waitingForKeyFrame;
        const bool enteringKeyFrameRecovery = forceWaitForKeyFrame || !hadDecoderSync;
        m_accessUnit.clear();
        m_currentKeyFrame = false;
        m_currentAccessUnitHasHeaders = false;
        m_currentAccessUnitInjectedHeaders = false;
        m_dropCurrentAccessUnit = true;
        m_loggedWaitingForKeyFrame = false;
        m_emitDiscontinuityOnNextFrame = true;
        if (enteringKeyFrameRecovery) {
            m_waitingForKeyFrame = true;
            m_headersInserted = false;
        }
    }

    void flush() {
        if (m_haveTimestamp) {
            flushFrame_(m_currentTimestamp);
        }
        m_accessUnit.clear();
        m_haveTimestamp = false;
    }

    void push(const SwRtpSession::Packet& packet) {
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(packet.payload.constData());
        const size_t size = packet.payload.size();
        if (!payload || size == 0) {
            return;
        }
        if (!m_haveTimestamp) {
            m_currentTimestamp = packet.timestamp;
            m_haveTimestamp = true;
        }
        if (packet.timestamp != m_currentTimestamp) {
            flushFrame_(m_currentTimestamp);
            m_currentTimestamp = packet.timestamp;
            m_dropCurrentAccessUnit = false;
        }
        if (m_dropCurrentAccessUnit) {
            if (packet.marker) {
                m_dropCurrentAccessUnit = false;
            }
            return;
        }

        const uint8_t nalType = payload[0] & 0x1F;
        if (nalType >= 1 && nalType <= 23) {
            appendCompleteNal_(payload, size, nalType);
        } else if (nalType == 24 || nalType == 25) {
            appendAggregationPacket_(payload, size, nalType == 25 ? 3U : 1U);
        } else if (nalType == 28 || nalType == 29) {
            const size_t fragmentHeaderSize = nalType == 29 ? 4U : 2U;
            if (size < fragmentHeaderSize) {
                return;
            }
            const uint8_t fuHeader = payload[1];
            const bool start = (fuHeader & 0x80) != 0;
            const bool end = (fuHeader & 0x40) != 0;
            const uint8_t fuNalType = fuHeader & 0x1F;
            const size_t fuPayloadSize = size > fragmentHeaderSize ? (size - fragmentHeaderSize) : 0U;
            if (start) {
                maybeAppendParameterSetsForKeyNal_(fuNalType);
                const uint8_t reconstructed = static_cast<uint8_t>((payload[0] & 0xE0) | fuNalType);
                reserveAccessUnitBytes_(4U + 1U + fuPayloadSize);
                appendStartCodeUnchecked_();
                m_accessUnit.append(reinterpret_cast<const char*>(&reconstructed), 1);
                noteNalType_(fuNalType);
            } else if (fuPayloadSize > 0U) {
                reserveAccessUnitBytes_(fuPayloadSize);
            }
            if (fuPayloadSize > 0U) {
                m_accessUnit.append(reinterpret_cast<const char*>(payload + fragmentHeaderSize),
                                    fuPayloadSize);
            }
            if (end && packet.marker) {
                flushFrame_(packet.timestamp);
                return;
            }
        } else {
            logUnsupportedPacketizationType_(nalType);
            return;
        }

        if (packet.marker) {
            flushFrame_(packet.timestamp);
        }
    }

private:
    static bool isKeyNal_(uint8_t nalType) { return nalType == 5; }

    static bool isParameterSet_(uint8_t nalType) {
        return nalType == 7 || nalType == 8;
    }

    void appendStartCode_() {
        reserveAccessUnitBytes_(4U);
        appendStartCodeUnchecked_();
    }

    void appendStartCodeUnchecked_() {
        static const char kStartCode[] = {0x00, 0x00, 0x00, 0x01};
        m_accessUnit.append(kStartCode, sizeof(kStartCode));
    }

    void appendParameterSets_() {
        reserveAccessUnitBytes_(parameterSetBytes_());
        if (!m_sps.isEmpty()) {
            appendStartCodeUnchecked_();
            m_accessUnit.append(m_sps);
        }
        if (!m_pps.isEmpty()) {
            appendStartCodeUnchecked_();
            m_accessUnit.append(m_pps);
        }
    }

    void maybeAppendParameterSetsForKeyNal_(uint8_t nalType) {
        if (isKeyNal_(nalType) && !m_headersInserted && !m_currentAccessUnitHasHeaders) {
            appendParameterSets_();
            m_currentAccessUnitInjectedHeaders = true;
        }
    }

    void noteNalType_(uint8_t nalType) {
        if (isKeyNal_(nalType)) {
            m_currentKeyFrame = true;
        }
        if (isParameterSet_(nalType)) {
            m_currentAccessUnitHasHeaders = true;
        }
    }

    void appendCompleteNal_(const uint8_t* data, size_t size, uint8_t nalType) {
        maybeAppendParameterSetsForKeyNal_(nalType);
        reserveAccessUnitBytes_(4U + size);
        appendStartCodeUnchecked_();
        m_accessUnit.append(reinterpret_cast<const char*>(data), size);
        noteNalType_(nalType);
    }

    void appendAggregationPacket_(const uint8_t* payload, size_t size, size_t offset) {
        while (offset + 2U <= size) {
            const uint16_t nalSize = static_cast<uint16_t>(
                (static_cast<uint16_t>(payload[offset]) << 8) |
                static_cast<uint16_t>(payload[offset + 1]));
            offset += 2U;
            if (offset + nalSize > size) {
                break;
            }
            const uint8_t nalType = static_cast<uint8_t>(payload[offset] & 0x1FU);
            appendCompleteNal_(payload + offset, nalSize, nalType);
            offset += nalSize;
        }
    }

    void logUnsupportedPacketizationType_(uint8_t nalType) {
        if ((nalType != 26U && nalType != 27U) || m_loggedUnsupportedPacketizationType) {
            return;
        }
        m_loggedUnsupportedPacketizationType = true;
        swCWarning(kSwLogCategory_SwRtpDepacketizerH264)
            << "[SwRtpDepacketizerH264] Unsupported H264 interleaved aggregation type="
            << static_cast<int>(nalType);
    }

    void reserveAccessUnitBytes_(size_t additionalBytes) {
        const size_t requiredSize = m_accessUnit.size() + additionalBytes;
        if (requiredSize <= m_accessUnit.capacity()) {
            return;
        }
        size_t newCapacity = m_accessUnit.capacity();
        if (newCapacity < 1024U) {
            newCapacity = 1024U;
        }
        while (newCapacity < requiredSize) {
            newCapacity *= 2U;
        }
        m_accessUnit.reserve(newCapacity);
    }

    size_t parameterSetBytes_() const {
        size_t total = 0U;
        if (!m_sps.isEmpty()) {
            total += 4U + m_sps.size();
        }
        if (!m_pps.isEmpty()) {
            total += 4U + m_pps.size();
        }
        return total;
    }

    void flushFrame_(uint32_t timestamp) {
        if (m_accessUnit.isEmpty()) {
            return;
        }
        const bool carriesHeaders = m_currentAccessUnitHasHeaders || m_currentAccessUnitInjectedHeaders;
        if (m_waitingForKeyFrame && !m_currentKeyFrame && !carriesHeaders) {
            if (!m_loggedWaitingForKeyFrame) {
                m_loggedWaitingForKeyFrame = true;
                swCWarning(kSwLogCategory_SwRtpDepacketizerH264)
                    << "[SwRtpDepacketizerH264] Dropping non-key frame while waiting for a decodable keyframe";
            }
            clearFrameState_();
            return;
        }
        if (m_waitingForKeyFrame && m_currentKeyFrame) {
            m_waitingForKeyFrame = false;
            m_loggedWaitingForKeyFrame = false;
            swCWarning(kSwLogCategory_SwRtpDepacketizerH264)
                << "[SwRtpDepacketizerH264] Received decodable keyframe";
        }
        if (m_packetCallback) {
            SwVideoPacket packet(SwVideoPacket::Codec::H264,
                                 std::move(m_accessUnit),
                                 static_cast<std::int64_t>(timestamp),
                                 static_cast<std::int64_t>(timestamp),
                                 m_currentKeyFrame);
            if (m_emitDiscontinuityOnNextFrame) {
                packet.setDiscontinuity(true);
                m_emitDiscontinuityOnNextFrame = false;
            }
            m_packetCallback(packet);
        }
        if (carriesHeaders) {
            m_headersInserted = true;
        }
        clearFrameState_();
    }

    void clearFrameState_() {
        m_accessUnit.clear();
        m_currentKeyFrame = false;
        m_currentAccessUnitHasHeaders = false;
        m_currentAccessUnitInjectedHeaders = false;
    }

    static std::string toLower_(std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    static void trim_(std::string& text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
            text.erase(text.begin());
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
            text.pop_back();
        }
    }

    PacketCallback m_packetCallback{};
    SwByteArray m_sps{};
    SwByteArray m_pps{};
    SwByteArray m_accessUnit{};
    bool m_haveTimestamp{false};
    uint32_t m_currentTimestamp{0};
    bool m_currentKeyFrame{false};
    bool m_waitingForKeyFrame{true};
    bool m_loggedWaitingForKeyFrame{false};
    bool m_currentAccessUnitHasHeaders{false};
    bool m_currentAccessUnitInjectedHeaders{false};
    bool m_headersInserted{false};
    bool m_dropCurrentAccessUnit{false};
    bool m_emitDiscontinuityOnNextFrame{true};
    bool m_loggedUnsupportedPacketizationType{false};
};
