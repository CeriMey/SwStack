#ifndef SWQUICSERVER_H
#define SWQUICSERVER_H

#include "SwObject.h"
#include "SwString.h"
#include "SwUdpSocket.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicFrameCodec.h"
#include "quic/SwQuicPacketCodec.h"
#include "quic/SwQuicPacketHeader.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

// UDP driver for server-side SwQuicConnection endpoints.
//
// Incoming datagrams are routed to their connection by QUIC destination
// connection ID (RFC 9000 section 5.2); the sender's UDP endpoint is only the
// fallback for the very first Initial of a connection and for the plaintext
// loopback self-tests. After every receive/timeout the connection's outgoing
// datagrams are flushed back to the current remote endpoint.
class SwQuicServer : public SwObject {
    SW_OBJECT(SwQuicServer, SwObject)

public:
    explicit SwQuicServer(SwObject* parent = nullptr)
        : SwObject(parent),
          m_localConnectionIdLength(8) {
        SwObject::connect(&m_socket, &SwUdpSocket::readyRead, this, [this]() {
            SwString ignoredError;
            poll(0, &ignoredError);
        });
    }

    ~SwQuicServer() override {
        close();
    }

    bool listen(const SwString& bindAddress, uint16_t port, SwString* error = nullptr) {
        close();
        m_socket.setMaxDatagramSize(65536);
        m_socket.setMaxPendingDatagrams(2048);
        m_socket.setMaxReadBatchDatagrams(256);

        if (!m_socket.bind(bindAddress,
                           port,
                           SwUdpSocket::ShareAddress | SwUdpSocket::ReuseAddressHint)) {
            setError_(error, m_socket.errorString());
            return false;
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    void close() {
        m_socket.close();
        m_connections.clear();
        m_connectionIdIndex.clear();
    }

    bool isListening() const { return m_socket.isOpen(); }
    uint16_t localPort() const { return m_socket.localPort(); }
    SwString localAddress() const { return m_socket.localAddress(); }

    // Length of the connection IDs this server issues; incoming short-header
    // packets are parsed with it.
    void setLocalConnectionIdLength(std::size_t length) {
        m_localConnectionIdLength = length;
    }
    std::size_t localConnectionIdLength() const { return m_localConnectionIdLength; }

    int poll(int timeoutMs = 0, SwString* error = nullptr) {
        if (!isListening()) {
            setError_(error, SwString("QUIC server is not listening"));
            return -1;
        }

        m_socket.pollPendingDatagrams(timeoutMs);

        int processed = 0;
        while (m_socket.hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            const SwByteArray datagram = m_socket.receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                continue;
            }

            SwString datagramError;
            if (!processDatagram_(datagram, sender, senderPort, &datagramError)) {
                emitPacketRejected_(sender, senderPort, datagramError);
                continue;
            }
            ++processed;
        }

        fireExpiredTimers_();

        if (error) {
            *error = SwString();
        }
        return processed;
    }

    // Delay until the earliest connection timer fires, or -1 when idle. A
    // caller integrating with an event loop should schedule poll(0) after
    // this delay.
    std::int64_t nextTimeoutMs() const {
        const std::uint64_t now = nowMs_();
        std::int64_t next = -1;
        for (std::map<std::string, Entry_>::const_iterator it = m_connections.begin();
             it != m_connections.end(); ++it) {
            const std::int64_t t = it->second.connection.nextTimeoutMs(now);
            if (t >= 0 && (next < 0 || t < next)) {
                next = t;
            }
        }
        return next;
    }

    bool sendInitialPacket(const SwString& host,
                           uint16_t port,
                           const SwQuicPacketHeader& header,
                           const std::vector<SwQuicFrame>& frames,
                           SwString* error = nullptr) {
        if (!isListening()) {
            setError_(error, SwString("QUIC server is not listening"));
            return false;
        }

        SwByteArray payload;
        if (!SwQuicFrameCodec::encodeFrames(frames, payload, error)) {
            return false;
        }

        SwByteArray packet;
        if (!SwQuicPacketCodec::encodeInitialPacket(header, payload, packet, error)) {
            return false;
        }

        return writeDatagram_(packet, host, port, error);
    }

    SwQuicConnection* connection(const SwString& host, uint16_t port) {
        std::map<std::string, Entry_>::iterator it =
            m_connections.find(endpointKey_(host, port));
        if (it == m_connections.end()) {
            return nullptr;
        }
        return &it->second.connection;
    }

    const SwQuicConnection* connection(const SwString& host, uint16_t port) const {
        std::map<std::string, Entry_>::const_iterator it =
            m_connections.find(endpointKey_(host, port));
        if (it == m_connections.end()) {
            return nullptr;
        }
        return &it->second.connection;
    }

    std::size_t connectionCount() const {
        return m_connections.size();
    }

    // ---- mode agnostique de la socket (driver externe) --------------------
    //
    // Permet de piloter les connexions QUIC sans que ce serveur possède la socket :
    // l'appelant reçoit les datagrammes ailleurs (p. ex. une socket unique multiplexée
    // qui démultiplexe d'autres protocoles) et les injecte ici ; les datagrammes sortants
    // sont émis via le send-sink au lieu de m_socket. Sans sink ni listen(), rien n'est
    // câblé : c'est un ajout générique, aucune sémantique applicative.

    using SendSink = std::function<bool(const SwByteArray& packet,
                                        const SwString& host,
                                        uint16_t port,
                                        SwString* error)>;

    // Installe le canal d'émission externe. S'il est défini, writeDatagram_ l'utilise
    // au lieu de la socket propre du serveur.
    void setSendSink(SendSink sink) { m_sendSink = std::move(sink); }
    bool hasSendSink() const { return static_cast<bool>(m_sendSink); }

    // Injecte un datagramme reçu hors de ce serveur (déjà classé « QUIC » par l'appelant).
    // Route par DCID puis pilote la connexion exactement comme le chemin poll()/socket.
    bool injectDatagram(const SwByteArray& datagram,
                        const SwString& sender,
                        uint16_t senderPort,
                        SwString* error = nullptr) {
        return processDatagram_(datagram, sender, senderPort, error);
    }

signals:
    DECLARE_SIGNAL(connectionUpdated, SwQuicConnection*)
    DECLARE_SIGNAL(packetRejected, const SwString&, uint16_t, const SwString&)

private:
    struct Entry_ {
        SwQuicConnection connection;
        SwString host;
        uint16_t port;

        Entry_() : port(0) {}
    };

    static void setError_(SwString* error, const SwString& message) {
        if (error) {
            *error = message;
        }
    }

    static std::string endpointKey_(const SwString& host, uint16_t port) {
        return host.toStdString() + ":" + SwString::number(static_cast<int>(port)).toStdString();
    }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // Destination connection ID of the first packet in a datagram: cleartext
    // in both long and short headers, so routing needs no keys.
    bool extractDestinationConnectionId_(const SwByteArray& datagram,
                                         std::string& outCid) const {
        if (datagram.isEmpty()) {
            return false;
        }
        const std::uint8_t firstByte = static_cast<std::uint8_t>(datagram.constData()[0]);

        if ((firstByte & 0x80U) != 0) { // long header
            if (datagram.size() < 6) {
                return false;
            }
            const std::size_t dcidLength =
                static_cast<std::uint8_t>(datagram.constData()[5]);
            if (dcidLength == 0 || datagram.size() < 6 + dcidLength) {
                return false;
            }
            outCid.assign(datagram.constData() + 6, dcidLength);
            return true;
        }

        // Short header: the length is implicit, we issued the CID ourselves.
        if (m_localConnectionIdLength == 0 ||
            datagram.size() < 1 + m_localConnectionIdLength) {
            return false;
        }
        outCid.assign(datagram.constData() + 1, m_localConnectionIdLength);
        return true;
    }

    bool processDatagram_(const SwByteArray& datagram,
                          const SwString& sender,
                          uint16_t senderPort,
                          SwString* error) {
        // Route by destination connection ID first (survives address change),
        // then by UDP endpoint, then create a new connection.
        std::string connectionKey;
        std::string cid;
        const bool hasCid = extractDestinationConnectionId_(datagram, cid);

        if (hasCid) {
            std::map<std::string, std::string>::const_iterator indexed =
                m_connectionIdIndex.find(cid);
            if (indexed != m_connectionIdIndex.end()) {
                connectionKey = indexed->second;
            }
        }
        if (connectionKey.empty()) {
            connectionKey = endpointKey_(sender, senderPort);
        }

        std::map<std::string, Entry_>::iterator it = m_connections.find(connectionKey);
        const bool isNewConnection = (it == m_connections.end());
        if (isNewConnection) {
            it = m_connections.insert(std::make_pair(connectionKey, Entry_())).first;
            if (hasCid) {
                m_connectionIdIndex[cid] = connectionKey;
            }
        }

        Entry_& entry = it->second;

        // The connection was found by its connection ID but the UDP source
        // changed: this is a peer migration (e.g. Wi-Fi -> cellular). Start
        // path validation before trusting the new path (RFC 9000 9).
        const bool addressChanged =
            !isNewConnection && (entry.host != sender || entry.port != senderPort);

        entry.host = sender;
        entry.port = senderPort;

        if (!entry.connection.receiveDatagram(datagram, nowMs_(), error)) {
            return false;
        }
        if (addressChanged) {
            entry.connection.onPeerAddressChanged(
                nowMs_(), static_cast<std::uint64_t>(datagram.size()), error);
        }

        flushConnection_(entry, error);
        connectionUpdated(&entry.connection);
        return true;
    }

    void fireExpiredTimers_() {
        const std::uint64_t now = nowMs_();
        for (std::map<std::string, Entry_>::iterator it = m_connections.begin();
             it != m_connections.end(); ++it) {
            if (it->second.connection.nextTimeoutMs(now) == 0) {
                it->second.connection.onTimeout(now);
                SwString ignored;
                flushConnection_(it->second, &ignored);
            }
        }
    }

    bool flushConnection_(Entry_& entry, SwString* error) {
        std::vector<SwByteArray> outgoing;
        if (!entry.connection.buildDatagrams(nowMs_(), outgoing, error)) {
            return false;
        }
        for (std::size_t i = 0; i < outgoing.size(); ++i) {
            if (!writeDatagram_(outgoing[i], entry.host, entry.port, error)) {
                return false;
            }
        }
        return true;
    }

    bool writeDatagram_(const SwByteArray& packet,
                        const SwString& host,
                        uint16_t port,
                        SwString* error) {
        // Un send-sink installé (setSendSink) prend la main sur l'émission : le datagramme
        // sortant part par ce canal externe au lieu de notre socket. Utile quand la socket UDP
        // est possédée et multiplexée par l'appelant (plusieurs protocoles sur un même port).
        if (m_sendSink) {
            return m_sendSink(packet, host, port, error);
        }
        const int64_t sent = m_socket.writeDatagram(packet.constData(),
                                                    static_cast<int64_t>(packet.size()),
                                                    host,
                                                    port);
        if (sent != static_cast<int64_t>(packet.size())) {
            setError_(error, m_socket.errorString());
            return false;
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    void emitPacketRejected_(const SwString& sender, uint16_t senderPort, const SwString& reason) {
        packetRejected(sender, senderPort, reason);
    }

    SwUdpSocket m_socket;
    SendSink m_sendSink; // si défini : émission via ce canal externe au lieu de m_socket
    std::map<std::string, Entry_> m_connections;
    std::map<std::string, std::string> m_connectionIdIndex;
    std::size_t m_localConnectionIdLength;
};

#endif
