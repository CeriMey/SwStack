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

/***************************************************************************************************
 * Minimal RTSP (RTP over UDP) video source.
 *
 * Focuses on a single video track, assumes H.264 payload, and performs a lightweight depayloader
 * (single NAL, STAP-A, FU-A) to emit SwVideoPacket::H264 packets.
 **************************************************************************************************/

#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "core/io/SwTcpSocket.h"
#include "core/io/SwUdpSocket.h"
#include "core/runtime/SwTimer.h"
#include "core/runtime/SwThread.h"
#include "core/types/SwByteArray.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
static constexpr const char* kSwLogCategory_SwRtspUdpSource = "sw.media.swrtspudpsource";


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

class SwRtspUdpSource : public SwVideoSource {
public:
    SwRtspUdpSource(const SwString& url, SwObject* parent = nullptr)
        : m_parent(parent), m_url(url) {
        parseUrl();
        m_rtspSocket = new SwTcpSocket(m_parent);
        m_rtpSocket = new SwUdpSocket(m_parent);
        m_rtcpSocket = new SwUdpSocket(m_parent);
        tuneUdpSocket(m_rtpSocket, 4 * 1024 * 1024, 64 * 1024, 160);
        tuneUdpSocket(m_rtcpSocket, 1 * 1024 * 1024, 4096, 32);
        m_reconnectTimer = new SwTimer(m_reconnectDelayMs, m_parent);
        m_reconnectTimer->setSingleShot(true);
        SwObject::connect(m_reconnectTimer, &SwTimer::timeout, [this]() { attemptReconnect(); });
        m_keepAliveTimer = new SwTimer(15000, m_parent);

        SwObject::connect(m_keepAliveTimer, &SwTimer::timeout, [this]() { sendKeepAlive(); });

        SwObject::connect(m_rtspSocket, SIGNAL(connected), [this]() {
            m_ctrlBuffer.clear();
            m_cseq = 0;
            m_sessionId.clear();
            m_state = RtspStep::Options;
            sendOptions();
        });
        SwObject::connect(m_rtspSocket, SIGNAL(disconnected), [this]() {
            if (m_rtspDisconnectSuppress.load() > 0) {
                return;
            }
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP disconnected";
            stopStreaming(false);
            scheduleReconnect("RTSP disconnected");
        });
        SwObject::connect(m_rtspSocket, SIGNAL(errorOccurred), [this](int code) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTSP socket error: " << code;
            stopStreaming(false);
            scheduleReconnect("RTSP socket error");
        });
    }

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

    void parseH265Fmtp(const std::string& fmtp) {
        if (fmtp.empty()) {
            return;
        }
        auto parts = split(fmtp, ';');
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
    }

    ~SwRtspUdpSource() override {
        stop();
        delete m_keepAliveTimer;
        delete m_reconnectTimer;
        delete m_rtspSocket;
        delete m_rtpSocket;
        delete m_rtcpSocket;
    }

    std::string name() const override { return "SwRtspUdpSource"; }

    bool initialize() { return true; }

    void setLocalAddress(const SwString& addr) { m_bindAddress = addr; }
    void setUseTcpTransport(bool enable) { m_useTcpTransport = enable; }

    void forceLocalBind(const SwString& addr, uint16_t rtpPort, uint16_t rtcpPort) {
        m_bindAddress = addr;
        m_forcedClientRtpPort = rtpPort;
        m_forcedClientRtcpPort = rtcpPort;
    }

    void start() override {
        m_autoReconnect.store(true);
        if (!isRunning()) {
            setRunning(true);
        }
        initiateConnection();
    }

    void stop() override {
        m_autoReconnect.store(false);
        cancelReconnect();
        setPlaceholderActive(false);
        if (!isRunning()) {
            stopStreaming(true);
            return;
        }
        stopStreaming(true);
    }

private:
    struct RtspRequest {
        std::string method;
        std::string url;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;
        int retries{0};
    };

    enum class RtspStep { None, Options, Describe, Setup, Play, Playing };

    class PollWorkerThread : public SwThread {
    public:
        PollWorkerThread(SwRtspUdpSource* owner, int intervalMs)
            : SwThread("SwRtspWorker", nullptr),
              m_owner(owner),
              m_intervalMs(intervalMs > 0 ? intervalMs : 1) {
        }

        void requestStop() { m_active.store(false); }
        void setInterval(int intervalMs) {
            if (intervalMs <= 0) {
                intervalMs = 1;
            }
            m_intervalMs.store(intervalMs);
        }

    protected:
        void run() override {
            while (m_active.load()) {
                if (!m_owner) {
                    break;
                }
                m_owner->poll();
                auto sleepMs = m_intervalMs.load();
                if (sleepMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
                }
            }
        }

    private:
        SwRtspUdpSource* m_owner{nullptr};
        std::atomic<bool> m_active{true};
        std::atomic<int> m_intervalMs{1};
    };

    void parseUrl() {
        std::string s = m_url.toStdString();
        const std::string prefix = "rtsp://";
        if (s.compare(0, prefix.size(), prefix) == 0) {
            s = s.substr(prefix.size());
        }
        auto slash = s.find('/');
        std::string hostPort = (slash == std::string::npos) ? s : s.substr(0, slash);
        m_path = (slash == std::string::npos) ? "/" : s.substr(slash);
        auto colon = hostPort.find(':');
        if (colon != std::string::npos) {
            m_host = SwString(hostPort.substr(0, colon));
            m_port = std::stoi(hostPort.substr(colon + 1));
        } else {
            m_host = SwString(hostPort);
            m_port = 554;
        }
        m_baseUrl = "rtsp://" + m_host.toStdString();
        if (m_port != 554) {
            m_baseUrl += ":" + std::to_string(m_port);
        }
    }

    void resetStreamState() {
        stopPollWorker();
        m_ctrlBuffer.clear();
        m_sdp.clear();
        m_trackControl.clear();
        m_sessionControl.clear();
        m_sessionId.clear();
        m_state = RtspStep::None;
        m_cseq = 0;
        m_serverRtpPort = 0;
        m_clientRtpPort = 0;
        m_clientRtcpPort = 0;
        m_payloadType = 96;
        m_clockRate = 90000;
        m_triedAggregatePlay.store(false);
        m_haveTimestamp = false;
        m_haveFirstTimestamp = false;
        m_firstTimestamp = 0;
        m_currentKeyFrame = false;
        m_framesDropped.store(0);
        m_currentCodec = SwVideoPacket::Codec::H264;
        m_nalBuffer.clear();
        m_tsDemux.reset();
        m_hevcVps.clear();
        m_hevcSps.clear();
        m_hevcPps.clear();
        m_hevcHeadersInserted = false;
        m_hevcProfileLevelId.clear();
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
    }

    void stopStreaming(bool hardStop) {
        stopPollWorker();
        if (m_keepAliveTimer) {
            m_keepAliveTimer->stop();
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
        m_tsDemux.reset();
        if (hardStop) {
            setRunning(false);
        }
    }

    void poll() {
        if (!isRunning()) {
            return;
        }
        readControlSocket();
        handleControlBuffer();
        handleRtpPackets();
        handleRtcpPackets();
        checkRtpTimeout();
        maybeEmitPlaceholder();
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
                if (channel == 0) {
                    handleRtpPacket(reinterpret_cast<const uint8_t*>(m_ctrlAccum.data() + 4), len);
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
            m_state = RtspStep::Setup;
            sendSetup();
            return;
        }
        if (m_state == RtspStep::Setup) {
            parseSession(header);
            parseTransport(header);
            if (m_sessionId.empty() || m_serverRtpPort == 0) {
                swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Missing session or server port after SETUP";
                return;
            }
            configureUdpSockets();
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] SETUP ok client_port=" << m_clientRtpPort << "-" << m_clientRtcpPort
                      << " server_port=" << m_serverRtpPort << "-" << m_serverRtcpPort
                      << " payload=" << m_payloadType;
            m_state = RtspStep::Play;
            m_triedAggregatePlay.store(false);
            sendPlay(false);
            return;
        }
        if (m_state == RtspStep::Play) {
            m_state = RtspStep::Playing;
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

    void parseTransport(const std::string& header) {
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
                    m_serverRtpPort = static_cast<uint16_t>(rtp);
                    m_serverRtcpPort = static_cast<uint16_t>(rtcp);
                }
                break;
            }
        }
    }

    bool parseSdp(const std::string& body) {
        std::istringstream iss(body);
        std::string line;
        bool inVideo = false;
        std::string control;
        std::string sessionControl;
        int payload = m_payloadType;
        int clock = m_clockRate;
        std::string codecName = "H264";
        std::string fmtpLine;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!inVideo && line.rfind("a=control:", 0) == 0) {
                sessionControl = line.substr(std::strlen("a=control:"));
                continue;
            }
            if (line.rfind("m=video", 0) == 0) {
                inVideo = true;
                auto parts = split(line, ' ');
                if (parts.size() >= 4) {
                    payload = std::atoi(parts[3].c_str());
                }
                continue;
            }
            if (!inVideo) {
                continue;
            }
            if (line.rfind("a=control:", 0) == 0) {
                control = line.substr(std::strlen("a=control:"));
                continue;
            }
            if (line.rfind("a=rtpmap:", 0) == 0) {
                auto pos = line.find(' ');
                if (pos != std::string::npos) {
                    payload = std::atoi(line.substr(std::strlen("a=rtpmap:"), pos - std::strlen("a=rtpmap:")).c_str());
                    auto codecPart = line.substr(pos + 1);
                    auto slash = codecPart.find('/');
                    if (slash != std::string::npos) {
                        codecName = codecPart.substr(0, slash);
                        clock = std::atoi(codecPart.substr(slash + 1).c_str());
                    }
                }
                continue;
            }
            if (line.rfind("a=fmtp:", 0) == 0) {
                fmtpLine = line.substr(std::strlen("a=fmtp:"));
                continue;
            }
        }
        if (control.empty()) {
            return false;
        }
        m_trackControl = control;
        if (!sessionControl.empty()) {
            m_sessionControl = sessionControl;
        }
        m_payloadType = payload;
        m_clockRate = (clock > 0) ? clock : 90000;
        m_codecName = toLower(codecName);
        if (isHevcCodecName(m_codecName)) {
            m_currentCodec = SwVideoPacket::Codec::H265;
        } else {
            m_currentCodec = SwVideoPacket::Codec::H264;
        }
        if (isHevcCodec()) {
            parseH265Fmtp(fmtpLine);
        }
        swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] SDP video codec=" << m_codecName
                  << " payload=" << m_payloadType << " clock=" << m_clockRate
                  << " fmtp=" << fmtpLine;
        return true;
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
        transport << "RTP/AVP;unicast;client_port="
                  << m_clientRtpPort << "-" << m_clientRtcpPort
                  << ";mode=\"PLAY\"";
        headers.emplace_back("Transport", transport.str());
        sendRtsp("SETUP", trackUrl(), headers);
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

    void initiateConnection() {
        if (!isRunning()) {
            return;
        }
        cancelReconnect();
        setPlaceholderActive(true);
        resetStreamState();
        startPollWorker();
        if (!m_rtspSocket->connectToHost(m_host, static_cast<uint16_t>(m_port))) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to connect to " << m_host.toStdString()
                      << ":" << m_port;
            scheduleReconnect("connectToHost failed");
            return;
        }
    }

    void scheduleReconnect(const char* reason) {
        if (!m_autoReconnect.load()) {
            return;
        }
        if (reason) {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Scheduling reconnect: " << reason;
        }
        setPlaceholderActive(true);
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

    void startPollWorker() {
        if (m_pollWorker) {
            m_pollWorker->setInterval(m_workerIntervalMs);
            return;
        }
        m_pollWorker = new PollWorkerThread(this, m_workerIntervalMs);
        if (!m_pollWorker->start()) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to start worker thread";
            delete m_pollWorker;
            m_pollWorker = nullptr;
        }
    }

    void stopPollWorker() {
        if (!m_pollWorker) {
            return;
        }
        m_pollWorker->requestStop();
        m_pollWorker->quit();
        m_pollWorker->wait();
        delete m_pollWorker;
        m_pollWorker = nullptr;
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
        SOCKET probe = INVALID_SOCKET;
        probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (probe == INVALID_SOCKET) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP self-probe socket creation failed: " << WSAGetLastError();
            return;
        }
        sockaddr_in target{};
        std::string addrStr = bindAddr.isEmpty() ? "127.0.0.1" : bindAddr.toStdString();
        if (addrStr == "0.0.0.0") {
            addrStr = "127.0.0.1";
        }
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        if (!stringToInAddr(addrStr, target.sin_addr)) {
            stringToInAddr("127.0.0.1", target.sin_addr);
        }
        const char* msg = "probe";
        int ret = sendto(probe, msg, 5, 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
        if (ret <= 0) {
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP self-probe sendto failed: " << WSAGetLastError();
        } else {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] UDP self-probe sent to " << addrStr << ":" << port;
        }
        closesocket(probe);
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
    std::string aggregatePlayUrl() const { return resolveControlUrl(m_sessionControl); }
    std::string playUrl() const {
        if (!m_sessionControl.empty()) {
            return aggregatePlayUrl();
        }
        return trackUrl();
    }

    bool allocateClientPorts() {
        if (m_useTcpTransport) {
            return true; // no UDP ports needed
        }
        if (!m_rtpSocket || !m_rtcpSocket) {
            return false;
        }
        SwString bindAddr = m_bindAddress.isEmpty() ? SwString("0.0.0.0") : m_bindAddress;
        if (m_forcedClientRtpPort != 0 && m_forcedClientRtcpPort != 0) {
            bool okRtp = m_rtpSocket->bind(bindAddr, m_forcedClientRtpPort);
            bool okRtcp = m_rtcpSocket->bind(bindAddr, m_forcedClientRtcpPort);
            if (okRtp && okRtcp) {
                m_clientRtpPort = m_forcedClientRtpPort;
                m_clientRtcpPort = m_forcedClientRtcpPort;
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Forced bind " << bindAddr.toStdString()
                          << ":" << m_clientRtpPort << "/" << m_clientRtcpPort;
                selfProbeUdp(bindAddr, m_clientRtpPort);
                return true;
            }
            swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Forced bind FAILED on " << bindAddr.toStdString()
                      << ":" << m_forcedClientRtpPort << "/" << m_forcedClientRtcpPort;
            return false;
        }
        auto tryBindPair = [&](uint16_t base) -> bool {
            if (base == 0) {
                return false;
            }
            bool okRtp = m_rtpSocket->bind(bindAddr, base);
            bool okRtcp = m_rtcpSocket->bind(bindAddr, static_cast<uint16_t>(base + 1));
            if (okRtp && okRtcp) {
                m_clientRtpPort = base;
                m_clientRtcpPort = static_cast<uint16_t>(base + 1);
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Local bind " << bindAddr.toStdString()
                          << ":" << m_clientRtpPort << "/" << m_clientRtcpPort;
                selfProbeUdp(bindAddr, m_clientRtpPort);
                return true;
            }
            m_rtpSocket->close();
            m_rtcpSocket->close();
            return false;
        };
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

    void handleRtpPackets() {
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
            m_lastRtpTime = std::chrono::steady_clock::now();
            handleRtpPacket(reinterpret_cast<const uint8_t*>(datagram.constData()),
                            static_cast<size_t>(datagram.size()));
        }
    }

    void handleRtpPacket(const uint8_t* data, size_t len) {
        if (!data || len < 12) {
            return;
        }
        uint8_t version = data[0] >> 6;
        if (version != 2) {
            return;
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
            return;
        }
        uint32_t timestamp = (static_cast<uint32_t>(data[4]) << 24) |
                             (static_cast<uint32_t>(data[5]) << 16) |
                             (static_cast<uint32_t>(data[6]) << 8) |
                             static_cast<uint32_t>(data[7]);

        size_t offset = 12 + static_cast<size_t>(csrcCount) * 4;
        if (offset > len) {
            return;
        }
        if (extension) {
            if (offset + 4 > len) {
                return;
            }
            uint16_t extLen = (static_cast<uint16_t>(data[offset + 2]) << 8) | static_cast<uint16_t>(data[offset + 3]);
            offset += 4 + static_cast<size_t>(extLen) * 4;
        }
        if (offset >= len) {
            return;
        }
        size_t payloadLen = len - offset;
        if (padding && payloadLen > 0) {
            uint8_t padCount = data[len - 1];
            if (padCount < payloadLen) {
                payloadLen -= padCount;
            }
        }
        const uint8_t* payload = data + offset;
        if (isTransportStreamCodec()) {
            handleTsPayload(timestamp, payload, payloadLen);
        } else if (isHevcCodec()) {
            depayH265(marker, timestamp, payload, payloadLen);
        } else {
            depayH264(marker, timestamp, payload, payloadLen);
        }
    }

    void handleRtcpPackets() {
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
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Sent RTCP RR (local SSRC=0x"
                      << std::hex << mySsrc << std::dec
                      << ", remote SSRC=0x" << std::hex << senderSsrc << std::dec
                      << ", port=" << targetPort << ")";
        } else {
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Failed to send RTCP RR";
        }
    }

    void depayH264(bool marker, uint32_t timestamp, const uint8_t* payload, size_t size) {
        if (!payload || size == 0) {
            return;
        }
        m_currentCodec = SwVideoPacket::Codec::H264;
        if (m_codecName != "h264") {
            if (!m_loggedUnsupportedCodec.exchange(true)) {
                swCError(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Unsupported codec in SDP: " << m_codecName << " (expected h264)";
            }
            return;
        }
        m_hevcHeadersInserted = false;
        auto pktCount = ++m_rtpPackets;
        if (pktCount <= 3 || (pktCount % 200) == 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTP packet #" << pktCount
                      << " size=" << size << " ts=" << timestamp
                      << " marker=" << (marker ? 1 : 0);
        }

        if (!m_haveTimestamp) {
            m_currentTimestamp = timestamp;
            m_haveTimestamp = true;
        }
        if (!m_haveFirstTimestamp) {
            m_firstTimestamp = timestamp;
            m_haveFirstTimestamp = true;
        }

        if (timestamp != m_currentTimestamp) {
            flushFrame(m_currentTimestamp);
            m_currentTimestamp = timestamp;
        }

        uint8_t nalType = payload[0] & 0x1F;
        if (nalType >= 1 && nalType <= 23) {
            appendStartCode();
            m_nalBuffer.insert(m_nalBuffer.end(), payload, payload + size);
            if (nalType == 5) {
                m_currentKeyFrame = true;
            }
        } else if (nalType == 24) { // STAP-A
            size_t offset = 1;
            while (offset + 2 <= size) {
                uint16_t nalSize = (static_cast<uint16_t>(payload[offset]) << 8) |
                                   static_cast<uint16_t>(payload[offset + 1]);
                offset += 2;
                if (offset + nalSize > size) {
                    break;
                }
                appendStartCode();
                m_nalBuffer.insert(m_nalBuffer.end(), payload + offset, payload + offset + nalSize);
                uint8_t innerType = payload[offset] & 0x1F;
                if (innerType == 5) {
                    m_currentKeyFrame = true;
                }
                offset += nalSize;
            }
        } else if (nalType == 28) { // FU-A
            if (size < 2) {
                return;
            }
            uint8_t fuHeader = payload[1];
            bool start = (fuHeader & 0x80) != 0;
            bool end = (fuHeader & 0x40) != 0;
            uint8_t reconstructed = (payload[0] & 0xE0) | (fuHeader & 0x1F);
            if (start) {
                appendStartCode();
                m_nalBuffer.push_back(reconstructed);
            }
            if (size > 2) {
                m_nalBuffer.insert(m_nalBuffer.end(), payload + 2, payload + size);
            }
            if ((fuHeader & 0x1F) == 5) {
                m_currentKeyFrame = true;
            }
            if (end && marker) {
                flushFrame(timestamp);
                return;
            }
        } else {
            return; // unsupported aggregation
        }

        if (marker) {
            flushFrame(timestamp);
        }
    }

    void depayH265(bool marker, uint32_t timestamp, const uint8_t* payload, size_t size) {
        if (!payload || size < 3) {
            return;
        }
        m_currentCodec = SwVideoPacket::Codec::H265;
        auto pktCount = ++m_rtpPackets;
        if (pktCount <= 3 || (pktCount % 200) == 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] RTP (H265) packet #" << pktCount
                      << " size=" << size << " ts=" << timestamp
                      << " marker=" << (marker ? 1 : 0);
        }
        uint8_t nalType = static_cast<uint8_t>((payload[0] >> 1) & 0x3F);
        if (!m_haveTimestamp) {
            m_currentTimestamp = timestamp;
            m_haveTimestamp = true;
        }
        if (!m_haveFirstTimestamp) {
            m_firstTimestamp = timestamp;
            m_haveFirstTimestamp = true;
        }
        if (timestamp != m_currentTimestamp) {
            flushFrame(m_currentTimestamp);
            m_currentTimestamp = timestamp;
        }
        if (!m_hevcHeadersInserted) {
            appendHevcParameterSets();
            m_hevcHeadersInserted = true;
        }
        if (nalType <= 47) {
            appendStartCode();
            m_nalBuffer.insert(m_nalBuffer.end(), payload, payload + size);
            if (isHevcKeyNal(nalType)) {
                m_currentKeyFrame = true;
            }
        } else if (nalType == 48) { // Aggregation Packet (AP)
            size_t offset = 2;
            while (offset + 2 <= size) {
                uint16_t nalSize = static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
                offset += 2;
                if (offset + nalSize > size) {
                    break;
                }
                appendStartCode();
                m_nalBuffer.insert(m_nalBuffer.end(), payload + offset, payload + offset + nalSize);
                uint8_t innerType = static_cast<uint8_t>((payload[offset] >> 1) & 0x3F);
                if (isHevcKeyNal(innerType)) {
                    m_currentKeyFrame = true;
                }
                offset += nalSize;
            }
        } else if (nalType == 49) { // Fragmentation Unit (FU)
            if (size < 4) {
                return;
            }
            uint8_t fuHeader = payload[2];
            bool start = (fuHeader & 0x80) != 0;
            bool end = (fuHeader & 0x40) != 0;
            uint8_t fuNalType = fuHeader & 0x3F;
            uint8_t reconstructed0 = static_cast<uint8_t>((payload[0] & 0x81) | (fuNalType << 1));
            uint8_t reconstructed1 = payload[1];
            if (start) {
                appendStartCode();
                m_nalBuffer.push_back(reconstructed0);
                m_nalBuffer.push_back(reconstructed1);
                if (isHevcKeyNal(fuNalType)) {
                    m_currentKeyFrame = true;
                }
            }
            if (size > 3) {
                m_nalBuffer.insert(m_nalBuffer.end(), payload + 3, payload + size);
            }
            if (end && marker) {
                flushFrame(timestamp);
                return;
            }
        } else {
            return;
        }
        if (marker) {
            flushFrame(timestamp);
        }
    }

    void handleTsPayload(uint32_t rtpTimestamp, const uint8_t* data, size_t size) {
        m_tsDemux.feed(data, size, rtpTimestamp, [this](const std::vector<uint8_t>& es, bool key, uint32_t ts) {
            SwVideoPacket::Codec codec = m_tsDemux.isHevc() ? SwVideoPacket::Codec::H265
                                                            : SwVideoPacket::Codec::H264;
            SwByteArray payload(reinterpret_cast<const char*>(es.data()), static_cast<int>(es.size()));
            SwVideoPacket packet(codec,
                                 payload,
                                 static_cast<std::int64_t>(ts),
                                 static_cast<std::int64_t>(ts),
                                 key);
            emitPacket(packet);
            setPlaceholderActive(false);
            auto emitted = ++m_framesEmitted;
            if (emitted <= 3 || (emitted % 100) == 0) {
                swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource][TS] Emitted frame #" << emitted
                          << " bytes=" << payload.size()
                          << " key=" << (key ? 1 : 0);
            }
        });
    }

    void appendStartCode() {
        static const uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};
        m_nalBuffer.insert(m_nalBuffer.end(), std::begin(startCode), std::end(startCode));
    }

    void appendHevcParameterSets() {
        auto append = [this](const std::vector<uint8_t>& nal) {
            if (nal.empty()) {
                return;
            }
            appendStartCode();
            m_nalBuffer.insert(m_nalBuffer.end(), nal.begin(), nal.end());
        };
        append(m_hevcVps);
        append(m_hevcSps);
        append(m_hevcPps);
    }

    static bool isHevcKeyNal(uint8_t nalType) {
        return (nalType >= 16 && nalType <= 21);
    }

    void checkRtpTimeout() {
        if (!isRunning()) {
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
            uint16_t portHint = m_detectedRtpPort ? m_detectedRtpPort : m_serverRtpPort;
            swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] No RTP received for " << elapsed
                        << " s (local=" << m_clientRtpPort
                        << ", expected remote=" << portHint << ")";
            setPlaceholderActive(true);
        }
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
        if (shouldDropFrame(timestamp, m_currentKeyFrame)) {
            auto dropped = ++m_framesDropped;
            if (dropped <= 3 || (dropped % 25) == 0) {
                swCWarning(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Dropping frame #" << dropped
                            << " to reduce latency (ts=" << timestamp << ")";
            }
            m_nalBuffer.clear();
            m_currentKeyFrame = false;
            return;
        }
        setPlaceholderActive(false);
        SwByteArray payload(reinterpret_cast<const char*>(m_nalBuffer.data()),
                            static_cast<int>(m_nalBuffer.size()));
        SwVideoPacket packet(m_currentCodec,
                             payload,
                             static_cast<std::int64_t>(timestamp),
                             static_cast<std::int64_t>(timestamp),
                             m_currentKeyFrame);
        emitPacket(packet);
        auto emitted = ++m_framesEmitted;
        if (emitted <= 3 || (emitted % 100) == 0) {
            swCDebug(kSwLogCategory_SwRtspUdpSource) << "[SwRtspUdpSource] Emitted frame #" << emitted
                      << " bytes=" << payload.size()
                      << " key=" << (m_currentKeyFrame ? 1 : 0);
        }
        m_nalBuffer.clear();
        m_currentKeyFrame = false;
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

    SwObject* m_parent{nullptr};
    SwString m_url;
    SwString m_host;
    int m_port{554};
    SwString m_path{"/"};
    SwString m_bindAddress{};
    std::string m_baseUrl;
    std::map<int, RtspRequest> m_pendingRequests;
    int m_maxRequestRetries{1};

    SwTcpSocket* m_rtspSocket{nullptr};
    SwUdpSocket* m_rtpSocket{nullptr};
    SwUdpSocket* m_rtcpSocket{nullptr};
    PollWorkerThread* m_pollWorker{nullptr};
    SwTimer* m_keepAliveTimer{nullptr};
    SwTimer* m_reconnectTimer{nullptr};

    std::string m_ctrlBuffer;
    std::string m_sdp;
    std::string m_trackControl;
    std::string m_sessionControl;
    std::string m_sessionId;
    RtspStep m_state{RtspStep::None};
    int m_cseq{0};
    int m_payloadType{96};
    int m_clockRate{90000};
    std::atomic<bool> m_triedAggregatePlay{false};
    std::string m_codecName{"h264"};
    std::atomic<bool> m_loggedUnsupportedCodec{false};
    bool m_useTcpTransport{false};
    bool m_triedTcpFallback{false};
    std::atomic<bool> m_autoReconnect{false};

    uint16_t m_clientRtpPort{0};
    uint16_t m_clientRtcpPort{0};
    uint16_t m_serverRtpPort{0};
    uint16_t m_serverRtcpPort{0};
    uint16_t m_forcedClientRtpPort{0};
    uint16_t m_forcedClientRtcpPort{0};

    uint32_t m_currentTimestamp{0};
    bool m_haveTimestamp{false};
    bool m_haveFirstTimestamp{false};
    uint32_t m_firstTimestamp{0};
    bool m_currentKeyFrame{false};
    std::atomic<uint64_t> m_rtpPackets{0};
    std::atomic<uint64_t> m_framesEmitted{0};
    std::atomic<uint64_t> m_framesDropped{0};
    std::atomic<uint64_t> m_payloadMismatch{0};
    std::vector<uint8_t> m_nalBuffer;
    bool m_lowLatencyDrop{true};
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
    std::chrono::steady_clock::time_point m_playStart{};
    std::string m_ctrlAccum;
    std::string m_ctrlTextBuffer;
    uint32_t m_localSsrc{0};
    uint32_t m_remoteSsrc{0};
    uint16_t m_detectedRtpPort{0};
    uint16_t m_detectedRtcpPort{0};
    int m_workerIntervalMs{2};
    int m_reconnectDelayMs{2000};
    std::atomic<bool> m_reconnectPending{false};
    std::vector<uint8_t> m_hevcVps;
    std::vector<uint8_t> m_hevcSps;
    std::vector<uint8_t> m_hevcPps;
    bool m_hevcHeadersInserted{false};
    std::string m_hevcProfileLevelId;
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

        bool isHevc() const { return hevcStream; }

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
