#pragma once

/**
 * @file src/media/rtsp/SwRtspServerTransport.h
 * @brief RTSP control-plane server publishing RTP/UDP media sessions.
 */

#include "core/io/SwTcpServer.h"
#include "core/io/SwTcpSocket.h"
#include "core/io/SwUdpSocket.h"
#include "media/rtp/SwRtpPacketizer.h"
#include "media/server/SwVideoTransportServer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

class SwRtspServerTransport : public SwVideoTransportServer {
public:
    SwRtspServerTransport()
        : m_callbackContext(new SwObject()) {
        SwObject::connect(&m_controlServer,
                          &SwTcpServer::newConnection,
                          m_callbackContext,
                          [this]() { onNewConnection_(); });
    }

    ~SwRtspServerTransport() override {
        stop();
        delete m_callbackContext;
        m_callbackContext = nullptr;
    }

    SwString protocolName() const override {
        return "rtsp";
    }

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
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running || m_streams.isEmpty() || config().endpoint.port == 0U) {
            return false;
        }
        const SwString bindAddress = controlBindAddress_();
        if (!m_controlServer.listen(bindAddress, config().endpoint.port)) {
            return false;
        }
        m_running = true;
        return true;
    }

    void stop() override {
        std::vector<std::shared_ptr<ClientSession>> clients;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running && m_clients.empty()) {
                return;
            }
            m_running = false;
            m_controlServer.close();
            clients.swap(m_clients);
        }

        for (size_t i = 0; i < clients.size(); ++i) {
            closeSession_(clients[i]);
        }
    }

    bool isRunning() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    bool publishVideoPacket(const SwString& streamId,
                            const SwVideoPacket& packet) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        const SwVideoPublishStream* stream = findStreamLocked_(streamId);
        if (!m_running || !stream || packet.payload().isEmpty()) {
            ++m_metrics.framesDropped;
            return false;
        }

        ++m_metrics.framesAccepted;
        m_metrics.videoBytesAccepted += packet.payload().size();

        std::vector<SwByteArray> datagrams =
            m_packetizer.packetize(*stream, packet, config().mtuBytes);
        if (datagrams.empty()) {
            ++m_metrics.framesDropped;
            ++m_metrics.transport.sendFailures;
            return false;
        }

        size_t eligibleClients = 0U;
        size_t completeClientSends = 0U;
        size_t datagramsSent = 0U;
        size_t bytesSent = 0U;
        for (size_t clientIndex = 0; clientIndex < m_clients.size(); ++clientIndex) {
            const std::shared_ptr<ClientSession>& client = m_clients[clientIndex];
            if (!client || !client->playing || !client->rtpSocket ||
                client->destinationAddress.empty() || client->clientRtpPort == 0U) {
                continue;
            }
            ++eligibleClients;
            bool sentAll = true;
            for (size_t i = 0; i < datagrams.size(); ++i) {
                const SwByteArray& datagram = datagrams[i];
                const int64_t sent = client->rtpSocket->writeDatagram(datagram.constData(),
                                                                      static_cast<int64_t>(datagram.size()),
                                                                      SwString(client->destinationAddress),
                                                                      client->clientRtpPort);
                if (sent != static_cast<int64_t>(datagram.size())) {
                    sentAll = false;
                    break;
                }
                ++datagramsSent;
                bytesSent += datagram.size();
            }
            if (sentAll) {
                ++completeClientSends;
            }
        }

        if (eligibleClients == 0U || completeClientSends == 0U) {
            ++m_metrics.framesDropped;
            ++m_metrics.transport.sendFailures;
            return false;
        }

        ++m_metrics.framesSent;
        m_metrics.videoBytesSent += packet.payload().size();
        m_metrics.transport.datagramsSent += datagramsSent;
        m_metrics.transport.bytesSent += bytesSent;
        return true;
    }

    SwVideoServerMetrics metrics() const override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_metrics;
    }

    size_t activeClientCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_clients.size();
    }

private:
    struct RtspRequest {
        std::string method{};
        std::string url{};
        std::string version{};
        std::map<std::string, std::string> headers{};
        int cseq{0};
    };

    struct ClientSession {
        SwTcpSocket* control{nullptr};
        std::string buffer{};
        std::string sessionId{};
        std::string clientAddress{};
        std::string destinationAddress{};
        uint16_t clientRtpPort{0};
        uint16_t clientRtcpPort{0};
        uint16_t serverRtpPort{0};
        uint16_t serverRtcpPort{0};
        std::shared_ptr<SwUdpSocket> rtpSocket{};
        std::shared_ptr<SwUdpSocket> rtcpSocket{};
        bool setup{false};
        bool playing{false};
        bool closing{false};
    };

    static std::string toLower_(std::string value) {
        std::transform(value.begin(),
                       value.end(),
                       value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    static void trimInPlace_(std::string& value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
    }

    static std::string trimmed_(std::string value) {
        trimInPlace_(value);
        return value;
    }

    static bool isWildcardAddress_(const std::string& value) {
        const std::string lower = toLower_(trimmed_(value));
        return lower.empty() || lower == "0.0.0.0" || lower == "::" || lower == "*";
    }

    static std::string headerValue_(const RtspRequest& request,
                                    const std::string& key) {
        const auto it = request.headers.find(toLower_(key));
        return it == request.headers.end() ? std::string() : it->second;
    }

    static bool parseRtspRequest_(const std::string& raw, RtspRequest& out) {
        std::istringstream input(raw);
        std::string line;
        if (!std::getline(input, line)) {
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream requestLine(line);
        requestLine >> out.method >> out.url >> out.version;
        if (out.method.empty() || out.url.empty()) {
            return false;
        }

        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string key = toLower_(line.substr(0, colon));
            std::string value = line.substr(colon + 1U);
            trimInPlace_(key);
            trimInPlace_(value);
            out.headers[key] = value;
        }

        const std::string cseq = headerValue_(out, "cseq");
        out.cseq = cseq.empty() ? 0 : std::atoi(cseq.c_str());
        return true;
    }

    static bool parsePortPair_(const std::string& text,
                               const std::string& key,
                               uint16_t& first,
                               uint16_t& second) {
        const std::string lower = toLower_(text);
        std::size_t pos = lower.find(key);
        if (pos == std::string::npos) {
            return false;
        }
        pos += key.size();
        const int parsedFirst = std::atoi(lower.c_str() + pos);
        const std::size_t dash = lower.find('-', pos);
        int parsedSecond = parsedFirst + 1;
        if (dash != std::string::npos) {
            parsedSecond = std::atoi(lower.c_str() + dash + 1U);
        }
        if (parsedFirst <= 0 || parsedFirst > 65535 ||
            parsedSecond <= 0 || parsedSecond > 65535) {
            return false;
        }
        first = static_cast<uint16_t>(parsedFirst);
        second = static_cast<uint16_t>(parsedSecond);
        return true;
    }

    static std::string parseTransportParameter_(const std::string& text,
                                                const std::string& key) {
        const std::string lower = toLower_(text);
        std::size_t pos = lower.find(key);
        if (pos == std::string::npos) {
            return std::string();
        }
        pos += key.size();
        const std::size_t end = text.find(';', pos);
        return trimmed_(text.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
    }

    static bool transportRequestsMulticast_(const std::string& transport) {
        return toLower_(transport).find("multicast") != std::string::npos;
    }

    SwString controlBindAddress_() const {
        if (!config().endpoint.bindAddress.isEmpty()) {
            return config().endpoint.bindAddress;
        }
        return config().endpoint.host.contains(":") ? SwString("::") : SwString("0.0.0.0");
    }

    SwString rtpBindAddress_() const {
        const SwString bindAddress = controlBindAddress_();
        if (!bindAddress.isEmpty()) {
            return bindAddress;
        }
        return "0.0.0.0";
    }

    const SwVideoPublishStream* findStreamLocked_(const SwString& streamId) const {
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            if (it->id == streamId || it->trackId == streamId) {
                return &(*it);
            }
        }
        return nullptr;
    }

    const SwVideoPublishStream* firstStreamLocked_() const {
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            return &(*it);
        }
        return nullptr;
    }

    void onNewConnection_() {
        while (SwTcpSocket* socket = m_controlServer.nextPendingConnection()) {
            std::shared_ptr<ClientSession> session(new ClientSession());
            session->control = socket;
            session->clientAddress = socket->peerAddress().toStdString();
            socket->setParent(m_callbackContext);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_running) {
                    socket->close();
                    socket->deleteLater();
                    continue;
                }
                m_clients.push_back(session);
            }

            SwObject::connect(socket,
                              &SwTcpSocket::readyRead,
                              m_callbackContext,
                              [this, session]() { readClient_(session); });
            SwObject::connect(socket,
                              &SwTcpSocket::disconnected,
                              m_callbackContext,
                              [this, session]() { removeClient_(session); });
        }
    }

    void readClient_(const std::shared_ptr<ClientSession>& session) {
        if (!session || !session->control || session->closing) {
            return;
        }
        while (true) {
            const SwString chunk = session->control->read();
            if (chunk.isEmpty()) {
                break;
            }
            session->buffer += chunk.toStdString();
        }
        processClientBuffer_(session);
    }

    void processClientBuffer_(const std::shared_ptr<ClientSession>& session) {
        while (session && !session->closing) {
            const std::size_t headerEnd = session->buffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                return;
            }
            const std::string raw = session->buffer.substr(0, headerEnd + 4U);
            session->buffer.erase(0, headerEnd + 4U);

            RtspRequest request;
            if (!parseRtspRequest_(raw, request)) {
                sendResponse_(session, 400, "Bad Request", 0, std::vector<std::string>());
                continue;
            }
            handleRequest_(session, request);
        }
    }

    void handleRequest_(const std::shared_ptr<ClientSession>& session,
                        const RtspRequest& request) {
        const std::string method = toLower_(request.method);
        if (method == "options") {
            sendResponse_(session,
                          200,
                          "OK",
                          request.cseq,
                          {"Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, GET_PARAMETER, TEARDOWN"});
        } else if (method == "describe") {
            handleDescribe_(session, request);
        } else if (method == "setup") {
            handleSetup_(session, request);
        } else if (method == "play") {
            uint16_t nextSequence = 0;
            uint32_t rtpTime = 0;
            std::string sessionId;
            bool setup = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                setup = session->setup;
                if (!setup) {
                    sessionId = session->sessionId;
                } else {
                    session->playing = true;
                    sessionId = session->sessionId;
                    nextSequence = m_packetizer.nextSequenceNumber();
                    rtpTime = m_packetizer.lastTimestamp();
                }
            }
            if (!setup) {
                sendResponse_(session, 455, "Method Not Valid In This State", request.cseq, {});
                return;
            }
            std::ostringstream rtpInfo;
            rtpInfo << "RTP-Info: url=" << request.url
                    << "/trackID=0;seq=" << nextSequence
                    << ";rtptime=" << rtpTime;
            sendResponse_(session,
                          200,
                          "OK",
                          request.cseq,
                          {"Session: " + sessionId, rtpInfo.str()});
        } else if (method == "pause") {
            std::string sessionId;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                session->playing = false;
                sessionId = session->sessionId;
            }
            sendResponse_(session, 200, "OK", request.cseq, {"Session: " + sessionId});
        } else if (method == "get_parameter") {
            std::string sessionId;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                sessionId = session->sessionId;
            }
            sendResponse_(session, 200, "OK", request.cseq, {"Session: " + sessionId});
        } else if (method == "teardown") {
            std::string sessionId;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                sessionId = session->sessionId;
            }
            sendResponse_(session, 200, "OK", request.cseq, {"Session: " + sessionId});
            session->closing = true;
            removeClient_(session);
        } else {
            sendResponse_(session, 501, "Not Implemented", request.cseq, {});
        }
    }

    void handleDescribe_(const std::shared_ptr<ClientSession>& session,
                         const RtspRequest& request) {
        SwVideoPublishStream stream;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const SwVideoPublishStream* found = firstStreamLocked_();
            if (!found) {
                sendResponse_(session, 404, "Not Found", request.cseq, {});
                return;
            }
            stream = *found;
        }

        const std::string body = makeSdp_(session, request, stream);
        std::vector<std::string> headers;
        headers.push_back("Content-Base: " + contentBase_(request));
        headers.push_back("Content-Type: application/sdp");
        sendResponse_(session, 200, "OK", request.cseq, headers, body);
    }

    void handleSetup_(const std::shared_ptr<ClientSession>& session,
                      const RtspRequest& request) {
        const std::string transport = headerValue_(request, "transport");
        uint16_t clientRtp = 0;
        uint16_t clientRtcp = 0;
        const bool multicast = transportRequestsMulticast_(transport) ||
                               config().endpoint.deliveryMode == SwMediaTransportDeliveryMode::Multicast;
        if (transport.empty() ||
            (!multicast && !parsePortPair_(transport, "client_port=", clientRtp, clientRtcp)) ||
            (multicast && config().endpoint.deliveryMode != SwMediaTransportDeliveryMode::Multicast)) {
            sendResponse_(session, 461, "Unsupported Transport", request.cseq, {});
            return;
        }

        std::string destination = parseTransportParameter_(transport, "destination=");
        if (destination.empty()) {
            destination = multicast ? config().endpoint.host.toStdString() : session->clientAddress;
        }
        if (destination.empty() || (multicast && isWildcardAddress_(destination))) {
            sendResponse_(session, 400, "Bad Request", request.cseq, {});
            return;
        }

        uint32_t ssrc = 0;
        std::string sessionId;
        uint16_t serverRtpPort = 0;
        uint16_t serverRtcpPort = 0;
        bool opened = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            opened = openSessionRtpSockets_(session);
            if (opened) {
                session->clientRtpPort = multicast ? session->serverRtpPort : clientRtp;
                session->clientRtcpPort = multicast ? session->serverRtcpPort : clientRtcp;
                session->destinationAddress = destination;
                session->setup = true;
                if (session->sessionId.empty()) {
                    std::ostringstream id;
                    id << "swrtsp-" << ++m_nextSessionId;
                    session->sessionId = id.str();
                }
                sessionId = session->sessionId;
                serverRtpPort = session->serverRtpPort;
                serverRtcpPort = session->serverRtcpPort;
                ssrc = m_packetizer.ssrc();
            }
        }
        if (!opened) {
            sendResponse_(session, 500, "Internal Server Error", request.cseq, {});
            return;
        }

        std::ostringstream transportHeader;
        if (multicast) {
            transportHeader << "Transport: RTP/AVP;multicast;destination="
                            << destination
                            << ";port=" << serverRtpPort << "-" << serverRtcpPort
                            << ";ttl=" << static_cast<int>(config().endpoint.ttl)
                            << ";ssrc=" << std::hex << ssrc << std::dec;
        } else {
            transportHeader << "Transport: RTP/AVP;unicast;client_port="
                            << clientRtp << "-" << clientRtcp
                            << ";server_port=" << serverRtpPort
                            << "-" << serverRtcpPort
                            << ";ssrc=" << std::hex << ssrc << std::dec;
        }

        sendResponse_(session,
                      200,
                      "OK",
                      request.cseq,
                      {transportHeader.str(), "Session: " + sessionId + ";timeout=30"});
    }

    bool openSessionRtpSockets_(const std::shared_ptr<ClientSession>& session) {
        if (!session) {
            return false;
        }
        session->rtpSocket.reset(new SwUdpSocket());
        session->rtcpSocket.reset(new SwUdpSocket());
        const SwString bindAddress = rtpBindAddress_();
        const SwUdpSocket::BindMode bindMode =
            SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint;
        if (!session->rtpSocket->bind(bindAddress, 0, bindMode)) {
            return false;
        }
        if (!session->rtcpSocket->bind(bindAddress, 0, bindMode)) {
            session->rtpSocket->close();
            return false;
        }
        if (config().endpoint.deliveryMode == SwMediaTransportDeliveryMode::Multicast &&
            (!configureMulticastSocket_(session->rtpSocket) ||
             !configureMulticastSocket_(session->rtcpSocket))) {
            session->rtpSocket->close();
            session->rtcpSocket->close();
            return false;
        }
        session->serverRtpPort = session->rtpSocket->localPort();
        session->serverRtcpPort = session->rtcpSocket->localPort();
        return session->serverRtpPort != 0U && session->serverRtcpPort != 0U;
    }

    bool configureMulticastSocket_(const std::shared_ptr<SwUdpSocket>& socket) const {
        if (!socket) {
            return false;
        }
        if (!socket->setMulticastTimeToLive(config().endpoint.ttl)) {
            return false;
        }
        if (!socket->setMulticastLoopbackEnabled(config().endpoint.multicastLoopback)) {
            return false;
        }
        if (!config().endpoint.interfaceAddress.isEmpty() &&
            !socket->setMulticastInterface(config().endpoint.interfaceAddress)) {
            return false;
        }
        return true;
    }

    std::string contentBase_(const RtspRequest& request) const {
        std::string base = request.url;
        const std::size_t query = base.find('?');
        if (query != std::string::npos) {
            base.erase(query);
        }
        if (base.empty()) {
            base = "rtsp://127.0.0.1:" + std::to_string(config().endpoint.port) + "/";
        }
        if (base.back() != '/') {
            base += "/";
        }
        return base;
    }

    std::string publicAddressForSession_(const std::shared_ptr<ClientSession>& session) const {
        if (config().endpoint.deliveryMode == SwMediaTransportDeliveryMode::Multicast &&
            !config().endpoint.host.isEmpty() &&
            config().endpoint.host != "0.0.0.0") {
            return config().endpoint.host.toStdString();
        }
        if (session && session->control) {
            const std::string local = session->control->localAddress().toStdString();
            if (!isWildcardAddress_(local)) {
                return local;
            }
        }
        const std::string configured = config().endpoint.host.toStdString();
        if (!isWildcardAddress_(configured)) {
            return configured;
        }
        return "127.0.0.1";
    }

    std::string makeSdp_(const std::shared_ptr<ClientSession>& session,
                         const RtspRequest& request,
                         const SwVideoPublishStream& stream) const {
        const uint8_t payloadType = SwRtpPacketizer::payloadTypeForCodec(stream.codec);
        const std::string address = publicAddressForSession_(session);
        const std::string addressFamily = address.find(':') == std::string::npos ? "IP4" : "IP6";
        const std::string codec = sdpCodecName_(stream.codec);

        std::ostringstream sdp;
        sdp << "v=0\r\n";
        sdp << "o=- 0 0 IN " << addressFamily << " " << address << "\r\n";
        sdp << "s=" << config().name.toStdString() << "\r\n";
        sdp << "c=IN " << addressFamily << " " << address << "\r\n";
        sdp << "t=0 0\r\n";
        sdp << "a=control:*\r\n";
        sdp << "m=video 0 RTP/AVP " << static_cast<int>(payloadType) << "\r\n";
        sdp << "a=rtpmap:" << static_cast<int>(payloadType) << " " << codec << "/90000\r\n";
        if (stream.codec == SwVideoPacket::Codec::H264) {
            sdp << "a=fmtp:" << static_cast<int>(payloadType) << " packetization-mode=1\r\n";
        }
        sdp << "a=control:" << trackControl_(request) << "\r\n";
        return sdp.str();
    }

    static std::string sdpCodecName_(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return "H264";
        case SwVideoPacket::Codec::H265:
            return "H265";
        case SwVideoPacket::Codec::AV1:
            return "AV1";
        case SwVideoPacket::Codec::VP8:
            return "VP8";
        case SwVideoPacket::Codec::VP9:
            return "VP9";
        case SwVideoPacket::Codec::MotionJPEG:
            return "JPEG";
        default:
            break;
        }
        return "H264";
    }

    static std::string trackControl_(const RtspRequest&) {
        return "trackID=0";
    }

    void sendResponse_(const std::shared_ptr<ClientSession>& session,
                       int status,
                       const char* reason,
                       int cseq,
                       const std::vector<std::string>& headers,
                       const std::string& body = std::string()) {
        if (!session || !session->control || session->closing) {
            return;
        }
        std::ostringstream response;
        response << "RTSP/1.0 " << status << " " << reason << "\r\n";
        if (cseq > 0) {
            response << "CSeq: " << cseq << "\r\n";
        }
        response << "Server: SwStack-RTSP\r\n";
        for (size_t i = 0; i < headers.size(); ++i) {
            response << headers[i] << "\r\n";
        }
        if (!body.empty()) {
            response << "Content-Length: " << body.size() << "\r\n";
        }
        response << "\r\n";
        response << body;
        session->control->write(SwString(response.str()));
    }

    void removeClient_(const std::shared_ptr<ClientSession>& session) {
        if (!session) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = std::remove_if(m_clients.begin(),
                                     m_clients.end(),
                                     [&session](const std::shared_ptr<ClientSession>& candidate) {
                                         return candidate == session;
                                     });
            m_clients.erase(it, m_clients.end());
        }
        closeSession_(session);
    }

    static void closeSession_(const std::shared_ptr<ClientSession>& session) {
        if (!session) {
            return;
        }
        session->playing = false;
        session->setup = false;
        if (session->rtpSocket) {
            session->rtpSocket->close();
        }
        if (session->rtcpSocket) {
            session->rtcpSocket->close();
        }
        if (session->control) {
            SwTcpSocket* socket = session->control;
            session->control = nullptr;
            socket->close();
            socket->deleteLater();
        }
    }

    mutable std::mutex m_mutex;
    SwObject* m_callbackContext{nullptr};
    SwTcpServer m_controlServer{};
    SwList<SwVideoPublishStream> m_streams{};
    SwVideoServerMetrics m_metrics{};
    SwRtpPacketizer m_packetizer{};
    std::vector<std::shared_ptr<ClientSession>> m_clients{};
    uint32_t m_nextSessionId{0};
    bool m_running{false};
};
