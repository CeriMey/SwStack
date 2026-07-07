#ifndef SWHTTP3CLIENT_H
#define SWHTTP3CLIENT_H

#include "SwByteArray.h"
#include "SwMap.h"
#include "SwString.h"
#include "SwUdpSocket.h"
#include "http3/SwHttp3Connection.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicHandshakeClient.h"
#include "quic/SwQuicSessionTicket.h"
#include "quic/SwTls13Messages.h"

#include <chrono>
#include <cstdint>
#include <vector>

// Blocking, poll-driven HTTP/3 client (RFC 9114 over QUIC v1 + TLS 1.3).
//
// It is the QUIC counterpart of SwHttpClient: given a URL host/port/path it
// runs the full handshake (SwQuicHandshakeClient, authenticating the server by
// default), opens a 1-RTT connection, issues one HTTP/3 request on a
// client-initiated bidirectional stream, and returns the response. It owns its
// own SwUdpSocket and drives it to completion within a timeout, so it needs no
// external event loop -- callers get a synchronous request()/get()/post().
//
// ALPN is "h3"; QPACK is static-table only (always interoperable). For a
// self-signed peer, call setVerifyCertificateChain(false) to keep verifying the
// CertificateVerify signature while not requiring a trusted root.
class SwHttp3Client {
public:
    SwHttp3Client()
        : m_verifyPeer(true),
          m_verifyChain(true),
          m_statusCode(0) {
    }

    void setVerifyPeer(bool verify) { m_verifyPeer = verify; }
    void setVerifyCertificateChain(bool verify) { m_verifyChain = verify; }

    // Resume with a previously captured ticket: the next request is sent as
    // 0-RTT early data if the ticket permits it.
    void setResumptionTicket(const SwQuicSessionTicket& ticket) {
        m_resumptionTicket = ticket;
        m_haveResumptionTicket = true;
    }
    // A NewSessionTicket captured from the last request (valid() when present),
    // usable with setResumptionTicket() for a subsequent 0-RTT request.
    bool hasSessionTicket() const { return m_receivedTicket.valid; }
    const SwQuicSessionTicket& sessionTicket() const { return m_receivedTicket; }
    // Whether the last request's early data was accepted as 0-RTT.
    bool earlyDataAccepted() const { return m_earlyDataAccepted; }

    int statusCode() const { return m_statusCode; }
    const SwByteArray& responseBody() const { return m_responseBody; }
    SwString responseBodyAsString() const { return SwString(m_responseBody.toStdString()); }
    const SwMap<SwString, SwString>& responseHeaders() const { return m_responseHeaders; }

    bool get(const SwString& host, uint16_t port, const SwString& path,
             int timeoutMs = 10000, SwString* error = nullptr) {
        return request(SwByteArray("GET"), host, port, path, SwByteArray(),
                       SwByteArray(), timeoutMs, error);
    }

    bool post(const SwString& host, uint16_t port, const SwString& path,
              const SwByteArray& body,
              const SwByteArray& contentType = SwByteArray("application/json"),
              int timeoutMs = 10000, SwString* error = nullptr) {
        return request(SwByteArray("POST"), host, port, path, body, contentType,
                       timeoutMs, error);
    }

    bool request(const SwByteArray& method, const SwString& host, uint16_t port,
                 const SwString& path, const SwByteArray& body,
                 const SwByteArray& contentType, int timeoutMs, SwString* error = nullptr) {
        m_statusCode = 0;
        m_responseBody = SwByteArray();
        m_responseHeaders.clear();

        SwUdpSocket socket;
        socket.setMaxPendingDatagrams(64);
        if (!socket.bind(SwString("0.0.0.0"), 0)) {
            setError_(error, socket.errorString());
            return false;
        }

        // Build the HTTP/3 request first: when resuming it is sent as 0-RTT
        // early data inside the handshake's very first flight.
        SwHttp3Connection::Request h3Request;
        h3Request.method = method;
        h3Request.scheme = SwByteArray("https");
        h3Request.authority = SwByteArray(host.toStdString());
        h3Request.path = SwByteArray(path.toStdString());
        h3Request.extraHeaders = m_requestHeaders;
        if (!contentType.isEmpty()) {
            h3Request.extraHeaders.push_back(std::make_pair(SwByteArray("content-type"),
                                                            contentType));
        }
        h3Request.body = body;

        SwByteArray requestStream;
        if (!SwHttp3Connection::buildRequest(h3Request, requestStream, error)) {
            return false;
        }

        SwQuicHandshakeClient handshake;
        handshake.setVerifyPeer(m_verifyPeer);
        handshake.setVerifyCertificateChain(m_verifyChain);
        // Only send 0-RTT when we have a ticket that allows early data AND the
        // request fits within the server's remembered max_early_data_size
        // (RFC 9001 4.6). Otherwise resume without early data / fall back to
        // a full 1-RTT request.
        const bool resuming = m_haveResumptionTicket &&
                              m_resumptionTicket.allowsEarlyData() &&
                              static_cast<std::uint64_t>(requestStream.size()) <=
                                  m_resumptionTicket.serverInitialMaxData;
        if (resuming) {
            handshake.setResumption(m_resumptionTicket, requestStream);
        }

        SwByteArray initial;
        if (!handshake.start(host, initial, error)) {
            return false;
        }
        socket.writeDatagram(initial.constData(), static_cast<int64_t>(initial.size()),
                             host, port);

        const std::uint64_t deadline = nowMs_() + static_cast<std::uint64_t>(timeoutMs);
        while (!handshake.handshakeComplete() && nowMs_() < deadline) {
            socket.pollPendingDatagrams(5);
            while (socket.hasPendingDatagrams()) {
                SwString from;
                uint16_t fromPort = 0;
                const SwByteArray datagram = socket.receiveDatagram(&from, &fromPort);
                if (datagram.isEmpty()) {
                    continue;
                }
                std::vector<SwByteArray> replies;
                if (!handshake.processIncomingDatagram(datagram, replies, error)) {
                    return false;
                }
                for (std::size_t i = 0; i < replies.size(); ++i) {
                    socket.writeDatagram(replies[i].constData(),
                                         static_cast<int64_t>(replies[i].size()), host, port);
                }
            }
        }
        if (!handshake.handshakeComplete()) {
            setError_(error, SwString("QUIC/TLS handshake did not complete before timeout"));
            return false;
        }

        // 1-RTT connection over the derived application keys.
        SwQuicConnection connection(SwQuicConnection::Role::Client);
        connection.setLocalConnectionId(handshake.sourceConnectionId());
        connection.setPeerConnectionId(handshake.destinationConnectionId());
        connection.setLevelKeys(SwQuicConnection::Level::Application,
                                handshake.serverApplicationKeys(),
                                handshake.clientApplicationKeys());
        connection.setHandshakeConfirmed(true);
        if (handshake.hasPeerTransportParameters()) {
            connection.applyPeerTransportParameters(handshake.peerTransportParameters());
        }
        // 0-RTT and 1-RTT share the Application packet-number space: continue
        // the 1-RTT numbers after any 0-RTT packets already sent (RFC 9001 5.4).
        connection.setNextTxPacketNumber(SwQuicConnection::Level::Application,
                                         handshake.clientEarlyPacketNumber());

        m_earlyDataAccepted = resuming && handshake.earlyDataAccepted();

        // RFC 9114 6.2.1: each side MUST open a control stream and send its
        // SETTINGS as the first frame. Open the client control stream
        // (client-initiated unidirectional, id 2) before the request.
        {
            SwHttp3Frame::SettingList settings;
            settings.push_back(std::make_pair(SwHttp3Frame::settingQpackMaxTableCapacity(),
                                              std::uint64_t(0)));
            settings.push_back(std::make_pair(SwHttp3Frame::settingQpackBlockedStreams(),
                                              std::uint64_t(0)));
            settings.push_back(std::make_pair(SwHttp3Frame::settingMaxFieldSectionSize(),
                                              std::uint64_t(65536)));
            SwByteArray controlStream;
            if (!SwHttp3Connection::buildControlStream(settings, controlStream, error)) {
                return false;
            }
            if (!connection.sendStreamData(2, controlStream, false, error)) {
                return false;
            }
        }

        // Send the request on 1-RTT unless it already went out as accepted
        // 0-RTT early data. On 0-RTT rejection the request is resent here.
        if (!m_earlyDataAccepted) {
            if (!connection.sendStreamData(0, requestStream, true, error)) {
                return false;
            }
        }

        std::vector<SwByteArray> outgoing;
        if (!connection.buildDatagrams(nowMs_(), outgoing, error)) {
            return false;
        }
        for (std::size_t i = 0; i < outgoing.size(); ++i) {
            socket.writeDatagram(outgoing[i].constData(),
                                 static_cast<int64_t>(outgoing[i].size()), host, port);
        }

        SwByteArray responseStream;
        const std::uint64_t responseDeadline = nowMs_() + static_cast<std::uint64_t>(timeoutMs);
        while (nowMs_() < responseDeadline) {
            socket.pollPendingDatagrams(5);
            while (socket.hasPendingDatagrams()) {
                SwString from;
                uint16_t fromPort = 0;
                const SwByteArray datagram = socket.receiveDatagram(&from, &fromPort);
                if (datagram.isEmpty()) {
                    continue;
                }
                if (!connection.receiveDatagram(datagram, nowMs_(), error)) {
                    return false;
                }
            }

            responseStream.append(connection.readStream(0));
            const SwQuicStream* stream0 = connection.streams().stream(0);
            if (!responseStream.isEmpty() && stream0 && stream0->isReceiveComplete()) {
                // Capture a NewSessionTicket delivered on the 1-RTT CRYPTO
                // stream, for future 0-RTT.
                captureSessionTicket_(handshake, connection.cryptoData(
                    SwQuicConnection::Level::Application));
                return finishResponse_(responseStream, error);
            }

            // Drive the timers so a PTO retransmits the request if it (or the
            // response) was lost or reordered ahead of the server being ready,
            // then flush ACKs / retransmissions / probes back to the server.
            const std::int64_t timeout = connection.nextTimeoutMs(nowMs_());
            if (timeout == 0) {
                connection.onTimeout(nowMs_());
            }
            std::vector<SwByteArray> pending;
            if (connection.buildDatagrams(nowMs_(), pending, error)) {
                for (std::size_t i = 0; i < pending.size(); ++i) {
                    socket.writeDatagram(pending[i].constData(),
                                         static_cast<int64_t>(pending[i].size()), host, port);
                }
            }
        }

        setError_(error, SwString("HTTP/3 response did not complete before timeout"));
        return false;
    }

    // Extra request headers applied to every request until cleared.
    void setRawHeader(const SwByteArray& name, const SwByteArray& value) {
        m_requestHeaders.push_back(std::make_pair(name, value));
    }
    void clearRawHeaders() { m_requestHeaders.clear(); }

private:
    static void setError_(SwString* error, const SwString& message) {
        if (error) {
            *error = message;
        }
    }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // Parse NewSessionTicket messages from the 1-RTT CRYPTO stream and store
    // the resulting resumption ticket (RFC 8446 4.6.1).
    void captureSessionTicket_(SwQuicHandshakeClient& handshake, const SwByteArray& appCrypto) {
        if (appCrypto.isEmpty() || m_receivedTicket.valid) {
            return;
        }
        std::vector<SwTls13Messages::HandshakeMessage> messages;
        if (!SwTls13Messages::splitMessages(appCrypto, messages, nullptr)) {
            return;
        }
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].type == 0x04) { // NewSessionTicket
                SwQuicSessionTicket ticket;
                if (handshake.processNewSessionTicket(messages[i].body, SwByteArray(),
                                                      ticket, nullptr)) {
                    m_receivedTicket = ticket;
                    return;
                }
            }
        }
    }

    bool finishResponse_(const SwByteArray& responseStream, SwString* error) {
        SwHttp3Connection::Response response;
        if (!SwHttp3Connection::parseResponse(responseStream, response, error)) {
            return false;
        }
        m_statusCode = SwString(response.status.toStdString()).toInt();
        m_responseBody = response.body;
        for (std::size_t i = 0; i < response.headers.size(); ++i) {
            m_responseHeaders[SwString(response.headers[i].first.toStdString())] =
                SwString(response.headers[i].second.toStdString());
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    bool m_verifyPeer;
    bool m_verifyChain;
    std::vector<std::pair<SwByteArray, SwByteArray> > m_requestHeaders;

    // 0-RTT resumption.
    bool m_haveResumptionTicket = false;
    SwQuicSessionTicket m_resumptionTicket;
    SwQuicSessionTicket m_receivedTicket;
    bool m_earlyDataAccepted = false;

    int m_statusCode;
    SwByteArray m_responseBody;
    SwMap<SwString, SwString> m_responseHeaders;
};

#endif
