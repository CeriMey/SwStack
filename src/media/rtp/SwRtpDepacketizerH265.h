#pragma once

/**
 * @file src/media/rtp/SwRtpDepacketizerH265.h
 * @ingroup media
 * @brief Declares a low-latency RTP/H265 depacketizer emitting Annex-B access units.
 */

#include "media/SwVideoPacket.h"
#include "media/rtp/SwRtpSession.h"
#include "SwDebug.h"

#include <cctype>
#include <functional>
#include <string>
#include <utility>
static constexpr const char* kSwLogCategory_SwRtpDepacketizerH265 = "sw.media.swrtpdepacketizerh265";

class SwRtpDepacketizerH265 {
public:
    using PacketCallback = std::function<void(const SwVideoPacket&)>;

    void setPacketCallback(PacketCallback callback) { m_packetCallback = std::move(callback); }

    bool isWaitingForKeyFrame() const { return m_waitingForKeyFrame; }

    void reset() {
        m_accessUnit.clear();
        m_haveTimestamp = false;
        m_currentTimestamp = 0;
        m_currentKeyFrame = false;
        m_currentRecoveryIdr = false;
        m_waitingForKeyFrame = true;
        m_currentAccessUnitHasHeaders = false;
        m_currentAccessUnitInjectedHeaders = false;
        m_headersInserted = false;
        m_dropCurrentAccessUnit = false;
        m_emitDiscontinuityOnNextFrame = true;
        m_loggedWaitingForKeyFrame = false;
    }

    void setFmtp(const SwString& fmtp) {
        m_vps.clear();
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
            const std::size_t equalPos = part.find('=');
            const std::string value =
                (equalPos == std::string::npos) ? std::string() : part.substr(equalPos + 1);
            if (lower.rfind("sprop-vps=", 0) == 0) {
                m_vps = SwByteArray::fromBase64(SwByteArray(value));
            } else if (lower.rfind("sprop-sps=", 0) == 0) {
                m_sps = SwByteArray::fromBase64(SwByteArray(value));
            } else if (lower.rfind("sprop-pps=", 0) == 0) {
                m_pps = SwByteArray::fromBase64(SwByteArray(value));
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
        m_currentRecoveryIdr = false;
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
        if (!payload || size < 3) {
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

        const uint8_t nalType = static_cast<uint8_t>((payload[0] >> 1) & 0x3F);
        if (nalType <= 47) {
            if (isParameterSet_(nalType)) {
                m_currentAccessUnitHasHeaders = true;
            }
            if (isKeyNal_(nalType) && !m_headersInserted && !m_currentAccessUnitHasHeaders &&
                hasCompleteParameterSetCache_()) {
                appendParameterSets_();
                m_currentAccessUnitInjectedHeaders = true;
            }
            reserveAccessUnitBytes_(4U + size);
            appendStartCodeUnchecked_();
            m_accessUnit.append(reinterpret_cast<const char*>(payload), size);
            if (isKeyNal_(nalType)) {
                m_currentKeyFrame = true;
            }
            if (isRecoveryIdrNal_(nalType)) {
                m_currentRecoveryIdr = true;
            }
        } else if (nalType == 48) {
            size_t offset = 2;
            while (offset + 2 <= size) {
                const uint16_t nalSize = static_cast<uint16_t>(
                    (static_cast<uint16_t>(payload[offset]) << 8) |
                    static_cast<uint16_t>(payload[offset + 1]));
                offset += 2;
                if (offset + nalSize > size) {
                    break;
                }
                const uint8_t innerType = static_cast<uint8_t>((payload[offset] >> 1) & 0x3F);
                if (isParameterSet_(innerType)) {
                    m_currentAccessUnitHasHeaders = true;
                }
                if (isKeyNal_(innerType) && !m_headersInserted && !m_currentAccessUnitHasHeaders &&
                    hasCompleteParameterSetCache_()) {
                    appendParameterSets_();
                    m_currentAccessUnitInjectedHeaders = true;
                }
                reserveAccessUnitBytes_(4U + nalSize);
                appendStartCodeUnchecked_();
                m_accessUnit.append(reinterpret_cast<const char*>(payload + offset), nalSize);
                if (isKeyNal_(innerType)) {
                    m_currentKeyFrame = true;
                }
                if (isRecoveryIdrNal_(innerType)) {
                    m_currentRecoveryIdr = true;
                }
                offset += nalSize;
            }
        } else if (nalType == 49) {
            if (size < 4) {
                return;
            }
            const uint8_t fuHeader = payload[2];
            const bool start = (fuHeader & 0x80) != 0;
            const bool end = (fuHeader & 0x40) != 0;
            const uint8_t fuNalType = fuHeader & 0x3F;
            const size_t fuPayloadSize = size > 3U ? (size - 3U) : 0U;
            if (start) {
                if (isKeyNal_(fuNalType) && !m_headersInserted && !m_currentAccessUnitHasHeaders &&
                    hasCompleteParameterSetCache_()) {
                    appendParameterSets_();
                    m_currentAccessUnitInjectedHeaders = true;
                }
                const uint8_t reconstructed0 =
                    static_cast<uint8_t>((payload[0] & 0x81) | (fuNalType << 1));
                reserveAccessUnitBytes_(4U + 2U + fuPayloadSize);
                appendStartCodeUnchecked_();
                m_accessUnit.append(reinterpret_cast<const char*>(&reconstructed0), 1);
                m_accessUnit.append(reinterpret_cast<const char*>(payload + 1), 1);
                if (isParameterSet_(fuNalType)) {
                    m_currentAccessUnitHasHeaders = true;
                }
                if (isKeyNal_(fuNalType)) {
                    m_currentKeyFrame = true;
                }
                if (isRecoveryIdrNal_(fuNalType)) {
                    m_currentRecoveryIdr = true;
                }
            } else if (fuPayloadSize > 0U) {
                reserveAccessUnitBytes_(fuPayloadSize);
            }
            if (fuPayloadSize > 0U) {
                m_accessUnit.append(reinterpret_cast<const char*>(payload + 3), fuPayloadSize);
            }
            if (end && packet.marker) {
                flushFrame_(packet.timestamp);
                return;
            }
        } else {
            return;
        }

        if (packet.marker) {
            flushFrame_(packet.timestamp);
        }
    }

private:
    static bool isKeyNal_(uint8_t nalType) { return nalType >= 16 && nalType <= 21; }

    static bool isRecoveryIdrNal_(uint8_t nalType) { return nalType == 19 || nalType == 20; }

    static bool isParameterSet_(uint8_t nalType) {
        return nalType == 32 || nalType == 33 || nalType == 34;
    }

    bool hasCompleteParameterSetCache_() const {
        return !m_vps.isEmpty() && !m_sps.isEmpty() && !m_pps.isEmpty();
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
        if (!hasCompleteParameterSetCache_()) {
            return;
        }
        reserveAccessUnitBytes_(parameterSetBytes_());
        if (!m_vps.isEmpty()) {
            appendStartCodeUnchecked_();
            m_accessUnit.append(m_vps);
        }
        if (!m_sps.isEmpty()) {
            appendStartCodeUnchecked_();
            m_accessUnit.append(m_sps);
        }
        if (!m_pps.isEmpty()) {
            appendStartCodeUnchecked_();
            m_accessUnit.append(m_pps);
        }
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
        if (!m_vps.isEmpty()) {
            total += 4U + m_vps.size();
        }
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
        const bool recoveryReady = m_currentRecoveryIdr && carriesHeaders;
        if (m_waitingForKeyFrame && !recoveryReady) {
            if (!m_loggedWaitingForKeyFrame) {
                m_loggedWaitingForKeyFrame = true;
                swCWarning(kSwLogCategory_SwRtpDepacketizerH265)
                    << "[SwRtpDepacketizerH265] Dropping AU while waiting for a recovery IDR with guaranteed headers";
            }
            clearFrameState_();
            return;
        }
        const bool recoveryAccepted = m_waitingForKeyFrame && recoveryReady;
        if (recoveryAccepted) {
            m_waitingForKeyFrame = false;
            m_loggedWaitingForKeyFrame = false;
            swCWarning(kSwLogCategory_SwRtpDepacketizerH265)
                << "[SwRtpDepacketizerH265] Received recovery IDR headers="
                << (m_currentAccessUnitHasHeaders ? "in-band" : "injected");
        }
        if (m_packetCallback) {
            SwVideoPacket packet(SwVideoPacket::Codec::H265,
                                 std::move(m_accessUnit),
                                 static_cast<std::int64_t>(timestamp),
                                 static_cast<std::int64_t>(timestamp),
                                  m_currentKeyFrame);
            if (recoveryAccepted || m_emitDiscontinuityOnNextFrame) {
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
        m_currentRecoveryIdr = false;
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
    SwByteArray m_vps{};
    SwByteArray m_sps{};
    SwByteArray m_pps{};
    SwByteArray m_accessUnit{};
    bool m_haveTimestamp{false};
    uint32_t m_currentTimestamp{0};
    bool m_currentKeyFrame{false};
    bool m_currentRecoveryIdr{false};
    bool m_waitingForKeyFrame{true};
    bool m_loggedWaitingForKeyFrame{false};
    bool m_currentAccessUnitHasHeaders{false};
    bool m_currentAccessUnitInjectedHeaders{false};
    bool m_headersInserted{false};
    bool m_dropCurrentAccessUnit{false};
    bool m_emitDiscontinuityOnNextFrame{true};
};
