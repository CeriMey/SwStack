/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/**
 * @file src/media/SwRtspUdpSource.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwRtspUdpSource in the CoreSw media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the RTSP UDP source interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwRtspUdpSource.
 *
 * Source-oriented declarations here describe how data or media is produced over time, how
 * consumers observe availability, and which lifetime guarantees apply to delivered payloads.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


/***************************************************************************************************
 * Minimal RTSP (RTP over UDP) video source.
 *
 * Focuses on a single video track, assumes H.264 payload, and performs a lightweight depayloader
 * (single NAL, STAP-A, FU-A) to emit SwVideoPacket::H264 packets.
 **************************************************************************************************/

#include "media/SwMediaUrl.h"
#include "media/SwAudioDecoder.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "media/rtp/SwMpegTsDemux.h"
#include "media/rtp/SwRtpDepacketizerH264.h"
#include "media/rtp/SwRtpDepacketizerH265.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwRtpSessionDescriptor.h"
#include "core/io/SwTcpSocket.h"
#include "core/io/SwUdpSocket.h"
#include "core/runtime/SwTimer.h"
#include "core/types/SwByteArray.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>
static constexpr const char* kSwLogCategory_SwRtspUdpSource = "sw.media.swrtspudpsource";


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

class SwRtspUdpSource : public SwVideoSource {
public:
    /**
     * @brief Constructs a `SwRtspUdpSource` instance.
     * @param url Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param url Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwRtspUdpSource(const SwString& url, SwObject* parent = nullptr)
        : m_url(url) {
        SW_UNUSED(parent);
        parseUrl();
        m_callbackContext = new SwObject();
        m_rtspSocket = new SwTcpSocket();
        m_reconnectTimer = new SwTimer(m_reconnectDelayMs);
        m_reconnectTimer->setSingleShot(true);
        SwObject::connect(m_reconnectTimer, &SwTimer::timeout, m_callbackContext, [this]() { attemptReconnect(); });
        m_keepAliveTimer = new SwTimer(15000);
        m_monitorTimer = new SwTimer(100);

        SwObject::connect(m_keepAliveTimer, &SwTimer::timeout, m_callbackContext, [this]() { sendKeepAlive(); });
        SwObject::connect(m_monitorTimer, &SwTimer::timeout, m_callbackContext, [this]() {
            maybeSendRtcpKeepAlive();
            drainBufferedRtpPackets(true);
            checkRtpTimeout();
        });

        SwObject::connect(m_rtspSocket, &SwTcpSocket::connected, m_callbackContext, [this]() {
            m_ctrlBuffer.clear();
            m_cseq = 0;
            m_sessionId.clear();
            m_state = RtspStep::Options;
            sendOptions();
        });
        SwObject::connect(m_rtspSocket, &SwTcpSocket::readyRead, m_callbackContext, [this]() {
            readControlSocket();
            handleControlBuffer();
        });
        SwObject::connect(m_rtspSocket, &SwTcpSocket::disconnected, m_callbackContext, [this]() {
            if (m_rtspDisconnectSuppress.load() > 0) {
                return;
            }
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP disconnected";
            stopStreaming(false);
            scheduleReconnect("RTSP disconnected");
        });
        SwObject::connect(m_rtspSocket, &SwTcpSocket::errorOccurred, m_callbackContext, [this](int code) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP socket error: " << code;
            stopStreaming(false);
            scheduleReconnect("RTSP socket error");
        });

        m_h264Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitRecoveredPacket_(packet);
        });
        m_h265Depacketizer.setPacketCallback([this](const SwVideoPacket& packet) {
            emitRecoveredPacket_(packet);
        });
        m_sharedTsDemux.setPacketCallback([this](const SwVideoPacket& packet) {
            emitRecoveredPacket_(packet);
        });
    }

    /**
     * @brief Performs the `base64Decode` operation.
     * @param input Value passed to the method.
     * @return The requested base64 Decode.
     */
    static std::vector<uint8_t> base64Decode(const std::string& input) {
        static const int8_t table[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
        };
        std::vector<uint8_t> out;
        int val = 0;
        int bits = -8;
        for (unsigned char c : input) {
            if (table[c] == -1) {
                if (c == '=') {
                    break;
                }
                continue;
            }
            val = (val << 6) + table[c];
            bits += 6;
            if (bits >= 0) {
                out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return out;
    }

    /**
     * @brief Performs the `stripFmtpPayloadPrefix` operation.
     * @param fmtp Value passed to the method.
     * @return The requested strip Fmtp Payload Prefix.
     */
    static std::string stripFmtpPayloadPrefix(const std::string& fmtp) {
        auto pos = fmtp.find(' ');
        if (pos == std::string::npos) {
            return fmtp;
        }
        return fmtp.substr(pos + 1);
    }

    /**
     * @brief Performs the `parseH264Fmtp` operation.
     * @param fmtp Value passed to the method.
     */
    void parseH264Fmtp(const std::string& fmtp) {
        m_h264Sps.clear();
        m_h264Pps.clear();
        m_h264Depacketizer.setFmtp(SwString(fmtp));
        if (fmtp.empty()) {
            return;
        }
        auto parts = split(stripFmtpPayloadPrefix(fmtp), ';');
        for (auto& raw : parts) {
            std::string trimmed = raw;
            trim(trimmed);
            std::string entry = toLower(trimmed);
            if (entry.rfind("sprop-parameter-sets=", 0) != 0) {
                continue;
            }
            auto value = trimmed.substr(trimmed.find('=') + 1);
            auto sets = split(value, ',');
            if (!sets.empty()) {
                m_h264Sps = base64Decode(sets[0]);
            }
            if (sets.size() > 1) {
                m_h264Pps = base64Decode(sets[1]);
            }
        }
        if (!m_loggedH264Fmtp.exchange(true)) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Parsed H264 fmtp"
                        << " sps=" << m_h264Sps.size()
                        << " pps=" << m_h264Pps.size();
        }
    }

    /**
     * @brief Performs the `parseH265Fmtp` operation.
     * @param fmtp Value passed to the method.
     */
    void parseH265Fmtp(const std::string& fmtp) {
        m_hevcVps.clear();
        m_hevcSps.clear();
        m_hevcPps.clear();
        m_hevcProfileLevelId.clear();
        m_h265Depacketizer.setFmtp(SwString(fmtp));
        if (fmtp.empty()) {
            return;
        }
        auto parts = split(stripFmtpPayloadPrefix(fmtp), ';');
        for (auto& raw : parts) {
            std::string trimmed = raw;
            trim(trimmed);
            std::string entry = toLower(trimmed);
            if (entry.rfind("profile-tier-level-id=", 0) == 0) {
                std::string hex = entry.substr(std::strlen("profile-tier-level-id="));
                if (hex.size() == 6 || hex.size() == 12) {
                    m_hevcProfileLevelId = hex;
                }
            }
            if (entry.rfind("sprop-vps=", 0) == 0) {
                auto b64 = trimmed.substr(trimmed.find('=') + 1);
                auto data = base64Decode(b64);
                if (!data.empty()) {
                    m_hevcVps = data;
                }
            }
            if (entry.rfind("sprop-sps=", 0) == 0) {
                auto b64 = trimmed.substr(trimmed.find('=') + 1);
                auto data = base64Decode(b64);
                if (!data.empty()) {
                    m_hevcSps = data;
                }
            }
            if (entry.rfind("sprop-pps=", 0) == 0) {
                auto b64 = trimmed.substr(trimmed.find('=') + 1);
                auto data = base64Decode(b64);
                if (!data.empty()) {
                    m_hevcPps = data;
                }
            }
        }
        if ((!m_hevcVps.empty() || !m_hevcSps.empty() || !m_hevcPps.empty()) &&
            !m_loggedH265Fmtp.exchange(true)) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Parsed H265 fmtp"
                        << " vps=" << m_hevcVps.size()
                        << " sps=" << m_hevcSps.size()
                        << " pps=" << m_hevcPps.size();
        }
    }

    /**
     * @brief Destroys the `SwRtspUdpSource` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwRtspUdpSource() override {
        delete m_callbackContext;
        m_callbackContext = nullptr;
        stop();
        delete m_monitorTimer;
        delete m_keepAliveTimer;
        delete m_reconnectTimer;
        delete m_rtspSocket;
        delete m_rtpSocket;
        delete m_rtcpSocket;
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const override { return "SwRtspUdpSource"; }

    /**
     * @brief Returns the current initialize.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool initialize() { return true; }

    /**
     * @brief Sets the local Address.
     * @param addr Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLocalAddress(const SwString& addr) { m_bindAddress = addr; }
    void setEnableAudio(bool enable) { m_enableAudio = enable; }
    void setEnableMetadata(bool enable) { m_enableMetadata = enable; }
    /**
     * @brief Sets the use Tcp Transport.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUseTcpTransport(bool enable) { m_useTcpTransport = enable; }

    /**
     * @brief Performs the `forceLocalBind` operation.
     * @param addr Value passed to the method.
     * @param rtpPort Value passed to the method.
     * @param rtcpPort Value passed to the method.
     */
    void forceLocalBind(const SwString& addr, uint16_t rtpPort, uint16_t rtcpPort) {
        m_bindAddress = addr;
        m_forcedClientRtpPort = rtpPort;
        m_forcedClientRtcpPort = rtcpPort;
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() override {
        m_autoReconnect.store(true);
        m_triedTcpFallback = m_useTcpTransport;
        m_autoTcpFallbackActive = false;
        if (!isRunning()) {
            setRunning(true);
        }
        if (m_monitorTimer && !m_monitorTimer->isActive()) {
            m_monitorTimer->start();
        }
        initiateConnection();
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() override {
        m_autoReconnect.store(false);
        cancelReconnect();
        if (m_monitorTimer) {
            m_monitorTimer->stop();
        }
        if (!isRunning()) {
            stopStreaming(true);
            emitStatus(SwVideoSource::StreamState::Stopped, "Stream stopped");
            return;
        }
        stopStreaming(true);
        emitStatus(SwVideoSource::StreamState::Stopped, "Stream stopped");
    }

private:
    struct RtspRequest {
        std::string method;
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        int retries{0};
    };

    struct RtspTrack {
        std::string mediaType;
        std::string control;
        int payloadType{96};
        int clockRate{90000};
        int channelCount{0};
        std::string codecName;
        std::string fmtpLine;

        bool isVideo() const { return mediaType == "video"; }
        bool isAudio() const { return mediaType == "audio"; }
        bool isMetadata() const { return mediaType == "application"; }
    };

    enum class RtspStep { None, Options, Describe, Setup, Play, Playing };
    enum class SetupTarget { Video, Audio, Metadata };

    struct TransportInfo {
        uint16_t serverRtpPort{0};
        uint16_t serverRtcpPort{0};
        uint8_t interleavedRtpChannel{0};
        uint8_t interleavedRtcpChannel{1};
    };

    struct BufferedRtpPacket {
        SwByteArray datagram;
        std::chrono::steady_clock::time_point arrivalTime{};
    };

    void parseUrl() {
        const SwMediaUrl parsed = SwMediaUrl::parse(m_url);
        m_host = parsed.host();
        m_port = parsed.port() > 0 ? parsed.port() : 554;
        m_path = parsed.pathWithQuery();
        if (m_path.isEmpty()) {
            m_path = "/";
        }
        m_baseUrl = "rtsp://" + m_host.toStdString();
        if (m_port != 554) {
            m_baseUrl += ":" + std::to_string(m_port);
        }
    }

    void resetStreamState() {
        m_ctrlBuffer.clear();
        m_sdp.clear();
        m_tracks.clear();
        setTracks(SwList<SwMediaTrack>());
        m_selectedTrackIndex = -1;
        m_selectedAudioTrackIndex = -1;
        m_selectedMetadataTrackIndex = -1;
        m_trackControl.clear();
        m_audioTrackControl.clear();
        m_metadataTrackControl.clear();
        m_sessionControl.clear();
        m_sessionId.clear();
        m_state = RtspStep::None;
        m_cseq = 0;
        m_serverRtpPort = 0;
        m_clientRtpPort = 0;
        m_clientRtcpPort = 0;
        m_audioClientRtpPort = 0;
        m_audioClientRtcpPort = 0;
        m_audioServerRtpPort = 0;
        m_audioServerRtcpPort = 0;
        m_metadataClientRtpPort = 0;
        m_metadataClientRtcpPort = 0;
        m_metadataServerRtpPort = 0;
        m_metadataServerRtcpPort = 0;
        m_payloadType = 96;
        m_clockRate = 90000;
        m_audioPayloadType = -1;
        m_audioClockRate = 0;
        m_audioChannelCount = 0;
        m_metadataPayloadType = -1;
        m_metadataClockRate = 0;
        m_metadataCodecName.clear();
        m_triedAggregatePlay.store(false);
        m_haveTimestamp = false;
        m_haveFirstTimestamp = false;
        m_haveSequence = false;
        m_firstTimestamp = 0;
        m_currentKeyFrame = false;
        m_framesDropped.store(0);
        m_loggedFirstCompressedFrame.store(false);
        m_currentCodec = SwVideoPacket::Codec::H264;
        m_nalBuffer.clear();
        m_h264Sps.clear();
        m_h264Pps.clear();
        m_h264HeadersInserted = false;
        m_currentAccessUnitHasH264Headers = false;
        m_currentAccessUnitInjectedH264Headers = false;
        m_currentAccessUnitHasHevcHeaders = false;
        m_currentAccessUnitInjectedHevcHeaders = false;
        m_dropCurrentAccessUnit = false;
        m_emitDecoderDiscontinuityOnNextFrame = true;
        m_loggedH264Fmtp.store(false);
        m_loggedH264ParameterSetInjection.store(false);
        m_loggedMissingH264ParameterSets.store(false);
        m_loggedInBandH264ParameterSets.store(false);
        m_loggedH265Fmtp.store(false);
        m_loggedH265ParameterSetInjection.store(false);
        m_loggedMissingH265ParameterSets.store(false);
        m_loggedInBandH265ParameterSets.store(false);
        m_waitingForKeyFrame = true;
        m_loggedWaitingForKeyFrame.store(false);
        m_h264Depacketizer.reset();
        m_h265Depacketizer.reset();
        m_sharedTsDemux.reset();
        m_tsDemux.reset();
        m_hevcVps.clear();
        m_hevcSps.clear();
        m_hevcPps.clear();
        m_hevcHeadersInserted = false;
        m_hevcProfileLevelId.clear();
        if (m_udpSession) {
            m_udpSession->stop();
            m_udpSession.reset();
        }
        if (m_audioUdpSession) {
            m_audioUdpSession->stop();
            m_audioUdpSession.reset();
        }
        if (m_metadataUdpSession) {
            m_metadataUdpSession->stop();
            m_metadataUdpSession.reset();
        }
        if (m_rtpSocket) {
            m_rtpSocket->close();
        }
        if (m_rtcpSocket) {
            m_rtcpSocket->close();
        }
        closeRtspSocket();
        if (m_keepAliveTimer) {
            m_keepAliveTimer->stop();
        }
        m_localSsrc = generateSsrc();
        m_remoteSsrc = 0;
        m_detectedRtpPort = 0;
        m_detectedRtcpPort = 0;
        m_interleavedRtpChannel = 0;
        m_interleavedRtcpChannel = 1;
        m_audioInterleavedRtpChannel = 2;
        m_audioInterleavedRtcpChannel = 3;
        m_metadataInterleavedRtpChannel = 4;
        m_metadataInterleavedRtcpChannel = 5;
        m_setupTargets.clear();
        m_setupTargetIndex = 0;
        m_lastRtpTime = std::chrono::steady_clock::time_point{};
        m_lastPlaceholderTime = std::chrono::steady_clock::time_point{};
        m_lastPliTime = std::chrono::steady_clock::time_point{};
        m_lastRtcpKeepAliveTime = std::chrono::steady_clock::time_point{};
        m_lastLoggedRtpTimeoutSec.store(-1);
        m_rtpReorderBuffer.clear();
        m_rtpReorderExpectedValid = false;
    }

    void stopStreaming(bool hardStop) {
        if (m_keepAliveTimer) {
            m_keepAliveTimer->stop();
        }
        if (m_udpSession) {
            m_udpSession->stop();
            m_udpSession.reset();
        }
        if (m_audioUdpSession) {
            m_audioUdpSession->stop();
            m_audioUdpSession.reset();
        }
        if (m_metadataUdpSession) {
            m_metadataUdpSession->stop();
            m_metadataUdpSession.reset();
        }
        closeRtspSocket();
        if (m_rtpSocket) {
            m_rtpSocket->close();
        }
        if (m_rtcpSocket) {
            m_rtcpSocket->close();
        }
        m_state = RtspStep::None;
        m_haveTimestamp = false;
        m_nalBuffer.clear();
        m_currentKeyFrame = false;
        m_currentAccessUnitHasH264Headers = false;
        m_currentAccessUnitInjectedH264Headers = false;
        m_currentAccessUnitHasHevcHeaders = false;
        m_currentAccessUnitInjectedHevcHeaders = false;
        m_dropCurrentAccessUnit = false;
        m_emitDecoderDiscontinuityOnNextFrame = true;
        m_waitingForKeyFrame = true;
        m_loggedWaitingForKeyFrame.store(false);
        m_h264Depacketizer.reset();
        m_h265Depacketizer.reset();
        m_sharedTsDemux.reset();
        m_tsDemux.reset();
        m_rtpReorderBuffer.clear();
        m_rtpReorderExpectedValid = false;
        if (hardStop) {
            setRunning(false);
        }
    }

    void readControlSocket() {
        if (!m_rtspSocket || !m_rtspSocket->isOpen()) {
            return;
        }
        for (;;) {
            SwString chunk = m_rtspSocket->read(4096);
            if (chunk.isEmpty()) {
                break;
            }
            m_ctrlAccum.append(chunk.toStdString());
        }
    }

    void handleControlBuffer() {
        for (;;) {
            if (m_ctrlAccum.empty()) {
                return;
            }
            if (m_ctrlAccum[0] == '$') {
                if (m_ctrlAccum.size() < 4) {
                    return;
                }
                uint8_t channel = static_cast<uint8_t>(m_ctrlAccum[1]);
                uint16_t len = static_cast<uint16_t>((static_cast<uint8_t>(m_ctrlAccum[2]) << 8) |
                                                     static_cast<uint8_t>(m_ctrlAccum[3]));
                if (m_ctrlAccum.size() < 4 + len) {
                    return;
                }
                const uint8_t* interleavedData =
                    reinterpret_cast<const uint8_t*>(m_ctrlAccum.data() + 4);
                if (channel == m_interleavedRtpChannel) {
                    if (handleRtpPacket(interleavedData, len)) {
                        m_lastRtpTime = std::chrono::steady_clock::now();
                    }
                } else if (channel == m_interleavedRtcpChannel) {
                    handleRtcpPacket(interleavedData, len);
                } else if (channel == m_audioInterleavedRtpChannel) {
                    SwRtpSession::Packet packet;
                    if (parseAuxiliaryRtpPacket_(interleavedData, len, m_audioPayloadType, packet)) {
                        handleAudioUdpSessionPacket_(packet);
                    }
                } else if (channel == m_metadataInterleavedRtpChannel) {
                    SwRtpSession::Packet packet;
                    if (parseAuxiliaryRtpPacket_(interleavedData, len, m_metadataPayloadType, packet)) {
                        handleMetadataUdpSessionPacket_(packet);
                    }
                }
                m_ctrlAccum.erase(0, 4 + len);
                continue;
            }
            auto dollar = m_ctrlAccum.find('$');
            std::string text = (dollar == std::string::npos) ? m_ctrlAccum : m_ctrlAccum.substr(0, dollar);
            m_ctrlAccum.erase(0, text.size());
            m_ctrlTextBuffer.append(text);
            while (true) {
                auto headerEnd = m_ctrlTextBuffer.find("\r\n\r\n");
                if (headerEnd == std::string::npos) {
                    break;
                }
                std::string header = m_ctrlTextBuffer.substr(0, headerEnd);
                int contentLength = parseContentLength(header);
                if (m_ctrlTextBuffer.size() < headerEnd + 4 + static_cast<size_t>(contentLength)) {
                    break;
                }
                std::string body = m_ctrlTextBuffer.substr(headerEnd + 4, static_cast<size_t>(contentLength));
                m_ctrlTextBuffer.erase(0, headerEnd + 4 + static_cast<size_t>(contentLength));
                handleResponse(header, body);
            }
        }
    }

    void handleResponse(const std::string& header, const std::string& body) {
        int status = parseStatusCode(header);
        int cseq = parseCSeq(header);
        auto pendingIt = m_pendingRequests.find(cseq);
        std::string method = (pendingIt != m_pendingRequests.end()) ? pendingIt->second.method : std::string();
        logRtspResponse(method, cseq, status, header, body);
        if (status != 200) {
            bool shouldRetry = (status >= 500);
            if (shouldRetry && pendingIt != m_pendingRequests.end()) {
                RtspRequest req = pendingIt->second;
                m_pendingRequests.erase(pendingIt);
                if (req.retries < m_maxRequestRetries) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Retrying " << req.method << " after status " << status;
                    resendRequest(req);
                    return;
                }
            }
            if (status == 460 && m_state == RtspStep::Play && !m_triedAggregatePlay.exchange(true)) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP 460, retrying PLAY with aggregate control";
                sendPlay(true);
                return;
            }
            if (status == 461 && method == "SETUP" && m_useTcpTransport && m_autoTcpFallbackActive) {
                revertAutoTcpFallback("TCP SETUP rejected by server");
                return;
            }
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP error status: " << status;
            return;
        }
        if (pendingIt != m_pendingRequests.end()) {
            m_pendingRequests.erase(pendingIt);
        }
        if (m_state == RtspStep::Options) {
            m_state = RtspStep::Describe;
            sendDescribe();
            return;
        }
        if (m_state == RtspStep::Describe) {
            m_sdp = body;
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] SDP:\n" << body;
            if (!parseSdp(body)) {
                swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to parse SDP";
                return;
            }
            if (!allocateClientPorts()) {
                swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to allocate RTP/RTCP ports";
                return;
            }
            if (m_selectedAudioTrackIndex >= 0 && !allocateAudioClientPorts()) {
                swCWarning(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Failed to allocate audio RTP/RTCP ports, disabling audio track";
                m_selectedAudioTrackIndex = -1;
                m_audioTrackControl.clear();
                m_audioCodecName.clear();
                m_audioPayloadType = -1;
                m_audioClockRate = 0;
                m_audioChannelCount = 0;
                publishTracks_();
            }
            if (m_selectedMetadataTrackIndex >= 0 && !allocateMetadataClientPorts()) {
                swCWarning(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Failed to allocate metadata RTP/RTCP ports, disabling metadata track";
                m_selectedMetadataTrackIndex = -1;
                m_metadataTrackControl.clear();
                m_metadataCodecName.clear();
                m_metadataPayloadType = -1;
                m_metadataClockRate = 0;
                publishTracks_();
            }
            m_setupTargets.clear();
            m_setupTargets.push_back(SetupTarget::Video);
            if (m_selectedAudioTrackIndex >= 0 && !m_audioTrackControl.empty()) {
                m_setupTargets.push_back(SetupTarget::Audio);
            }
            if (m_selectedMetadataTrackIndex >= 0 && !m_metadataTrackControl.empty()) {
                m_setupTargets.push_back(SetupTarget::Metadata);
            }
            m_setupTargetIndex = 0;
            m_state = RtspStep::Setup;
            sendSetup();
            return;
        }
        if (m_state == RtspStep::Setup) {
            TransportInfo transportInfo{};
            parseSession(header);
            parseTransportInfo(header, transportInfo);
            const SetupTarget setupTarget = currentSetupTarget_();
            auto setupTargetName = [setupTarget]() -> const char* {
                switch (setupTarget) {
                case SetupTarget::Audio:
                    return "audio";
                case SetupTarget::Metadata:
                    return "metadata";
                case SetupTarget::Video:
                default:
                    return "video";
                }
            };
            if (m_sessionId.empty() || (!m_useTcpTransport && transportInfo.serverRtpPort == 0)) {
                swCError(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Missing session or server port after "
                    << setupTargetName() << " SETUP";
                return;
            }
            if (setupTarget == SetupTarget::Audio) {
                m_audioServerRtpPort = transportInfo.serverRtpPort;
                m_audioServerRtcpPort = transportInfo.serverRtcpPort;
                m_audioInterleavedRtpChannel = transportInfo.interleavedRtpChannel;
                m_audioInterleavedRtcpChannel = transportInfo.interleavedRtcpChannel;
                configureAudioUdpSession_();
                swCDebug(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Audio SETUP ok client_port=" << m_audioClientRtpPort
                    << "-" << m_audioClientRtcpPort
                    << " server_port=" << m_audioServerRtpPort
                    << "-" << m_audioServerRtcpPort
                    << " payload=" << m_audioPayloadType;
            } else if (setupTarget == SetupTarget::Metadata) {
                m_metadataServerRtpPort = transportInfo.serverRtpPort;
                m_metadataServerRtcpPort = transportInfo.serverRtcpPort;
                m_metadataInterleavedRtpChannel = transportInfo.interleavedRtpChannel;
                m_metadataInterleavedRtcpChannel = transportInfo.interleavedRtcpChannel;
                configureMetadataUdpSession_();
                swCDebug(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Metadata SETUP ok client_port=" << m_metadataClientRtpPort
                    << "-" << m_metadataClientRtcpPort
                    << " server_port=" << m_metadataServerRtpPort
                    << "-" << m_metadataServerRtcpPort
                    << " payload=" << m_metadataPayloadType;
            } else {
                m_serverRtpPort = transportInfo.serverRtpPort;
                m_serverRtcpPort = transportInfo.serverRtcpPort;
                m_interleavedRtpChannel = transportInfo.interleavedRtpChannel;
                m_interleavedRtcpChannel = transportInfo.interleavedRtcpChannel;
                configureUdpSockets();
                swCDebug(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Video SETUP ok client_port=" << m_clientRtpPort
                    << "-" << m_clientRtcpPort
                    << " server_port=" << m_serverRtpPort
                    << "-" << m_serverRtcpPort
                    << " payload=" << m_payloadType;
            }
            ++m_setupTargetIndex;
            if (m_setupTargetIndex < m_setupTargets.size()) {
                sendSetup();
                return;
            }
            m_setupTargetIndex = 0;
            m_state = RtspStep::Play;
            m_triedAggregatePlay.store(false);
            sendPlay(false);
            return;
        }
        if (m_state == RtspStep::Play) {
            m_state = RtspStep::Playing;
            m_autoTcpFallbackActive = false;
            if (m_keepAliveTimer && !m_keepAliveTimer->isActive()) {
                m_keepAliveTimer->start();
            }
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Streaming started (PT=" << m_payloadType
                      << ", client_port=" << m_clientRtpPort << ", server_port=" << m_serverRtpPort << ")";
            return;
        }
    }

    int parseStatusCode(const std::string& header) const {
        auto firstSpace = header.find(' ');
        if (firstSpace == std::string::npos) {
            return -1;
        }
        auto secondSpace = header.find(' ', firstSpace + 1);
        if (secondSpace == std::string::npos) {
            return -1;
        }
        return std::atoi(header.substr(firstSpace + 1, secondSpace - firstSpace - 1).c_str());
    }

    int parseContentLength(const std::string& header) const {
        std::istringstream iss(header);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.size() > 0 && line.back() == '\r') {
                line.pop_back();
            }
            std::string lower = toLower(line);
            const std::string key = "content-length:";
            if (lower.find(key) == 0) {
                return std::atoi(line.substr(key.size()).c_str());
            }
        }
        return 0;
    }

    int parseCSeq(const std::string& header) const {
        std::istringstream iss(header);
        std::string line;
        const std::string key = "cseq:";
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::string lower = toLower(line);
            if (lower.find(key) == 0) {
                return std::atoi(line.substr(key.size()).c_str());
            }
        }
        return -1;
    }

    void logRtspResponse(const std::string& method, int cseq, int status,
                         const std::string& header, const std::string& body) const {
        if (body.empty()) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] <<< (CSeq " << cseq
                      << ", method=" << (method.empty() ? "?" : method)
                      << ", status=" << status << ")\n"
                      << header;
            return;
        }

        swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] <<< (CSeq " << cseq
                  << ", method=" << (method.empty() ? "?" : method)
                  << ", status=" << status << ")\n"
                  << header
                  << "\n--BODY--\n" << body;
    }

    void resendRequest(const RtspRequest& req) {
        sendRtsp(req.method, req.url, req.headers, req.body, req.retries + 1);
    }

    void parseSession(const std::string& header) {
        std::istringstream iss(header);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.size() > 0 && line.back() == '\r') {
                line.pop_back();
            }
            std::string lower = toLower(line);
            const std::string key = "session:";
            if (lower.find(key) == 0) {
                auto semicolon = line.find(';');
                m_sessionId = (semicolon == std::string::npos) ? line.substr(key.size()) : line.substr(key.size(), semicolon - key.size());
                trim(m_sessionId);
                break;
            }
        }
    }

    void parseTransportInfo(const std::string& header, TransportInfo& info) {
        std::istringstream iss(header);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.size() > 0 && line.back() == '\r') {
                line.pop_back();
            }
            std::string lower = toLower(line);
            const std::string key = "transport:";
            if (lower.find(key) == 0) {
                auto pos = lower.find("server_port=");
                if (pos != std::string::npos) {
                    int rtp = 0;
                    int rtcp = 0;
#if defined(_MSC_VER)
                    ::sscanf_s(lower.c_str() + pos, "server_port=%d-%d", &rtp, &rtcp);
#else
                    std::sscanf(lower.c_str() + pos, "server_port=%d-%d", &rtp, &rtcp);
#endif
                    info.serverRtpPort = static_cast<uint16_t>(rtp);
                    info.serverRtcpPort = static_cast<uint16_t>(rtcp);
                }
                pos = lower.find("interleaved=");
                if (pos != std::string::npos) {
                    int rtp = 0;
                    int rtcp = 1;
#if defined(_MSC_VER)
                    ::sscanf_s(lower.c_str() + pos, "interleaved=%d-%d", &rtp, &rtcp);
#else
                    std::sscanf(lower.c_str() + pos, "interleaved=%d-%d", &rtp, &rtcp);
#endif
                    info.interleavedRtpChannel = static_cast<uint8_t>(rtp);
                    info.interleavedRtcpChannel = static_cast<uint8_t>(rtcp);
                }
                break;
            }
        }
    }

    bool parseSdp(const std::string& body) {
        std::istringstream iss(body);
        std::string line;
        std::string sessionControl;
        RtspTrack currentTrack;
        bool haveTrack = false;
        m_tracks.clear();

        auto finalizeTrack = [this, &currentTrack, &haveTrack]() {
            if (!haveTrack) {
                return;
            }
            currentTrack.mediaType = toLower(currentTrack.mediaType);
            currentTrack.codecName = toLower(currentTrack.codecName);
            if (currentTrack.codecName.empty() && currentTrack.payloadType == 33) {
                currentTrack.codecName = "mp2t";
                currentTrack.clockRate = 90000;
            }
            m_tracks.push_back(currentTrack);
            currentTrack = RtspTrack{};
            haveTrack = false;
        };

        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!haveTrack && line.rfind("a=control:", 0) == 0) {
                sessionControl = line.substr(std::strlen("a=control:"));
                continue;
            }
            if (line.rfind("m=", 0) == 0) {
                finalizeTrack();
                haveTrack = true;
                auto parts = split(line, ' ');
                if (!parts.empty() && parts[0].size() > 2) {
                    currentTrack.mediaType = parts[0].substr(2);
                }
                if (parts.size() >= 4) {
                    currentTrack.payloadType = std::atoi(parts[3].c_str());
                }
                if (currentTrack.payloadType == 33) {
                    currentTrack.clockRate = 90000;
                }
                continue;
            }
            if (!haveTrack) {
                continue;
            }
            if (line.rfind("a=control:", 0) == 0) {
                currentTrack.control = line.substr(std::strlen("a=control:"));
                continue;
            }
            if (line.rfind("a=rtpmap:", 0) == 0) {
                auto pos = line.find(' ');
                if (pos != std::string::npos) {
                    int payload = std::atoi(line.substr(std::strlen("a=rtpmap:"), pos - std::strlen("a=rtpmap:")).c_str());
                    auto codecPart = line.substr(pos + 1);
                    auto slash = codecPart.find('/');
                    if (slash != std::string::npos) {
                        if (payload == currentTrack.payloadType || currentTrack.codecName.empty()) {
                            currentTrack.payloadType = payload;
                            currentTrack.codecName = codecPart.substr(0, slash);
                            auto remaining = codecPart.substr(slash + 1);
                            auto nextSlash = remaining.find('/');
                            currentTrack.clockRate = std::atoi(remaining.substr(0, nextSlash).c_str());
                            if (nextSlash != std::string::npos) {
                                currentTrack.channelCount =
                                    std::atoi(remaining.substr(nextSlash + 1).c_str());
                            }
                        }
                    }
                }
                continue;
            }
            if (line.rfind("a=fmtp:", 0) == 0) {
                std::string raw = line.substr(std::strlen("a=fmtp:"));
                auto pos = raw.find(' ');
                int payload = currentTrack.payloadType;
                std::string fmtpBody = raw;
                if (pos != std::string::npos) {
                    payload = std::atoi(raw.substr(0, pos).c_str());
                    fmtpBody = raw.substr(pos + 1);
                }
                if (payload == currentTrack.payloadType) {
                    currentTrack.fmtpLine = fmtpBody;
                }
                continue;
            }
        }
        finalizeTrack();
        if (m_tracks.empty()) {
            return false;
        }
        publishTracks_();

        auto trackScore = [this](const RtspTrack& track) -> int {
            if (!track.isVideo()) {
                return -1;
            }
            std::string codec = toLower(track.codecName);
            if (codec == "h264") {
                return 300;
            }
            if (isHevcCodecName(codec)) {
                return 250;
            }
            if (codec == "mp2t" || codec == "mpegts" || track.payloadType == 33) {
                return 200;
            }
            return -1;
        };
        auto audioTrackScore = [this](const RtspTrack& track) -> int {
            if (!track.isAudio()) {
                return -1;
            }
            const std::string codec = toLower(track.codecName);
            if (!isAudioCodecSupported_(codec)) {
                return -1;
            }
            if (codec == "pcmu" || codec == "pcma") {
                return 300;
            }
            if (codec == "opus") {
                return 250;
            }
            if (codec == "mpeg4-generic" || codec == "mp4a-latm" || codec == "aac") {
                return 200;
            }
            return 50;
        };
        auto metadataTrackScore = [](const RtspTrack& track) -> int {
            if (!track.isMetadata()) {
                return -1;
            }
            const std::string codec = toLower(track.codecName);
            if (codec == "smpte336m" || codec == "klv") {
                return 300;
            }
            return 100;
        };

        m_selectedTrackIndex = -1;
        m_selectedAudioTrackIndex = -1;
        m_selectedMetadataTrackIndex = -1;
        int bestScore = -1;
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            int score = trackScore(m_tracks[i]);
            if (score > bestScore) {
                bestScore = score;
                m_selectedTrackIndex = static_cast<int>(i);
            }
        }
        if (m_enableAudio) {
            bestScore = -1;
            for (std::size_t i = 0; i < m_tracks.size(); ++i) {
                int score = audioTrackScore(m_tracks[i]);
                if (score > bestScore) {
                    bestScore = score;
                    m_selectedAudioTrackIndex = static_cast<int>(i);
                }
            }
        }
        if (m_enableMetadata) {
            bestScore = -1;
            for (std::size_t i = 0; i < m_tracks.size(); ++i) {
                int score = metadataTrackScore(m_tracks[i]);
                if (score > bestScore) {
                    bestScore = score;
                    m_selectedMetadataTrackIndex = static_cast<int>(i);
                }
            }
        }
        if (m_selectedTrackIndex < 0) {
            return false;
        }

        const RtspTrack& selectedTrack = m_tracks[static_cast<std::size_t>(m_selectedTrackIndex)];
        m_trackControl = selectedTrack.control;
        if (!sessionControl.empty()) {
            m_sessionControl = sessionControl;
        }
        m_payloadType = selectedTrack.payloadType;
        m_clockRate = (selectedTrack.clockRate > 0) ? selectedTrack.clockRate : 90000;
        m_codecName = toLower(selectedTrack.codecName.empty()
                                  ? (selectedTrack.payloadType == 33 ? std::string("mp2t") : std::string())
                                  : selectedTrack.codecName);
        if (m_codecName.empty()) {
            return false;
        }
        if (m_selectedAudioTrackIndex >= 0 &&
            static_cast<std::size_t>(m_selectedAudioTrackIndex) < m_tracks.size()) {
            const RtspTrack& audioTrack = m_tracks[static_cast<std::size_t>(m_selectedAudioTrackIndex)];
            m_audioTrackControl = audioTrack.control;
            m_audioPayloadType = audioTrack.payloadType;
            m_audioClockRate = audioTrack.clockRate;
            m_audioCodecName = toLower(audioTrack.codecName);
            m_audioChannelCount = audioTrack.channelCount > 0 ? audioTrack.channelCount : 1;
        } else {
            m_audioTrackControl.clear();
            m_audioPayloadType = -1;
            m_audioClockRate = 0;
            m_audioCodecName.clear();
            m_audioChannelCount = 0;
        }
        if (m_selectedMetadataTrackIndex >= 0 &&
            static_cast<std::size_t>(m_selectedMetadataTrackIndex) < m_tracks.size()) {
            const RtspTrack& metadataTrack =
                m_tracks[static_cast<std::size_t>(m_selectedMetadataTrackIndex)];
            m_metadataTrackControl = metadataTrack.control;
            m_metadataPayloadType = metadataTrack.payloadType;
            m_metadataClockRate = metadataTrack.clockRate;
            m_metadataCodecName = toLower(metadataTrack.codecName);
        } else {
            m_metadataTrackControl.clear();
            m_metadataPayloadType = -1;
            m_metadataClockRate = 0;
            m_metadataCodecName.clear();
        }
        if (isHevcCodecName(m_codecName)) {
            m_currentCodec = SwVideoPacket::Codec::H265;
        } else {
            m_currentCodec = SwVideoPacket::Codec::H264;
        }
        if (m_codecName == "h264") {
            parseH264Fmtp(selectedTrack.fmtpLine);
        } else if (isHevcCodec()) {
            parseH265Fmtp(selectedTrack.fmtpLine);
        }
        swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Selected SDP video track index="
                    << m_selectedTrackIndex
                    << " codec=" << m_codecName
                    << " payload=" << m_payloadType << " clock=" << m_clockRate
                    << " control=" << m_trackControl
                    << " tracks=" << m_tracks.size()
                    << " transport=" << (m_useTcpTransport ? "tcp" : "udp");
        if (!m_audioCodecName.empty()) {
            swCWarning(kSwLogCategory_SwRtspUdpSource)
                << "[SwRtspUdpSource] Selected SDP audio track index=" << m_selectedAudioTrackIndex
                << " codec=" << m_audioCodecName
                << " payload=" << m_audioPayloadType
                << " clock=" << m_audioClockRate
                << " control=" << m_audioTrackControl;
        } else {
            for (std::size_t i = 0; i < m_tracks.size(); ++i) {
                const auto& track = m_tracks[i];
                if (!track.isAudio()) {
                    continue;
                }
                if (!m_enableAudio) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource)
                        << "[SwRtspUdpSource] Audio track present but disabled by open options index="
                        << static_cast<int>(i)
                        << " codec=" << SwString(track.codecName)
                        << " payload=" << track.payloadType;
                } else {
                    swCWarning(kSwLogCategory_SwRtspUdpSource)
                        << "[SwRtspUdpSource] Audio track present but unsupported index="
                        << static_cast<int>(i)
                        << " codec=" << SwString(track.codecName)
                        << " payload=" << track.payloadType;
                }
                break;
            }
        }
        if (!m_metadataCodecName.empty()) {
            swCWarning(kSwLogCategory_SwRtspUdpSource)
                << "[SwRtspUdpSource] Selected SDP metadata track index=" << m_selectedMetadataTrackIndex
                << " codec=" << m_metadataCodecName
                << " payload=" << m_metadataPayloadType
                << " clock=" << m_metadataClockRate
                << " control=" << m_metadataTrackControl;
        } else {
            for (std::size_t i = 0; i < m_tracks.size(); ++i) {
                const auto& track = m_tracks[i];
                if (!track.isMetadata()) {
                    continue;
                }
                if (!m_enableMetadata) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource)
                        << "[SwRtspUdpSource] Metadata track present but disabled by open options index="
                        << static_cast<int>(i)
                        << " codec=" << SwString(track.codecName)
                        << " payload=" << track.payloadType;
                } else {
                    swCWarning(kSwLogCategory_SwRtspUdpSource)
                        << "[SwRtspUdpSource] Metadata track present but not selected index="
                        << static_cast<int>(i)
                        << " codec=" << SwString(track.codecName)
                        << " payload=" << track.payloadType;
                }
                break;
            }
        }
        publishTracks_();
        return true;
    }

    void publishTracks_() {
        SwList<SwMediaTrack> publishedTracks;
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            const auto& track = m_tracks[i];
            SwMediaTrack mediaTrack;
            mediaTrack.id = SwString("track-") + SwString(std::to_string(i));
            if (track.isVideo()) {
                mediaTrack.type = SwMediaTrack::Type::Video;
            } else if (track.isAudio()) {
                mediaTrack.type = SwMediaTrack::Type::Audio;
            } else if (track.isMetadata()) {
                mediaTrack.type = SwMediaTrack::Type::Metadata;
            } else {
                mediaTrack.type = SwMediaTrack::Type::Unknown;
            }
            mediaTrack.codec = SwString(track.codecName);
            mediaTrack.payloadType = track.payloadType;
            mediaTrack.clockRate = track.clockRate;
            mediaTrack.sampleRate = track.isAudio() ? track.clockRate : 0;
            mediaTrack.channelCount = track.channelCount;
            mediaTrack.control = SwString(track.control);
            mediaTrack.fmtp = SwString(track.fmtpLine);
            mediaTrack.selected = (static_cast<int>(i) == m_selectedTrackIndex) ||
                                  (static_cast<int>(i) == m_selectedAudioTrackIndex) ||
                                  (static_cast<int>(i) == m_selectedMetadataTrackIndex);
            if (track.isAudio() && !isAudioCodecSupported_(track.codecName)) {
                mediaTrack.availability = SwMediaTrack::Availability::Unsupported;
            } else {
                mediaTrack.availability = SwMediaTrack::Availability::Available;
            }
            publishedTracks.append(mediaTrack);
        }
        setTracks(publishedTracks);
    }

    void sendDescribe() {
        std::vector<std::pair<std::string, std::string>> headers = {
            {"Accept", "application/sdp"}
        };
        sendRtsp("DESCRIBE", fullUrl(), headers);
    }

    void sendOptions();

    void sendSetup() {
        std::ostringstream transport;
        std::vector<std::pair<std::string, std::string>> headers;
        const SetupTarget setupTarget = currentSetupTarget_();
        uint8_t interleavedRtpChannel = m_interleavedRtpChannel;
        uint8_t interleavedRtcpChannel = m_interleavedRtcpChannel;
        uint16_t clientRtpPort = m_clientRtpPort;
        uint16_t clientRtcpPort = m_clientRtcpPort;
        if (setupTarget == SetupTarget::Audio) {
            interleavedRtpChannel = m_audioInterleavedRtpChannel;
            interleavedRtcpChannel = m_audioInterleavedRtcpChannel;
            clientRtpPort = m_audioClientRtpPort;
            clientRtcpPort = m_audioClientRtcpPort;
        } else if (setupTarget == SetupTarget::Metadata) {
            interleavedRtpChannel = m_metadataInterleavedRtpChannel;
            interleavedRtcpChannel = m_metadataInterleavedRtcpChannel;
            clientRtpPort = m_metadataClientRtpPort;
            clientRtcpPort = m_metadataClientRtcpPort;
        }
        if (m_useTcpTransport) {
            transport << "RTP/AVP/TCP;unicast;interleaved="
                      << static_cast<int>(interleavedRtpChannel)
                      << "-"
                      << static_cast<int>(interleavedRtcpChannel)
                      << ";mode=\"PLAY\"";
        } else {
            transport << "RTP/AVP;unicast;client_port="
                      << clientRtpPort << "-"
                      << clientRtcpPort
                      << ";mode=\"PLAY\"";
        }
        headers.emplace_back("Transport", transport.str());
        if (!m_sessionId.empty()) {
            headers.emplace_back("Session", m_sessionId);
        }
        sendRtsp("SETUP", setupTrackUrl_(), headers);
    }

    void sendPlay(bool aggregateOnly) {
        std::vector<std::pair<std::string, std::string>> headers;
        if (!m_sessionId.empty()) {
            headers.emplace_back("Session", m_sessionId);
        }
        headers.emplace_back("Range", "npt=0.000-");
        sendRtsp("PLAY", aggregateOnly ? aggregatePlayUrl() : playUrl(), headers);
        m_playStart = std::chrono::steady_clock::now();
    }

    void sendKeepAlive() {
        if (!isRunning() || m_sessionId.empty()) {
            return;
        }
        std::vector<std::pair<std::string, std::string>> headers = {{"Session", m_sessionId}};
        sendRtsp("OPTIONS", fullUrl(), headers);
    }

    void configureUdpSockets() {
        if (m_useTcpTransport) {
            return;
        }
        if (m_udpSession && m_udpSession->isRunning()) {
            m_udpSession->sendUdpPunch(m_host, m_serverRtpPort, m_serverRtcpPort);
            return;
        }
        if (m_rtpSocket && m_serverRtpPort != 0) {
            sendUdpPunch(m_rtpSocket, m_serverRtpPort, "RTP");
        }
        if (m_rtcpSocket && m_serverRtcpPort != 0) {
            sendUdpPunch(m_rtcpSocket, m_serverRtcpPort, "RTCP");
        }
    }

    void tuneUdpSocket(SwUdpSocket* socket, int recvBytes, size_t datagramSize, size_t queueLimit) {
        if (!socket) {
            return;
        }
        socket->setReceiveBufferSize(recvBytes);
        socket->setMaxDatagramSize(datagramSize);
        socket->setMaxPendingDatagrams(queueLimit);
    }

    bool stringToInAddr(const std::string& text, in_addr& addr) const {
#if defined(_WIN32)
        return InetPtonA(AF_INET, text.c_str(), &addr) == 1;
#else
        return inet_pton(AF_INET, text.c_str(), &addr) == 1;
#endif
    }

    void suppressRtspDisconnectWhile(const std::function<void()>& action) {
        m_rtspDisconnectSuppress.fetch_add(1);
        if (action) {
            action();
        }
        m_rtspDisconnectSuppress.fetch_sub(1);
    }

    void closeRtspSocket() {
        if (!m_rtspSocket) {
            return;
        }
        suppressRtspDisconnectWhile([this]() {
            m_rtspSocket->close();
        });
    }

    bool isHevcCodecName(const std::string& name) const {
        if (name == "h265" || name == "hevc" || name == "hev1" || name == "hvc1") {
            return true;
        }
        return (name.find("h265") != std::string::npos || name.find("hevc") != std::string::npos);
    }

    bool isTransportStreamCodec() const {
        return (m_codecName == "mp2t" || m_codecName == "mpegts" || m_payloadType == 33);
    }

    bool isHevcCodec() const {
        return isHevcCodecName(m_codecName);
    }

    SwAudioPacket::Codec audioCodecFromName_(const std::string& name) const {
        const std::string normalized = toLower(name);
        if (normalized == "pcmu" || normalized == "g711u" || normalized == "mulaw") {
            return SwAudioPacket::Codec::PCMU;
        }
        if (normalized == "pcma" || normalized == "g711a" || normalized == "alaw") {
            return SwAudioPacket::Codec::PCMA;
        }
        if (normalized == "opus") {
            return SwAudioPacket::Codec::Opus;
        }
        if (normalized == "aac" || normalized == "mpeg4-generic" || normalized == "mp4a-latm") {
            return SwAudioPacket::Codec::AAC;
        }
        return SwAudioPacket::Codec::Unknown;
    }

    bool isAudioCodecSupported_(const std::string& name) const {
        const auto codec = audioCodecFromName_(name);
        if (codec == SwAudioPacket::Codec::Unknown) {
            return false;
        }
        return SwAudioDecoderFactory::instance().acquire(codec) != nullptr;
    }

    void initiateConnection() {
        if (!isRunning()) {
            return;
        }
        cancelReconnect();
        emitStatus(m_framesEmitted.load() > 0 ? SwVideoSource::StreamState::Recovering
                                              : SwVideoSource::StreamState::Connecting,
                   m_framesEmitted.load() > 0 ? SwString("Reconnecting RTSP session...")
                                              : SwString("Connecting RTSP session..."));
        resetStreamState();
        if (!m_rtspSocket->connectToHost(m_host, static_cast<uint16_t>(m_port))) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to connect to " << m_host.toStdString()
                      << ":" << m_port;
            scheduleReconnect("connectToHost failed");
            return;
        }
    }

    void triggerTcpFallback(const char* reason) {
        if (m_useTcpTransport || m_triedTcpFallback || !isRunning()) {
            return;
        }
        m_triedTcpFallback = true;
        m_autoTcpFallbackActive = true;
        emitStatus(SwVideoSource::StreamState::Recovering, "Switching RTSP transport to TCP...");
        swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Falling back to RTSP/TCP"
                    << (reason ? SwString(" (") + SwString(reason) + SwString(")") : SwString());
        m_useTcpTransport = true;
        stopStreaming(false);
        initiateConnection();
    }

    void revertAutoTcpFallback(const char* reason) {
        if (!m_autoTcpFallbackActive) {
            return;
        }
        emitStatus(SwVideoSource::StreamState::Recovering, "Returning RTSP transport to UDP...");
        swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Returning to RTSP/UDP"
                    << (reason ? SwString(" (") + SwString(reason) + SwString(")") : SwString());
        m_useTcpTransport = false;
        m_autoTcpFallbackActive = false;
        stopStreaming(false);
        initiateConnection();
    }

    void scheduleReconnect(const char* reason) {
        if (!m_autoReconnect.load()) {
            return;
        }
        if (reason) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Scheduling reconnect: " << reason;
        }
        emitStatus(SwVideoSource::StreamState::Recovering,
                   reason ? SwString(reason) : SwString("Scheduling RTSP reconnect..."));
        if (!m_reconnectTimer) {
            return;
        }
        if (m_reconnectPending.exchange(true)) {
            return;
        }
        m_reconnectTimer->stop();
        m_reconnectTimer->start(m_reconnectDelayMs);
    }

    void cancelReconnect() {
        if (m_reconnectTimer) {
            m_reconnectTimer->stop();
        }
        m_reconnectPending.store(false);
    }

    void attemptReconnect() {
        m_reconnectPending.store(false);
        if (!m_autoReconnect.load()) {
            return;
        }
        initiateConnection();
    }

    void setPlaceholderActive(bool enable) {
        if (enable) {
            bool expected = false;
            if (m_placeholderActive.compare_exchange_strong(expected, true)) {
                m_lastPlaceholderTime = std::chrono::steady_clock::time_point{};
                m_hevcHeadersInserted = false;
            }
        } else {
            bool expected = true;
            if (m_placeholderActive.compare_exchange_strong(expected, false)) {
                m_lastPlaceholderTime = std::chrono::steady_clock::time_point{};
            }
        }
    }

    void ensurePlaceholderFrame() {
        if (m_placeholderFormat.isValid() && !m_placeholderPayload.isEmpty()) {
            return;
        }
        constexpr int width = 320;
        constexpr int height = 180;
        m_placeholderFormat = SwDescribeVideoFormat(SwVideoPixelFormat::RGBA32, width, height);
        std::vector<uint8_t> buffer(m_placeholderFormat.dataSize, 0);
        static const std::array<std::array<uint8_t, 4>, 8> colors = {{
            {0xD8, 0x2B, 0x2B, 0xFF},
            {0xFF, 0x8C, 0x00, 0xFF},
            {0xFF, 0xD3, 0x00, 0xFF},
            {0x00, 0xA4, 0x4A, 0xFF},
            {0x00, 0x83, 0xC7, 0xFF},
            {0x49, 0x3C, 0xB3, 0xFF},
            {0x8B, 0x2F, 0x97, 0xFF},
            {0x20, 0x20, 0x20, 0xFF}
        }};
        int stripeWidth = std::max(1, width / static_cast<int>(colors.size()));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int colorIndex = std::min(static_cast<int>(colors.size()) - 1, x / stripeWidth);
                size_t offset = static_cast<size_t>(y * width + x) * 4;
                buffer[offset + 0] = colors[colorIndex][0];
                buffer[offset + 1] = colors[colorIndex][1];
                buffer[offset + 2] = colors[colorIndex][2];
                buffer[offset + 3] = colors[colorIndex][3];
            }
        }
        m_placeholderPayload = SwByteArray(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }

    void emitPlaceholderFrame() {
        if (!m_placeholderActive.load()) {
            return;
        }
        ensurePlaceholderFrame();
        if (!m_placeholderFormat.isValid() || m_placeholderPayload.isEmpty()) {
            return;
        }
        SwByteArray payload(m_placeholderPayload);
        auto now = std::chrono::steady_clock::now();
        auto pts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        SwVideoPacket packet(SwVideoPacket::Codec::RawRGBA,
                             payload,
                             static_cast<std::int64_t>(pts),
                             static_cast<std::int64_t>(pts),
                             true);
        packet.setRawFormat(m_placeholderFormat);
        emitPacket(packet);
        auto emitted = ++m_placeholderFramesEmitted;
        if (emitted <= 3 || (emitted % 50) == 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Placeholder frame #" << emitted << " emitted";
        }
    }

    void maybeEmitPlaceholder() {
        if (!m_placeholderActive.load()) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (m_lastPlaceholderTime.time_since_epoch().count() != 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPlaceholderTime).count();
            if (elapsed < m_placeholderIntervalMs) {
                return;
            }
        }
        m_lastPlaceholderTime = now;
        emitPlaceholderFrame();
    }

    void sendUdpPunch(SwUdpSocket* socket, uint16_t remotePort, const char* label) {
        if (!socket || remotePort == 0) {
            return;
        }
        static const char payload[] = "ping";
        int64_t sent = socket->writeDatagram(payload, sizeof(payload) - 1, m_host, remotePort);
        if (sent > 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP punch " << label
                      << " sent to " << m_host.toStdString() << ":" << remotePort;
        } else {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP punch " << label
                        << " failed to " << m_host.toStdString() << ":" << remotePort;
        }
    }

    void selfProbeUdp(const SwString& bindAddr, uint16_t port) {
#if defined(_WIN32)
        SwString addrStr = (bindAddr.isEmpty() || bindAddr == "0.0.0.0") ? "127.0.0.1" : bindAddr;
        SwUdpSocket probeSocket;
        const char* msg = "probe";
        int64_t ret = probeSocket.writeDatagram(msg, 5, addrStr, port);
        if (ret <= 0) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP self-probe sendto failed";
        } else {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP self-probe sent to " << addrStr << ":" << port;
        }
#else
        (void)bindAddr;
        (void)port;
#endif
    }

    void sendRtsp(const std::string& method,
                  const std::string& url,
                  const std::vector<std::pair<std::string, std::string>>& headers,
                  const std::string& body = std::string(),
                  int retryCount = 0) {
        if (!m_rtspSocket || !m_rtspSocket->isOpen()) {
            return;
        }
        std::ostringstream oss;
        oss << method << " " << url << " RTSP/1.0\r\n";
        int cseq = ++m_cseq;
        oss << "CSeq: " << cseq << "\r\n";
        oss << "User-Agent: SwRtspUdpSource/1.0\r\n";
        for (const auto& h : headers) {
            oss << h.first << ": " << h.second << "\r\n";
        }
        if (!body.empty()) {
            oss << "Content-Length: " << body.size() << "\r\n";
        }
        oss << "\r\n";
        if (!body.empty()) {
            oss << body;
        }
        if (retryCount > 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] >>> (CSeq " << cseq << ", retry " << retryCount << ")\n" << oss.str();
        } else {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] >>> (CSeq " << cseq << ")\n" << oss.str();
        }
        m_rtspSocket->write(SwString(oss.str()));
        RtspRequest req;
        req.method = method;
        req.url = url;
        req.headers = headers;
        req.body = body;
        req.retries = retryCount;
        m_pendingRequests[cseq] = req;
    }

    std::string fullUrl() const {
        return m_baseUrl + m_path.toStdString();
    }

    std::string resolveControlUrl(const std::string& control) const {
        if (control.empty() || control == "*") {
            return fullUrl();
        }
        if (control.find("rtsp://") == 0) {
            return control;
        }
        if (!control.empty() && control.front() == '/') {
            return m_baseUrl + control;
        }
        std::string base = fullUrl();
        if (!base.empty() && base.back() != '/') {
            base.push_back('/');
        }
        return base + control;
    }

    std::string trackUrl() const { return resolveControlUrl(m_trackControl); }
    std::string audioTrackUrl() const { return resolveControlUrl(m_audioTrackControl); }
    std::string metadataTrackUrl() const { return resolveControlUrl(m_metadataTrackControl); }
    std::string aggregatePlayUrl() const { return resolveControlUrl(m_sessionControl); }
    std::string playUrl() const {
        if (!m_sessionControl.empty() || m_setupTargets.size() > 1) {
            return aggregatePlayUrl();
        }
        return trackUrl();
    }

    SetupTarget currentSetupTarget_() const {
        if (m_setupTargets.empty() || m_setupTargetIndex >= m_setupTargets.size()) {
            return SetupTarget::Video;
        }
        return m_setupTargets[m_setupTargetIndex];
    }

    std::string setupTrackUrl_() const {
        switch (currentSetupTarget_()) {
        case SetupTarget::Audio:
            return audioTrackUrl();
        case SetupTarget::Metadata:
            return metadataTrackUrl();
        case SetupTarget::Video:
        default:
            return trackUrl();
        }
    }

    bool allocateClientPorts() {
        if (m_useTcpTransport) {
            return true; // no UDP ports needed
        }
        SwString bindAddr = m_bindAddress.isEmpty() ? SwString("0.0.0.0") : m_bindAddress;
        auto tryBindPair = [this, &bindAddr](uint16_t base) -> bool {
            if (base == 0) {
                return false;
            }
            if (startUdpSession_(bindAddr, base, static_cast<uint16_t>(base + 1))) {
                m_clientRtpPort = base;
                m_clientRtcpPort = static_cast<uint16_t>(base + 1);
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Local RTP session "
                          << bindAddr.toStdString() << ":" << m_clientRtpPort << "/" << m_clientRtcpPort;
                return true;
            }
            return false;
        };
        if (m_forcedClientRtpPort != 0 && m_forcedClientRtcpPort != 0) {
            if (startUdpSession_(bindAddr, m_forcedClientRtpPort, m_forcedClientRtcpPort)) {
                m_clientRtpPort = m_forcedClientRtpPort;
                m_clientRtcpPort = m_forcedClientRtcpPort;
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Forced RTP session "
                          << bindAddr.toStdString() << ":" << m_clientRtpPort << "/" << m_clientRtcpPort;
                return true;
            }
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Forced RTP session FAILED on "
                      << bindAddr.toStdString() << ":" << m_forcedClientRtpPort << "/" << m_forcedClientRtcpPort;
            return false;
        }
        // Prefer the range around 37700 if it is free.
        if (tryBindPair(37700)) {
            return true;
        }
        for (uint16_t base = 50000; base < 65000; base += 2) {
            if (tryBindPair(base)) {
                return true;
            } else {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Bind failed on " << bindAddr.toStdString()
                          << ":" << base << "/" << base + 1;
            }
        }
        return false;
    }

    bool allocateAudioClientPorts() {
        if (m_useTcpTransport || m_selectedAudioTrackIndex < 0 || m_audioTrackControl.empty()) {
            return true;
        }
        SwString bindAddr = m_bindAddress.isEmpty() ? SwString("0.0.0.0") : m_bindAddress;
        auto tryBindPair = [this, &bindAddr](uint16_t base) -> bool {
            if (base == 0 || base == m_clientRtpPort) {
                return false;
            }
            if (startAudioUdpSession_(bindAddr, base, static_cast<uint16_t>(base + 1))) {
                m_audioClientRtpPort = base;
                m_audioClientRtcpPort = static_cast<uint16_t>(base + 1);
                swCDebug(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Local audio RTP session "
                    << bindAddr.toStdString() << ":" << m_audioClientRtpPort
                    << "/" << m_audioClientRtcpPort;
                return true;
            }
            return false;
        };
        for (uint16_t base = 52000; base < 65000; base += 2) {
            if (tryBindPair(base)) {
                return true;
            }
        }
        return false;
    }

    bool allocateMetadataClientPorts() {
        if (m_useTcpTransport || m_selectedMetadataTrackIndex < 0 || m_metadataTrackControl.empty()) {
            return true;
        }
        SwString bindAddr = m_bindAddress.isEmpty() ? SwString("0.0.0.0") : m_bindAddress;
        auto tryBindPair = [this, &bindAddr](uint16_t base) -> bool {
            const uint16_t rtcpPort = static_cast<uint16_t>(base + 1);
            if (base == 0 ||
                base == m_clientRtpPort ||
                base == m_audioClientRtpPort ||
                rtcpPort == m_clientRtcpPort ||
                rtcpPort == m_audioClientRtcpPort) {
                return false;
            }
            if (startMetadataUdpSession_(bindAddr, base, rtcpPort)) {
                m_metadataClientRtpPort = base;
                m_metadataClientRtcpPort = rtcpPort;
                swCDebug(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] Local metadata RTP session "
                    << bindAddr.toStdString() << ":" << m_metadataClientRtpPort
                    << "/" << m_metadataClientRtcpPort;
                return true;
            }
            return false;
        };
        for (uint16_t base = 54000; base < 65000; base += 2) {
            if (tryBindPair(base)) {
                return true;
            }
        }
        return false;
    }

    bool startUdpSession_(const SwString& bindAddr, uint16_t rtpPort, uint16_t rtcpPort) {
        if (m_udpSession) {
            m_udpSession->stop();
            m_udpSession.reset();
        }
        SwRtpSessionDescriptor descriptor;
        descriptor.bindAddress = bindAddr;
        descriptor.localRtpPort = rtpPort;
        descriptor.localRtcpPort = rtcpPort;
        descriptor.codec = isHevcCodec()
                               ? SwVideoPacket::Codec::H265
                               : (isTransportStreamCodec() ? SwVideoPacket::Codec::H264
                                                           : SwVideoPacket::Codec::H264);
        descriptor.payloadType = m_payloadType;
        descriptor.clockRate = m_clockRate;
        descriptor.format = isTransportStreamCodec()
                                ? SwMediaOpenOptions::UdpPayloadFormat::MpegTs
                                : SwMediaOpenOptions::UdpPayloadFormat::Rtp;
        if (m_selectedTrackIndex >= 0 &&
            static_cast<std::size_t>(m_selectedTrackIndex) < m_tracks.size()) {
            descriptor.fmtp = SwString(m_tracks[static_cast<std::size_t>(m_selectedTrackIndex)].fmtpLine);
        }
        descriptor.lowLatency = m_lowLatencyDrop;
        m_udpSession = std::make_unique<SwRtpSession>(descriptor);
        m_udpSession->setPacketCallback([this](const SwRtpSession::Packet& packet) {
            handleUdpSessionPacket_(packet);
        });
        m_udpSession->setGapCallback([this](uint16_t expected, uint16_t actual) {
            handleUdpSessionGap_(expected, actual);
        });
        m_udpSession->setTimeoutCallback([this](int secondsWithoutData) {
            handleUdpSessionTimeout_(secondsWithoutData);
        });
        if (!m_udpSession->start()) {
            m_udpSession.reset();
            return false;
        }
        return true;
    }

    bool startAudioUdpSession_(const SwString& bindAddr, uint16_t rtpPort, uint16_t rtcpPort) {
        if (m_audioUdpSession) {
            m_audioUdpSession->stop();
            m_audioUdpSession.reset();
        }
        SwRtpSessionDescriptor descriptor;
        descriptor.bindAddress = bindAddr;
        descriptor.localRtpPort = rtpPort;
        descriptor.localRtcpPort = rtcpPort;
        descriptor.codec = SwVideoPacket::Codec::Unknown;
        descriptor.payloadType = m_audioPayloadType >= 0 ? m_audioPayloadType : 96;
        descriptor.clockRate = m_audioClockRate > 0 ? m_audioClockRate : 8000;
        descriptor.format = SwMediaOpenOptions::UdpPayloadFormat::Rtp;
        if (m_selectedAudioTrackIndex >= 0 &&
            static_cast<std::size_t>(m_selectedAudioTrackIndex) < m_tracks.size()) {
            descriptor.fmtp =
                SwString(m_tracks[static_cast<std::size_t>(m_selectedAudioTrackIndex)].fmtpLine);
        }
        descriptor.allowKeyFrameRequests = false;
        descriptor.lowLatency = m_lowLatencyDrop;
        m_audioUdpSession = std::make_unique<SwRtpSession>(descriptor);
        m_audioUdpSession->setPacketCallback([this](const SwRtpSession::Packet& packet) {
            handleAudioUdpSessionPacket_(packet);
        });
        m_audioUdpSession->setTimeoutCallback([this](int secondsWithoutData) {
            if (secondsWithoutData >= 4 && !m_audioCodecName.empty()) {
                swCWarning(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] No audio RTP received for " << secondsWithoutData
                    << " s (codec=" << m_audioCodecName << ")";
            }
        });
        if (!m_audioUdpSession->start()) {
            m_audioUdpSession.reset();
            return false;
        }
        return true;
    }

    bool startMetadataUdpSession_(const SwString& bindAddr, uint16_t rtpPort, uint16_t rtcpPort) {
        if (m_metadataUdpSession) {
            m_metadataUdpSession->stop();
            m_metadataUdpSession.reset();
        }
        SwRtpSessionDescriptor descriptor;
        descriptor.bindAddress = bindAddr;
        descriptor.localRtpPort = rtpPort;
        descriptor.localRtcpPort = rtcpPort;
        descriptor.codec = SwVideoPacket::Codec::Unknown;
        descriptor.payloadType = m_metadataPayloadType >= 0 ? m_metadataPayloadType : 97;
        descriptor.clockRate = m_metadataClockRate > 0 ? m_metadataClockRate : 90000;
        descriptor.format = SwMediaOpenOptions::UdpPayloadFormat::Rtp;
        descriptor.allowKeyFrameRequests = false;
        descriptor.lowLatency = m_lowLatencyDrop;
        if (m_selectedMetadataTrackIndex >= 0 &&
            static_cast<std::size_t>(m_selectedMetadataTrackIndex) < m_tracks.size()) {
            descriptor.fmtp =
                SwString(m_tracks[static_cast<std::size_t>(m_selectedMetadataTrackIndex)].fmtpLine);
        }
        m_metadataUdpSession = std::make_unique<SwRtpSession>(descriptor);
        m_metadataUdpSession->setPacketCallback([this](const SwRtpSession::Packet& packet) {
            handleMetadataUdpSessionPacket_(packet);
        });
        m_metadataUdpSession->setTimeoutCallback([this](int secondsWithoutData) {
            if (secondsWithoutData >= 4 && !m_metadataCodecName.empty()) {
                swCWarning(kSwLogCategory_SwRtspUdpSource)
                    << "[SwRtspUdpSource] No metadata RTP received for " << secondsWithoutData
                    << " s (codec=" << m_metadataCodecName << ")";
            }
        });
        if (!m_metadataUdpSession->start()) {
            m_metadataUdpSession.reset();
            return false;
        }
        return true;
    }

    void handleUdpSessionPacket_(const SwRtpSession::Packet& packet) {
        if (packet.senderPort != 0) {
            if (m_detectedRtpPort == 0) {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Detected RTP source port "
                          << packet.senderAddress.toStdString() << ":" << packet.senderPort;
            } else if (m_detectedRtpPort != packet.senderPort) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTP source port changed from "
                            << m_detectedRtpPort << " to " << packet.senderPort;
            }
            m_detectedRtpPort = packet.senderPort;
        }
        if (m_remoteSsrc == 0 && packet.ssrc != 0) {
            m_remoteSsrc = packet.ssrc;
        }
        m_lastRtpTime = std::chrono::steady_clock::now();
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(packet.payload.constData());
        const size_t payloadLen = packet.payload.size();
        if (!payload || payloadLen == 0) {
            return;
        }
        processTransportPacket_(packet);
    }

    void handleAudioUdpSessionPacket_(const SwRtpSession::Packet& packet) {
        if (m_selectedAudioTrackIndex < 0 || m_audioCodecName.empty()) {
            return;
        }
        SwMediaPacket mediaPacket;
        mediaPacket.setType(SwMediaPacket::Type::Audio);
        mediaPacket.setTrackId(SwString("track-") + SwString(std::to_string(m_selectedAudioTrackIndex)));
        mediaPacket.setCodec(SwString(m_audioCodecName));
        mediaPacket.setPayload(packet.payload);
        mediaPacket.setPts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDiscontinuity(false);
        mediaPacket.setPayloadType(m_audioPayloadType);
        mediaPacket.setClockRate(m_audioClockRate);
        mediaPacket.setSampleRate(m_audioClockRate);
        mediaPacket.setChannelCount(m_audioChannelCount > 0 ? m_audioChannelCount : 1);
        emitMediaPacket(mediaPacket);
    }

    void handleMetadataUdpSessionPacket_(const SwRtpSession::Packet& packet) {
        if (m_selectedMetadataTrackIndex < 0 || m_metadataCodecName.empty()) {
            return;
        }
        SwMediaPacket mediaPacket;
        mediaPacket.setType(SwMediaPacket::Type::Metadata);
        mediaPacket.setTrackId(SwString("track-") + SwString(std::to_string(m_selectedMetadataTrackIndex)));
        mediaPacket.setCodec(SwString(m_metadataCodecName));
        mediaPacket.setPayload(packet.payload);
        mediaPacket.setPts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDiscontinuity(false);
        mediaPacket.setPayloadType(m_metadataPayloadType);
        mediaPacket.setClockRate(m_metadataClockRate);
        emitMediaPacket(mediaPacket);
    }

    void configureAudioUdpSession_() {
        if (m_useTcpTransport || !m_audioUdpSession || !m_audioUdpSession->isRunning()) {
            return;
        }
        if (m_audioServerRtpPort != 0 || m_audioServerRtcpPort != 0) {
            m_audioUdpSession->sendUdpPunch(m_host, m_audioServerRtpPort, m_audioServerRtcpPort);
        }
    }

    void configureMetadataUdpSession_() {
        if (m_useTcpTransport || !m_metadataUdpSession || !m_metadataUdpSession->isRunning()) {
            return;
        }
        if (m_metadataServerRtpPort != 0 || m_metadataServerRtcpPort != 0) {
            m_metadataUdpSession->sendUdpPunch(m_host, m_metadataServerRtpPort, m_metadataServerRtcpPort);
        }
    }

    void handleUdpSessionGap_(uint16_t expected, uint16_t actual) {
        ++m_sequenceDiscontinuities;
        const int lostPackets = static_cast<int>(static_cast<uint16_t>(actual - expected));
        const bool forceKeyFrameRecovery =
            isTransportStreamCodec() || (isHevcCodec() ? (lostPackets > 2) : (lostPackets > 4));
        if (isTransportStreamCodec()) {
            m_sharedTsDemux.reset();
        } else if (isHevcCodec()) {
            m_h265Depacketizer.onSequenceGap(forceKeyFrameRecovery);
        } else {
            m_h264Depacketizer.onSequenceGap(forceKeyFrameRecovery);
        }
        if (forceKeyFrameRecovery || lostPackets > 1 || m_framesEmitted.load() == 0 || isTransportStreamCodec()) {
            requestKeyFrame("rtp loss");
        }
    }

    void handleUdpSessionTimeout_(int secondsWithoutData) {
        if (!isRunning() || m_state != RtspStep::Playing) {
            return;
        }
        if (!m_useTcpTransport && !m_triedTcpFallback) {
            triggerTcpFallback("UDP stalled");
            return;
        }
        auto previousElapsed = m_lastLoggedRtpTimeoutSec.exchange(secondsWithoutData);
        if (previousElapsed != secondsWithoutData) {
            uint16_t portHint = m_detectedRtpPort ? m_detectedRtpPort : m_serverRtpPort;
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] No RTP received for " << secondsWithoutData
                        << " s (local=" << m_clientRtpPort
                        << ", expected remote=" << portHint << ")";
        }
        emitStatus(SwVideoSource::StreamState::Recovering, "Video stream stalled...");
    }

    void processTransportPacket_(const SwRtpSession::Packet& packet) {
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(packet.payload.constData());
        const size_t payloadLen = packet.payload.size();
        if (!payload || payloadLen == 0) {
            return;
        }
        if (isTransportStreamCodec()) {
            m_sharedTsDemux.feed(payload, payloadLen, packet.timestamp);
            return;
        }
        if (isHevcCodec()) {
            m_h265Depacketizer.push(packet);
            maybeRequestKeyFrameWhileWaiting_();
            return;
        }
        m_h264Depacketizer.push(packet);
        maybeRequestKeyFrameWhileWaiting_();
    }

    void maybeRequestKeyFrameWhileWaiting_() {
        if (isTransportStreamCodec()) {
            m_lastWaitingKeyFrameRequestTime = {};
            return;
        }
        const bool waitingForKeyFrame = isHevcCodec() ? m_h265Depacketizer.isWaitingForKeyFrame()
                                                      : m_h264Depacketizer.isWaitingForKeyFrame();
        if (!waitingForKeyFrame) {
            m_lastWaitingKeyFrameRequestTime = {};
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (m_lastWaitingKeyFrameRequestTime.time_since_epoch().count() != 0) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastWaitingKeyFrameRequestTime).count();
            if (elapsed < 1000) {
                return;
            }
        }
        m_lastWaitingKeyFrameRequestTime = now;
        requestKeyFrame("waiting keyframe");
    }

    void emitRecoveredPacket_(const SwVideoPacket& packet) {
        SwVideoPacket outputPacket = packet;
        m_currentCodec = outputPacket.codec();
        const uint32_t timestamp = static_cast<uint32_t>(outputPacket.pts() >= 0 ? outputPacket.pts() : 0);
        if (!m_haveFirstTimestamp) {
            m_firstTimestamp = timestamp;
            m_haveFirstTimestamp = true;
        }
        if (shouldDropFrame(timestamp, outputPacket.isKeyFrame())) {
            auto dropped = ++m_framesDropped;
            if (dropped <= 3 || (dropped % 25) == 0) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Dropping frame #" << dropped
                            << " to reduce latency (ts=" << timestamp << ")";
            }
            return;
        }
        emitStatus(SwVideoSource::StreamState::Streaming, "Streaming");
        emitPacket(outputPacket);
        auto emitted = ++m_framesEmitted;
        if (!m_loggedFirstCompressedFrame.exchange(true)) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] First compressed frame emitted "
                        << " codec=" << static_cast<int>(outputPacket.codec())
                        << " bytes=" << outputPacket.payload().size()
                        << " key=" << (outputPacket.isKeyFrame() ? 1 : 0)
                        << " ts=" << timestamp;
        }
        if (emitted <= 3 || (emitted % 100) == 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Emitted frame #" << emitted
                      << " bytes=" << outputPacket.payload().size()
                      << " key=" << (outputPacket.isKeyFrame() ? 1 : 0);
        }
    }

    static bool parseRtpSequenceNumber(const SwByteArray& datagram, uint16_t& sequenceNumber) {
        if (datagram.size() < 12) {
            return false;
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(datagram.constData());
        if (!data || (data[0] >> 6) != 2) {
            return false;
        }
        sequenceNumber = static_cast<uint16_t>((static_cast<uint16_t>(data[2]) << 8) |
                                               static_cast<uint16_t>(data[3]));
        return true;
    }

    static int16_t compareRtpSequence(uint16_t sequenceNumber, uint16_t reference) {
        return static_cast<int16_t>(sequenceNumber - reference);
    }

    std::map<uint16_t, BufferedRtpPacket>::iterator findNextBufferedRtpPacket(uint16_t expectedSequence) {
        auto bestIt = m_rtpReorderBuffer.end();
        int bestDelta = std::numeric_limits<int>::max();
        for (auto it = m_rtpReorderBuffer.begin(); it != m_rtpReorderBuffer.end(); ++it) {
            int delta = static_cast<int>(compareRtpSequence(it->first, expectedSequence));
            if (delta < 0) {
                continue;
            }
            if (delta < bestDelta) {
                bestDelta = delta;
                bestIt = it;
            }
        }
        return bestIt;
    }

    void queueRtpDatagram(const SwByteArray& datagram,
                          const std::chrono::steady_clock::time_point& arrivalTime) {
        uint16_t sequenceNumber = 0;
        if (!parseRtpSequenceNumber(datagram, sequenceNumber)) {
            return;
        }

        if (!m_rtpReorderExpectedValid) {
            m_rtpReorderExpectedSequence = m_haveSequence
                                               ? static_cast<uint16_t>(m_lastSequence + 1)
                                               : sequenceNumber;
            m_rtpReorderExpectedValid = true;
        }

        if (compareRtpSequence(sequenceNumber, m_rtpReorderExpectedSequence) < 0) {
            return;
        }

        auto inserted = m_rtpReorderBuffer.emplace(sequenceNumber, BufferedRtpPacket{datagram, arrivalTime});
        if (!inserted.second) {
            inserted.first->second.arrivalTime = arrivalTime;
        }
    }

    bool parseAuxiliaryRtpPacket_(const uint8_t* data,
                                  size_t len,
                                  int expectedPayloadType,
                                  SwRtpSession::Packet& outPacket) const {
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
        if (expectedPayloadType >= 0 && payloadType != static_cast<uint8_t>(expectedPayloadType)) {
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
        return true;
    }

    void drainBufferedRtpPackets(bool allowGapAdvance) {
        auto now = std::chrono::steady_clock::now();
        while (!m_rtpReorderBuffer.empty()) {
            if (!m_rtpReorderExpectedValid) {
                m_rtpReorderExpectedSequence = m_haveSequence
                                                   ? static_cast<uint16_t>(m_lastSequence + 1)
                                                   : m_rtpReorderBuffer.begin()->first;
                m_rtpReorderExpectedValid = true;
            }

            auto exactIt = m_rtpReorderBuffer.find(m_rtpReorderExpectedSequence);
            if (exactIt != m_rtpReorderBuffer.end()) {
                SwByteArray datagram = exactIt->second.datagram;
                m_rtpReorderBuffer.erase(exactIt);
                if (handleRtpPacket(reinterpret_cast<const uint8_t*>(datagram.constData()),
                                    static_cast<size_t>(datagram.size()))) {
                    m_lastRtpTime = now;
                }
                m_rtpReorderExpectedSequence = static_cast<uint16_t>(m_rtpReorderExpectedSequence + 1);
                continue;
            }

            auto nextIt = findNextBufferedRtpPacket(m_rtpReorderExpectedSequence);
            if (nextIt == m_rtpReorderBuffer.end()) {
                auto staleIt = m_rtpReorderBuffer.begin();
                while (staleIt != m_rtpReorderBuffer.end()) {
                    if (compareRtpSequence(staleIt->first, m_rtpReorderExpectedSequence) >= 0) {
                        break;
                    }
                    staleIt = m_rtpReorderBuffer.erase(staleIt);
                }
                break;
            }

            const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   now - nextIt->second.arrivalTime)
                                   .count();
            if (!allowGapAdvance &&
                m_rtpReorderBuffer.size() < m_rtpReorderMaxPackets &&
                ageMs < m_rtpReorderMaxDelayMs) {
                break;
            }

            m_rtpReorderExpectedSequence = nextIt->first;
        }
    }

    void handleRtpPackets() {
        if (m_udpSession && m_udpSession->isRunning()) {
            return;
        }
        if (!m_rtpSocket || !m_rtpSocket->isOpen()) {
            return;
        }
        while (m_rtpSocket->hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            SwByteArray datagram = m_rtpSocket->receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                break;
            }
            if (senderPort != 0) {
                if (m_detectedRtpPort == 0) {
                    swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Detected RTP source port "
                              << sender.toStdString() << ":" << senderPort;
                } else if (m_detectedRtpPort != senderPort) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTP source port changed from "
                                << m_detectedRtpPort << " to " << senderPort;
                }
                m_detectedRtpPort = senderPort;
            }
            auto total = ++m_rtpPackets;
            if (total <= 3 || (total % 200) == 0) {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTP datagram #" << total
                          << " bytes=" << datagram.size()
                          << " from " << sender.toStdString() << ":" << senderPort;
            }
            queueRtpDatagram(datagram, std::chrono::steady_clock::now());
            drainBufferedRtpPackets(false);
        }
    }

    bool handleRtpPacket(const uint8_t* data, size_t len) {
        if (!data || len < 12) {
            return false;
        }
        uint8_t version = data[0] >> 6;
        if (version != 2) {
            return false;
        }
        bool padding = (data[0] & 0x20) != 0;
        bool extension = (data[0] & 0x10) != 0;
        uint8_t csrcCount = data[0] & 0x0F;
        bool marker = (data[1] & 0x80) != 0;
        uint8_t payloadType = data[1] & 0x7F;
        if (payloadType != static_cast<uint8_t>(m_payloadType)) {
            auto dropped = ++m_payloadMismatch;
            if (dropped <= 3 || (dropped % 50) == 0) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Dropping RTP (PT=" << static_cast<int>(payloadType)
                            << " expected " << m_payloadType << ")";
            }
            return false;
        }
        uint16_t sequenceNumber = static_cast<uint16_t>((static_cast<uint16_t>(data[2]) << 8) |
                                                        static_cast<uint16_t>(data[3]));
        uint32_t senderSsrc = (static_cast<uint32_t>(data[8]) << 24) |
                              (static_cast<uint32_t>(data[9]) << 16) |
                              (static_cast<uint32_t>(data[10]) << 8) |
                              static_cast<uint32_t>(data[11]);
        if (m_remoteSsrc == 0 && senderSsrc != 0) {
            m_remoteSsrc = senderSsrc;
            requestKeyFrame("startup");
        }
        if (m_haveSequence) {
            uint16_t expected = static_cast<uint16_t>(m_lastSequence + 1);
            int16_t delta = static_cast<int16_t>(sequenceNumber - expected);
            if (delta != 0) {
                auto gapCount = ++m_sequenceDiscontinuities;
                if (delta > 1 && (gapCount <= 3 || (gapCount % 25) == 0)) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTP sequence discontinuity expected="
                                << expected << " got=" << sequenceNumber;
                }
                if (delta < 0) {
                    // Late / reordered packet: ignore it but keep the stream alive.
                    return true;
                }
                handleUdpSessionGap_(expected, sequenceNumber);
            }
        }
        m_lastSequence = sequenceNumber;
        m_haveSequence = true;
        uint32_t timestamp = (static_cast<uint32_t>(data[4]) << 24) |
                             (static_cast<uint32_t>(data[5]) << 16) |
                             (static_cast<uint32_t>(data[6]) << 8) |
                             static_cast<uint32_t>(data[7]);

        size_t offset = 12 + static_cast<size_t>(csrcCount) * 4;
        if (offset > len) {
            return false;
        }
        if (extension) {
            if (offset + 4 > len) {
                return false;
            }
            uint16_t extLen = (static_cast<uint16_t>(data[offset + 2]) << 8) | static_cast<uint16_t>(data[offset + 3]);
            offset += 4 + static_cast<size_t>(extLen) * 4;
        }
        if (offset >= len) {
            return false;
        }
        size_t payloadLen = len - offset;
        if (padding && payloadLen > 0) {
            uint8_t padCount = data[len - 1];
            if (padCount < payloadLen) {
                payloadLen -= padCount;
            }
        }
        SwRtpSession::Packet packet;
        packet.payload = SwByteArray(reinterpret_cast<const char*>(data + offset), static_cast<int>(payloadLen));
        packet.payloadType = payloadType;
        packet.sequenceNumber = sequenceNumber;
        packet.timestamp = timestamp;
        packet.ssrc = senderSsrc;
        packet.marker = marker;
        processTransportPacket_(packet);
        return true;
    }

    void handleRtcpPackets() {
        if (m_udpSession && m_udpSession->isRunning()) {
            return;
        }
        if (!m_rtcpSocket || !m_rtcpSocket->isOpen()) {
            return;
        }
        while (m_rtcpSocket->hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            SwByteArray datagram = m_rtcpSocket->receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                break;
            }
            if (senderPort != 0) {
                if (m_detectedRtcpPort == 0) {
                    swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Detected RTCP source port "
                              << sender.toStdString() << ":" << senderPort;
                } else if (m_detectedRtcpPort != senderPort) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTCP source port changed from "
                                << m_detectedRtcpPort << " to " << senderPort;
                }
                m_detectedRtcpPort = senderPort;
                uint16_t inferredRtp = static_cast<uint16_t>(senderPort > 0 ? senderPort - 1 : senderPort);
                if (inferredRtp != 0 && inferredRtp != m_detectedRtpPort) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Inferring RTP source port "
                                << sender.toStdString() << ":" << inferredRtp
                                << " from RTCP";
                    m_detectedRtpPort = inferredRtp;
                    sendUdpPunch(m_rtpSocket, inferredRtp, "RTP-inferred");
                }
            }
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTCP datagram bytes=" << datagram.size()
                      << " from " << sender.toStdString() << ":" << senderPort;
            handleRtcpPacket(reinterpret_cast<const uint8_t*>(datagram.constData()),
                             static_cast<size_t>(datagram.size()));
        }
    }

    void handleRtcpPacket(const uint8_t* data, size_t len) {
        if (!data || len < 4) {
            return;
        }
        uint8_t version = data[0] >> 6;
        uint8_t packetType = data[1];
        uint16_t words = (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]);
        swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTCP packet v=" << static_cast<int>(version)
                  << " pt=" << static_cast<int>(packetType)
                  << " words=" << words;
        if (packetType == 200 && len >= 28) {
            uint32_t senderSsrc = (static_cast<uint32_t>(data[4]) << 24) |
                                  (static_cast<uint32_t>(data[5]) << 16) |
                                  (static_cast<uint32_t>(data[6]) << 8) |
                                  static_cast<uint32_t>(data[7]);
            m_remoteSsrc = senderSsrc;
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTCP Sender Report SSRC=0x"
                      << std::hex << senderSsrc << std::dec;
            sendRtcpReceiverReport(senderSsrc);
        }
    }

    void sendRtcpReceiverReport(uint32_t senderSsrc) {
        if (!m_rtcpSocket || !m_rtcpSocket->isOpen()) {
            return;
        }
        uint16_t targetPort = m_detectedRtcpPort ? m_detectedRtcpPort : m_serverRtcpPort;
        if (targetPort == 0) {
            return;
        }
        uint8_t packet[8] = {0};
        packet[0] = 0x80; // Version 2, no padding, zero report blocks
        packet[1] = 201;  // Receiver Report
        packet[2] = 0x00;
        packet[3] = 0x01; // length in 32-bit words minus one
        uint32_t mySsrc = m_localSsrc ? m_localSsrc : generateSsrc();
        packet[4] = static_cast<uint8_t>((mySsrc >> 24) & 0xFF);
        packet[5] = static_cast<uint8_t>((mySsrc >> 16) & 0xFF);
        packet[6] = static_cast<uint8_t>((mySsrc >> 8) & 0xFF);
        packet[7] = static_cast<uint8_t>(mySsrc & 0xFF);
        int64_t sent = m_rtcpSocket->writeDatagram(reinterpret_cast<const char*>(packet), 8,
                                                   m_host, targetPort);
        if (sent > 0) {
            m_lastRtcpKeepAliveTime = std::chrono::steady_clock::now();
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Sent RTCP RR (local SSRC=0x"
                      << std::hex << mySsrc << std::dec
                      << ", remote SSRC=0x" << std::hex << senderSsrc << std::dec
                      << ", port=" << targetPort << ")";
        } else {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to send RTCP RR";
        }
    }

    void maybeSendRtcpKeepAlive() {
        if (m_udpSession && m_udpSession->isRunning()) {
            return;
        }
        if (!isRunning() || m_state != RtspStep::Playing || m_useTcpTransport) {
            return;
        }
        if (m_remoteSsrc == 0) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (m_lastRtcpKeepAliveTime.time_since_epoch().count() != 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastRtcpKeepAliveTime).count();
            if (elapsed < 1000) {
                return;
            }
        }
        sendRtcpReceiverReport(m_remoteSsrc);
    }

    void requestKeyFrame(const char* reason) {
        if (m_udpSession && m_udpSession->isRunning() && !m_useTcpTransport) {
            m_udpSession->requestKeyFrame(reason ? SwString(reason) : SwString());
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (m_remoteSsrc == 0) {
            return;
        }
        if (m_lastPliTime.time_since_epoch().count() != 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPliTime).count();
            if (elapsed < 1000) {
                return;
            }
        }
        m_lastPliTime = now;
        sendRtcpPli(m_remoteSsrc, reason);
    }

    void sendRtcpPli(uint32_t mediaSsrc, const char* reason) {
        if (!m_rtcpSocket || !m_rtcpSocket->isOpen()) {
            return;
        }
        uint16_t targetPort = m_detectedRtcpPort ? m_detectedRtcpPort : m_serverRtcpPort;
        if (targetPort == 0) {
            return;
        }
        uint8_t packet[12] = {0};
        packet[0] = 0x81; // V=2, FMT=1 (PLI)
        packet[1] = 206;  // PSFB
        packet[2] = 0x00;
        packet[3] = 0x02; // 12 bytes => 3 words, minus one => 2
        uint32_t mySsrc = m_localSsrc ? m_localSsrc : generateSsrc();
        packet[4] = static_cast<uint8_t>((mySsrc >> 24) & 0xFF);
        packet[5] = static_cast<uint8_t>((mySsrc >> 16) & 0xFF);
        packet[6] = static_cast<uint8_t>((mySsrc >> 8) & 0xFF);
        packet[7] = static_cast<uint8_t>(mySsrc & 0xFF);
        packet[8] = static_cast<uint8_t>((mediaSsrc >> 24) & 0xFF);
        packet[9] = static_cast<uint8_t>((mediaSsrc >> 16) & 0xFF);
        packet[10] = static_cast<uint8_t>((mediaSsrc >> 8) & 0xFF);
        packet[11] = static_cast<uint8_t>(mediaSsrc & 0xFF);
        int64_t sent = m_rtcpSocket->writeDatagram(reinterpret_cast<const char*>(packet), 12, m_host, targetPort);
        if (sent > 0) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Sent RTCP PLI"
                        << (reason ? SwString(" (") + SwString(reason) + SwString(")") : SwString())
                        << " remote SSRC=0x" << std::hex << mediaSsrc << std::dec
                        << " port=" << targetPort;
        } else {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to send RTCP PLI";
        }
    }

    void checkRtpTimeout() {
        if (m_udpSession && m_udpSession->isRunning() && !m_useTcpTransport) {
            return;
        }
        if (!isRunning() || m_state != RtspStep::Playing) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (m_lastRtpTime.time_since_epoch().count() == 0) {
            if (m_rtpPackets.load() == 0 && (now.time_since_epoch().count() % 1000000000LL) < 10000000LL) {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Waiting for RTP...";
            }
            return;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastRtpTime).count();
        if (elapsed > 3) {
            if (!m_useTcpTransport && !m_triedTcpFallback) {
                triggerTcpFallback("UDP stalled");
                return;
            }
            auto previousElapsed = m_lastLoggedRtpTimeoutSec.exchange(elapsed);
            if (previousElapsed != elapsed) {
                uint16_t portHint = m_detectedRtpPort ? m_detectedRtpPort : m_serverRtpPort;
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] No RTP received for " << elapsed
                            << " s (local=" << m_clientRtpPort
                            << ", expected remote=" << portHint << ")";
            }
            emitStatus(SwVideoSource::StreamState::Recovering, "Video stream stalled...");
            return;
        }
        m_lastLoggedRtpTimeoutSec.store(-1);
    }

    uint64_t rtpDelta(uint32_t newer, uint32_t older) const {
        if (newer >= older) {
            return static_cast<uint64_t>(newer - older);
        }
        return static_cast<uint64_t>(newer) + (0x100000000ULL - static_cast<uint64_t>(older));
    }

    bool shouldDropFrame(uint32_t timestamp, bool keyFrame) const {
        if (!m_lowLatencyDrop || keyFrame || !m_haveFirstTimestamp || m_clockRate <= 0) {
            return false;
        }
        if (m_playStart.time_since_epoch().count() == 0) {
            return false;
        }
        auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - m_playStart)
                          .count();
        if (wallMs < 0) {
            wallMs = 0;
        }
        uint64_t wallRtp = (static_cast<uint64_t>(wallMs) * static_cast<uint64_t>(m_clockRate)) / 1000ULL;
        uint64_t mediaRtp = rtpDelta(timestamp, m_firstTimestamp);
        uint64_t allowedLag = (static_cast<uint64_t>(m_latencyTargetMs) * static_cast<uint64_t>(m_clockRate)) / 1000ULL;
        if (allowedLag == 0) {
            allowedLag = static_cast<uint64_t>(m_clockRate) / 100ULL;
        }
        return mediaRtp > (wallRtp + allowedLag);
    }

    void flushFrame(uint32_t timestamp) {
        if (m_nalBuffer.empty()) {
            return;
        }
        bool carriesH264Headers = (m_currentCodec == SwVideoPacket::Codec::H264) &&
                                  (m_currentAccessUnitHasH264Headers || m_currentAccessUnitInjectedH264Headers);
        bool carriesHevcHeaders = (m_currentCodec == SwVideoPacket::Codec::H265) &&
                                  (m_currentAccessUnitHasHevcHeaders || m_currentAccessUnitInjectedHevcHeaders);
        bool carriesCodecHeaders = carriesH264Headers || carriesHevcHeaders;
        if (m_waitingForKeyFrame && !m_currentKeyFrame && !carriesCodecHeaders) {
            auto dropped = ++m_framesDropped;
            if (!m_loggedWaitingForKeyFrame.exchange(true)) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Dropping non-key frame while waiting for a decodable keyframe";
            } else if (dropped <= 3 || (dropped % 25) == 0) {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Still waiting for keyframe, dropped frame #"
                          << dropped << " ts=" << timestamp;
            }
            m_nalBuffer.clear();
            m_currentKeyFrame = false;
            m_currentAccessUnitHasH264Headers = false;
            m_currentAccessUnitInjectedH264Headers = false;
            m_currentAccessUnitHasHevcHeaders = false;
            m_currentAccessUnitInjectedHevcHeaders = false;
            return;
        }
        if (shouldDropFrame(timestamp, m_currentKeyFrame)) {
            auto dropped = ++m_framesDropped;
            if (dropped <= 3 || (dropped % 25) == 0) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Dropping frame #" << dropped
                            << " to reduce latency (ts=" << timestamp << ")";
            }
            m_nalBuffer.clear();
            m_currentKeyFrame = false;
            m_currentAccessUnitHasH264Headers = false;
            m_currentAccessUnitInjectedH264Headers = false;
            m_currentAccessUnitHasHevcHeaders = false;
            m_currentAccessUnitInjectedHevcHeaders = false;
            return;
        }
        if (m_currentKeyFrame && m_waitingForKeyFrame) {
            m_waitingForKeyFrame = false;
            m_loggedWaitingForKeyFrame.store(false);
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Received decodable keyframe";
        }
        emitStatus(SwVideoSource::StreamState::Streaming, "Streaming");
        SwByteArray payload(reinterpret_cast<const char*>(m_nalBuffer.data()),
                            static_cast<int>(m_nalBuffer.size()));
        SwVideoPacket packet(m_currentCodec,
                             payload,
                             static_cast<std::int64_t>(timestamp),
                             static_cast<std::int64_t>(timestamp),
                             m_currentKeyFrame);
        if (m_emitDecoderDiscontinuityOnNextFrame) {
            packet.setDiscontinuity(true);
            m_emitDecoderDiscontinuityOnNextFrame = false;
        }
        emitPacket(packet);
        auto emitted = ++m_framesEmitted;
        if (!m_loggedFirstCompressedFrame.exchange(true)) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] First compressed frame emitted "
                        << " codec=" << static_cast<int>(packet.codec())
                        << " bytes=" << payload.size()
                        << " key=" << (m_currentKeyFrame ? 1 : 0)
                        << " ts=" << timestamp;
        }
        if (emitted <= 3 || (emitted % 100) == 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Emitted frame #" << emitted
                      << " bytes=" << payload.size()
                      << " key=" << (m_currentKeyFrame ? 1 : 0);
        }
        if (m_currentCodec == SwVideoPacket::Codec::H264 && carriesH264Headers) {
            m_h264HeadersInserted = true;
        }
        if (m_currentCodec == SwVideoPacket::Codec::H265 && carriesHevcHeaders) {
            m_hevcHeadersInserted = true;
        }
        m_nalBuffer.clear();
        m_currentKeyFrame = false;
        m_currentAccessUnitHasH264Headers = false;
        m_currentAccessUnitInjectedH264Headers = false;
        m_currentAccessUnitHasHevcHeaders = false;
        m_currentAccessUnitInjectedHevcHeaders = false;
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static void trim(std::string& s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.erase(s.begin());
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.pop_back();
        }
    }

    uint32_t generateSsrc() const {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dist(1u, 0xFFFFFFFFu);
        return dist(gen);
    }

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> out;
        std::string current;
        for (char c : s) {
            if (c == delim) {
                if (!current.empty()) {
                    out.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) {
            out.push_back(current);
        }
        return out;
    }

    SwString m_url;
    SwString m_host;
    int m_port{554};
    SwString m_path{"/"};
    SwString m_bindAddress{};
    std::string m_baseUrl;
    std::map<int, RtspRequest> m_pendingRequests;
    int m_maxRequestRetries{1};

    SwObject* m_callbackContext{nullptr};
    SwTcpSocket* m_rtspSocket{nullptr};
    SwUdpSocket* m_rtpSocket{nullptr};
    SwUdpSocket* m_rtcpSocket{nullptr};
    std::unique_ptr<SwRtpSession> m_udpSession{};
    std::unique_ptr<SwRtpSession> m_audioUdpSession{};
    std::unique_ptr<SwRtpSession> m_metadataUdpSession{};
    std::chrono::steady_clock::time_point m_lastWaitingKeyFrameRequestTime{};
    SwTimer* m_monitorTimer{nullptr};
    SwTimer* m_keepAliveTimer{nullptr};
    SwTimer* m_reconnectTimer{nullptr};

    std::string m_ctrlBuffer;
    std::string m_sdp;
    std::vector<RtspTrack> m_tracks;
    int m_selectedTrackIndex{-1};
    int m_selectedAudioTrackIndex{-1};
    int m_selectedMetadataTrackIndex{-1};
    std::string m_trackControl;
    std::string m_audioTrackControl;
    std::string m_metadataTrackControl;
    std::string m_sessionControl;
    std::string m_sessionId;
    RtspStep m_state{RtspStep::None};
    int m_cseq{0};
    int m_payloadType{96};
    int m_clockRate{90000};
    int m_audioPayloadType{-1};
    int m_audioClockRate{0};
    int m_audioChannelCount{0};
    int m_metadataPayloadType{-1};
    int m_metadataClockRate{0};
    std::atomic<bool> m_triedAggregatePlay{false};
    std::string m_codecName{"h264"};
    std::string m_audioCodecName{};
    std::string m_metadataCodecName{};
    bool m_enableAudio{false};
    bool m_enableMetadata{false};
    std::atomic<bool> m_loggedUnsupportedCodec{false};
    bool m_useTcpTransport{false};
    bool m_triedTcpFallback{false};
    bool m_autoTcpFallbackActive{false};
    std::atomic<bool> m_autoReconnect{false};

    uint16_t m_clientRtpPort{0};
    uint16_t m_clientRtcpPort{0};
    uint16_t m_serverRtpPort{0};
    uint16_t m_serverRtcpPort{0};
    uint16_t m_audioClientRtpPort{0};
    uint16_t m_audioClientRtcpPort{0};
    uint16_t m_audioServerRtpPort{0};
    uint16_t m_audioServerRtcpPort{0};
    uint16_t m_metadataClientRtpPort{0};
    uint16_t m_metadataClientRtcpPort{0};
    uint16_t m_metadataServerRtpPort{0};
    uint16_t m_metadataServerRtcpPort{0};
    uint16_t m_forcedClientRtpPort{0};
    uint16_t m_forcedClientRtcpPort{0};
    uint8_t m_interleavedRtpChannel{0};
    uint8_t m_interleavedRtcpChannel{1};
    uint8_t m_audioInterleavedRtpChannel{2};
    uint8_t m_audioInterleavedRtcpChannel{3};
    uint8_t m_metadataInterleavedRtpChannel{4};
    uint8_t m_metadataInterleavedRtcpChannel{5};
    std::vector<SetupTarget> m_setupTargets{};
    std::size_t m_setupTargetIndex{0};

    uint32_t m_currentTimestamp{0};
    bool m_haveTimestamp{false};
    bool m_haveFirstTimestamp{false};
    bool m_haveSequence{false};
    uint32_t m_firstTimestamp{0};
    bool m_currentKeyFrame{false};
    uint16_t m_lastSequence{0};
    std::atomic<uint64_t> m_rtpPackets{0};
    std::atomic<uint64_t> m_framesEmitted{0};
    std::atomic<uint64_t> m_framesDropped{0};
    std::atomic<uint64_t> m_payloadMismatch{0};
    std::atomic<uint64_t> m_sequenceDiscontinuities{0};
    std::atomic<bool> m_loggedFirstCompressedFrame{false};
    std::vector<uint8_t> m_nalBuffer;
    bool m_lowLatencyDrop{false};
    int m_latencyTargetMs{150};
    SwVideoPacket::Codec m_currentCodec{SwVideoPacket::Codec::H264};
    std::atomic<bool> m_placeholderActive{false};
    SwByteArray m_placeholderPayload;
    SwVideoFormatInfo m_placeholderFormat{};
    std::chrono::steady_clock::time_point m_lastPlaceholderTime{};
    int m_placeholderIntervalMs{500};
    std::atomic<uint64_t> m_placeholderFramesEmitted{0};
    std::atomic<int> m_rtspDisconnectSuppress{0};
    std::chrono::steady_clock::time_point m_lastRtpTime{};
    std::atomic<long long> m_lastLoggedRtpTimeoutSec{-1};
    std::chrono::steady_clock::time_point m_playStart{};
    std::chrono::steady_clock::time_point m_lastPliTime{};
    std::chrono::steady_clock::time_point m_lastRtcpKeepAliveTime{};
    std::string m_ctrlAccum;
    std::string m_ctrlTextBuffer;
    uint32_t m_localSsrc{0};
    uint32_t m_remoteSsrc{0};
    uint16_t m_detectedRtpPort{0};
    uint16_t m_detectedRtcpPort{0};
    std::map<uint16_t, BufferedRtpPacket> m_rtpReorderBuffer;
    bool m_rtpReorderExpectedValid{false};
    uint16_t m_rtpReorderExpectedSequence{0};
    std::size_t m_rtpReorderMaxPackets{24};
    int m_rtpReorderMaxDelayMs{12};
    int m_reconnectDelayMs{2000};
    std::atomic<bool> m_reconnectPending{false};
    std::vector<uint8_t> m_h264Sps;
    std::vector<uint8_t> m_h264Pps;
    bool m_h264HeadersInserted{false};
    bool m_currentAccessUnitHasH264Headers{false};
    bool m_currentAccessUnitInjectedH264Headers{false};
    bool m_currentAccessUnitHasHevcHeaders{false};
    bool m_currentAccessUnitInjectedHevcHeaders{false};
    bool m_dropCurrentAccessUnit{false};
    bool m_emitDecoderDiscontinuityOnNextFrame{true};
    int m_rtpResyncGapThreshold{32};
    std::atomic<bool> m_loggedH264Fmtp{false};
    std::atomic<bool> m_loggedH264ParameterSetInjection{false};
    std::atomic<bool> m_loggedMissingH264ParameterSets{false};
    std::atomic<bool> m_loggedInBandH264ParameterSets{false};
    std::atomic<bool> m_loggedH265Fmtp{false};
    std::atomic<bool> m_loggedH265ParameterSetInjection{false};
    std::atomic<bool> m_loggedMissingH265ParameterSets{false};
    std::atomic<bool> m_loggedInBandH265ParameterSets{false};
    bool m_waitingForKeyFrame{true};
    std::atomic<bool> m_loggedWaitingForKeyFrame{false};
    std::vector<uint8_t> m_hevcVps;
    std::vector<uint8_t> m_hevcSps;
    std::vector<uint8_t> m_hevcPps;
    bool m_hevcHeadersInserted{false};
    std::string m_hevcProfileLevelId;
    SwRtpDepacketizerH264 m_h264Depacketizer{};
    SwRtpDepacketizerH265 m_h265Depacketizer{};
    SwMpegTsDemux m_sharedTsDemux{};
    struct TsDemux {
        bool patParsed{false};
        bool pmtParsed{false};
        std::vector<uint16_t> pmtPids;
        uint16_t videoPid{0};
        std::vector<uint8_t> tsBuffer;
        std::vector<uint8_t> pesBuffer;
        bool pesKey{false};
        uint64_t pesCount{0};
        uint64_t tsPackets{0};
        bool loggedNoVideoPid{false};
        bool loggedNonVideo{false};
        uint64_t pesPts{0};
        bool hasPesPts{false};
        bool hevcStream{false};

        /**
         * @brief Resets the object to a baseline state.
         */
        void reset() {
            patParsed = false;
            pmtParsed = false;
            pmtPids.clear();
            videoPid = 0;
            tsBuffer.clear();
            pesBuffer.clear();
            pesKey = false;
            pesCount = 0;
            tsPackets = 0;
            loggedNoVideoPid = false;
            loggedNonVideo = false;
            pesPts = 0;
            hasPesPts = false;
            hevcStream = false;
        }

        /**
         * @brief Returns whether the object reports start Code H264 Idr.
         * @param data Value passed to the method.
         * @return The requested start Code H264 Idr.
         *
         * @details This query does not modify the object state.
         */
        static bool hasStartCodeH264Idr(const std::vector<uint8_t>& data) {
            for (size_t i = 0; i + 4 < data.size(); ++i) {
                if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                    uint8_t nal = data[i + 4] & 0x1F;
                    if (nal == 5) {
                        return true;
                    }
                } else if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
                    uint8_t nal = data[i + 3] & 0x1F;
                    if (nal == 5) {
                        return true;
                    }
                }
            }
            return false;
        }

        /**
         * @brief Returns whether the object reports start Code Hevc Idr.
         * @param data Value passed to the method.
         * @return The requested start Code Hevc Idr.
         *
         * @details This query does not modify the object state.
         */
        static bool hasStartCodeHevcIdr(const std::vector<uint8_t>& data) {
            for (size_t i = 0; i + 5 < data.size(); ++i) {
                if (data[i] == 0x00 && data[i + 1] == 0x00 &&
                    ((data[i + 2] == 0x00 && data[i + 3] == 0x01) || (data[i + 2] == 0x01))) {
                    size_t headerIdx = (data[i + 2] == 0x01) ? i + 3 : i + 4;
                    if (headerIdx + 1 >= data.size()) {
                        continue;
                    }
                    uint8_t nalType = static_cast<uint8_t>((data[headerIdx] >> 1) & 0x3F);
                    if (nalType >= 16 && nalType <= 21) {
                        return true;
                    }
                }
            }
            return false;
        }

        /**
         * @brief Returns whether the object reports hevc.
         * @return `true` when the object reports hevc; otherwise `false`.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        bool isHevc() const { return hevcStream; }

        /**
         * @brief Performs the `feed` operation.
         * @param data Value passed to the method.
         * @param size Size value used by the operation.
         * @param rtpTs Value passed to the method.
         * @param emitFrame Value passed to the method.
         */
        void feed(const uint8_t* data, size_t size, uint32_t rtpTs,
                  const std::function<void(const std::vector<uint8_t>&, bool, uint32_t)>& emitFrame) {
            if (!data || size == 0) {
                return;
            }
            ++tsPackets;
            tsBuffer.insert(tsBuffer.end(), data, data + size);
            while (tsBuffer.size() >= 188) {
                std::vector<uint8_t> pkt(tsBuffer.begin(), tsBuffer.begin() + 188);
                tsBuffer.erase(tsBuffer.begin(), tsBuffer.begin() + 188);
                if (pkt[0] != 0x47) {
                    if ((tsPackets % 50) == 0) {
                        swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] Bad sync byte, dropping packet " << tsPackets;
                    }
                    continue;
                }
                bool payloadStart = (pkt[1] & 0x40) != 0;
                uint16_t pid = static_cast<uint16_t>(((pkt[1] & 0x1F) << 8) | pkt[2]);
                uint8_t afc = static_cast<uint8_t>((pkt[3] >> 4) & 0x3);
                size_t offset = 4;
                if (afc & 0x2) {
                    if (offset >= pkt.size()) {
                        continue;
                    }
                    uint8_t afl = pkt[offset];
                    offset += 1 + afl;
                }
                if (!(afc & 0x1) || offset >= pkt.size()) {
                    continue;
                }
                const uint8_t* pay = pkt.data() + offset;
                size_t paySize = pkt.size() - offset;

                if (pid == 0) {
                    parsePAT(pay, paySize, payloadStart);
                    continue;
                }
                for (auto p : pmtPids) {
                    if (pid == p) {
                        parsePMT(pay, paySize, payloadStart);
                        break;
                    }
                }
                if (videoPid != 0 && pid == videoPid) {
                    handlePES(pay, paySize, payloadStart, rtpTs, emitFrame);
                }
            }
            if (videoPid == 0 && (tsPackets % 200) == 0 && !loggedNoVideoPid) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] Still no video PID after " << tsPackets << " TS packets";
            }
        }

        /**
         * @brief Performs the `parsePAT` operation.
         * @param data Value passed to the method.
         * @param size Size value used by the operation.
         * @param payloadStart Value passed to the method.
         */
        void parsePAT(const uint8_t* data, size_t size, bool payloadStart) {
            if (!payloadStart || size < 8 || patParsed) {
                return;
            }
            size_t idx = static_cast<size_t>(data[0]) + 1; // pointer_field
            if (idx + 8 > size) {
                return;
            }
            if (data[idx] != 0x00) {
                return;
            }
            size_t sectionLen = ((data[idx + 1] & 0x0F) << 8) | data[idx + 2];
            size_t end = idx + 3 + sectionLen;
            if (end > size) {
                return;
            }
            size_t pos = idx + 8; // skip to program loop
            if (pos + 4 > end) {
                return;
            }
            uint16_t programMapPid = static_cast<uint16_t>(((data[pos + 2] & 0x1F) << 8) | data[pos + 3]);
            if (std::find(pmtPids.begin(), pmtPids.end(), programMapPid) == pmtPids.end()) {
                pmtPids.push_back(programMapPid);
            }
            patParsed = true;
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PAT found PMT PID=" << programMapPid << " (total PMTs=" << pmtPids.size() << ")";
        }

        /**
         * @brief Performs the `parsePMT` operation.
         * @param data Value passed to the method.
         * @param size Size value used by the operation.
         * @param payloadStart Value passed to the method.
         */
        void parsePMT(const uint8_t* data, size_t size, bool payloadStart) {
            if (!payloadStart || size < 12) {
                return;
            }
            if (pmtParsed && videoPid != 0) {
                return;
            }
            size_t idx = static_cast<size_t>(data[0]) + 1; // pointer_field
            if (idx + 12 > size) {
                return;
            }
            if (data[idx] != 0x02) {
                return;
            }
            size_t sectionLen = ((data[idx + 1] & 0x0F) << 8) | data[idx + 2];
            size_t end = idx + 3 + sectionLen;
            if (end > size) {
                return;
            }
            size_t programInfoLen = ((data[idx + 10] & 0x0F) << 8) | data[idx + 11];
            size_t pos = idx + 12 + programInfoLen;
            while (pos + 5 <= end) {
                uint8_t streamType = data[pos];
                uint16_t elemPid = static_cast<uint16_t>(((data[pos + 1] & 0x1F) << 8) | data[pos + 2]);
                uint16_t esInfoLen = static_cast<uint16_t>(((data[pos + 3] & 0x0F) << 8) | data[pos + 4]);
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PMT entry streamType=0x" << std::hex << static_cast<int>(streamType)
                          << std::dec << " PID=" << elemPid;
                if (streamType == 0x1B) { // H264
                    videoPid = elemPid;
                    pmtParsed = true;
                    hevcStream = false;
                    swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PMT found video PID=" << videoPid << " streamType=0x1B";
                    return;
                } else if (streamType == 0x24) { // H265
                    videoPid = elemPid;
                    pmtParsed = true;
                    hevcStream = true;
                    swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PMT found video PID=" << videoPid << " streamType=0x24 (HEVC)";
                    return;
                } else if (!loggedNonVideo) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PMT streamType=0x" << std::hex << static_cast<int>(streamType)
                                << std::dec << " (not supported video), PID=" << elemPid;
                    loggedNonVideo = true;
                }
                pos += 5 + esInfoLen;
            }
        }

        /**
         * @brief Performs the `parsePts` operation.
         * @param data Value passed to the method.
         * @param size Size value used by the operation.
         * @param ptsOut Value passed to the method.
         * @return The requested parse Pts.
         */
        static bool parsePts(const uint8_t* data, size_t size, uint64_t& ptsOut) {
            if (size < 14) {
                return false;
            }
            uint8_t flags = data[7];
            uint8_t headerLen = data[8];
            if (!(flags & 0x80)) {
                return false;
            }
            if (headerLen < 5 || size < 9 + headerLen) {
                return false;
            }
            const uint8_t* p = data + 9;
            uint64_t pts = 0;
            pts |= (static_cast<uint64_t>((p[0] >> 1) & 0x07) << 30);
            pts |= (static_cast<uint64_t>(p[1]) << 22) | (static_cast<uint64_t>((p[2] >> 1) & 0x7F) << 15);
            pts |= (static_cast<uint64_t>(p[3]) << 7) | (static_cast<uint64_t>((p[4] >> 1) & 0x7F));
            ptsOut = pts;
            return true;
        }

        /**
         * @brief Performs the `handlePES` operation.
         * @param data Value passed to the method.
         * @param size Size value used by the operation.
         * @param payloadStart Value passed to the method.
         * @param rtpTs Value passed to the method.
         * @param emitFrame Value passed to the method.
         */
        void handlePES(const uint8_t* data, size_t size, bool payloadStart, uint32_t rtpTs,
                       const std::function<void(const std::vector<uint8_t>&, bool, uint32_t)>& emitFrame) {
            if (payloadStart) {
                if (!pesBuffer.empty()) {
                    emitFrame(pesBuffer, pesKey, hasPesPts ? static_cast<uint32_t>(pesPts & 0xFFFFFFFFu) : rtpTs);
                    pesBuffer.clear();
                    pesKey = false;
                    hasPesPts = false;
                }
                if (size < 6) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PES start too short size=" << size;
                    return;
                }
                if (!(data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01)) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PES start missing start code";
                    return;
                }
                uint8_t streamId = data[3];
                if ((streamId & 0xF0) != 0xE0 && streamId != 0x1B) {
                    if ((pesCount % 20) == 0) {
                        swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PES stream id not video: 0x" << std::hex << static_cast<int>(streamId) << std::dec;
                    }
                    return;
                }
                size_t headerLen = (size > 8) ? static_cast<size_t>(data[8]) : 0;
                size_t payloadOffset = 9 + headerLen;
                if (payloadOffset > size) {
                    swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] PES header truncated size=" << size << " headerLen=" << headerLen;
                    return;
                }
                uint64_t parsedPts = 0;
                if (parsePts(data, size, parsedPts)) {
                    pesPts = parsedPts;
                    hasPesPts = true;
                }
                const uint8_t* payload = data + payloadOffset;
                size_t payloadSize = size - payloadOffset;
                pesBuffer.insert(pesBuffer.end(), payload, payload + payloadSize);
                if (hevcStream ? hasStartCodeHevcIdr(pesBuffer) : hasStartCodeH264Idr(pesBuffer)) {
                    pesKey = true;
                }
            } else {
                pesBuffer.insert(pesBuffer.end(), data, data + size);
                if (hevcStream ? hasStartCodeHevcIdr(pesBuffer) : hasStartCodeH264Idr(pesBuffer)) {
                    pesKey = true;
                }
            }
            ++pesCount;
            if (pesCount <= 3 || (pesCount % 50) == 0) {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] Accumulating PES #" << pesCount
                          << " size=" << pesBuffer.size()
                          << " key=" << (pesKey ? 1 : 0)
                          << " pts=" << (hasPesPts ? static_cast<unsigned long long>(pesPts) : 0ULL);
            }
        }
    } m_tsDemux;
};

inline void SwRtspUdpSource::sendOptions() {
    sendRtsp("OPTIONS", fullUrl(), {});
}
