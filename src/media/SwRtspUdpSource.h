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
 * (single NAL, STAP-A/B, FU-A/B) to emit SwVideoPacket::H264 packets.
 **************************************************************************************************/

#include "media/SwMediaUrl.h"
#include "media/SwAudioDecoder.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwRtspTrackGraph.h"
#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "media/rtp/SwRtpSession.h"
#include "media/rtp/SwRtpSessionDescriptor.h"
#include "media/rtsp/SwRtspAuth.h"
#include "media/rtsp/SwRtspHeaderUtils.h"
#include "core/io/SwAbstractSocket.h"
#include "core/io/SwSslSocket.h"
#include "core/io/SwTcpSocket.h"
#include "core/runtime/SwThread.h"
#include "core/runtime/SwTimer.h"
#include "core/types/SwByteArray.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <condition_variable>
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
     * @param options Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwRtspUdpSource(const SwMediaOpenOptions& options, SwObject* parent = nullptr)
        : m_openOptions(options)
        , m_url(options.sourceUrl()) {
        SW_UNUSED(parent);
        applyOpenOptions_();
        parseUrl();
        m_sourceThread = new SwThread("SwRtspUdpSourceThread");
        m_sourceThread->start();
        m_callbackContext = new SwObject();
        m_trackGraph.reset(new SwRtspTrackGraph());
        m_rtspSocket = createControlSocket_();
        m_reconnectTimer = new SwTimer(m_reconnectDelayMs);
        m_reconnectTimer->setSingleShot(true);
        SwObject::connect(m_reconnectTimer, &SwTimer::timeout, m_callbackContext, [this]() { attemptReconnect(); });
        m_keepAliveTimer = new SwTimer(m_keepAliveIntervalMs);
        m_monitorTimer = new SwTimer(100);

        SwObject::connect(m_keepAliveTimer, &SwTimer::timeout, m_callbackContext, [this]() { sendKeepAlive(); });
        SwObject::connect(m_monitorTimer, &SwTimer::timeout, m_callbackContext, [this]() {
            maybeSendRtcpKeepAlive();
            checkRtpTimeout();
        });
        connectControlSocket_();

        if (m_sourceThread) {
            m_callbackContext->moveToThread(m_sourceThread);
            m_rtspSocket->moveToThread(m_sourceThread);
            m_reconnectTimer->moveToThread(m_sourceThread);
            m_keepAliveTimer->moveToThread(m_sourceThread);
            m_monitorTimer->moveToThread(m_sourceThread);
        }

        configureTrackGraph_();
    }

    SwRtspUdpSource(const SwString& url, SwObject* parent = nullptr)
        : SwRtspUdpSource(SwMediaOpenOptions::fromUrl(url), parent) {}

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
        if (fmtp.empty()) {
            return;
        }
        std::vector<uint8_t> sps;
        std::vector<uint8_t> pps;
        int packetizationMode = -1;
        auto parts = split(stripFmtpPayloadPrefix(fmtp), ';');
        for (auto& raw : parts) {
            std::string trimmed = raw;
            trim(trimmed);
            std::string entry = toLower(trimmed);
            if (entry.rfind("packetization-mode=", 0) == 0) {
                packetizationMode = std::atoi(trimmed.substr(trimmed.find('=') + 1).c_str());
                continue;
            }
            if (entry.rfind("sprop-parameter-sets=", 0) != 0) {
                continue;
            }
            auto value = trimmed.substr(trimmed.find('=') + 1);
            auto sets = split(value, ',');
            if (!sets.empty()) {
                sps = base64Decode(sets[0]);
            }
            if (sets.size() > 1) {
                pps = base64Decode(sets[1]);
            }
        }
        if (!m_loggedH264Fmtp.exchange(true)) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Parsed H264 fmtp"
                        << " packetizationMode=" << packetizationMode
                        << " sps=" << sps.size()
                        << " pps=" << pps.size();
        }
    }

    /**
     * @brief Performs the `parseH265Fmtp` operation.
     * @param fmtp Value passed to the method.
     */
    void parseH265Fmtp(const std::string& fmtp) {
        if (fmtp.empty()) {
            return;
        }
        std::vector<uint8_t> vps;
        std::vector<uint8_t> sps;
        std::vector<uint8_t> pps;
        auto parts = split(stripFmtpPayloadPrefix(fmtp), ';');
        for (auto& raw : parts) {
            std::string trimmed = raw;
            trim(trimmed);
            std::string entry = toLower(trimmed);
            if (entry.rfind("sprop-vps=", 0) == 0) {
                auto b64 = trimmed.substr(trimmed.find('=') + 1);
                auto data = base64Decode(b64);
                if (!data.empty()) {
                    vps = std::move(data);
                }
            }
            if (entry.rfind("sprop-sps=", 0) == 0) {
                auto b64 = trimmed.substr(trimmed.find('=') + 1);
                auto data = base64Decode(b64);
                if (!data.empty()) {
                    sps = std::move(data);
                }
            }
            if (entry.rfind("sprop-pps=", 0) == 0) {
                auto b64 = trimmed.substr(trimmed.find('=') + 1);
                auto data = base64Decode(b64);
                if (!data.empty()) {
                    pps = std::move(data);
                }
            }
        }
        if ((!vps.empty() || !sps.empty() || !pps.empty()) &&
            !m_loggedH265Fmtp.exchange(true)) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Parsed H265 fmtp"
                        << " vps=" << vps.size()
                        << " sps=" << sps.size()
                        << " pps=" << pps.size();
        }
    }

    /**
     * @brief Destroys the `SwRtspUdpSource` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwRtspUdpSource() override {
        runOnSourceThreadBlocking_([this]() {
            m_autoReconnect.store(false);
            cancelReconnect();
            if (m_monitorTimer) {
                m_monitorTimer->stop();
            }
            if (m_trackGraph) {
                m_trackGraph->stop();
            }
            stopStreaming(true);
            emitStatus(SwVideoSource::StreamState::Stopped, "Stream stopped");
        });
        if (m_sourceThread) {
            m_sourceThread->quit();
            m_sourceThread->wait();
        }
        delete m_monitorTimer;
        m_monitorTimer = nullptr;
        delete m_keepAliveTimer;
        m_keepAliveTimer = nullptr;
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
        delete m_rtspSocket;
        m_rtspSocket = nullptr;
        delete m_callbackContext;
        m_callbackContext = nullptr;
        delete m_sourceThread;
        m_sourceThread = nullptr;
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
    void setLocalAddress(const SwString& addr) {
        m_bindAddress = addr;
        m_openOptions.bindAddress = addr;
    }
    void setEnableAudio(bool enable) {
        m_enableAudio = enable;
        m_openOptions.enableAudio = enable;
    }
    void setEnableMetadata(bool enable) {
        m_enableMetadata = enable;
        m_openOptions.enableMetadata = enable;
    }
    /**
     * @brief Sets the use Tcp Transport.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUseTcpTransport(bool enable) {
        m_useTcpTransport = enable;
        m_openOptions.transport = enable ? SwMediaOpenOptions::TransportPreference::Tcp
                                         : SwMediaOpenOptions::TransportPreference::Udp;
    }

    void setCredentials(const SwString& userName, const SwString& password) {
        m_openOptions.userName = userName;
        m_openOptions.password = password;
        m_rtspUserName = userName.toStdString();
        m_rtspPassword = password.toStdString();
    }

    void setTrustedCaFile(const SwString& path) {
        m_openOptions.trustedCaFile = path;
        m_trustedCaFile = path;
        if (m_useTls && m_rtspSocket && !isRunning()) {
            if (auto* sslSocket = dynamic_cast<SwSslSocket*>(m_rtspSocket)) {
                sslSocket->setTrustedCaFile(path);
            }
        }
    }

    void setLowLatencyMode(bool enabled, int latencyTargetMs = 500) {
        m_lowLatencyDrop = enabled;
        if (latencyTargetMs > 0) {
            m_latencyTargetMs = latencyTargetMs;
        }
        if (m_trackGraph) {
            SwRtspTrackGraph::VideoConfig videoConfig;
            videoConfig.codec = isHevcCodec() ? SwVideoPacket::Codec::H265
                                              : SwVideoPacket::Codec::H264;
            videoConfig.payloadType = m_payloadType;
            videoConfig.clockRate = m_clockRate;
            videoConfig.liveTrimEnabled = m_lowLatencyDrop;
            videoConfig.latencyTargetMs = m_latencyTargetMs;
            videoConfig.transportStream = isTransportStreamCodec();
            if (m_selectedTrackIndex >= 0 &&
                static_cast<std::size_t>(m_selectedTrackIndex) < m_tracks.size()) {
                videoConfig.fmtp =
                    SwString(m_tracks[static_cast<std::size_t>(m_selectedTrackIndex)].fmtpLine);
            }
            m_trackGraph->setVideoConfig(videoConfig);
        }
    }

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
        m_openOptions.bindAddress = addr;
        m_openOptions.rtpPort = rtpPort;
        m_openOptions.rtcpPort = rtcpPort;
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() override {
        postToSourceThread_([this]() {
            m_autoReconnect.store(true);
            m_triedTcpFallback = m_useTcpTransport;
            m_autoTcpFallbackActive = false;
            if (m_trackGraph) {
                m_trackGraph->start();
            }
            if (!isRunning()) {
                setRunning(true);
            }
            if (m_monitorTimer && !m_monitorTimer->isActive()) {
                m_monitorTimer->start();
            }
            initiateConnection();
        });
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() override {
        m_autoReconnect.store(false);
        postToSourceThread_([this]() {
            cancelReconnect();
            if (m_monitorTimer) {
                m_monitorTimer->stop();
            }
            if (m_trackGraph) {
                m_trackGraph->stop();
            }
            if (!isRunning()) {
                stopStreaming(true);
                emitStatus(SwVideoSource::StreamState::Stopped, "Stream stopped");
                return;
            }
            stopStreaming(true);
            emitStatus(SwVideoSource::StreamState::Stopped, "Stream stopped");
        });
    }

    void handleConsumerPressureChanged(const SwVideoSource::ConsumerPressure& pressure) override {
        postToSourceThread_([this, pressure]() {
            if (m_trackGraph) {
                m_trackGraph->setConsumerPressure(pressure);
            }
        });
    }

private:
    struct RtspRequest {
        std::string method;
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        int retries{0};
        int authRetries{0};
    };

    using RtspAuthChallenge = SwRtspAuthChallenge;

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

    using TransportInfo = SwRtspTransportInfo;

    void applyOpenOptions_() {
        const SwMediaUrl& parsed = m_openOptions.mediaUrl;
        const SwString scheme = parsed.scheme().toLower();
        m_useTls = (scheme == "rtsps");
        m_rtspUserName = m_openOptions.userName.toStdString();
        m_rtspPassword = m_openOptions.password.toStdString();
        m_trustedCaFile = m_openOptions.trustedCaFile;
        m_enableAudio = m_openOptions.enableAudio;
        m_enableMetadata = m_openOptions.enableMetadata;
        m_bindAddress = m_openOptions.bindAddress;
        if (m_openOptions.rtpPort != 0) {
            m_forcedClientRtpPort = m_openOptions.rtpPort;
            m_forcedClientRtcpPort =
                m_openOptions.rtcpPort != 0
                    ? m_openOptions.rtcpPort
                    : static_cast<uint16_t>(m_openOptions.rtpPort + 1);
        }
        if (m_openOptions.transport == SwMediaOpenOptions::TransportPreference::Tcp) {
            m_useTcpTransport = true;
        }
        setLowLatencyMode(m_openOptions.lowLatency, 500);
    }

    static std::string hostForRtspUri_(const SwString& host) {
        const std::string text = host.toStdString();
        if (text.find(':') != std::string::npos &&
            (text.empty() || text.front() != '[' || text.back() != ']')) {
            return "[" + text + "]";
        }
        return text;
    }

    void parseUrl() {
        const SwMediaUrl& parsed = m_openOptions.mediaUrl;
        const SwString scheme = parsed.scheme().isEmpty() ? SwString("rtsp") : parsed.scheme().toLower();
        m_host = parsed.host();
        m_port = parsed.port() > 0 ? parsed.port() : (scheme == "rtsps" ? 322 : 554);
        m_path = parsed.pathWithQuery();
        if (m_path.isEmpty()) {
            m_path = "/";
        }
        m_rtspScheme = scheme.toStdString();
        m_baseUrl = m_rtspScheme + "://" + hostForRtspUri_(m_host);
        const int defaultPort = (scheme == "rtsps") ? 322 : 554;
        if (m_port != defaultPort) {
            m_baseUrl += ":" + std::to_string(m_port);
        }
    }

    SwAbstractSocket* createControlSocket_() {
        if (m_useTls) {
            auto* sslSocket = new SwSslSocket();
            sslSocket->setPeerHostName(m_host);
            if (!m_trustedCaFile.isEmpty()) {
                sslSocket->setTrustedCaFile(m_trustedCaFile);
            }
            return sslSocket;
        }
        return new SwTcpSocket();
    }

    void connectControlSocket_() {
        if (!m_rtspSocket) {
            return;
        }
        SwObject::connect(m_rtspSocket, &SwAbstractSocket::connected, m_callbackContext, [this]() {
            m_cseq = 0;
            m_sessionId.clear();
            m_state = RtspStep::Options;
            sendOptions();
        });
        SwObject::connect(m_rtspSocket, &SwIODevice::readyRead, m_callbackContext, [this]() {
            readControlSocket();
            handleControlBuffer();
        });
        SwObject::connect(m_rtspSocket, &SwAbstractSocket::disconnected, m_callbackContext, [this]() {
            if (m_rtspDisconnectSuppress.load() > 0) {
                return;
            }
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP disconnected";
            stopStreaming(false);
            scheduleReconnect("RTSP disconnected");
        });
        SwObject::connect(m_rtspSocket, &SwAbstractSocket::errorOccurred, m_callbackContext, [this](int code) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP socket error: " << code;
            stopStreaming(false);
            scheduleReconnect("RTSP socket error");
        });
    }

    void resetStreamState() {
        m_sdp.clear();
        m_tracks.clear();
        m_programTracks.clear();
        setTracks(SwList<SwMediaTrack>());
        m_selectedTrackIndex = -1;
        m_selectedAudioTrackIndex = -1;
        m_selectedMetadataTrackIndex = -1;
        m_trackControl.clear();
        m_audioTrackControl.clear();
        m_metadataTrackControl.clear();
        m_sessionControl.clear();
        m_controlBaseUrl.clear();
        m_sessionId.clear();
        m_pendingRequests.clear();
        m_authChallenge = RtspAuthChallenge{};
        m_authNonceCount = 0;
        m_authRetrySerial = 0;
        m_keepAliveUsesGetParameter = false;
        m_sessionTimeoutSeconds = 60;
        m_keepAliveIntervalMs = 15000;
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
        m_loggedH264Fmtp.store(false);
        m_loggedH265Fmtp.store(false);
        m_rtpPackets.store(0);
        m_framesEmitted.store(0);
        m_sequenceDiscontinuities.store(0);
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
        if (m_keepAliveTimer) {
            m_keepAliveTimer->stop();
            m_keepAliveTimer->setInterval(m_keepAliveIntervalMs);
        }
        m_localSsrc = generateSsrc();
        m_remoteSsrc = 0;
        m_detectedRtpPort = 0;
        m_interleavedRtpChannel = 0;
        m_interleavedRtcpChannel = 1;
        m_audioInterleavedRtpChannel = 2;
        m_audioInterleavedRtcpChannel = 3;
        m_metadataInterleavedRtpChannel = 4;
        m_metadataInterleavedRtcpChannel = 5;
        m_setupTargets.clear();
        m_setupTargetIndex = 0;
        m_lastRtpTime = std::chrono::steady_clock::time_point{};
        m_lastPliTime = std::chrono::steady_clock::time_point{};
        m_lastRtcpKeepAliveTime = std::chrono::steady_clock::time_point{};
        m_lastLoggedRtpTimeoutSec.store(-1);
        if (m_trackGraph) {
            m_trackGraph->reset();
        }
    }

    void stopStreaming(bool hardStop) {
        if (m_keepAliveTimer) {
            m_keepAliveTimer->stop();
        }
        sendTeardownBestEffort_();
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
        m_state = RtspStep::None;
        if (m_trackGraph) {
            m_trackGraph->reset();
        }
        m_pendingRequests.clear();
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
                    SwRtpSession::Packet packet;
                    if (parseAuxiliaryRtpPacket_(interleavedData, len, -1, packet)) {
                        if (m_remoteSsrc == 0 && packet.ssrc != 0) {
                            m_remoteSsrc = packet.ssrc;
                            requestKeyFrame("startup");
                        }
                        ++m_rtpPackets;
                        if (m_trackGraph) {
                            m_trackGraph->submitVideoPacket(packet, true);
                        }
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
        const bool hasPending = (pendingIt != m_pendingRequests.end());
        const RtspRequest pendingRequest = hasPending ? pendingIt->second : RtspRequest{};
        std::string method = (pendingIt != m_pendingRequests.end()) ? pendingIt->second.method : std::string();
        logRtspResponse(method, cseq, status, header, body);
        if (status != 200) {
            if (status == 401 && hasPending) {
                m_pendingRequests.erase(pendingIt);
                if (retryWithAuthentication_(header, pendingRequest)) {
                    return;
                }
            }
            bool shouldRetry = (status >= 500);
            if (shouldRetry && hasPending) {
                RtspRequest req = pendingRequest;
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
        if (hasPending) {
            m_pendingRequests.erase(pendingIt);
        }
        if (m_state == RtspStep::Options) {
            m_keepAliveUsesGetParameter = supportsRtspMethod_(header, "GET_PARAMETER");
            m_state = RtspStep::Describe;
            sendDescribe();
            return;
        }
        if (m_state == RtspStep::Describe) {
            m_sdp = body;
            m_controlBaseUrl = parseControlBaseUrl_(header);
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
                      << sanitizeRtspMessageForLog_(header);
            return;
        }

        swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] <<< (CSeq " << cseq
                  << ", method=" << (method.empty() ? "?" : method)
                  << ", status=" << status << ")\n"
                  << sanitizeRtspMessageForLog_(header)
                  << "\n--BODY--\n" << body;
    }

    static std::string sanitizeRtspMessageForLog_(const std::string& message) {
        return SwRtspHeaderUtils::sanitizeMessageForLog(message);
    }

    static bool hasHeader_(const std::vector<std::pair<std::string, std::string>>& headers,
                           const char* name) {
        if (!name || !*name) {
            return false;
        }
        const std::string needle = toLower(name);
        for (const auto& header : headers) {
            if (toLower(header.first) == needle) {
                return true;
            }
        }
        return false;
    }

    void resendRequest(const RtspRequest& req) {
        sendRtsp(req.method, req.url, req.headers, req.body, req.retries + 1, req.authRetries);
    }

    std::vector<std::string> headerValues_(const std::string& header, const char* key) const {
        return SwRtspHeaderUtils::headerValues(header, key);
    }

    std::string headerValue_(const std::string& header, const char* key) const {
        return SwRtspHeaderUtils::headerValue(header, key);
    }

    std::string parseControlBaseUrl_(const std::string& header) const {
        return SwRtspHeaderUtils::parseControlBaseUrl(header);
    }

    bool supportsRtspMethod_(const std::string& header, const char* method) const {
        return SwRtspHeaderUtils::supportsMethod(header, method);
    }

    std::string buildAuthorizationHeader_(const std::string& method, const std::string& url) {
        return SwRtspAuth::buildAuthorizationHeader(m_authChallenge,
                                                    m_rtspUserName,
                                                    m_rtspPassword,
                                                    method,
                                                    url,
                                                    m_authNonceCount);
    }

    bool retryWithAuthentication_(const std::string& header, const RtspRequest& request) {
        if (request.authRetries >= 2 || m_rtspUserName.empty()) {
            return false;
        }
        RtspAuthChallenge challenge;
        if (!SwRtspAuth::selectChallenge(header, challenge)) {
            return false;
        }
        if (challenge.nonce != m_authChallenge.nonce || challenge.algorithm != m_authChallenge.algorithm) {
            m_authNonceCount = 0;
        }
        m_authChallenge = challenge;
        sendRtsp(request.method,
                 request.url,
                 request.headers,
                 request.body,
                 request.retries,
                 request.authRetries + 1);
        return true;
    }

    void parseSession(const std::string& header) {
        SwRtspSessionInfo sessionInfo;
        if (!SwRtspHeaderUtils::parseSessionInfo(header, sessionInfo)) {
            return;
        }
        m_sessionId = sessionInfo.sessionId;
        if (sessionInfo.timeoutSeconds > 0) {
            m_sessionTimeoutSeconds = sessionInfo.timeoutSeconds;
            m_keepAliveIntervalMs =
                SwRtspHeaderUtils::keepAliveIntervalMsForTimeoutSeconds(sessionInfo.timeoutSeconds);
            if (m_keepAliveTimer && !m_keepAliveTimer->isActive()) {
                m_keepAliveTimer->setInterval(m_keepAliveIntervalMs);
            }
        }
    }

    void parseTransportInfo(const std::string& header, TransportInfo& info) {
        SwRtspHeaderUtils::parseTransportInfo(header, info);
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
        configureTrackGraph_();
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
        for (const auto& track : m_programTracks) {
            if (track.type == SwMediaTrack::Type::Video) {
                bool haveSdpVideo = false;
                for (const auto& existing : publishedTracks) {
                    if (existing.type == SwMediaTrack::Type::Video) {
                        haveSdpVideo = true;
                        break;
                    }
                }
                if (haveSdpVideo) {
                    continue;
                }
            }
            bool duplicate = false;
            for (const auto& existing : publishedTracks) {
                if (existing.id == track.id) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                publishedTracks.append(track);
            }
        }
        setTracks(publishedTracks);
    }

    void postToSourceThread_(std::function<void()> task) {
        if (!task) {
            return;
        }
        ThreadHandle* targetThread = m_callbackContext ? m_callbackContext->threadHandle()
                                                       : nullptr;
        if (targetThread) {
            const std::thread::id targetId = targetThread->threadId();
            if (targetId != std::thread::id{} &&
                targetId == std::this_thread::get_id()) {
                task();
                return;
            }
            targetThread->postTask(std::move(task));
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->postEvent(std::move(task));
            return;
        }
        task();
    }

    void runOnSourceThreadBlocking_(std::function<void()> task) {
        if (!task) {
            return;
        }
        ThreadHandle* targetThread = m_callbackContext ? m_callbackContext->threadHandle()
                                                       : nullptr;
        if (!targetThread || !ThreadHandle::isLive(targetThread) || !targetThread->isRunning()) {
            task();
            return;
        }
        const std::thread::id targetId = targetThread->threadId();
        if (targetId != std::thread::id{} &&
            targetId == std::this_thread::get_id()) {
            task();
            return;
        }

        struct BlockingContext {
            std::mutex mutex;
            std::condition_variable cv;
            bool completed{false};
        };

        std::shared_ptr<BlockingContext> ctx = std::make_shared<BlockingContext>();
        if (!targetThread->postTask([task, ctx]() mutable {
                task();
                {
                    std::lock_guard<std::mutex> lock(ctx->mutex);
                    ctx->completed = true;
                }
                ctx->cv.notify_one();
            })) {
            task();
            return;
        }

        std::unique_lock<std::mutex> lock(ctx->mutex);
        ctx->cv.wait(lock, [ctx]() { return ctx->completed; });
    }

    void updateProgramTracks_(const SwList<SwMediaTrack>& tracks) {
        m_programTracks = tracks;
        publishTracks_();
    }

    void configureTrackGraph_() {
        if (!m_trackGraph) {
            return;
        }
        SwRtspTrackGraph::VideoConfig videoConfig;
        videoConfig.codec = isHevcCodec() ? SwVideoPacket::Codec::H265
                                          : SwVideoPacket::Codec::H264;
        videoConfig.payloadType = m_payloadType;
        videoConfig.clockRate = m_clockRate;
        videoConfig.liveTrimEnabled = m_lowLatencyDrop;
        videoConfig.latencyTargetMs = m_latencyTargetMs;
        videoConfig.transportStream = isTransportStreamCodec();
        if (m_selectedTrackIndex >= 0 &&
            static_cast<std::size_t>(m_selectedTrackIndex) < m_tracks.size()) {
            videoConfig.fmtp =
                SwString(m_tracks[static_cast<std::size_t>(m_selectedTrackIndex)].fmtpLine);
        }
        m_trackGraph->setVideoConfig(videoConfig);
        m_trackGraph->setConsumerPressure(consumerPressure());
        m_trackGraph->setVideoPacketCallback([this](const SwVideoPacket& packet) {
            emitStatus(SwVideoSource::StreamState::Streaming, "Streaming");
            emitPacket(packet);
            ++m_framesEmitted;
        });
        m_trackGraph->setMediaPacketCallback([this](const SwMediaPacket& packet) {
            emitMediaPacket(packet);
        });
        m_trackGraph->setTracksChangedCallback([this](const SwList<SwMediaTrack>& tracks) {
            postToSourceThread_([this, tracks]() {
                updateProgramTracks_(tracks);
            });
        });
        m_trackGraph->setKeyFrameRequestCallback([this](const SwString& reason) {
            postToSourceThread_([this, reason]() {
                requestKeyFrame(reason);
            });
        });
        m_trackGraph->setRecoveryCallback([this](SwMediaSource::RecoveryEvent::Kind kind,
                                                 const SwString& reason) {
            emitRecovery(kind, reason);
        });
    }

    SwMediaPacket makeAudioMediaPacket_(const SwRtpSession::Packet& packet) const {
        SwMediaPacket mediaPacket;
        mediaPacket.setType(SwMediaPacket::Type::Audio);
        mediaPacket.setTrackId(SwString("track-") +
                               SwString(std::to_string(m_selectedAudioTrackIndex)));
        mediaPacket.setCodec(SwString(m_audioCodecName));
        mediaPacket.setPayload(packet.payload);
        mediaPacket.setPts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDiscontinuity(false);
        mediaPacket.setPayloadType(m_audioPayloadType);
        mediaPacket.setClockRate(m_audioClockRate);
        mediaPacket.setSampleRate(m_audioClockRate);
        mediaPacket.setChannelCount(m_audioChannelCount > 0 ? m_audioChannelCount : 1);
        return mediaPacket;
    }

    SwMediaPacket makeMetadataMediaPacket_(const SwRtpSession::Packet& packet) const {
        SwMediaPacket mediaPacket;
        mediaPacket.setType(SwMediaPacket::Type::Metadata);
        mediaPacket.setTrackId(SwString("track-") +
                               SwString(std::to_string(m_selectedMetadataTrackIndex)));
        mediaPacket.setCodec(SwString(m_metadataCodecName));
        mediaPacket.setPayload(packet.payload);
        mediaPacket.setPts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDts(static_cast<std::int64_t>(packet.timestamp));
        mediaPacket.setDiscontinuity(false);
        mediaPacket.setPayloadType(m_metadataPayloadType);
        mediaPacket.setClockRate(m_metadataClockRate);
        return mediaPacket;
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
                      << static_cast<int>(interleavedRtcpChannel);
        } else {
            transport << "RTP/AVP;unicast;client_port="
                      << clientRtpPort << "-"
                      << clientRtcpPort;
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
    }

    void sendKeepAlive() {
        if (!isRunning() || m_sessionId.empty()) {
            return;
        }
        std::vector<std::pair<std::string, std::string>> headers = {{"Session", m_sessionId}};
        std::string keepAliveUrl = aggregatePlayUrl();
        if (keepAliveUrl.empty()) {
            keepAliveUrl = playUrl();
        }
        if (keepAliveUrl.empty()) {
            keepAliveUrl = fullUrl();
        }
        if (m_keepAliveUsesGetParameter) {
            sendRtsp("GET_PARAMETER", keepAliveUrl, headers);
        } else {
            sendRtsp("OPTIONS", keepAliveUrl, headers);
        }
    }

    void sendTeardownBestEffort_() {
        if (!m_rtspSocket || !m_rtspSocket->isOpen() || m_sessionId.empty()) {
            return;
        }
        std::string url = aggregatePlayUrl();
        if (url.empty()) {
            url = playUrl();
        }
        if (url.empty()) {
            url = fullUrl();
        }
        std::ostringstream oss;
        oss << "TEARDOWN " << url << " RTSP/1.0\r\n";
        oss << "CSeq: " << (++m_cseq) << "\r\n";
        oss << "User-Agent: SwRtspUdpSource/1.0\r\n";
        std::string hostHeader = hostForRtspUri_(m_host);
        const int defaultPort = m_useTls ? 322 : 554;
        if (m_port > 0 && m_port != defaultPort) {
            hostHeader += ":" + std::to_string(m_port);
        }
        oss << "Host: " << hostHeader << "\r\n";
        const std::string authorization = buildAuthorizationHeader_("TEARDOWN", url);
        if (!authorization.empty()) {
            oss << "Authorization: " << authorization << "\r\n";
        }
        oss << "Session: " << m_sessionId << "\r\n\r\n";
        m_rtspSocket->write(SwString(oss.str()));
        m_rtspSocket->waitForBytesWritten(200);
    }

    void configureUdpSockets() {
        if (m_useTcpTransport) {
            return;
        }
        if (m_udpSession && m_udpSession->isRunning()) {
            m_udpSession->sendUdpPunch(m_host, m_serverRtpPort, m_serverRtcpPort);
        }
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
        bool connectOk = false;
        if (m_useTls) {
            auto* sslSocket = dynamic_cast<SwSslSocket*>(m_rtspSocket);
            connectOk = sslSocket && sslSocket->connectToHostEncrypted(m_host, static_cast<uint16_t>(m_port));
        } else {
            connectOk = m_rtspSocket->connectToHost(m_host, static_cast<uint16_t>(m_port));
        }
        if (!connectOk) {
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
        emitRecovery(SwMediaSource::RecoveryEvent::Kind::TransportReset,
                     reason ? SwString(reason) : SwString("Switching RTSP transport to TCP"));
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
        emitRecovery(SwMediaSource::RecoveryEvent::Kind::TransportReset,
                     reason ? SwString(reason) : SwString("Returning RTSP transport to UDP"));
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
        emitRecovery(SwMediaSource::RecoveryEvent::Kind::Reconnect,
                     reason ? SwString(reason) : SwString("Scheduling RTSP reconnect..."));
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


    void sendRtsp(const std::string& method,
                  const std::string& url,
                  const std::vector<std::pair<std::string, std::string>>& headers,
                  const std::string& body = std::string(),
                  int retryCount = 0,
                  int authRetryCount = 0) {
        if (!m_rtspSocket || !m_rtspSocket->isOpen()) {
            return;
        }
        std::vector<std::pair<std::string, std::string>> effectiveHeaders = headers;
        if (!hasHeader_(effectiveHeaders, "Host")) {
            std::string hostHeader = hostForRtspUri_(m_host);
            const int defaultPort = m_useTls ? 322 : 554;
            if (m_port > 0 && m_port != defaultPort) {
                hostHeader += ":" + std::to_string(m_port);
            }
            effectiveHeaders.emplace_back("Host", hostHeader);
        }
        const std::string authorization = buildAuthorizationHeader_(method, url);
        if (!authorization.empty() && !hasHeader_(effectiveHeaders, "Authorization")) {
            effectiveHeaders.emplace_back("Authorization", authorization);
        }
        std::ostringstream oss;
        oss << method << " " << url << " RTSP/1.0\r\n";
        int cseq = ++m_cseq;
        oss << "CSeq: " << cseq << "\r\n";
        oss << "User-Agent: SwRtspUdpSource/1.0\r\n";
        for (const auto& h : effectiveHeaders) {
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
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] >>> (CSeq " << cseq
                      << ", retry " << retryCount << ", authRetry " << authRetryCount << ")\n"
                      << sanitizeRtspMessageForLog_(oss.str());
        } else {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] >>> (CSeq " << cseq
                      << ", authRetry " << authRetryCount << ")\n"
                      << sanitizeRtspMessageForLog_(oss.str());
        }
        m_rtspSocket->write(SwString(oss.str()));
        RtspRequest req;
        req.method = method;
        req.url = url;
        req.headers = headers;
        req.body = body;
        req.retries = retryCount;
        req.authRetries = authRetryCount;
        m_pendingRequests[cseq] = req;
    }

    std::string fullUrl() const {
        return m_baseUrl + m_path.toStdString();
    }

    std::string controlBaseUrl_() const {
        return m_controlBaseUrl.empty() ? fullUrl() : m_controlBaseUrl;
    }

    std::string resolveControlUrl(const std::string& control) const {
        if (control.empty() || control == "*") {
            return controlBaseUrl_();
        }
        if (control.find("rtsp://") == 0 || control.find("rtsps://") == 0) {
            return control;
        }
        if (!control.empty() && control.front() == '/') {
            return m_baseUrl + control;
        }
        std::string base = controlBaseUrl_();
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
        if (!m_sessionControl.empty() || !m_controlBaseUrl.empty() || m_setupTargets.size() > 1) {
            return aggregatePlayUrl();
        }
        return trackUrl();
    }

    static uint16_t nextPreferredClientRtpPort_() {
        static std::atomic<unsigned int> nextSlot{0};
        static constexpr uint16_t kFirstPort = 37700;
        static constexpr uint16_t kLastPort = 64998;
        static constexpr unsigned int kPortCount =
            static_cast<unsigned int>(((kLastPort - kFirstPort) / 2) + 1);
        const unsigned int slot = nextSlot.fetch_add(1, std::memory_order_relaxed);
        return static_cast<uint16_t>(kFirstPort + ((slot % kPortCount) * 2));
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
        const SwString bindAddr = m_bindAddress;
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
        const uint16_t preferredBase = nextPreferredClientRtpPort_();
        if (tryBindPair(preferredBase)) {
            return true;
        }
        for (uint16_t base = 37700; base < 65000; base += 2) {
            if (base == preferredBase) {
                continue;
            }
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
        const SwString bindAddr = m_bindAddress;
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
        const SwString bindAddr = m_bindAddress;
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
        m_udpSession.reset(new SwRtpSession(descriptor));
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
        m_audioUdpSession.reset(new SwRtpSession(descriptor));
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
        m_metadataUdpSession.reset(new SwRtpSession(descriptor));
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
        ++m_rtpPackets;
        m_lastRtpTime = std::chrono::steady_clock::now();
        const uint8_t* payload = reinterpret_cast<const uint8_t*>(packet.payload.constData());
        const size_t payloadLen = packet.payload.size();
        if (!payload || payloadLen == 0) {
            return;
        }
        if (m_trackGraph) {
            m_trackGraph->submitVideoPacket(packet, false);
        }
    }

    void handleAudioUdpSessionPacket_(const SwRtpSession::Packet& packet) {
        if (m_selectedAudioTrackIndex < 0 || m_audioCodecName.empty()) {
            return;
        }
        if (m_trackGraph) {
            m_trackGraph->submitAudioPacket(makeAudioMediaPacket_(packet));
        }
    }

    void handleMetadataUdpSessionPacket_(const SwRtpSession::Packet& packet) {
        if (m_selectedMetadataTrackIndex < 0 || m_metadataCodecName.empty()) {
            return;
        }
        if (m_trackGraph) {
            m_trackGraph->submitMetadataPacket(makeMetadataMediaPacket_(packet));
        }
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
        if (m_trackGraph) {
            m_trackGraph->submitVideoGap(expected, actual);
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
        if (previousElapsed < 0) {
            emitRecovery(SwMediaSource::RecoveryEvent::Kind::Timeout,
                         "Video stream stalled...");
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

    bool sendInterleavedFrame_(uint8_t channel, const uint8_t* data, size_t len) {
        if (!m_rtspSocket || !m_rtspSocket->isOpen() || !data || len == 0 ||
            len > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) {
            return false;
        }
        std::string frame;
        frame.resize(4 + len);
        frame[0] = '$';
        frame[1] = static_cast<char>(channel);
        const auto payloadLength = static_cast<uint16_t>(len);
        frame[2] = static_cast<char>((payloadLength >> 8) & 0xFF);
        frame[3] = static_cast<char>(payloadLength & 0xFF);
        std::memcpy(&frame[0] + 4, data, len);
        return m_rtspSocket->write(SwString(frame.data(), frame.size()));
    }

    void sendRtcpReceiverReport(uint32_t senderSsrc) {
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
        bool sent = false;
        if (m_useTcpTransport) {
            sent = sendInterleavedFrame_(m_interleavedRtcpChannel, packet, sizeof(packet));
        }
        if (sent) {
            m_lastRtcpKeepAliveTime = std::chrono::steady_clock::now();
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Sent RTCP RR (local SSRC=0x"
                      << std::hex << mySsrc << std::dec
                      << ", remote SSRC=0x" << std::hex << senderSsrc << std::dec
                      << ", transport=" << (m_useTcpTransport ? "interleaved" : "udp-session") << ")";
        } else {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to send RTCP RR";
        }
    }

    void maybeSendRtcpKeepAlive() {
        if (m_udpSession && m_udpSession->isRunning()) {
            return;
        }
        if (!isRunning() || m_state != RtspStep::Playing) {
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
        const bool sent = m_useTcpTransport &&
                          sendInterleavedFrame_(m_interleavedRtcpChannel, packet, sizeof(packet));
        if (sent) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Sent RTCP PLI"
                        << (reason ? SwString(" (") + SwString(reason) + SwString(")") : SwString())
                        << " remote SSRC=0x" << std::hex << mediaSsrc << std::dec
                        << " transport=interleaved";
        } else {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to send RTCP PLI";
        }
    }

    void requestKeyFrame(const SwString& reason) {
        if (reason.isEmpty()) {
            requestKeyFrame(static_cast<const char*>(nullptr));
            return;
        }
        const std::string text = reason.toStdString();
        requestKeyFrame(text.c_str());
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
            if (previousElapsed < 0) {
                emitRecovery(SwMediaSource::RecoveryEvent::Kind::Timeout,
                             "Video stream stalled...");
            }
            return;
        }
        m_lastLoggedRtpTimeoutSec.store(-1);
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

    SwMediaOpenOptions m_openOptions{};
    SwString m_url;
    SwString m_host;
    int m_port{554};
    SwString m_path{"/"};
    SwString m_bindAddress{};
    SwString m_trustedCaFile{};
    std::string m_baseUrl;
    std::string m_rtspScheme{"rtsp"};
    std::string m_rtspUserName{};
    std::string m_rtspPassword{};
    std::map<int, RtspRequest> m_pendingRequests;
    int m_maxRequestRetries{1};

    SwObject* m_callbackContext{nullptr};
    SwThread* m_sourceThread{nullptr};
    std::unique_ptr<SwRtspTrackGraph> m_trackGraph{};
    SwAbstractSocket* m_rtspSocket{nullptr};
    std::unique_ptr<SwRtpSession> m_udpSession{};
    std::unique_ptr<SwRtpSession> m_audioUdpSession{};
    std::unique_ptr<SwRtpSession> m_metadataUdpSession{};
    SwTimer* m_monitorTimer{nullptr};
    SwTimer* m_keepAliveTimer{nullptr};
    SwTimer* m_reconnectTimer{nullptr};

    std::string m_sdp;
    std::vector<RtspTrack> m_tracks;
    SwList<SwMediaTrack> m_programTracks{};
    int m_selectedTrackIndex{-1};
    int m_selectedAudioTrackIndex{-1};
    int m_selectedMetadataTrackIndex{-1};
    std::string m_trackControl;
    std::string m_audioTrackControl;
    std::string m_metadataTrackControl;
    std::string m_sessionControl;
    std::string m_controlBaseUrl;
    std::string m_sessionId;
    RtspAuthChallenge m_authChallenge{};
    RtspStep m_state{RtspStep::None};
    int m_cseq{0};
    uint32_t m_authNonceCount{0};
    int m_authRetrySerial{0};
    int m_sessionTimeoutSeconds{60};
    int m_keepAliveIntervalMs{15000};
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
    bool m_useTls{false};
    bool m_useTcpTransport{false};
    bool m_triedTcpFallback{false};
    bool m_autoTcpFallbackActive{false};
    bool m_keepAliveUsesGetParameter{false};
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

    std::atomic<uint64_t> m_rtpPackets{0};
    std::atomic<uint64_t> m_framesEmitted{0};
    std::atomic<uint64_t> m_sequenceDiscontinuities{0};
    bool m_lowLatencyDrop{true};
    int m_latencyTargetMs{500};
    std::atomic<int> m_rtspDisconnectSuppress{0};
    std::chrono::steady_clock::time_point m_lastRtpTime{};
    std::atomic<long long> m_lastLoggedRtpTimeoutSec{-1};
    std::chrono::steady_clock::time_point m_lastPliTime{};
    std::chrono::steady_clock::time_point m_lastRtcpKeepAliveTime{};
    std::string m_ctrlAccum;
    std::string m_ctrlTextBuffer;
    uint32_t m_localSsrc{0};
    uint32_t m_remoteSsrc{0};
    uint16_t m_detectedRtpPort{0};
    int m_reconnectDelayMs{2000};
    std::atomic<bool> m_reconnectPending{false};
    std::atomic<bool> m_loggedH264Fmtp{false};
    std::atomic<bool> m_loggedH265Fmtp{false};
    
};

inline void SwRtspUdpSource::sendOptions() {
    sendRtsp("OPTIONS", fullUrl(), {});
}
