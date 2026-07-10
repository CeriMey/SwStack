#include "SwPair.h"
#include "SwMap.h"
#ifndef SWQUICSERVER_H
#define SWQUICSERVER_H

#include "SwVector.h"
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
#include <limits>
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
        m_socket.setMaxDatagramSize(2048);
        m_socket.setMaxPendingDatagrams(2048);
        m_socket.setMaxReadBatchDatagrams(256);
        m_socket.setBatchReceive(true);

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
    void setMaxConnections(std::size_t maximum) { m_maxConnections = maximum; }
    std::size_t maxConnections() const { return m_maxConnections; }

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
        for (SwMap<SwString, Entry_>::const_iterator it = m_connections.begin();
             it != m_connections.end(); ++it) {
            std::int64_t t = it->second.connection.nextTimeoutMs(now);
            if (it->second.candidateActive) {
                const std::uint64_t deadline = (std::min)(
                    it->second.candidateRetryDeadlineMs,
                    it->second.candidateExpiryMs);
                const std::int64_t candidate = deadline <= now
                                                   ? 0
                                                   : static_cast<std::int64_t>(deadline - now);
                if (t < 0 || candidate < t) t = candidate;
            }
            if (t >= 0 && (next < 0 || t < next)) {
                next = t;
            }
        }
        return next;
    }

    bool sendInitialPacket(const SwString& host,
                           uint16_t port,
                           const SwQuicPacketHeader& header,
                           const SwVector<SwQuicFrame>& frames,
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
        SwMap<SwString, Entry_>::iterator it =
            m_connections.find(endpointKey_(host, port));
        if (it == m_connections.end()) {
            return nullptr;
        }
        return &it->second.connection;
    }

    const SwQuicConnection* connection(const SwString& host, uint16_t port) const {
        SwMap<SwString, Entry_>::const_iterator it =
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
        bool candidateActive = false;
        SwString candidateHost;
        uint16_t candidatePort = 0;
        SwByteArray candidateChallenge;
        std::uint64_t candidateBytesReceived = 0;
        std::uint64_t candidateBytesSent = 0;
        std::uint64_t candidateRetryDeadlineMs = 0;
        std::uint64_t candidateExpiryMs = 0;
        std::size_t candidateAttempts = 0;

        Entry_() : port(0) {}
    };

    static void setError_(SwString* error, const SwString& message) {
        if (error) {
            *error = message;
        }
    }

    static SwString endpointKey_(const SwString& host, uint16_t port) {
        return host + ":" + SwString::number(static_cast<unsigned int>(port));
    }

    static std::uint64_t nowMs_() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // Destination connection ID of the first packet in a datagram: cleartext
    // in both long and short headers, so routing needs no keys.
    bool extractDestinationConnectionId_(const SwByteArray& datagram,
                                         SwString& outCid) const {
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

    static bool isPlausibleInitial_(const SwByteArray& datagram) {
        if (datagram.size() < 7) {
            return false;
        }
        const std::uint8_t first = static_cast<std::uint8_t>(datagram.constData()[0]);
        if ((first & 0xc0U) != 0xc0U || (first & 0x30U) != 0) {
            return false;
        }
        if (static_cast<std::uint8_t>(datagram.constData()[1]) != 0 ||
            static_cast<std::uint8_t>(datagram.constData()[2]) != 0 ||
            static_cast<std::uint8_t>(datagram.constData()[3]) != 0 ||
            static_cast<std::uint8_t>(datagram.constData()[4]) != 1) {
            return false;
        }
        const std::size_t dcidLength =
            static_cast<std::uint8_t>(datagram.constData()[5]);
        if (dcidLength == 0 || dcidLength > SwQuicConnectionId::kMaxLength ||
            static_cast<std::size_t>(datagram.size()) < 7 + dcidLength) {
            return false;
        }
        const std::size_t scidOffset = 6 + dcidLength;
        const std::size_t scidLength =
            static_cast<std::uint8_t>(datagram.constData()[scidOffset]);
        return scidLength <= SwQuicConnectionId::kMaxLength &&
               static_cast<std::size_t>(datagram.size()) >= 7 + dcidLength + scidLength;
    }

    bool processDatagram_(const SwByteArray& datagram,
                          const SwString& sender,
                          uint16_t senderPort,
                          SwString* error) {
        // Route by destination connection ID first (survives address change),
        // then by UDP endpoint, then create a new connection.
        SwString connectionKey;
        SwString cid;
        const bool hasCid = extractDestinationConnectionId_(datagram, cid);

        if (hasCid) {
            SwMap<SwString, SwString>::const_iterator indexed =
                m_connectionIdIndex.find(cid);
            if (indexed != m_connectionIdIndex.end()) {
                connectionKey = indexed->second;
            }
        }
        if (connectionKey.empty()) {
            connectionKey = endpointKey_(sender, senderPort);
        }

        SwMap<SwString, Entry_>::iterator it = m_connections.find(connectionKey);
        const bool isNewConnection = (it == m_connections.end());
        if (isNewConnection) {
            if (!isPlausibleInitial_(datagram)) {
                setError_(error, SwString("Unroutable QUIC packet is not an Initial"));
                return false;
            }
            if (m_maxConnections > 0 && m_connections.size() >= m_maxConnections) {
                setError_(error, SwString("QUIC connection limit reached"));
                return false;
            }
            it = m_connections.insert(SwMakePair(connectionKey, Entry_())).first;
            if (hasCid) {
                m_connectionIdIndex[cid] = connectionKey;
            }
        }

        Entry_& entry = it->second;

        const bool addressChanged =
            !isNewConnection && (entry.host != sender || entry.port != senderPort);

        if (isNewConnection) {
            entry.host = sender;
            entry.port = senderPort;
        }

        const std::uint64_t now = nowMs_();
        SwQuicConnection::PathControlEvents pathEvents;
        bool authenticated = false;
        std::uint64_t authenticatedBytes = 0;
        if (!entry.connection.receiveDatagramWithPathEvents(
                datagram, now, pathEvents, error, &authenticated, &authenticatedBytes)) {
            if (isNewConnection) {
                m_connections.erase(it);
                if (hasCid) {
                    SwMap<SwString, SwString>::iterator cidIt =
                        m_connectionIdIndex.find(cid);
                    if (cidIt != m_connectionIdIndex.end() &&
                        cidIt->second == connectionKey) {
                        m_connectionIdIndex.erase(cidIt);
                    }
                }
            }
            return false;
        }
        if (addressChanged && authenticated) {
            if (!processCandidatePath_(entry, pathEvents, authenticatedBytes,
                                       sender, senderPort, now, error)) {
                return false;
            }
        } else if (authenticated) {
            for (std::size_t i = 0; i < pathEvents.challenges.size(); ++i) {
                if (!sendPathControl_(entry, true, pathEvents.challenges[i],
                                      entry.host, entry.port,
                                      (std::numeric_limits<std::uint64_t>::max)(),
                                      now, nullptr, error)) {
                    return false;
                }
            }
        }

        if (!flushConnection_(entry, error)) {
            return false;
        }
        connectionUpdated(&entry.connection);
        return true;
    }

    static void addSaturated_(std::uint64_t& target, std::uint64_t value) {
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        target = value > maximum - target ? maximum : target + value;
    }

    static std::uint64_t candidateBudget_(const Entry_& entry) {
        const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
        const std::uint64_t permitted = entry.candidateBytesReceived > maximum / 3
                                            ? maximum
                                            : entry.candidateBytesReceived * 3;
        return entry.candidateBytesSent >= permitted
                   ? 0
                   : permitted - entry.candidateBytesSent;
    }

    void clearCandidate_(Entry_& entry, bool cancelValidation) {
        if (cancelValidation) entry.connection.cancelPathValidation();
        entry.candidateActive = false;
        entry.candidateHost.clear();
        entry.candidatePort = 0;
        entry.candidateChallenge.clear();
        entry.candidateBytesReceived = 0;
        entry.candidateBytesSent = 0;
        entry.candidateRetryDeadlineMs = 0;
        entry.candidateExpiryMs = 0;
        entry.candidateAttempts = 0;
    }

    bool sendPathControl_(Entry_& entry,
                          bool response,
                          const SwByteArray& data,
                          const SwString& host,
                          std::uint16_t port,
                          std::uint64_t maxWireBytes,
                          std::uint64_t now,
                          std::size_t* sentBytes,
                          SwString* error) {
        SwByteArray packet;
        if (!entry.connection.buildPathControlDatagram(response, data, now,
                                                        maxWireBytes, packet, error)) {
            return false;
        }
        if (sentBytes) *sentBytes = 0;
        if (packet.isEmpty()) return true;
        if (!writeDatagram_(packet, host, port, error)) return false;
        if (sentBytes) *sentBytes = static_cast<std::size_t>(packet.size());
        return true;
    }

    bool sendCandidateChallenge_(Entry_& entry,
                                 std::uint64_t now,
                                 SwString* error) {
        if (!entry.candidateActive || now < entry.candidateRetryDeadlineMs) return true;
        if (entry.candidateAttempts >= 3 || now >= entry.candidateExpiryMs) {
            clearCandidate_(entry, true);
            return true;
        }
        std::size_t sent = 0;
        if (!sendPathControl_(entry, false, entry.candidateChallenge,
                              entry.candidateHost, entry.candidatePort,
                              candidateBudget_(entry), now, &sent, error)) {
            return false;
        }
        if (sent > 0) ++entry.candidateAttempts;
        addSaturated_(entry.candidateBytesSent, static_cast<std::uint64_t>(sent));
        entry.candidateRetryDeadlineMs = now + (250ULL << entry.candidateAttempts);
        return true;
    }

    bool processCandidatePath_(Entry_& entry,
                               const SwQuicConnection::PathControlEvents& events,
                               std::uint64_t authenticatedBytes,
                               const SwString& sender,
                               std::uint16_t senderPort,
                               std::uint64_t now,
                               SwString* error) {
        if (!entry.candidateActive || entry.candidateHost != sender ||
            entry.candidatePort != senderPort) {
            clearCandidate_(entry, true);
            entry.candidateActive = true;
            entry.candidateHost = sender;
            entry.candidatePort = senderPort;
            entry.candidateBytesReceived = authenticatedBytes;
            entry.candidateRetryDeadlineMs = now;
            entry.candidateExpiryMs = now + 3000;
            if (!entry.connection.beginPathValidation(
                    now, entry.candidateChallenge, error)) {
                clearCandidate_(entry, false);
                return false;
            }
        } else {
            addSaturated_(entry.candidateBytesReceived, authenticatedBytes);
        }

        if (!sendCandidateChallenge_(entry, now, error)) return false;
        for (std::size_t i = 0; i < events.challenges.size(); ++i) {
            std::size_t sent = 0;
            if (!sendPathControl_(entry, true, events.challenges[i], sender, senderPort,
                                  candidateBudget_(entry), now, &sent, error)) {
                return false;
            }
            addSaturated_(entry.candidateBytesSent, static_cast<std::uint64_t>(sent));
        }
        for (std::size_t i = 0; i < events.responses.size(); ++i) {
            if (!entry.connection.matchesPathResponse(events.responses[i])) continue;
            entry.connection.commitPathMigration();
            entry.host = sender;
            entry.port = senderPort;
            clearCandidate_(entry, false);
            break;
        }
        return true;
    }

    void fireExpiredTimers_() {
        const std::uint64_t now = nowMs_();
        for (SwMap<SwString, Entry_>::iterator it = m_connections.begin();
             it != m_connections.end();) {
            if (it->second.candidateActive) {
                SwString ignoredCandidateError;
                if (now >= it->second.candidateExpiryMs) {
                    clearCandidate_(it->second, true);
                } else if (now >= it->second.candidateRetryDeadlineMs) {
                    (void)sendCandidateChallenge_(it->second, now,
                                                  &ignoredCandidateError);
                }
            }
            if (it->second.connection.nextTimeoutMs(now) == 0) {
                it->second.connection.onTimeout(now);
                SwString ignored;
                flushConnection_(it->second, &ignored);
            }
            if (it->second.connection.state() == SwQuicConnection::State::Closed) {
                const SwString doomedKey = it->first;
                SwMap<SwString, Entry_>::iterator doomed = it;
                ++it;
                m_connections.erase(doomed);
                for (SwMap<SwString, SwString>::iterator cidIt =
                         m_connectionIdIndex.begin();
                     cidIt != m_connectionIdIndex.end();) {
                    if (cidIt->second == doomedKey) {
                        SwMap<SwString, SwString>::iterator doomedCid = cidIt;
                        ++cidIt;
                        m_connectionIdIndex.erase(doomedCid);
                    } else {
                        ++cidIt;
                    }
                }
            } else {
                ++it;
            }
        }
    }

    bool flushConnection_(Entry_& entry, SwString* error) {
        SwVector<SwByteArray> outgoing;
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
        const int64_t sent = m_socket.writeDatagramCached(packet.constData(),
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
    SwMap<SwString, Entry_> m_connections;
    SwMap<SwString, SwString> m_connectionIdIndex;
    std::size_t m_localConnectionIdLength;
    std::size_t m_maxConnections = 4096;
};

#endif
