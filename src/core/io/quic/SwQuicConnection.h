#include "SwPair.h"
#include "SwMap.h"
#ifndef SWQUICCONNECTION_H
#define SWQUICCONNECTION_H

#include "SwVector.h"
#include "SwDequeue.h"
#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicAckTracker.h"
#include "quic/SwQuicCongestionControl.h"
#include "quic/SwQuicFlowControl.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicFrameCodec.h"
#include "quic/SwQuicLossRecovery.h"
#include "quic/SwQuicLimits.h"
#include "quic/SwQuicPacketCodec.h"
#include "quic/SwQuicPacketHeader.h"
#include "quic/SwQuicPacketProtector.h"
#include "quic/SwQuicRandom.h"
#include "quic/SwQuicStream.h"
#include "quic/SwQuicStreamMap.h"
#include "quic/SwQuicTransportParameters.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <vector>

// Sans-IO QUIC connection endpoint.
//
// The connection never touches a socket and never reads a clock: the driver
// (SwQuicServer, a client handshake loop, or a test) feeds it received
// datagrams with receiveDatagram(bytes, nowMs), drains outgoing datagrams
// with buildDatagrams(nowMs, out), and schedules a call to onTimeout(nowMs)
// whenever nextTimeoutMs(nowMs) elapses. All RFC 9000/9002 machinery -- packet
// number spaces, ACK generation, loss recovery, congestion control, flow
// control, PTO/idle timers -- lives behind those three calls.
//
// Packet protection is per encryption level: install keys with setLevelKeys()
// as the TLS handshake derives them. Before Initial keys are installed the
// legacy plaintext-Initial mode used by the loopback self-tests keeps working
// through receiveInitialPacket()/buildAckFrame().
class SwQuicConnection {
public:
    enum class State {
        Open,
        Closing,   // we sent (or are about to send) CONNECTION_CLOSE
        Draining,  // the peer sent CONNECTION_CLOSE; we stay silent
        Closed
    };

    enum class Role {
        Client,
        Server
    };

    enum class Level {
        Initial = 0,
        Handshake = 1,
        Application = 2
    };

    static std::size_t kLevelCount() { return 3; }
    static std::size_t kMaxUdpPayload() { return SwQuicLimits::maximumUdpPayloadBytes(); }
    static std::uint8_t kPacketNumberLength() { return 4; }
    static std::uint64_t kLocalAckDelayExponent() { return 3; }
    static std::uint64_t kLocalMaxAckDelayMs() { return 25; }
    static std::size_t kMaxPathControlEvents() { return 4; }
    static std::size_t kMaxInspectedPathPacketNumbers() { return 256; }
    static std::size_t kMaxNewTokens_() { return 64; }

    // Événements de validation extraits d'un paquet déjà authentifié. Le
    // driver de socket les émet sur le tuple où ils ont été reçus, au lieu de
    // les laisser rejoindre la file de perte globale du chemin actif.
    struct PathControlEvents {
        SwVector<SwByteArray> challenges;
        SwVector<SwByteArray> responses;

        void clear() {
            challenges.clear();
            responses.clear();
        }
    };

    explicit SwQuicConnection(Role role = Role::Server)
        : m_role(role),
          m_state(State::Open),
          m_version(1),
          m_plaintextInitialTx(false),
          m_handshakeConfirmed(false),
          m_hasPeerParams(false),
          m_peerAckDelayExponent(3),
          m_peerMaxAckDelayMs(25),
          m_effectiveIdleTimeoutMs(0),
          m_lastNetworkActivityMs(0),
          m_hasNetworkActivity(false),
          m_drainDeadlineMs(0),
          m_hasDrainDeadline(false),
          m_ptoCount(0),
          m_closeQueued(false),
          m_closeSent(false),
          m_closeErrorCode(0),
          m_closeIsApplication(false),
          m_peerMaxStreamsBidi(0),
          m_peerMaxStreamsUni(0),
          m_receivedPingCount(0),
          m_sawVersionNegotiation(false),
          m_sawRetry(false),
          m_localParams(),
          m_peerParams() {
        m_localParams.initialMaxData = 262144;
        m_localParams.initialMaxStreamDataBidiLocal = 262144;
        m_localParams.initialMaxStreamDataBidiRemote = 262144;
        m_localParams.initialMaxStreamDataUni = 262144;
        m_localParams.initialMaxStreamsBidi = 100;
        m_localParams.initialMaxStreamsUni = 100;
        m_localParams.maxUdpPayloadSize = SwQuicLimits::maximumUdpPayloadBytes();
        // DATAGRAM is negotiated explicitly and must still fit one QUIC packet.
        m_localParams.maxDatagramFrameSize = SwQuicLimits::maximumDatagramFrameBytes();
        m_localParams.resetStreamAt = true;
    }

    // ---- identity / configuration ---------------------------------------

    Role role() const { return m_role; }
    void setRole(Role role) { m_role = role; }

    std::uint32_t version() const { return m_version; }
    void setVersion(std::uint32_t version) { m_version = version; }

    // Connection ID the peer uses to address us: its length is needed to
    // parse incoming short-header packets, and it is our SCID in long headers.
    void setLocalConnectionId(const SwQuicConnectionId& connectionId) {
        m_localConnectionId = connectionId;
    }
    const SwQuicConnectionId& localConnectionId() const { return m_localConnectionId; }

    // Connection ID we put in the destination field of packets we send.
    void setPeerConnectionId(const SwQuicConnectionId& connectionId) {
        m_peerConnectionId = connectionId;
        m_currentPeerConnectionIdSequence = 0;
        if (!connectionId.isEmpty()) {
            m_peerIssuedConnectionIds[0] = connectionId.bytes();
        }
    }
    const SwQuicConnectionId& peerConnectionId() const { return m_peerConnectionId; }

    void setInitialToken(const SwByteArray& token) { m_initialToken = token; }

    SwQuicTransportParameters& localTransportParameters() { return m_localParams; }
    const SwQuicTransportParameters& localTransportParameters() const { return m_localParams; }

    // Install the exact parameters advertised by the handshake before any
    // application packet is processed. Merely mutating localTransportParameters()
    // cannot resize the already-constructed flow controllers.
    void applyLocalTransportParameters(const SwQuicTransportParameters& parameters) {
        m_localParams = parameters;
        m_localMaxStreamsBidi = parameters.initialMaxStreamsBidi;
        m_localMaxStreamsUni = parameters.initialMaxStreamsUni;
        m_retiredPeerBidiRanges.clear();
        m_connectionFlow = SwQuicConnectionFlowControl(parameters.initialMaxData);
        m_streamFlow.clear();
    }

    // Apply the transport parameters the peer sent in its TLS handshake.
    void applyPeerTransportParameters(const SwQuicTransportParameters& parameters) {
        m_peerParams = parameters;
        m_hasPeerParams = true;
        m_peerAckDelayExponent = parameters.ackDelayExponent;
        m_peerMaxAckDelayMs = parameters.maxAckDelayMs;
        m_connectionFlow.setPeerMaxData(parameters.initialMaxData);
        m_peerMaxStreamsBidi = parameters.initialMaxStreamsBidi;
        m_peerMaxStreamsUni = parameters.initialMaxStreamsUni;
        m_effectiveIdleTimeoutMs = combineIdleTimeouts_(m_localParams.maxIdleTimeoutMs,
                                                        parameters.maxIdleTimeoutMs);
    }
    bool hasPeerTransportParameters() const { return m_hasPeerParams; }
    const SwQuicTransportParameters& peerTransportParameters() const { return m_peerParams; }

    // ---- packet protection keys ------------------------------------------

    void setLevelKeys(Level level,
                      const SwQuicInitialKeys& rxKeys,
                      const SwQuicInitialKeys& txKeys) {
        Space_& space = space_(level);
        space.rxKeys = rxKeys;
        space.txKeys = txKeys;
        space.hasRxKeys = true;
        space.hasTxKeys = true;
    }

    bool hasLevelKeys(Level level) const { return space_(level).hasRxKeys; }

    // Discard an encryption level (RFC 9001 section 4.9): its keys and its
    // packet-number space state are dropped for good.
    void dropLevelKeys(Level level) {
        Space_& space = space_(level);
        space.hasRxKeys = false;
        space.hasTxKeys = false;
        space.rxKeys = SwQuicInitialKeys();
        space.txKeys = SwQuicInitialKeys();
        space.pendingFrames.clear();
        space.sentPackets.clear();
        space.ackTracker.clear();
        space.hasAckDeadline = false;
    }

    // Loopback/self-test mode: the Initial space sends unprotected packets
    // through SwQuicPacketCodec instead of requiring Initial keys.
    void setPlaintextInitialMode(bool enabled) { m_plaintextInitialTx = enabled; }

    // Seed the next transmit packet number for a level. Used when a separate
    // handshake driver already spent some 1-RTT packet numbers (e.g. sending
    // HANDSHAKE_DONE) before handing the app space to this connection, so that
    // no AEAD nonce is reused (RFC 9001 5.3).
    void setNextTxPacketNumber(Level level, std::uint64_t packetNumber) {
        space_(level).nextTxPn = packetNumber;
    }

    bool handshakeConfirmed() const { return m_handshakeConfirmed; }
    void setHandshakeConfirmed(bool confirmed) {
        m_handshakeConfirmed = confirmed;
        for (std::size_t i = 0; i < kLevelCount(); ++i) {
            m_spaces[i].loss.setHandshakeConfirmed(confirmed);
        }
    }

    // ---- receive path -----------------------------------------------------

    // Feed one received UDP datagram (possibly carrying coalesced packets).
    bool receiveDatagram(const SwByteArray& datagram,
                         std::uint64_t nowMs,
                         SwString* error = nullptr,
                         bool* authenticatedOut = nullptr) {
        return receiveDatagramImpl_(datagram, nowMs, error, authenticatedOut, nullptr);
    }

    // Variante utilisée par un driver qui connaît le tuple candidat. Les
    // PATH_CHALLENGE/PATH_RESPONSE sont remontés séparément et ne sont jamais
    // mis dans pendingFrames, donc un PTO du chemin actif ne peut pas les
    // retransmettre vers le mauvais tuple.
    bool receiveDatagramWithPathEvents(const SwByteArray& datagram,
                                       std::uint64_t nowMs,
                                       PathControlEvents& pathEvents,
                                       SwString* error = nullptr,
                                       bool* authenticatedOut = nullptr,
                                       std::uint64_t* authenticatedBytesOut = nullptr) {
        pathEvents.clear();
        if (authenticatedBytesOut) {
            *authenticatedBytesOut = 0;
        }
        PathControlEvents* const previous = m_pathControlEventsOut;
        m_pathControlEventsOut = &pathEvents;
        bool authenticated = false;
        bool* const authenticatedResult = authenticatedOut ? authenticatedOut : &authenticated;
        const bool received = receiveDatagramImpl_(datagram, nowMs, error,
                                                   authenticatedResult,
                                                   authenticatedBytesOut);
        m_pathControlEventsOut = previous;
        if (!received || !*authenticatedResult) {
            pathEvents.clear();
        }
        return received;
    }

    // Authenticates and decodes one established 1-RTT packet without
    // committing packet numbers, ACK/loss state, streams, flow control or
    // idle activity. A path driver uses this before admitting a new remote
    // tuple. Non-path frames are reported but deliberately left unacknowledged
    // so QUIC retransmits them after address validation succeeds.
    bool inspectDatagramPathEvents(const SwByteArray& datagram,
                                   std::uint64_t nowMs,
                                   PathControlEvents& pathEvents,
                                   bool& hadNonPathFrames,
                                   SwString* error = nullptr,
                                   bool* authenticatedOut = nullptr,
                                   std::uint64_t* authenticatedBytesOut = nullptr) {
        pathEvents.clear();
        hadNonPathFrames = false;
        if (authenticatedOut) *authenticatedOut = false;
        if (authenticatedBytesOut) *authenticatedBytesOut = 0;
        if (datagram.isEmpty() ||
            (static_cast<std::uint8_t>(datagram.constData()[0]) & 0x80U) != 0) {
            setError_(error, "Path inspection requires a 1-RTT short header");
            return false;
        }

        PathControlEvents* const previousEvents = m_pathControlEventsOut;
        const bool previousInspect = m_inspectPathControlOnly;
        const bool previousNonPath = m_inspectedNonPathFrames;
        const bool previousReplay = m_inspectedPathPacketReplay;
        m_pathControlEventsOut = &pathEvents;
        m_inspectPathControlOnly = true;
        m_inspectedNonPathFrames = false;
        m_inspectedPathPacketReplay = false;
        bool authenticated = false;
        bool* const authenticatedResult = authenticatedOut
            ? authenticatedOut : &authenticated;
        const bool received = receiveDatagramImpl_(
            datagram, nowMs, error, authenticatedResult,
            authenticatedBytesOut);
        hadNonPathFrames = m_inspectedNonPathFrames;
        m_pathControlEventsOut = previousEvents;
        m_inspectPathControlOnly = previousInspect;
        m_inspectedNonPathFrames = previousNonPath;
        m_inspectedPathPacketReplay = previousReplay;
        if (!received || !*authenticatedResult) {
            pathEvents.clear();
            hadNonPathFrames = false;
        }
        return received;
    }

    bool receiveDatagramImpl_(const SwByteArray& datagram,
                              std::uint64_t nowMs,
                              SwString* error,
                              bool* authenticatedOut,
                              std::uint64_t* authenticatedBytesOut) {
        if (!m_inspectPathControlOnly) m_touchedStreams.clear();
        if (authenticatedOut) {
            *authenticatedOut = false;
        }
        if (authenticatedBytesOut) {
            *authenticatedBytesOut = 0;
        }
        if (m_state == State::Closed) {
            setError_(error, "Cannot receive QUIC packet on a closed connection");
            return false;
        }

        // Le header court 1-RTT occupe toujours tout le datagramme UDP dans
        // notre dataplane. Évite une copie profonde de 1,2 Ko par paquet.
        if (!datagram.isEmpty() &&
            (static_cast<std::uint8_t>(datagram.constData()[0]) & 0x80U) == 0) {
            std::size_t consumed = 0;
            bool authenticated = false;
            if (!receivePacket_(datagram, nowMs, consumed, authenticated, error)) {
                return false;
            }
            // Seuls des octets authentifiés créditent le budget 3x du chemin
            // candidat. Créditer avant l'AEAD permettrait à un paquet forgé
            // portant un CID observable d'augmenter la surface de réflexion.
            // PATH_RESPONSE peut valider le chemin pendant receivePacket_() :
            // dans ce cas le budget vient d'être levé et n'a plus à croître.
            if (authenticated && !m_pathValidated &&
                !m_inspectPathControlOnly) {
                m_bytesReceivedThisPath += static_cast<std::uint64_t>(datagram.size());
            }
            if (authenticatedOut) {
                *authenticatedOut = authenticated;
            }
            if (authenticatedBytesOut && authenticated) {
                *authenticatedBytesOut =
                    m_inspectPathControlOnly && m_inspectedPathPacketReplay
                        ? 0
                        : static_cast<std::uint64_t>(datagram.size());
            }
            // Un paquet au CID visible mais non authentifié ne doit pas maintenir
            // artificiellement la connexion en vie (idle-timeout DoS).
            if (authenticated && !m_inspectPathControlOnly) {
                m_lastNetworkActivityMs = nowMs;
                m_hasNetworkActivity = true;
            }
            clearError_(error);
            return true;
        }

        std::size_t offset = 0;
        std::uint64_t authenticatedBytes = 0;
        while (offset < datagram.size()) {
            // The first packet is almost always the only long-header packet.
            // Feed the caller-owned datagram directly in that common case and
            // allocate a suffix only for an actually coalesced second packet.
            SwByteArray remainderStorage;
            const SwByteArray* remainder = &datagram;
            if (offset > 0) {
                remainderStorage = datagram.mid(
                    static_cast<int>(offset),
                    static_cast<int>(datagram.size() - offset));
                remainder = &remainderStorage;
            }
            std::size_t consumed = 0;
            bool authenticated = false;
            if (!receivePacket_(*remainder, nowMs, consumed, authenticated, error)) {
                return false;
            }
            if (authenticated) {
                authenticatedBytes += static_cast<std::uint64_t>(consumed);
            }
            if (consumed == 0) {
                break; // rest of the datagram was skipped (padding, unknown keys)
            }
            offset += consumed;
        }

        // Même règle pour un datagramme coalescé : seuls les octets des
        // paquets effectivement ouverts par l'AEAD sont crédités. Un paquet
        // long inconnu/sans clé ou un suffixe sauté ne peut donc augmenter le
        // budget d'un paquet authentifié qui le précède.
        if (authenticatedBytes > 0 && !m_pathValidated &&
            !m_inspectPathControlOnly) {
            m_bytesReceivedThisPath += authenticatedBytes;
        }
        if (authenticatedOut) {
            *authenticatedOut = authenticatedBytes > 0;
        }
        if (authenticatedBytesOut) {
            *authenticatedBytesOut = authenticatedBytes;
        }
        if (authenticatedBytes > 0 && !m_inspectPathControlOnly) {
            m_lastNetworkActivityMs = nowMs;
            m_hasNetworkActivity = true;
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Legacy plaintext entry point kept for the loopback self-tests: decodes
    // one unprotected Initial packet and runs it through the same frame
    // machinery as the protected path.
    bool receiveInitialPacket(const SwByteArray& packet, SwString* error = nullptr) {
        if (m_state == State::Closed) {
            setError_(error, "Cannot receive QUIC packet on a closed connection");
            return false;
        }

        SwQuicPacketHeader header;
        SwByteArray payload;
        if (!SwQuicPacketCodec::decodeInitialPacket(packet, header, payload, error)) {
            return false;
        }

        m_lastInitialHeader = header;
        return processDecodedPacket_(Level::Initial, header.packetNumber(), payload, 0, error);
    }

    // ---- application send API ----------------------------------------------

    // Bornes du driver I/O : 0 conserve le comportement historique illimité. Le quota par build
    // empêche une ACK compression / grande cwnd de vider plusieurs Mo vers sendto en une rafale.
    void setMaxDatagramsPerBuild(std::size_t maximum) { m_maxDatagramsPerBuild = maximum; }
    void setDatagramPacing(std::size_t datagramsPerMillisecond,
                           std::size_t maximumBurst) {
        m_pacingRatePerMillisecond = datagramsPerMillisecond;
        m_pacingMaximumBurst = maximumBurst < datagramsPerMillisecond
                                   ? datagramsPerMillisecond
                                   : maximumBurst;
        m_pacingLastRefillMs = 0;
        m_pacingTokens = m_pacingMaximumBurst;
    }
    void setMaxPendingDatagramFrames(std::size_t maximum) {
        m_maxPendingDatagramFrames = maximum;
    }
    // Upper bound imposed by the currently selected underlay/virtual carrier.
    // QUIC's peer max_udp_payload_size is also honoured; this local bound lets
    // an embedding transport reserve its own encapsulation budget without
    // changing the globally advertised transport parameter.
    bool setMaximumUdpPayloadSize(std::size_t maximum) {
        if (maximum < SwQuicLimits::minimumInitialUdpPayloadBytes() ||
            maximum > SwQuicLimits::maximumUdpPayloadBytes()) {
            return false;
        }
        std::size_t effective = maximum;
        if (m_hasPeerParams &&
            m_peerParams.maxUdpPayloadSize >=
                SwQuicLimits::minimumInitialUdpPayloadBytes() &&
            m_peerParams.maxUdpPayloadSize < effective) {
            effective = static_cast<std::size_t>(m_peerParams.maxUdpPayloadSize);
        }
        const std::size_t applicationBudget =
            packetPayloadBudgetForWire_(Level::Application, effective);
        const Space_& application = space_(Level::Application);
        for (SwDequeue<SwQuicFrame>::const_iterator it =
                 application.pendingFrames.begin();
             it != application.pendingFrames.end(); ++it) {
            if (frameEncodedSize_(*it) > applicationBudget) {
                return false;
            }
        }
        m_maximumUdpPayloadSize = maximum;
        return true;
    }
    std::size_t maximumUdpPayloadSize() const {
        return effectiveMaximumUdpPayloadSize_();
    }
    void setMaxReceivedDatagrams(std::size_t maximumCount,
                                 std::size_t maximumBytes) {
        m_maxReceivedDatagrams = maximumCount;
        m_maxReceivedDatagramBytes = maximumBytes;
        while (!m_datagrams.empty() &&
               ((m_maxReceivedDatagrams > 0 &&
                 m_datagrams.size() > m_maxReceivedDatagrams) ||
                (m_maxReceivedDatagramBytes > 0 &&
                 m_receivedDatagramBytes > m_maxReceivedDatagramBytes))) {
            const std::size_t size =
                static_cast<std::size_t>(m_datagrams.front().size());
            m_receivedDatagramBytes = size <= m_receivedDatagramBytes
                ? m_receivedDatagramBytes - size
                : 0;
            m_datagrams.pop_front();
            ++m_droppedReceivedDatagrams;
        }
    }
    void setMaxStreamReassemblyBytes(std::size_t maximum) {
        m_maxStreamReassemblyBytes = maximum == 0 ? 1 : maximum;
    }
    void setMaxStreamReassemblyFragments(std::size_t maximum) {
        m_maxStreamReassemblyFragments = maximum == 0 ? 1 : maximum;
    }
    void setMaxBufferedStreamSendBytes(std::size_t maximum) {
        m_maxBufferedStreamSendBytes = maximum;
    }

    // Actual retained STREAM payload across the application send buffers,
    // frames waiting for packetization, and retransmittable sent packets.
    // Counting duplicate retransmissions is intentional: this is a memory
    // pressure measurement, not a logical byte offset.
    std::size_t queuedStreamSendBytes() const {
        std::size_t total = m_bufferedStreamSendBytes;
        const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        const Space_& application = space_(Level::Application);
        for (SwDequeue<SwQuicFrame>::const_iterator it =
                 application.pendingFrames.begin();
             it != application.pendingFrames.end(); ++it) {
            if (it->type() != SwQuicFrame::Type::Stream) continue;
            const std::size_t bytes = static_cast<std::size_t>(it->data().size());
            total = bytes > maximum - total ? maximum : total + bytes;
        }
        for (SwMap<std::uint64_t, SentRecord_>::const_iterator packet =
                 application.sentPackets.begin();
             packet != application.sentPackets.end(); ++packet) {
            for (std::size_t i = 0;
                 i < packet->second.retransmittable.size(); ++i) {
                const SwQuicFrame& frame = packet->second.retransmittable[i];
                if (frame.type() != SwQuicFrame::Type::Stream) continue;
                const std::size_t bytes =
                    static_cast<std::size_t>(frame.data().size());
                total = bytes > maximum - total ? maximum : total + bytes;
            }
        }
        return total;
    }

    std::size_t maxBufferedStreamSendBytes() const {
        return m_maxBufferedStreamSendBytes;
    }

    std::size_t availableStreamSendBytes() const {
        if (m_maxBufferedStreamSendBytes == 0) {
            return (std::numeric_limits<std::size_t>::max)();
        }
        const std::size_t queued = queuedStreamSendBytes();
        return queued >= m_maxBufferedStreamSendBytes
                   ? 0
                   : m_maxBufferedStreamSendBytes - queued;
    }

    // Payload retained in this stream's application send buffer, before it is
    // transformed into STREAM frames. A non-zero value after packet building
    // therefore identifies data that flow control (or another staging limit)
    // has not admitted yet. Unknown and already-drained streams return zero.
    std::size_t unstagedStreamSendBytes(std::uint64_t streamId) const {
        SwMap<std::uint64_t, SendStream_>::const_iterator it =
            m_sendStreams.find(streamId);
        if (it == m_sendStreams.end()) return 0;
        const std::size_t stored =
            static_cast<std::size_t>(it->second.buffer.size());
        return it->second.bufferOffset < stored
                   ? stored - it->second.bufferOffset
                   : 0;
    }

    // Buffer stream data for transmission; it goes out on the next
    // buildDatagrams() within flow-control and congestion limits.
    bool sendStreamData(std::uint64_t streamId,
                        const SwByteArray& data,
                        bool fin,
                        SwString* error = nullptr) {
        if (m_state != State::Open) {
            setError_(error, "QUIC connection is not open for sending");
            return false;
        }

        if (!validateSendStreamState_(streamId, error)) {
            return false;
        }

        const std::size_t dataSize = static_cast<std::size_t>(data.size());
        const std::size_t queuedBytes = queuedStreamSendBytes();
        if (m_maxBufferedStreamSendBytes > 0 &&
            (queuedBytes > m_maxBufferedStreamSendBytes ||
             dataSize > m_maxBufferedStreamSendBytes - queuedBytes)) {
            setError_(error, "QUIC stream send buffer is full");
            return false;
        }

        SendStream_& stream = m_sendStreams[streamId];
        if (stream.finQueued || stream.resetQueued) {
            setError_(error, "QUIC stream already finished");
            return false;
        }
        // Compact only geometrically. The previous implementation sliced the
        // complete remainder for every 1000-byte chunk, making a large write O(n²).
        if (stream.bufferOffset > 0 &&
            stream.bufferOffset >= static_cast<std::size_t>(stream.buffer.size()) / 2) {
            stream.buffer = stream.buffer.mid(
                static_cast<int>(stream.bufferOffset),
                static_cast<int>(static_cast<std::size_t>(stream.buffer.size()) -
                                 stream.bufferOffset));
            stream.bufferOffset = 0;
        }
        stream.buffer.append(data);
        m_bufferedStreamSendBytes += dataSize;
        if (fin) {
            stream.finQueued = true;
        }
        if (error) {
            *error = SwString();
        }
        return true;
    }

    // Abort the local send side with RESET_STREAM. Bytes that have not yet
    // reached a packet are discarded; already-sent STREAM frames stop being
    // retransmitted. The reset itself remains retransmittable until ACKed.
    bool resetStream(std::uint64_t streamId,
                     std::uint64_t applicationErrorCode,
                     SwString* error = nullptr) {
        if (m_state != State::Open) {
            setError_(error, "QUIC connection is not open for sending");
            return false;
        }
        if (!validateSendStreamState_(streamId, error)) {
            return false;
        }

        SendStream_& stream = m_sendStreams[streamId];
        if (stream.resetQueued) {
            clearError_(error);
            return true;
        }

        const std::size_t stored = static_cast<std::size_t>(stream.buffer.size());
        const std::size_t remaining = stream.bufferOffset < stored
            ? stored - stream.bufferOffset
            : 0;
        m_bufferedStreamSendBytes = remaining <= m_bufferedStreamSendBytes
            ? m_bufferedStreamSendBytes - remaining
            : 0;
        stream.buffer = SwByteArray();
        stream.bufferOffset = 0;
        stream.resetQueued = true;
        stream.resetAcked = false;
        removePendingSendFramesForStream_(streamId, false);
        space_(Level::Application).pendingFrames.push_back(
            SwQuicFrame::resetStream(streamId, applicationErrorCode,
                                     stream.nextOffset));
        clearError_(error);
        return true;
    }

    // Abort the send side while retaining retransmission of the prefix ending
    // at reliableSize (draft-ietf-quic-reliable-stream-reset). The prefix must
    // already have been handed to QUIC before this operation is requested.
    bool resetStreamAt(std::uint64_t streamId,
                       std::uint64_t applicationErrorCode,
                       std::uint64_t reliableSize,
                       SwString* error = nullptr) {
        if (m_state != State::Open) {
            setError_(error, "QUIC connection is not open for sending");
            return false;
        }
        if (!m_hasPeerParams || !m_peerParams.resetStreamAt) {
            setError_(error, "Peer did not negotiate reset_stream_at");
            return false;
        }
        if (!validateSendStreamState_(streamId, error)) return false;

        SendStream_& stream = m_sendStreams[streamId];
        if (stream.resetQueued) {
            if (stream.resetAt && stream.resetReliableSize == reliableSize) {
                clearError_(error);
                return true;
            }
            setError_(error, "QUIC stream already has a different reset queued");
            return false;
        }
        if (reliableSize > stream.nextOffset) {
            setError_(error, "RESET_STREAM_AT reliable size exceeds sent stream data");
            return false;
        }

        const std::size_t stored = static_cast<std::size_t>(stream.buffer.size());
        const std::size_t remaining = stream.bufferOffset < stored
            ? stored - stream.bufferOffset : 0;
        m_bufferedStreamSendBytes = remaining <= m_bufferedStreamSendBytes
            ? m_bufferedStreamSendBytes - remaining : 0;
        stream.buffer = SwByteArray();
        stream.bufferOffset = 0;
        stream.resetQueued = true;
        stream.resetAt = true;
        stream.resetReliableSize = reliableSize;
        stream.resetAcked = false;
        removePendingSendFramesAfterReliableSize_(streamId, reliableSize);
        space_(Level::Application).pendingFrames.push_back(
            SwQuicFrame::resetStreamAt(streamId, applicationErrorCode,
                                       stream.nextOffset, reliableSize));
        clearError_(error);
        return true;
    }

    // Ask the peer to abort its send side. This is intentionally independent
    // from resetStream(): a bidirectional request rejected while its body is
    // still arriving needs both STOP_SENDING and a RESET_STREAM response.
    bool stopReceivingStream(std::uint64_t streamId,
                             std::uint64_t applicationErrorCode,
                             SwString* error = nullptr) {
        if (m_state != State::Open) {
            setError_(error, "QUIC connection is not open for sending");
            return false;
        }
        if (isRetiredPeerBidirectional_(streamId)) {
            clearError_(error);
            return true;
        }
        const bool locallyInitiated =
            (m_role == Role::Client) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        const bool unidirectional = (streamId & 0x2U) != 0;
        if (locallyInitiated && unidirectional) {
            setError_(error, "Cannot stop receiving on a send-only stream");
            return false;
        }
        if (!locallyInitiated && !m_streams.hasStream(streamId) &&
            m_resetStreams.find(streamId) == m_resetStreams.end()) {
            setError_(error, "Cannot stop receiving before the peer opens its stream");
            return false;
        }
        space_(Level::Application).pendingFrames.push_back(
            SwQuicFrame::stopSending(streamId, applicationErrorCode));
        clearError_(error);
        return true;
    }

    // Register a locally-created stream whose bytes were sent by a separate
    // handshake driver (notably accepted 0-RTT) before this dataplane existed.
    void registerLocalStream(std::uint64_t streamId) {
        m_localOpenedStreams[streamId] = true;
    }

    // Inject stream data received out-of-band into the receive path, as if it
    // had arrived in a STREAM frame. Used to replay 0-RTT early data (collected
    // by the handshake driver before this connection existed) into the app.
    bool injectEarlyStream(std::uint64_t streamId, std::uint64_t offset,
                           const SwByteArray& data, bool fin, SwString* error = nullptr) {
        const SwQuicFrame frame = SwQuicFrame::stream(streamId, offset, data, fin);
        return processStreamFrame_(frame, error);
    }

    // Queue an unreliable DATAGRAM frame (RFC 9221).
    bool queueDatagramFrame(const SwByteArray& data, SwString* error = nullptr) {
        if (m_state != State::Open) {
            setError_(error, "QUIC connection is not open for sending");
            return false;
        }
        if (m_hasPeerParams && m_peerParams.maxDatagramFrameSize == 0) {
            setError_(error, "Peer does not accept QUIC DATAGRAM frames");
            return false;
        }
        const std::size_t frameSize = 1 + SwQuicVarIntCodec::encodedSize(
            static_cast<std::uint64_t>(data.size())) + static_cast<std::size_t>(data.size());
        if (frameSize > packetPayloadBudget_(Level::Application) ||
            (m_hasPeerParams && frameSize > m_peerParams.maxDatagramFrameSize)) {
            setError_(error, "QUIC DATAGRAM does not fit the negotiated packet size");
            return false;
        }
        Space_& app = space_(Level::Application);
        if (m_maxPendingDatagramFrames > 0 &&
            app.pendingFrames.size() >= m_maxPendingDatagramFrames) {
            setError_(error, "QUIC DATAGRAM pending queue is full");
            return false;
        }
        app.pendingFrames.push_back(SwQuicFrame::datagram(data));
        ++m_queuedDatagramFrames;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    bool queueDatagramFrame(SwByteArray&& data, SwString* error = nullptr) {
        if (m_state != State::Open) {
            setError_(error, "QUIC connection is not open for sending");
            return false;
        }
        if (m_hasPeerParams && m_peerParams.maxDatagramFrameSize == 0) {
            setError_(error, "Peer does not accept QUIC DATAGRAM frames");
            return false;
        }
        const std::size_t frameSize = 1 + SwQuicVarIntCodec::encodedSize(
            static_cast<std::uint64_t>(data.size())) + static_cast<std::size_t>(data.size());
        if (frameSize > packetPayloadBudget_(Level::Application) ||
            (m_hasPeerParams && frameSize > m_peerParams.maxDatagramFrameSize)) {
            setError_(error, "QUIC DATAGRAM does not fit the negotiated packet size");
            return false;
        }
        Space_& app = space_(Level::Application);
        if (m_maxPendingDatagramFrames > 0 &&
            app.pendingFrames.size() >= m_maxPendingDatagramFrames) {
            setError_(error, "QUIC DATAGRAM pending queue is full");
            return false;
        }
        app.pendingFrames.push_back(SwQuicFrame::datagram(std::move(data)));
        ++m_queuedDatagramFrames;
        clearError_(error);
        return true;
    }

    // Queue an arbitrary frame at an encryption level (CRYPTO during the
    // handshake, control frames, ...).
    void queueFrame(Level level, const SwQuicFrame& frame) {
        space_(level).pendingFrames.push_back(frame);
    }

    // Initiate connection close: a CONNECTION_CLOSE goes out on the next
    // buildDatagrams(), then the connection drains.
    void close(std::uint64_t errorCode,
               const SwString& reasonPhrase,
               bool isApplicationClose = true) {
        if (m_state != State::Open) {
            return;
        }
        m_state = State::Closing;
        m_closeQueued = true;
        m_closeErrorCode = errorCode;
        m_closeReason = reasonPhrase;
        m_closeIsApplication = isApplicationClose;
    }

    // ---- outgoing datagram assembly -----------------------------------------

    // Drain everything currently sendable into ready-to-write UDP datagrams.
    bool buildDatagrams(std::uint64_t nowMs,
                        SwVector<SwByteArray>& outDatagrams,
                        SwString* error = nullptr) {
        outDatagrams.clear();
        if (m_pacingRatePerMillisecond > 0) {
            if (m_pacingLastRefillMs == 0) {
                m_pacingLastRefillMs = nowMs;
                m_pacingTokens = m_pacingMaximumBurst;
            } else if (nowMs > m_pacingLastRefillMs) {
                const std::uint64_t elapsed = nowMs - m_pacingLastRefillMs;
                const std::uint64_t added =
                    elapsed * static_cast<std::uint64_t>(m_pacingRatePerMillisecond);
                const std::uint64_t available =
                    static_cast<std::uint64_t>(m_pacingTokens) + added;
                m_pacingTokens = static_cast<std::size_t>(
                    available > m_pacingMaximumBurst ? m_pacingMaximumBurst : available);
                m_pacingLastRefillMs = nowMs;
            }
        }

        if (m_state == State::Draining || m_state == State::Closed) {
            if (error) {
                *error = SwString();
            }
            return true;
        }

        if (m_state == State::Closing) {
            return buildCloseDatagram_(nowMs, outDatagrams, error);
        }

        for (std::size_t i = 0; i < kLevelCount(); ++i) {
            if (m_maxDatagramsPerBuild > 0 &&
                outDatagrams.size() >= m_maxDatagramsPerBuild) {
                break;
            }
            const Level level = static_cast<Level>(i);
            if (!buildDatagramsForLevel_(level, nowMs, outDatagrams, error)) {
                return false;
            }
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    // ---- timers ---------------------------------------------------------------

    // Milliseconds until the next timer fires, or -1 when no timer is armed.
    // The driver must call onTimeout(nowMs) once that delay elapsed, then
    // flush buildDatagrams().
    std::int64_t nextTimeoutMs(std::uint64_t nowMs) const {
        bool any = false;
        std::uint64_t deadline = 0;

        for (std::size_t i = 0; i < kLevelCount(); ++i) {
            const Space_& space = m_spaces[i];
            if (space.hasAckDeadline) {
                mergeDeadline_(any, deadline, space.ackDeadlineMs);
            }
            std::uint64_t ptoDeadline = 0;
            if (spacePtoDeadline_(static_cast<Level>(i), space, ptoDeadline)) {
                mergeDeadline_(any, deadline, ptoDeadline);
            }
        }

        if (m_effectiveIdleTimeoutMs > 0 && m_hasNetworkActivity &&
            m_state == State::Open) {
            mergeDeadline_(any, deadline, m_lastNetworkActivityMs + m_effectiveIdleTimeoutMs);
        }
        if (m_hasDrainDeadline) {
            mergeDeadline_(any, deadline, m_drainDeadlineMs);
        }

        if (!any) {
            return -1;
        }
        if (deadline <= nowMs) {
            return 0;
        }
        return static_cast<std::int64_t>(deadline - nowMs);
    }

    // Fire expired timers: PTO probes are queued (drain them with
    // buildDatagrams), idle/drain expiry closes the connection.
    void onTimeout(std::uint64_t nowMs) {
        if (m_hasDrainDeadline && nowMs >= m_drainDeadlineMs) {
            m_state = State::Closed;
            m_hasDrainDeadline = false;
            return;
        }

        if (m_effectiveIdleTimeoutMs > 0 && m_hasNetworkActivity &&
            m_state == State::Open &&
            nowMs >= m_lastNetworkActivityMs + m_effectiveIdleTimeoutMs) {
            m_state = State::Closed; // idle timeout: silent close (RFC 9000 10.1)
            return;
        }

        for (std::size_t i = 0; i < kLevelCount(); ++i) {
            Space_& space = m_spaces[i];
            std::uint64_t ptoDeadline = 0;
            if (spacePtoDeadline_(static_cast<Level>(i), space, ptoDeadline) &&
                nowMs >= ptoDeadline) {
                // Probe on PTO (RFC 9002 6.2.4): retransmit the oldest
                // unacknowledged ack-eliciting data if any (so a lost packet is
                // actually recovered), otherwise send a PING. The probe is owed
                // to this space and may be sent past the congestion window.
                requeueOldestUnacked_(space);
                if (space.probesPending < 2) {
                    ++space.probesPending;
                }
                ++m_ptoCount;
            }
            if (space.hasAckDeadline && nowMs >= space.ackDeadlineMs) {
                // Nothing to do here: buildDatagrams() sends the pending ACK
                // because the deadline has passed.
            }
        }
    }

    // ---- connection migration / path validation (RFC 9000 section 9) --------

    // Construit exactement un paquet 1-RTT de contrôle de chemin. Le paquet
    // consomme le prochain PN de l'espace Application ordinaire : deux tuples
    // ne peuvent donc jamais réutiliser le même nonce AEAD. La frame ciblée
    // n'est volontairement pas enregistrée comme retransmissible dans la loss
    // queue globale ; son driver possède le retry borné et le tuple de sortie.
    // maxWireBytes borne le budget 3x propre au candidat. UINT64_MAX désactive
    // uniquement cette borne externe (challenge initié localement, non réfléchi).
    bool buildPathControlDatagram(bool response,
                                  const SwByteArray& controlData,
                                  std::uint64_t nowMs,
                                  std::uint64_t maxWireBytes,
                                  SwByteArray& outDatagram,
                                  SwString* error = nullptr) {
        outDatagram.clear();
        if (m_state != State::Open || controlData.size() != 8 ||
            !space_(Level::Application).hasTxKeys) {
            setError_(error, "Cannot build QUIC path control packet");
            return false;
        }
        SwVector<SwQuicFrame> frames;
        frames.push_back(response ? SwQuicFrame::pathResponse(controlData)
                                  : SwQuicFrame::pathChallenge(controlData));
        bool amplificationBlocked = false;
        if (!encodePacket_(Level::Application, frames, true, nowMs, outDatagram,
                           amplificationBlocked, error, maxWireBytes,
                           false /* no loss/congestion state for targeted control */)) {
            return false;
        }
        if (amplificationBlocked) {
            outDatagram.clear();
        }
        clearError_(error);
        return true;
    }

    // Le driver appelle ceci seulement après un PATH_RESPONSE authentifié sur
    // le tuple candidat. Les PN et la loss map restent communs à la connexion ;
    // seul le contrôleur de congestion est réinitialisé pour le nouveau chemin.
    void commitPathMigration() {
        m_congestion = SwQuicCongestionControl();
        m_spaces[static_cast<std::size_t>(Level::Application)].loss =
            SwQuicLossRecovery();
        m_spaces[static_cast<std::size_t>(Level::Application)].loss
            .setHandshakeConfirmed(m_handshakeConfirmed);
        m_pathValidated = true;
        m_awaitingPathResponse = false;
        m_resetCcOnValidation = false;
        m_bytesReceivedThisPath = 0;
        m_bytesSentThisPath = 0;
    }

    // Starts driver-managed path validation without redirecting the normal
    // send queue. The caller sends the returned challenge on the candidate
    // tuple and calls commitPathMigration() only after a matching authenticated
    // PATH_RESPONSE arrives on that same tuple.
    bool beginPathValidation(std::uint64_t nowMs,
                             SwByteArray& challengeOut,
                             SwString* error = nullptr) {
        SwByteArray challenge;
        if (!SwQuicRandom::fill(challenge, 8, error)) {
            return false;
        }
        m_pathChallengeData = challenge;
        m_awaitingPathResponse = true;
        m_resetCcOnValidation = true;
        challengeOut = challenge;
        (void)nowMs;
        clearError_(error);
        return true;
    }

    bool matchesPathResponse(const SwByteArray& data) const {
        return m_awaitingPathResponse && data == m_pathChallengeData;
    }

    void cancelPathValidation() {
        m_awaitingPathResponse = false;
        m_resetCcOnValidation = false;
        m_pathChallengeData.clear();
    }

    // Called by the driver when a packet for this connection arrives from a
    // new peer address (the connection is found by its connection ID, so it
    // survives the address change). This starts path validation: a
    // PATH_CHALLENGE is queued toward the new path, sending on that path is
    // held under the 3x anti-amplification limit until it validates
    // (RFC 9000 9.3), and the congestion controller + RTT are reset once it
    // does (RFC 9000 9.4).
    // receivedBytesOnNewPath is the size of the datagram that triggered the
    // migration: the new path's anti-amplification budget (RFC 9000 8.1/9.3)
    // must count ONLY bytes received on that path, not the connection's whole
    // history, otherwise a spoofed migration turns into a reflection vector.
    bool onPeerAddressChanged(std::uint64_t nowMs,
                              std::uint64_t receivedBytesOnNewPath = 1200,
                              SwString* error = nullptr) {
        if (m_state != State::Open) {
            clearError_(error);
            return true;
        }

        SwByteArray challenge;
        if (!beginPathValidation(nowMs, challenge, error)) {
            return false;
        }
        m_pathValidated = false;
        // Reset BOTH per-path counters: the new path's budget starts from only
        // the triggering datagram's bytes (RFC 9000 9.3.1).
        m_bytesSentThisPath = 0;
        m_bytesReceivedThisPath = receivedBytesOnNewPath;
        space_(Level::Application).pendingFrames.push_back(
            SwQuicFrame::pathChallenge(challenge));
        clearError_(error);
        return true;
    }

    bool pathValidated() const { return m_pathValidated; }
    bool awaitingPathResponse() const { return m_awaitingPathResponse; }

    // Issue a fresh connection ID to the peer (NEW_CONNECTION_ID, RFC 9000
    // 5.1.1 / 19.15) so it can migrate to a new, unlinkable CID. Returns the
    // issued CID; the driver must route incoming packets addressed to it to
    // this connection.
    SwByteArray issueNewConnectionId(SwString* error = nullptr) {
        SwByteArray cidBytes;
        SwByteArray token;
        if (!SwQuicRandom::fill(cidBytes, 8, error) ||
            !SwQuicRandom::fill(token, 16, error)) {
            return SwByteArray();
        }
        const std::uint64_t sequence = m_nextLocalCidSequence++;
        m_issuedConnectionIds[sequence] = cidBytes;
        space_(Level::Application).pendingFrames.push_back(
            SwQuicFrame::newConnectionId(sequence, 0, cidBytes, token));
        clearError_(error);
        return cidBytes;
    }

    // ---- state / introspection ---------------------------------------------

    State state() const { return m_state; }
    const SwQuicPacketHeader& lastInitialHeader() const { return m_lastInitialHeader; }

    std::size_t receivedPingCount() const { return m_receivedPingCount; }
    std::size_t pendingDatagramCount() const { return m_datagrams.size(); }
    const SwByteArray& lastPathChallenge() const { return m_lastPathChallenge; }
    bool sawVersionNegotiation() const { return m_sawVersionNegotiation; }
    bool sawRetry() const { return m_sawRetry; }

    std::uint64_t closeErrorCode() const { return m_closeErrorCode; }
    const SwString& closeReason() const { return m_closeReason; }

    SwByteArray takeDatagram() {
        if (m_datagrams.empty()) {
            return SwByteArray();
        }

        SwByteArray datagram = std::move(m_datagrams.front());
        const std::size_t size = static_cast<std::size_t>(datagram.size());
        m_receivedDatagramBytes = size <= m_receivedDatagramBytes
            ? m_receivedDatagramBytes - size
            : 0;
        m_datagrams.pop_front();
        return datagram;
    }

    const SwQuicStreamMap& streams() const { return m_streams; }
    SwQuicStreamMap& streams() { return m_streams; }

    bool isStreamReceiveReset(std::uint64_t streamId) const {
        return m_resetStreams.find(streamId) != m_resetStreams.end();
    }

    bool streamHasFinalSize(std::uint64_t streamId) const {
        return m_streamFinalSize.find(streamId) != m_streamFinalSize.end();
    }

    bool isStreamSendReset(std::uint64_t streamId) const {
        SwMap<std::uint64_t, SendStream_>::const_iterator it =
            m_sendStreams.find(streamId);
        return it != m_sendStreams.end() && it->second.resetQueued;
    }

    SwByteArray readStream(std::uint64_t streamId) {
        SwByteArray data = m_streams.readContiguous(streamId);
        if (!data.isEmpty()) {
            creditFlowControl_(streamId, static_cast<std::uint64_t>(data.size()));
        }
        SwMap<std::uint64_t, PendingReliableReset_>::iterator pending =
            m_pendingReliableResets.find(streamId);
        const SwQuicStream* stream = m_streams.stream(streamId);
        if (pending != m_pendingReliableResets.end() && stream &&
            stream->readOffset() >= pending->second.reliableSize) {
            m_resetStreams[streamId] = pending->second.errorCode;
            SwQuicStreamFlowControl& streamFlow = streamFlowControl_(streamId);
            const std::uint64_t consumed = streamFlow.bytesConsumed();
            if (pending->second.finalSize > consumed) {
                creditFlowControl_(streamId,
                                   pending->second.finalSize - consumed);
            }
            m_pendingReliableResets.erase(pending);
            markStreamTouched_(streamId);
        }
        // A payload-less FIN can make an abandoned receive side terminal too.
        retireAcknowledgedPeerStreams_();
        return data;
    }

    // Return one unit of peer-initiated bidirectional-stream credit after the
    // application consumed the complete receive side and queued its complete
    // response (including FIN), or deliberately reset its send side. This only
    // marks the application half as released. Credit is returned later, once
    // the QUIC send state is terminal: RESET_STREAM ACKed, or FIN ACKed plus
    // every byte below the final offset ACKed.
    //
    // RFC 9000 section 4.6 makes MAX_STREAMS cumulative: every released stream
    // raises the largest stream count the peer may open. Remember released IDs
    // so a repeated HTTP pump cannot grant the same credit twice.
    bool releasePeerBidirectionalStream(std::uint64_t streamId,
                                        SwString* error = nullptr) {
        const bool peerInitiated =
            (m_role == Role::Server) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        if (!peerInitiated || (streamId & 0x2U) != 0) {
            setError_(error, "Only peer-initiated bidirectional streams can be released");
            return false;
        }

        SwMap<std::uint64_t, SendStream_>::iterator sendIt =
            m_sendStreams.find(streamId);
        if (sendIt == m_sendStreams.end() ||
            (!sendIt->second.finQueued && !sendIt->second.resetQueued)) {
            setError_(error, "QUIC stream cannot be released before its application send side finishes");
            return false;
        }

        if (sendIt->second.peerCreditPending) {
            clearError_(error);
            return true;
        }
        // RFC 9000 section 4.6: do not return cumulative stream credit until
        // both transport directions reach their terminal states.
        sendIt->second.peerCreditPending = true;
        retireAcknowledgedPeerStreams_();
        clearError_(error);
        return true;
    }

    bool isPeerBidirectionalStreamReleased(std::uint64_t streamId) const {
        if (isRetiredPeerBidirectional_(streamId)) {
            return true;
        }
        SwMap<std::uint64_t, SendStream_>::const_iterator it =
            m_sendStreams.find(streamId);
        return it != m_sendStreams.end() && it->second.peerCreditPending;
    }

    std::uint64_t localBidirectionalStreamLimit() const {
        return m_localMaxStreamsBidi;
    }

    std::size_t trackedReceiveStreamCount() const { return m_streams.streamCount(); }
    std::size_t trackedSendStreamCount() const { return m_sendStreams.size(); }
    std::size_t retiredPeerBidirectionalRangeCount() const {
        return m_retiredPeerBidiRanges.size();
    }

    // Streams whose receive state changed in the most recent datagram. This
    // lets event-driven adapters avoid scanning every historical stream on
    // every UDP packet.
    SwVector<std::uint64_t> takeTouchedStreamIds() {
        SwVector<std::uint64_t> touched = std::move(m_touchedStreams);
        m_touchedStreams.clear();
        return touched;
    }

    // Contiguous CRYPTO data received at a level; the Initial overload keeps
    // the historical accessor shape.
    const SwByteArray& cryptoData() const { return m_cryptoAssembled[0]; }
    const SwByteArray& cryptoData(Level level) const {
        return m_cryptoAssembled[static_cast<std::size_t>(level)];
    }
    SwByteArray takeCryptoData(Level level) {
        const std::size_t index = static_cast<std::size_t>(level);
        SwByteArray data = std::move(m_cryptoAssembled[index]);
        m_cryptoAssembled[index].clear();
        return data;
    }

    bool buildAckFrame(SwQuicFrame& outFrame,
                       std::uint64_t ackDelay = 0,
                       SwString* error = nullptr) const {
        return space_(Level::Initial).ackTracker.buildAckFrame(outFrame, ackDelay, error);
    }

    const SwQuicLossRecovery& lossRecovery(Level level) const { return space_(level).loss; }
    const SwQuicCongestionControl& congestionControl() const { return m_congestion; }
    const SwQuicConnectionFlowControl& connectionFlowControl() const { return m_connectionFlow; }

    struct DiagnosticStats {
        std::size_t pendingFrames = 0;
        std::size_t sentPackets = 0;
        std::size_t ackElicitingSinceAck = 0;
        std::size_t probesPending = 0;
        std::size_t bufferedStreamSendBytes = 0;
        std::uint64_t congestionWindow = 0;
        std::uint64_t bytesInFlight = 0;
        std::uint64_t nextTxPacketNumber = 0;
        std::uint64_t largestReceivedPacketNumber = 0;
        std::uint64_t ackDeadlineMs = 0;
        std::int64_t nextTimeoutMs = -1;
        bool hasLargestReceivedPacketNumber = false;
        bool hasAckDeadline = false;
        bool hasPendingAck = false;
        bool hasAckElicitingInFlight = false;
        std::uint64_t queuedDatagramFrames = 0;
        std::uint64_t encodedDatagramFrames = 0;
        std::uint64_t receivedDatagramFrames = 0;
        std::uint64_t droppedReceivedDatagrams = 0;
    };

    DiagnosticStats diagnosticStats(Level level, std::uint64_t nowMs) const {
        const Space_& space = space_(level);
        DiagnosticStats stats;
        stats.pendingFrames = space.pendingFrames.size();
        stats.sentPackets = space.sentPackets.size();
        stats.ackElicitingSinceAck = space.ackElicitingSinceAck;
        stats.probesPending = space.probesPending;
        stats.bufferedStreamSendBytes = m_bufferedStreamSendBytes;
        stats.congestionWindow = m_congestion.congestionWindow();
        stats.bytesInFlight = m_congestion.bytesInFlight();
        stats.nextTxPacketNumber = space.nextTxPn;
        stats.largestReceivedPacketNumber = space.largestReceivedPn;
        stats.ackDeadlineMs = space.ackDeadlineMs;
        stats.nextTimeoutMs = nextTimeoutMs(nowMs);
        stats.hasLargestReceivedPacketNumber = space.hasLargestReceivedPn;
        stats.hasAckDeadline = space.hasAckDeadline;
        stats.hasPendingAck = space.ackTracker.ackElicitingPending();
        stats.hasAckElicitingInFlight = space.hasAckElicitingInFlight;
        stats.queuedDatagramFrames = m_queuedDatagramFrames;
        stats.encodedDatagramFrames = m_encodedDatagramFrames;
        stats.receivedDatagramFrames = m_receivedDatagramFrames;
        stats.droppedReceivedDatagrams = m_droppedReceivedDatagrams;
        return stats;
    }

private:
    struct SentRecord_ {
        SwVector<SwQuicFrame> retransmittable;
        std::size_t sentBytes;
        std::uint64_t sentTimeMs;
        bool ackEliciting;

        SentRecord_() : sentBytes(0), sentTimeMs(0), ackEliciting(false) {}
    };

    struct Space_ {
        SwQuicAckTracker ackTracker;
        SwQuicLossRecovery loss;
        SwQuicInitialKeys rxKeys;
        SwQuicInitialKeys txKeys;
        bool hasRxKeys;
        bool hasTxKeys;
        std::uint64_t largestReceivedPn;
        bool hasLargestReceivedPn;
        std::uint64_t nextTxPn;
        SwDequeue<SwQuicFrame> pendingFrames; // FIFO O(1), surface SwStack
        SwMap<std::uint64_t, SentRecord_> sentPackets;
        std::uint64_t ackDeadlineMs;
        bool hasAckDeadline;
        std::uint64_t lastAckElicitingSentMs;
        bool hasAckElicitingInFlight;
        std::uint64_t largestReceivedTimeMs; // arrival time of largestReceivedPn
        std::size_t probesPending;           // PTO probes owed, allowed past cwnd
        std::size_t ackElicitingSinceAck;    // ack-eliciting rx'd since last ACK sent (RFC 9000 13.2.1)

        Space_()
            : hasRxKeys(false),
              hasTxKeys(false),
              largestReceivedPn(0),
              hasLargestReceivedPn(false),
              nextTxPn(0),
              ackDeadlineMs(0),
              hasAckDeadline(false),
              lastAckElicitingSentMs(0),
              hasAckElicitingInFlight(false),
              largestReceivedTimeMs(0),
              probesPending(0),
              ackElicitingSinceAck(0) {}
    };

    struct PendingReliableReset_ {
        std::uint64_t errorCode = 0;
        std::uint64_t finalSize = 0;
        std::uint64_t reliableSize = 0;
    };

    struct SendStream_ {
        SwByteArray buffer;          // queued storage; consumed prefix is retained amortized
        std::size_t bufferOffset;    // first byte not yet handed to a packet
        std::uint64_t nextOffset;    // wire offset of buffer[bufferOffset]
        bool finQueued;
        bool finSent;
        bool finAcked;
        bool resetQueued;
        bool resetAt;
        bool resetAcked;
        std::uint64_t resetReliableSize;
        bool peerCreditPending;
        std::uint64_t contiguousAckedOffset;
        // Disjoint half-open ranges strictly above contiguousAckedOffset.
        // They are merged on every ACK, so reordered/retransmitted packets do
        // not grow this set with duplicate byte ranges.
        SwMap<std::uint64_t, std::uint64_t> ackedRanges;

        SendStream_()
            : bufferOffset(0), nextOffset(0), finQueued(false), finSent(false),
              finAcked(false), resetQueued(false), resetAt(false), resetAcked(false),
              resetReliableSize(0), peerCreditPending(false), contiguousAckedOffset(0) {}
    };

    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static void clearError_(SwString* error) {
        if (error) {
            *error = SwString();
        }
    }

    static void mergeDeadline_(bool& any, std::uint64_t& deadline, std::uint64_t candidate) {
        if (!any || candidate < deadline) {
            deadline = candidate;
        }
        any = true;
    }

    static std::uint64_t combineIdleTimeouts_(std::uint64_t local, std::uint64_t peer) {
        if (local == 0) {
            return peer;
        }
        if (peer == 0) {
            return local;
        }
        return local < peer ? local : peer;
    }

    Space_& space_(Level level) { return m_spaces[static_cast<std::size_t>(level)]; }
    const Space_& space_(Level level) const { return m_spaces[static_cast<std::size_t>(level)]; }

    // On PTO, resend the retransmittable frames of the oldest unacknowledged
    // packet in the space so a lost packet is recovered directly; if none carry
    // retransmittable data, queue a PING to elicit an ACK.
    void requeueOldestUnacked_(Space_& space) {
        for (SwMap<std::uint64_t, SentRecord_>::iterator it = space.sentPackets.begin();
             it != space.sentPackets.end(); ++it) {
            bool queued = false;
            for (std::size_t i = 0; i < it->second.retransmittable.size(); ++i) {
                const SwQuicFrame& frame = it->second.retransmittable[i];
                // A stream can enter Reset Sent, become fully retired, or have
                // this byte range acknowledged through another transmission
                // while the original packet remains outstanding. PTO probes
                // must apply the same obsolescence filter as loss recovery;
                // otherwise stale STREAM data can be emitted after
                // RESET_STREAM/STOP_SENDING.
                if (shouldRetransmitFrame_(frame)) {
                    space.pendingFrames.push_back(frame);
                    queued = true;
                }
            }
            if (queued) {
                return;
            }
        }
        space.pendingFrames.push_back(SwQuicFrame::ping());
    }

    // PTO deadline for a space, valid only while ack-eliciting data is in
    // flight. max_ack_delay is included only for the Application space: the
    // peer must not delay Initial/Handshake ACKs (RFC 9002 6.2.1, appendix A.9).
    bool spacePtoDeadline_(Level level, const Space_& space, std::uint64_t& outDeadline) const {
        if (!space.hasAckElicitingInFlight || space.sentPackets.empty()) {
            return false;
        }
        const std::uint64_t maxAckDelay =
            (level == Level::Application) ? m_peerMaxAckDelayMs : 0;
        std::uint64_t pto = space.loss.computePtoMs(maxAckDelay);
        for (std::size_t i = 0; i < m_ptoCount && i < 16; ++i) {
            pto *= 2; // exponential backoff (RFC 9002 6.2.1)
        }
        outDeadline = space.lastAckElicitingSentMs + pto;
        return true;
    }

    // ---- receive internals ---------------------------------------------------

    bool packetNumberAlreadyConsumed_(Level level, std::uint64_t packetNumber) const {
        const Space_& space = space_(level);
        if (space.ackTracker.containsReceivedPacket(packetNumber)) {
            return true;
        }
        // Les numéros sortis de la fenêtre bornée de l'ACK tracker restent
        // des replays : ne jamais les rendre de nouveau acceptables après prune.
        return space.hasLargestReceivedPn && packetNumber <= space.largestReceivedPn &&
               space.largestReceivedPn - packetNumber >=
                   static_cast<std::uint64_t>(SwQuicAckTracker::kMaxTrackedPackets());
    }

    bool receivePacket_(const SwByteArray& packet,
                        std::uint64_t nowMs,
                        std::size_t& outConsumed,
                        bool& outAuthenticated,
                        SwString* error) {
        outConsumed = 0;
        outAuthenticated = false;
        if (packet.isEmpty()) {
            return true;
        }

        const std::uint8_t firstByte = static_cast<std::uint8_t>(packet.constData()[0]);

        if ((firstByte & 0x80U) == 0) {
            // Short header: always the last packet of a datagram.
            outConsumed = packet.size();
            Space_& space = space_(Level::Application);
            if (!space.hasRxKeys) {
                return true; // no 1-RTT keys yet: drop silently
            }

            std::uint64_t packetNumber = 0;
            bool keyPhase = false;
            SwByteArray plaintext;
            const std::uint64_t* largest =
                space.hasLargestReceivedPn ? &space.largestReceivedPn : nullptr;
            if (!SwQuicPacketProtector::unprotectShortHeader1Rtt(space.rxKeys,
                                                                 packet,
                                                                 m_localConnectionId.size(),
                                                                 packetNumber,
                                                                 keyPhase,
                                                                 plaintext,
                                                                 error,
                                                                 largest)) {
                if (std::getenv("VIGIL_QUIC_TRACE") != nullptr) {
                    const SwString reason = error ? *error : SwString();
                    std::fprintf(stderr,
                                 "QUIC_1RTT_DROP dcid_len=%zu packet_bytes=%zu "
                                 "largest_rx=%llu reason=%s\n",
                                 m_localConnectionId.size(), packet.size(),
                                 static_cast<unsigned long long>(
                                     space.hasLargestReceivedPn
                                         ? space.largestReceivedPn : 0),
                                 reason.c_str());
                    std::fflush(stderr);
                }
                // Undecryptable packet: discard silently, do not fail the
                // datagram (RFC 9000 12.2). It is the last packet anyway.
                clearError_(error);
                return true;
            }
            if (packetNumberAlreadyConsumed_(Level::Application, packetNumber)) {
                clearError_(error);
                return true;
            }
            outAuthenticated = true;
            return processDecodedPacket_(Level::Application, packetNumber, plaintext,
                                         nowMs, error);
        }

        // Long header. Version Negotiation has version 0 and fills the datagram.
        if (packet.size() >= 5) {
            const std::uint32_t version =
                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(packet.constData()[1])) << 24) |
                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(packet.constData()[2])) << 16) |
                (static_cast<std::uint32_t>(static_cast<std::uint8_t>(packet.constData()[3])) << 8) |
                static_cast<std::uint32_t>(static_cast<std::uint8_t>(packet.constData()[4]));
            if (version == 0) {
                m_sawVersionNegotiation = true;
                outConsumed = packet.size();
                return true;
            }
        }

        const std::uint8_t typeBits = static_cast<std::uint8_t>(firstByte & 0x30U);
        if (typeBits == 0x30U) {
            // Retry consumes the rest of the datagram; handled by the
            // handshake driver, not at this layer.
            m_sawRetry = true;
            outConsumed = packet.size();
            return true;
        }

        if (typeBits == 0x00U) { // Initial
            Space_& space = space_(Level::Initial);
            SwQuicPacketHeader header;
            SwByteArray plaintext;
            std::size_t consumed = 0;

            if (space.hasRxKeys) {
                const std::uint64_t* largest =
                    space.hasLargestReceivedPn ? &space.largestReceivedPn : nullptr;
                if (!SwQuicPacketProtector::unprotectInitial(space.rxKeys, packet, header,
                                                             plaintext, &consumed, error,
                                                             largest)) {
                    // Undecryptable Initial: discard silently and keep decoding
                    // any coalesced packets behind it (RFC 9000 12.2).
                    clearError_(error);
                    if (!skipLongHeaderPacket_(packet, true, outConsumed, nullptr)) {
                        outConsumed = packet.size();
                    }
                    return true;
                }
                if (packetNumberAlreadyConsumed_(Level::Initial, header.packetNumber())) {
                    clearError_(error);
                    outConsumed = consumed;
                    return true;
                }
                outAuthenticated = true;
            } else {
                if (!SwQuicPacketCodec::decodeInitialPacket(packet, header, plaintext,
                                                            &consumed, error)) {
                    return false;
                }
            }

            m_lastInitialHeader = header;
            outConsumed = consumed;
            return processDecodedPacket_(Level::Initial, header.packetNumber(), plaintext,
                                         nowMs, error);
        }

        if (typeBits == 0x20U) { // Handshake
            Space_& space = space_(Level::Handshake);
            if (!space.hasRxKeys) {
                // Cannot decrypt yet; skip the packet using its cleartext length.
                return skipLongHeaderPacket_(packet, false, outConsumed, error);
            }

            SwQuicPacketHeader header;
            SwByteArray plaintext;
            std::size_t consumed = 0;
            const std::uint64_t* largest =
                space.hasLargestReceivedPn ? &space.largestReceivedPn : nullptr;
            if (!SwQuicPacketProtector::unprotectHandshake(space.rxKeys, packet, header,
                                                           plaintext, &consumed, error,
                                                           largest)) {
                // Undecryptable Handshake: discard silently and keep decoding
                // any coalesced packets behind it (RFC 9000 12.2).
                clearError_(error);
                if (!skipLongHeaderPacket_(packet, false, outConsumed, nullptr)) {
                    outConsumed = packet.size();
                }
                return true;
            }
            if (packetNumberAlreadyConsumed_(Level::Handshake, header.packetNumber())) {
                clearError_(error);
                outConsumed = consumed;
                return true;
            }
            outAuthenticated = true;
            outConsumed = consumed;
            return processDecodedPacket_(Level::Handshake, header.packetNumber(), plaintext,
                                         nowMs, error);
        }

        // 0-RTT (0x10): not supported; skip it so coalesced packets behind it
        // still decode.
        return skipLongHeaderPacket_(packet, false, outConsumed, error);
    }

    static bool skipLongHeaderPacket_(const SwByteArray& packet,
                                      bool hasToken,
                                      std::size_t& outConsumed,
                                      SwString* error) {
        std::size_t offset = 1 + 4; // first byte + version
        std::uint8_t dcidLength = 0;
        std::uint8_t scidLength = 0;
        if (!readByteAt_(packet, offset, dcidLength, error)) {
            return false;
        }
        offset += dcidLength;
        if (!readByteAt_(packet, offset, scidLength, error)) {
            return false;
        }
        offset += scidLength;

        if (hasToken) {
            std::uint64_t tokenLength = 0;
            if (!SwQuicVarIntCodec::decode(packet, offset, tokenLength, error)) {
                return false;
            }
            offset += static_cast<std::size_t>(tokenLength);
        }

        std::uint64_t payloadLength = 0;
        if (offset >= packet.size() ||
            !SwQuicVarIntCodec::decode(packet, offset, payloadLength, error)) {
            setError_(error, "QUIC long header packet is truncated");
            return false;
        }
        if (payloadLength > static_cast<std::uint64_t>(packet.size() - offset)) {
            setError_(error, "QUIC long header payload is truncated");
            return false;
        }

        outConsumed = offset + static_cast<std::size_t>(payloadLength);
        return true;
    }

    static bool readByteAt_(const SwByteArray& bytes,
                            std::size_t& offset,
                            std::uint8_t& outValue,
                            SwString* error) {
        if (offset >= bytes.size()) {
            setError_(error, "QUIC packet is truncated");
            return false;
        }
        outValue = static_cast<std::uint8_t>(bytes.constData()[offset]);
        ++offset;
        return true;
    }

    bool processDecodedPacket_(Level level,
                               std::uint64_t packetNumber,
                               const SwByteArray& payload,
                               std::uint64_t nowMs,
                               SwString* error) {
        SwVector<SwQuicFrame> frames;
        if (!SwQuicFrameCodec::decodeFrames(payload, frames, error)) {
            return false;
        }

        if (m_inspectPathControlOnly) {
            bool replay = m_hasLargestInspectedPathPacketNumber &&
                          packetNumber <= m_largestInspectedPathPacketNumber &&
                          m_largestInspectedPathPacketNumber - packetNumber >=
                              static_cast<std::uint64_t>(
                                  kMaxInspectedPathPacketNumbers());
            for (std::uint64_t inspected : m_inspectedPathPacketNumbers) {
                if (inspected == packetNumber) {
                    replay = true;
                    break;
                }
            }
            if (replay) {
                m_inspectedPathPacketReplay = true;
                clearError_(error);
                return true;
            }
            m_inspectedPathPacketReplay = false;
            if (!m_hasLargestInspectedPathPacketNumber ||
                packetNumber > m_largestInspectedPathPacketNumber) {
                m_largestInspectedPathPacketNumber = packetNumber;
                m_hasLargestInspectedPathPacketNumber = true;
            }
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const SwQuicFrame::Type type = frames[i].type();
                if (type == SwQuicFrame::Type::Padding) continue;
                if (type == SwQuicFrame::Type::PathChallenge) {
                    if (m_pathControlEventsOut &&
                        m_pathControlEventsOut->challenges.size() <
                            kMaxPathControlEvents()) {
                        m_pathControlEventsOut->challenges.push_back(
                            frames[i].data());
                    }
                    continue;
                }
                if (type == SwQuicFrame::Type::PathResponse) {
                    if (m_pathControlEventsOut &&
                        m_pathControlEventsOut->responses.size() <
                            kMaxPathControlEvents()) {
                        m_pathControlEventsOut->responses.push_back(
                            frames[i].data());
                    }
                    continue;
                }
                m_inspectedNonPathFrames = true;
            }
            // Keep exact duplicate detection for the current bounded reordering
            // window. The high-water mark above permanently rejects older PNs,
            // so FIFO eviction can never make a stale packet acceptable again.
            SwDequeue<std::uint64_t> retainedInspected;
            for (std::uint64_t inspected : m_inspectedPathPacketNumbers) {
                if (inspected <= m_largestInspectedPathPacketNumber &&
                    m_largestInspectedPathPacketNumber - inspected >=
                        static_cast<std::uint64_t>(
                            kMaxInspectedPathPacketNumbers())) {
                    continue;
                }
                retainedInspected.push_back(inspected);
            }
            m_inspectedPathPacketNumbers = std::move(retainedInspected);
            m_inspectedPathPacketNumbers.push_back(packetNumber);
            clearError_(error);
            return true;
        }

        Space_& space = space_(level);
        bool ackEliciting = false;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (isAckElicitingFrame_(frames[i].type())) {
                ackEliciting = true;
                break;
            }
        }

        // Reordering test BEFORE largestReceivedPn is advanced: a gap in the
        // received packet-number sequence forces an immediate ACK (RFC 9000 13.2.1).
        const bool outOfOrder = space.hasLargestReceivedPn &&
                                packetNumber != space.largestReceivedPn + 1;

        space.ackTracker.recordReceivedPacket(packetNumber, ackEliciting);
        if (!space.hasLargestReceivedPn || packetNumber > space.largestReceivedPn) {
            space.largestReceivedPn = packetNumber;
            space.hasLargestReceivedPn = true;
            space.largestReceivedTimeMs = nowMs; // for a measured ACK Delay
        }

        if (ackEliciting) {
            ++space.ackElicitingSinceAck;
            // RFC 9000 13.2.1: an ACK is sent immediately when a SECOND
            // ack-eliciting packet arrives since the last ACK, or when a packet
            // is received out of order; otherwise an application ACK may be held
            // up to max_ack_delay. Initial/Handshake ACKs are always immediate.
            const bool immediate = (level != Level::Application) ||
                                   space.ackElicitingSinceAck >= 2 ||
                                   outOfOrder;
            if (!space.hasAckDeadline) {
                space.ackDeadlineMs = immediate ? nowMs : nowMs + kLocalMaxAckDelayMs();
                space.hasAckDeadline = true;
            } else if (immediate) {
                // A delayed deadline was already pending: pull the ACK forward to now.
                space.ackDeadlineMs = nowMs;
            }
        }

        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (!processFrame_(level, frames[i], nowMs, error)) {
                return false;
            }
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    static bool isAckElicitingFrame_(SwQuicFrame::Type type) {
        return type != SwQuicFrame::Type::Ack &&
               type != SwQuicFrame::Type::Padding &&
               type != SwQuicFrame::Type::ConnectionClose;
    }

    bool processFrame_(Level level,
                       SwQuicFrame& frame,
                       std::uint64_t nowMs,
                       SwString* error) {
        switch (frame.type()) {
        case SwQuicFrame::Type::Padding:
            return true;
        case SwQuicFrame::Type::Ping:
            ++m_receivedPingCount;
            return true;
        case SwQuicFrame::Type::Ack:
            return processAckFrame_(level, frame, nowMs, error);
        case SwQuicFrame::Type::Crypto:
            return processCryptoFrame_(level, frame, error);
        case SwQuicFrame::Type::Stream:
            return processStreamFrame_(frame, error);
        case SwQuicFrame::Type::ConnectionClose:
            m_state = State::Draining;
            m_closeErrorCode = frame.errorCode();
            m_closeReason = frame.reasonPhrase();
            armDrainTimer_(nowMs);
            return true;
        case SwQuicFrame::Type::Datagram:
            if (m_localParams.maxDatagramFrameSize == 0 ||
                1 + SwQuicVarIntCodec::encodedSize(
                        static_cast<std::uint64_t>(frame.data().size())) +
                        static_cast<std::size_t>(frame.data().size()) >
                    m_localParams.maxDatagramFrameSize) {
                setError_(error, "Peer sent an unnegotiated or oversized QUIC DATAGRAM");
                return false;
            }
            {
                const std::size_t dataSize =
                    static_cast<std::size_t>(frame.data().size());
                const bool countFull = m_maxReceivedDatagrams > 0 &&
                    m_datagrams.size() >= m_maxReceivedDatagrams;
                const bool bytesFull = m_maxReceivedDatagramBytes > 0 &&
                    (m_receivedDatagramBytes > m_maxReceivedDatagramBytes ||
                     dataSize > m_maxReceivedDatagramBytes -
                                    m_receivedDatagramBytes);
                if (countFull || bytesFull) {
                    // RFC 9221 DATAGRAM is unreliable; dropping under local
                    // backpressure is preferable to unbounded retention.
                    ++m_droppedReceivedDatagrams;
                    return true;
                }
                m_receivedDatagramBytes += dataSize;
            }
            m_datagrams.push_back(frame.takeData());
            ++m_receivedDatagramFrames;
            return true;
        case SwQuicFrame::Type::PathChallenge:
            m_lastPathChallenge = frame.data();
            if (m_pathControlEventsOut) {
                if (m_pathControlEventsOut->challenges.size() < kMaxPathControlEvents()) {
                    m_pathControlEventsOut->challenges.push_back(frame.data());
                }
            } else {
                // Chemin actif : la file normale est bien émise sur le tuple
                // actif. Un driver candidat utilise le canal ciblé ci-dessus.
                space_(levelForPathResponse_(level)).pendingFrames.push_back(
                    SwQuicFrame::pathResponse(frame.data()));
            }
            return true;
        case SwQuicFrame::Type::PathResponse:
            if (m_pathControlEventsOut) {
                if (m_pathControlEventsOut->responses.size() < kMaxPathControlEvents()) {
                    m_pathControlEventsOut->responses.push_back(frame.data());
                }
            } else if (m_awaitingPathResponse && frame.data() == m_pathChallengeData) {
                // Path validated (RFC 9000 8.2.3): the new path is now the
                // active one. Reset congestion controller and RTT for the new
                // path characteristics (RFC 9000 9.4) and lift the
                // anti-amplification limit.
                m_awaitingPathResponse = false;
                m_pathValidated = true;
                if (m_resetCcOnValidation) {
                    m_congestion = SwQuicCongestionControl();
                    m_spaces[static_cast<std::size_t>(Level::Application)].loss =
                        SwQuicLossRecovery();
                    m_spaces[static_cast<std::size_t>(Level::Application)].loss
                        .setHandshakeConfirmed(m_handshakeConfirmed);
                    m_resetCcOnValidation = false;
                }
                m_bytesReceivedThisPath = 0;
                m_bytesSentThisPath = 0;
            }
            return true;
        case SwQuicFrame::Type::MaxData:
            if (frame.maximum() > m_connectionFlow.peerMaxData()) {
                m_connectionFlow.setPeerMaxData(frame.maximum());
            }
            return true;
        case SwQuicFrame::Type::MaxStreamData: {
            if (isRetiredPeerBidirectional_(frame.streamId())) {
                return true;
            }
            if (!validatePeerSendControlStream_(frame.streamId(), error)) {
                return false;
            }
            std::uint64_t& limit = m_peerStreamMaxData[frame.streamId()];
            if (frame.maximum() > limit) {
                limit = frame.maximum();
            }
            return true;
        }
        case SwQuicFrame::Type::MaxStreams:
            if (frame.isBidirectional()) {
                if (frame.maximum() > m_peerMaxStreamsBidi) {
                    m_peerMaxStreamsBidi = frame.maximum();
                }
            } else if (frame.maximum() > m_peerMaxStreamsUni) {
                m_peerMaxStreamsUni = frame.maximum();
            }
            return true;
        case SwQuicFrame::Type::ResetStream:
            return processResetStreamFrame_(frame, error);
        case SwQuicFrame::Type::ResetStreamAt:
            if (!m_localParams.resetStreamAt) {
                setError_(error, "Peer sent RESET_STREAM_AT without negotiation");
                return false;
            }
            return processResetStreamAtFrame_(frame, error);
        case SwQuicFrame::Type::StopSending: {
            if (isRetiredPeerBidirectional_(frame.streamId())) {
                return true;
            }
            if (!validatePeerSendControlStream_(frame.streamId(), error)) {
                return false;
            }
            // The peer no longer wants this stream: enter Reset Sent and keep
            // retransmitting RESET_STREAM (not stale STREAM data) until ACKed.
            const bool reset = resetStream(frame.streamId(), frame.errorCode(), error);
            if (reset) {
                markStreamTouched_(frame.streamId());
            }
            return reset;
        }
        case SwQuicFrame::Type::NewToken:
            if (m_newTokens.size() < kMaxNewTokens_()) {
                m_newTokens.push_back(frame.data());
            }
            return true;
        case SwQuicFrame::Type::NewConnectionId: {
            if (frame.connectionId().isEmpty() ||
                frame.retirePriorTo() > frame.sequenceNumber()) {
                setError_(error, "Invalid NEW_CONNECTION_ID frame");
                return false;
            }
            for (SwMap<std::uint64_t, SwByteArray>::iterator it =
                     m_peerIssuedConnectionIds.begin();
                 it != m_peerIssuedConnectionIds.end();) {
                if (it->first < frame.retirePriorTo()) {
                    const std::uint64_t retired = it->first;
                    SwMap<std::uint64_t, SwByteArray>::iterator doomed = it;
                    ++it;
                    m_peerIssuedConnectionIds.erase(doomed);
                    m_peerStatelessResetTokens.erase(retired);
                    space_(Level::Application).pendingFrames.push_back(
                        SwQuicFrame::retireConnectionId(retired));
                } else {
                    ++it;
                }
            }
            SwMap<std::uint64_t, SwByteArray>::iterator existing =
                m_peerIssuedConnectionIds.find(frame.sequenceNumber());
            if (existing != m_peerIssuedConnectionIds.end()) {
                if (existing->second != frame.connectionId()) {
                    setError_(error, "NEW_CONNECTION_ID sequence changed its connection ID");
                    return false;
                }
                SwMap<std::uint64_t, SwByteArray>::iterator existingToken =
                    m_peerStatelessResetTokens.find(frame.sequenceNumber());
                if (existingToken != m_peerStatelessResetTokens.end() &&
                    existingToken->second != frame.statelessResetToken()) {
                    setError_(error, "NEW_CONNECTION_ID sequence changed its reset token");
                    return false;
                }
                m_peerStatelessResetTokens[frame.sequenceNumber()] =
                    frame.statelessResetToken();
                adoptPeerConnectionIdIfRetired_(frame);
                return true;
            }
            if (static_cast<std::uint64_t>(m_peerIssuedConnectionIds.size()) >=
                m_localParams.activeConnectionIdLimit) {
                setError_(error, "Peer exceeded active_connection_id_limit");
                return false;
            }
            m_peerIssuedConnectionIds[frame.sequenceNumber()] = frame.connectionId();
            m_peerStatelessResetTokens[frame.sequenceNumber()] =
                frame.statelessResetToken();
            adoptPeerConnectionIdIfRetired_(frame);
            return true;
        }
        case SwQuicFrame::Type::RetireConnectionId:
            if (frame.sequenceNumber() >= m_nextLocalCidSequence) {
                setError_(error, "Peer retired a connection ID that was never issued");
                return false;
            }
            m_issuedConnectionIds.erase(frame.sequenceNumber());
            return true;
        case SwQuicFrame::Type::DataBlocked:
        case SwQuicFrame::Type::StreamDataBlocked:
        case SwQuicFrame::Type::StreamsBlocked:
            // Peer signals it is limited by our advertised windows; window
            // updates are driven by readStream() consumption.
            return true;
        case SwQuicFrame::Type::HandshakeDone:
            if (m_role == Role::Client) {
                setHandshakeConfirmed(true);
                dropLevelKeys(Level::Handshake);
            }
            return true;
        }

        setError_(error, "Unsupported QUIC frame in connection");
        return false;
    }

    Level levelForPathResponse_(Level receivedLevel) const {
        if (space_(Level::Application).hasTxKeys) {
            return Level::Application;
        }
        return receivedLevel;
    }

    void adoptPeerConnectionIdIfRetired_(const SwQuicFrame& frame) {
        if (m_currentPeerConnectionIdSequence >= frame.retirePriorTo()) return;
        SwQuicConnectionId replacement;
        if (SwQuicConnectionId::fromBytes(frame.connectionId(), replacement, nullptr)) {
            m_peerConnectionId = replacement;
            m_currentPeerConnectionIdSequence = frame.sequenceNumber();
        }
    }

    bool processAckFrame_(Level level,
                          const SwQuicFrame& frame,
                          std::uint64_t nowMs,
                          SwString* error) {
        Space_& space = space_(level);

        // Rebuild the inclusive acked ranges from the wire encoding
        // (RFC 9000 19.3.1).
        SwVector<SwPair<std::uint64_t, std::uint64_t> > ranges;
        std::uint64_t high = frame.largestAcknowledged();
        if (frame.firstAckRange() > high) {
            setError_(error, "QUIC ACK first range underflows");
            return false;
        }
        std::uint64_t low = high - frame.firstAckRange();
        ranges.push_back(SwMakePair(low, high));

        const SwVector<SwQuicFrame::AckRange>& wireRanges = frame.ackRanges();
        for (std::size_t i = 0; i < wireRanges.size(); ++i) {
            if (low < wireRanges[i].gap + 2) {
                setError_(error, "QUIC ACK range underflows");
                return false;
            }
            high = low - wireRanges[i].gap - 2;
            if (wireRanges[i].rangeLength > high) {
                setError_(error, "QUIC ACK range length underflows");
                return false;
            }
            low = high - wireRanges[i].rangeLength;
            ranges.push_back(SwMakePair(low, high));
        }

        // Decode ack delay: microseconds shifted by the peer's exponent.
        const std::uint64_t ackDelayMs =
            (frame.ackDelay() << m_peerAckDelayExponent) / 1000;

        SwQuicLossRecovery::AckResult result;
        if (!space.loss.onAckReceived(frame.largestAcknowledged(), ackDelayMs, ranges,
                                      nowMs, result, error)) {
            return false;
        }

        m_ptoCount = 0;
        space.probesPending = 0; // the peer responded: no probe owed

        for (std::size_t i = 0; i < result.newlyAcked.size(); ++i) {
            SwMap<std::uint64_t, SentRecord_>::iterator it =
                space.sentPackets.find(result.newlyAcked[i]);
            if (it == space.sentPackets.end()) {
                continue;
            }
            if (it->second.ackEliciting) {
                m_congestion.onPacketAcked(it->second.sentBytes, it->second.sentTimeMs);
            }
            for (std::size_t j = 0; j < it->second.retransmittable.size(); ++j) {
                const SwQuicFrame& acknowledged = it->second.retransmittable[j];
                if (acknowledged.type() == SwQuicFrame::Type::Stream) {
                    SwMap<std::uint64_t, SendStream_>::iterator sendIt =
                        m_sendStreams.find(acknowledged.streamId());
                    if (sendIt != m_sendStreams.end()) {
                        acknowledgeSendStreamRange_(
                            sendIt->second, acknowledged.offset(),
                            static_cast<std::uint64_t>(acknowledged.data().size()));
                        if (acknowledged.fin()) {
                            sendIt->second.finAcked = true;
                        }
                    }
                } else if (acknowledged.type() == SwQuicFrame::Type::ResetStream ||
                           acknowledged.type() == SwQuicFrame::Type::ResetStreamAt) {
                    SwMap<std::uint64_t, SendStream_>::iterator sendIt =
                        m_sendStreams.find(acknowledged.streamId());
                    if (sendIt != m_sendStreams.end()) {
                        sendIt->second.resetAcked = true;
                    }
                }
            }
            space.sentPackets.erase(it);
        }

        // Persistent congestion detection (RFC 9002 7.6): gather the send-time
        // span of lost ack-eliciting packets while their records still exist.
        bool anyLostAckEliciting = false;
        std::uint64_t earliestLostSent = 0;
        std::uint64_t latestLostSent = 0;
        std::size_t lostAckElicitingCount = 0;

        for (std::size_t i = 0; i < result.lost.size(); ++i) {
            SwMap<std::uint64_t, SentRecord_>::iterator it =
                space.sentPackets.find(result.lost[i]);
            if (it == space.sentPackets.end()) {
                continue;
            }
            if (it->second.ackEliciting) {
                m_congestion.onPacketsLost(it->second.sentBytes, it->second.sentTimeMs,
                                           nowMs);
                if (!anyLostAckEliciting || it->second.sentTimeMs < earliestLostSent) {
                    earliestLostSent = it->second.sentTimeMs;
                }
                if (!anyLostAckEliciting || it->second.sentTimeMs > latestLostSent) {
                    latestLostSent = it->second.sentTimeMs;
                }
                anyLostAckEliciting = true;
                ++lostAckElicitingCount;
            }
            // Retransmission: lost retransmittable frames go back in the queue.
            for (std::size_t j = 0; j < it->second.retransmittable.size(); ++j) {
                if (shouldRetransmitFrame_(it->second.retransmittable[j])) {
                    space.pendingFrames.push_back(it->second.retransmittable[j]);
                }
            }
            space.sentPackets.erase(it);
        }

        // RFC 9002 7.6.2: if two ack-eliciting packets spanning more than the
        // persistent congestion duration are lost with none acknowledged
        // between them, collapse the congestion window to the minimum.
        if (space.loss.hasRttSample() && lostAckElicitingCount >= 2) {
            const double pcDuration =
                space.loss.computePtoMsExact(m_peerMaxAckDelayMs) * kPersistentCongestionThreshold();
            if (static_cast<double>(latestLostSent - earliestLostSent) >= pcDuration) {
                m_congestion.onPersistentCongestion();
            }
        }

        // Retire only after loss processing. A reordered ACK can acknowledge a
        // FIN while declaring older DATA packets lost in the same event; those
        // bytes still need retransmission and acknowledgement before the send
        // side is terminal and MAX_STREAMS can advance.
        retireAcknowledgedPeerStreams_();
        refreshInFlightFlag_(space);
        return true;
    }

    static double kPersistentCongestionThreshold() { return 3.0; }

    bool processCryptoFrame_(Level level, const SwQuicFrame& frame, SwString* error) {
        const std::size_t index = static_cast<std::size_t>(level);
        if (!m_cryptoReassembly[index].receive(frame.offset(), frame.data(), false, error)) {
            return false;
        }
        SwByteArray contiguous = m_cryptoReassembly[index].readContiguous();
        if (!contiguous.isEmpty()) {
            if (m_cryptoAssembled[index].isEmpty()) {
                m_cryptoAssembled[index] = std::move(contiguous);
            } else {
                m_cryptoAssembled[index].append(contiguous);
            }
        }
        return true;
    }

    // RFC 9000 4.5 / 19.4: a received RESET_STREAM carries the stream's Final
    // Size. Validate it against data already received and any established final
    // size (FINAL_SIZE_ERROR on a decrease/change), and account the reset
    // stream's bytes in connection-level flow control so both peers stay in
    // sync, then credit them as consumed (the data is discarded).
    bool processResetStreamFrame_(const SwQuicFrame& frame, SwString* error) {
        if (isRetiredPeerBidirectional_(frame.streamId())) {
            return true;
        }
        if (!enforceStreamLimit_(frame.streamId(), error)) {
            return false;
        }

        SwQuicStreamFlowControl& streamFlow = streamFlowControl_(frame.streamId());
        const std::uint64_t previousHighest = streamFlow.highestReceivedOffset();
        const std::uint64_t finalSize = frame.finalSize();

        SwMap<std::uint64_t, std::uint64_t>::iterator establishedIt =
            m_streamFinalSize.find(frame.streamId());
        if (finalSize < previousHighest ||
            (establishedIt != m_streamFinalSize.end() && establishedIt->second != finalSize)) {
            setError_(error, "RESET_STREAM final size conflicts with received data (FINAL_SIZE_ERROR)");
            return false;
        }
        m_resetStreams[frame.streamId()] = frame.errorCode();
        m_streamFinalSize[frame.streamId()] = finalSize;

        // Count the delta up to the final size against connection flow control.
        if (finalSize > previousHighest) {
            if (!streamFlow.onDataReceived(previousHighest, finalSize - previousHighest, error) ||
                !m_connectionFlow.onDataReceived(finalSize - previousHighest, error)) {
                return false;
            }
        }
        // RESET_STREAM discards every byte up to Final Size. Credit exactly
        // what the application had not consumed yet (including buffered data),
        // rather than only the newly-accounted suffix or the same bytes twice.
        const std::uint64_t alreadyConsumed = streamFlow.bytesConsumed();
        if (finalSize > alreadyConsumed) {
            creditFlowControl_(frame.streamId(), finalSize - alreadyConsumed);
        }
        markStreamTouched_(frame.streamId());
        retireAcknowledgedPeerStreams_();
        return true;
    }

    // RESET_STREAM_AT is receive-terminal only after the application has
    // consumed the reliable prefix. Until then STREAM retransmissions up to
    // Reliable Size remain admissible and the reset signal is withheld.
    bool processResetStreamAtFrame_(const SwQuicFrame& frame,
                                    SwString* error) {
        if (frame.reliableSize() > frame.finalSize()) {
            setError_(error,
                      "RESET_STREAM_AT reliable size exceeds final size (FRAME_ENCODING_ERROR)");
            return false;
        }
        if (isRetiredPeerBidirectional_(frame.streamId())) return true;
        if (!enforceStreamLimit_(frame.streamId(), error)) return false;

        SwQuicStreamFlowControl& streamFlow = streamFlowControl_(frame.streamId());
        const std::uint64_t previousHighest = streamFlow.highestReceivedOffset();
        const std::uint64_t finalSize = frame.finalSize();
        SwMap<std::uint64_t, std::uint64_t>::iterator established =
            m_streamFinalSize.find(frame.streamId());
        if (finalSize < previousHighest ||
            (established != m_streamFinalSize.end() &&
             established->second != finalSize)) {
            setError_(error,
                      "RESET_STREAM_AT final size conflicts with received data (FINAL_SIZE_ERROR)");
            return false;
        }
        if (finalSize > previousHighest &&
            (!streamFlow.onDataReceived(previousHighest,
                                        finalSize - previousHighest, error) ||
             !m_connectionFlow.onDataReceived(
                 finalSize - previousHighest, error))) return false;
        m_streamFinalSize[frame.streamId()] = finalSize;

        PendingReliableReset_ pending;
        pending.errorCode = frame.errorCode();
        pending.finalSize = finalSize;
        pending.reliableSize = frame.reliableSize();
        m_pendingReliableResets[frame.streamId()] = pending;
        if (pending.reliableSize == 0) {
            m_resetStreams[frame.streamId()] = pending.errorCode;
            const std::uint64_t consumed = streamFlow.bytesConsumed();
            if (finalSize > consumed) {
                creditFlowControl_(frame.streamId(), finalSize - consumed);
            }
            m_pendingReliableResets.erase(frame.streamId());
        }
        markStreamTouched_(frame.streamId());
        retireAcknowledgedPeerStreams_();
        clearError_(error);
        return true;
    }

    bool processStreamFrame_(const SwQuicFrame& frame, SwString* error) {
        if (isRetiredPeerBidirectional_(frame.streamId())) {
            return true; // retransmission for a fully closed stream
        }
        // Enforce the receive-side stream limit before creating any state
        // (RFC 9000 4.6): a peer opening a stream beyond the advertised
        // initial_max_streams is a STREAM_LIMIT_ERROR, not an unbounded
        // allocation (memory-exhaustion DoS).
        if (!enforceStreamLimit_(frame.streamId(), error)) {
            return false;
        }
        // Data beyond a final size established by a prior RESET_STREAM or FIN is
        // a FINAL_SIZE_ERROR (RFC 9000 4.5).
        SwMap<std::uint64_t, std::uint64_t>::iterator finalIt =
            m_streamFinalSize.find(frame.streamId());
        if (finalIt != m_streamFinalSize.end()) {
            const std::uint64_t end = frame.offset() +
                                      static_cast<std::uint64_t>(frame.data().size());
            if (end > finalIt->second || (frame.fin() && end != finalIt->second)) {
                setError_(error, "STREAM data conflicts with established final size (FINAL_SIZE_ERROR)");
                return false;
            }
        }
        if (frame.fin()) {
            const std::uint64_t end = frame.offset() +
                                      static_cast<std::uint64_t>(frame.data().size());
            m_streamFinalSize[frame.streamId()] = end;
        }
        SwQuicStreamFlowControl& streamFlow = streamFlowControl_(frame.streamId());

        const std::uint64_t previousHighest = streamFlow.highestReceivedOffset();
        if (!streamFlow.onDataReceived(frame.offset(),
                                       static_cast<std::uint64_t>(frame.data().size()),
                                       error)) {
            return false;
        }

        // Connection-level flow control counts increases of the per-stream
        // highest offset (RFC 9000 4.1), not raw bytes, so retransmitted or
        // overlapping data is not double counted.
        const std::uint64_t newHighest = streamFlow.highestReceivedOffset();
        if (newHighest > previousHighest &&
            !m_connectionFlow.onDataReceived(newHighest - previousHighest, error)) {
            return false;
        }

        const std::size_t flowWindow = static_cast<std::size_t>(
            localStreamWindow_(frame.streamId()) >
                    static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())
                ? (std::numeric_limits<std::size_t>::max)()
                : localStreamWindow_(frame.streamId()));
        const std::size_t reassemblyBudget =
            flowWindow < m_maxStreamReassemblyBytes ? flowWindow : m_maxStreamReassemblyBytes;
        if (!m_streams.receiveFrame(frame, reassemblyBudget,
                                    m_maxStreamReassemblyFragments, error)) {
            return false;
        }
        markStreamTouched_(frame.streamId());
        return true;
    }

    void markStreamTouched_(std::uint64_t streamId) {
        for (std::size_t i = 0; i < m_touchedStreams.size(); ++i) {
            if (m_touchedStreams[i] == streamId) return;
        }
        m_touchedStreams.push_back(streamId);
    }

    bool validateSendStreamState_(std::uint64_t streamId, SwString* error) {
        const bool locallyInitiated =
            (m_role == Role::Client) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        const bool unidirectional = (streamId & 0x2U) != 0;
        if (!locallyInitiated) {
            if (unidirectional) {
                setError_(error, "Cannot send on a peer-initiated unidirectional stream");
                return false;
            }
            if (!m_streams.hasStream(streamId) &&
                m_resetStreams.find(streamId) == m_resetStreams.end()) {
                setError_(error, "Cannot send before the peer opens its bidirectional stream");
                return false;
            }
            return true;
        }

        if (m_hasPeerParams) {
            const std::uint64_t number = streamId >> 2;
            const std::uint64_t limit =
                unidirectional ? m_peerMaxStreamsUni : m_peerMaxStreamsBidi;
            if (number >= limit) {
                setError_(error, "Peer stream limit reached");
                return false;
            }
        }
        m_localOpenedStreams[streamId] = true;
        return true;
    }

    bool validatePeerSendControlStream_(std::uint64_t streamId, SwString* error) const {
        const bool locallyInitiated =
            (m_role == Role::Client) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        const bool unidirectional = (streamId & 0x2U) != 0;
        if (locallyInitiated) {
            if (m_localOpenedStreams.find(streamId) == m_localOpenedStreams.end()) {
                setError_(error, "Peer referenced a locally initiated stream that is not open");
                return false;
            }
            return true;
        }
        if (unidirectional ||
            (!m_streams.hasStream(streamId) &&
             m_resetStreams.find(streamId) == m_resetStreams.end())) {
            setError_(error, "Peer referenced a stream without a local send side");
            return false;
        }
        return true;
    }

    bool isPeerInitiatedBidirectional_(std::uint64_t streamId) const {
        const bool peerInitiated =
            (m_role == Role::Server) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        return peerInitiated && (streamId & 0x2U) == 0;
    }

    static void acknowledgeSendStreamRange_(SendStream_& stream,
                                            std::uint64_t offset,
                                            std::uint64_t length) {
        if (length == 0) return;
        const std::uint64_t maximum =
            (std::numeric_limits<std::uint64_t>::max)();
        std::uint64_t end = length > maximum - offset ? maximum : offset + length;
        if (end <= stream.contiguousAckedOffset) return;
        if (offset < stream.contiguousAckedOffset) {
            offset = stream.contiguousAckedOffset;
        }

        std::uint64_t first = offset;
        std::uint64_t last = end;
        SwMap<std::uint64_t, std::uint64_t>::iterator next =
            stream.ackedRanges.lower_bound(first);
        if (next != stream.ackedRanges.begin()) {
            SwMap<std::uint64_t, std::uint64_t>::iterator previous = next;
            --previous;
            if (previous->second >= first) {
                first = previous->first;
                if (previous->second > last) last = previous->second;
                stream.ackedRanges.erase(previous);
            }
        }
        next = stream.ackedRanges.lower_bound(first);
        while (next != stream.ackedRanges.end() && next->first <= last) {
            if (next->second > last) last = next->second;
            SwMap<std::uint64_t, std::uint64_t>::iterator doomed = next++;
            stream.ackedRanges.erase(doomed);
        }
        stream.ackedRanges[first] = last;

        // Collapse every range that now touches the acknowledged prefix. In
        // the common in-order case this leaves the auxiliary map empty.
        while (!stream.ackedRanges.empty()) {
            SwMap<std::uint64_t, std::uint64_t>::iterator begin =
                stream.ackedRanges.begin();
            if (begin->first > stream.contiguousAckedOffset) break;
            if (begin->second > stream.contiguousAckedOffset) {
                stream.contiguousAckedOffset = begin->second;
            }
            stream.ackedRanges.erase(begin);
        }
    }

    static bool isSendStreamRangeAcked_(const SendStream_& stream,
                                        std::uint64_t offset,
                                        std::uint64_t length) {
        if (length == 0) return true;
        const std::uint64_t maximum =
            (std::numeric_limits<std::uint64_t>::max)();
        const std::uint64_t end = length > maximum - offset ? maximum : offset + length;
        if (end <= stream.contiguousAckedOffset) return true;
        if (offset < stream.contiguousAckedOffset) {
            offset = stream.contiguousAckedOffset;
        }
        SwMap<std::uint64_t, std::uint64_t>::const_iterator next =
            stream.ackedRanges.upper_bound(offset);
        if (next == stream.ackedRanges.begin()) return false;
        --next;
        return next->first <= offset && next->second >= end;
    }

    bool shouldRetransmitFrame_(const SwQuicFrame& frame) const {
        const bool belongsToStream =
            frame.type() == SwQuicFrame::Type::Stream ||
            frame.type() == SwQuicFrame::Type::ResetStream ||
            frame.type() == SwQuicFrame::Type::ResetStreamAt ||
            frame.type() == SwQuicFrame::Type::StopSending ||
            frame.type() == SwQuicFrame::Type::MaxStreamData;
        if (!belongsToStream) {
            return true;
        }
        if (isRetiredPeerBidirectional_(frame.streamId())) {
            return false;
        }
        if (frame.type() == SwQuicFrame::Type::StopSending ||
            frame.type() == SwQuicFrame::Type::MaxStreamData) {
            return true;
        }
        SwMap<std::uint64_t, SendStream_>::const_iterator sendIt =
            m_sendStreams.find(frame.streamId());
        if (sendIt == m_sendStreams.end()) {
            return true;
        }
        if (frame.type() == SwQuicFrame::Type::ResetStream ||
            frame.type() == SwQuicFrame::Type::ResetStreamAt) {
            return !sendIt->second.resetAcked;
        }
        if (sendIt->second.resetQueued) {
            return sendIt->second.resetAt &&
                   frame.type() == SwQuicFrame::Type::Stream &&
                   frame.offset() < sendIt->second.resetReliableSize;
        }
        const bool dataAcked = isSendStreamRangeAcked_(
            sendIt->second, frame.offset(),
            static_cast<std::uint64_t>(frame.data().size()));
        return !dataAcked || (frame.fin() && !sendIt->second.finAcked);
    }

    void removePendingSendFramesForStream_(std::uint64_t streamId,
                                           bool removeResetFrames) {
        Space_& app = space_(Level::Application);
        SwDequeue<SwQuicFrame> retained;
        while (!app.pendingFrames.empty()) {
            SwQuicFrame frame = std::move(app.pendingFrames.front());
            app.pendingFrames.pop_front();
            const bool streamFrame = frame.type() == SwQuicFrame::Type::Stream &&
                                     frame.streamId() == streamId;
            const bool resetFrame = removeResetFrames &&
                                    (frame.type() == SwQuicFrame::Type::ResetStream ||
                                     frame.type() == SwQuicFrame::Type::ResetStreamAt) &&
                                    frame.streamId() == streamId;
            const bool obsoleteReceiveControl = removeResetFrames &&
                (frame.type() == SwQuicFrame::Type::StopSending ||
                 frame.type() == SwQuicFrame::Type::MaxStreamData) &&
                frame.streamId() == streamId;
            if (!streamFrame && !resetFrame && !obsoleteReceiveControl) {
                retained.push_back(std::move(frame));
            }
        }
        app.pendingFrames = std::move(retained);
    }

    void removePendingSendFramesAfterReliableSize_(std::uint64_t streamId,
                                                    std::uint64_t reliableSize) {
        Space_& app = space_(Level::Application);
        SwDequeue<SwQuicFrame> retained;
        while (!app.pendingFrames.empty()) {
            SwQuicFrame frame = std::move(app.pendingFrames.front());
            app.pendingFrames.pop_front();
            if (frame.type() != SwQuicFrame::Type::Stream ||
                frame.streamId() != streamId || frame.offset() < reliableSize) {
                retained.push_back(std::move(frame));
            }
        }
        app.pendingFrames = std::move(retained);
    }

    bool isRetiredPeerBidirectional_(std::uint64_t streamId) const {
        if (!isPeerInitiatedBidirectional_(streamId)) {
            return false;
        }
        const std::uint64_t number = streamId >> 2;
        SwMap<std::uint64_t, std::uint64_t>::const_iterator next =
            m_retiredPeerBidiRanges.upper_bound(number);
        if (next == m_retiredPeerBidiRanges.begin()) {
            return false;
        }
        --next;
        return number <= next->second;
    }

    void markRetiredPeerBidirectional_(std::uint64_t streamId) {
        std::uint64_t first = streamId >> 2;
        std::uint64_t last = first;
        SwMap<std::uint64_t, std::uint64_t>::iterator next =
            m_retiredPeerBidiRanges.lower_bound(first);
        if (next != m_retiredPeerBidiRanges.begin()) {
            SwMap<std::uint64_t, std::uint64_t>::iterator previous = next;
            --previous;
            if (previous->second == (std::numeric_limits<std::uint64_t>::max)() ||
                previous->second + 1 >= first) {
                first = previous->first;
                if (previous->second > last) last = previous->second;
                m_retiredPeerBidiRanges.erase(previous);
            }
        }
        next = m_retiredPeerBidiRanges.lower_bound(first);
        while (next != m_retiredPeerBidiRanges.end() &&
               (last == (std::numeric_limits<std::uint64_t>::max)() ||
                next->first <= last + 1)) {
            if (next->second > last) last = next->second;
            SwMap<std::uint64_t, std::uint64_t>::iterator doomed = next++;
            m_retiredPeerBidiRanges.erase(doomed);
        }
        m_retiredPeerBidiRanges[first] = last;
    }

    void retirePeerBidirectionalState_(std::uint64_t streamId) {
        markRetiredPeerBidirectional_(streamId);
        removePendingSendFramesForStream_(streamId, true);
        m_streams.removeStream(streamId);
        m_streamFlow.erase(streamId);
        m_peerStreamMaxData.erase(streamId);
        m_resetStreams.erase(streamId);
        m_pendingReliableResets.erase(streamId);
        m_streamFinalSize.erase(streamId);
    }

    void retireAcknowledgedPeerStreams_() {
        for (SwMap<std::uint64_t, SendStream_>::iterator it = m_sendStreams.begin();
             it != m_sendStreams.end();) {
            const SwQuicStream* receiveStream = m_streams.stream(it->first);
            const bool receiveTerminal =
                m_resetStreams.find(it->first) != m_resetStreams.end() ||
                (receiveStream && receiveStream->isReceiveComplete());
            const bool resetTerminal =
                it->second.resetQueued && it->second.resetAcked;
            const bool gracefulTerminal =
                !it->second.resetQueued && it->second.finQueued &&
                it->second.finAcked &&
                it->second.contiguousAckedOffset >= it->second.nextOffset;
            if (!it->second.peerCreditPending || !receiveTerminal ||
                (!resetTerminal && !gracefulTerminal)) {
                ++it;
                continue;
            }
            const std::uint64_t streamId = it->first;
            retirePeerBidirectionalState_(streamId);
            it = m_sendStreams.erase(it);
            if (m_localMaxStreamsBidi < (std::uint64_t(1) << 60)) {
                ++m_localMaxStreamsBidi;
                space_(Level::Application).pendingFrames.push_back(
                    SwQuicFrame::maxStreams(true, m_localMaxStreamsBidi));
            }
        }
        if (m_sendStreams.empty()) {
            m_streamSchedulingCursor = 0;
        } else {
            m_streamSchedulingCursor %= m_sendStreams.size();
        }
    }

    // RFC 9000 4.6: reject a peer-initiated stream whose number exceeds the
    // limit we advertised. Only applies to peer-initiated streams; streams we
    // already track are fine.
    bool enforceStreamLimit_(std::uint64_t streamId, SwString* error) {
        if (m_streams.hasStream(streamId)) {
            return true; // already accepted
        }
        const bool peerInitiated =
            (m_role == Role::Server) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        if (!peerInitiated) {
            if (m_localOpenedStreams.find(streamId) == m_localOpenedStreams.end()) {
                setError_(error, "Peer sent data on a locally initiated stream that was never opened");
                return false;
            }
            return true;
        }
        const bool unidirectional = (streamId & 0x2U) != 0;
        const std::uint64_t streamNumber = streamId >> 2; // 0-based index
        const std::uint64_t limit = unidirectional ? m_localMaxStreamsUni
                                                    : m_localMaxStreamsBidi;
        if (streamNumber >= limit) {
            m_closeErrorCode = 0x04; // STREAM_LIMIT_ERROR
            setError_(error, "Peer exceeded the advertised stream limit (STREAM_LIMIT_ERROR)");
            return false;
        }
        return true;
    }

    SwQuicStreamFlowControl& streamFlowControl_(std::uint64_t streamId) {
        SwMap<std::uint64_t, SwQuicStreamFlowControl>::iterator it =
            m_streamFlow.find(streamId);
        if (it == m_streamFlow.end()) {
            it = m_streamFlow.insert(SwMakePair(
                streamId,
                SwQuicStreamFlowControl(localStreamWindow_(streamId)))).first;
        }
        return it->second;
    }

    std::uint64_t localStreamWindow_(std::uint64_t streamId) const {
        const bool peerInitiated =
            (m_role == Role::Server) ? ((streamId & 0x1U) == 0)
                                     : ((streamId & 0x1U) == 1);
        const bool unidirectional = (streamId & 0x2U) != 0;
        if (unidirectional) {
            return m_localParams.initialMaxStreamDataUni;
        }
        return peerInitiated ? m_localParams.initialMaxStreamDataBidiRemote
                             : m_localParams.initialMaxStreamDataBidiLocal;
    }

    std::uint64_t peerStreamSendLimit_(std::uint64_t streamId) const {
        SwMap<std::uint64_t, std::uint64_t>::const_iterator it =
            m_peerStreamMaxData.find(streamId);
        std::uint64_t limit = 0;
        if (m_hasPeerParams) {
            const bool weInitiated =
                (m_role == Role::Client) ? ((streamId & 0x1U) == 0)
                                         : ((streamId & 0x1U) == 1);
            const bool unidirectional = (streamId & 0x2U) != 0;
            if (unidirectional) {
                limit = m_peerParams.initialMaxStreamDataUni;
            } else {
                limit = weInitiated ? m_peerParams.initialMaxStreamDataBidiRemote
                                    : m_peerParams.initialMaxStreamDataBidiLocal;
            }
        }
        if (it != m_peerStreamMaxData.end() && it->second > limit) {
            limit = it->second;
        }
        return limit;
    }

    void creditFlowControl_(std::uint64_t streamId, std::uint64_t bytes) {
        SwQuicStreamFlowControl& streamFlow = streamFlowControl_(streamId);
        streamFlow.consume(bytes);
        m_connectionFlow.consume(bytes);

        Space_& app = space_(Level::Application);
        if (streamFlow.shouldSendMaxStreamData()) {
            const std::uint64_t newLimit = streamFlow.applyMaxStreamData();
            app.pendingFrames.push_back(SwQuicFrame::maxStreamData(streamId, newLimit));
        }
        if (m_connectionFlow.shouldSendMaxData()) {
            const std::uint64_t newLimit = m_connectionFlow.applyMaxData();
            app.pendingFrames.push_back(SwQuicFrame::maxData(newLimit));
        }
    }

    void armDrainTimer_(std::uint64_t nowMs) {
        // Drain for three PTOs (RFC 9000 10.2).
        const std::uint64_t pto =
            space_(Level::Application).loss.computePtoMs(m_peerMaxAckDelayMs);
        m_drainDeadlineMs = nowMs + 3 * pto;
        m_hasDrainDeadline = true;
    }

    void refreshInFlightFlag_(Space_& space) {
        space.hasAckElicitingInFlight = false;
        for (SwMap<std::uint64_t, SentRecord_>::const_iterator it =
                 space.sentPackets.begin();
             it != space.sentPackets.end(); ++it) {
            if (it->second.ackEliciting) {
                space.hasAckElicitingInFlight = true;
                return;
            }
        }
    }

    // ---- send internals -------------------------------------------------------

    bool levelCanSend_(Level level) const {
        const Space_& space = space_(level);
        if (space.hasTxKeys) {
            return true;
        }
        return level == Level::Initial && m_plaintextInitialTx;
    }

    bool buildCloseDatagram_(std::uint64_t nowMs,
                             SwVector<SwByteArray>& outDatagrams,
                             SwString* error) {
        if (!m_closeQueued || m_closeSent) {
            if (error) {
                *error = SwString();
            }
            return true;
        }

        // Send CONNECTION_CLOSE at the highest level with send keys.
        Level level = Level::Initial;
        if (levelCanSend_(Level::Application)) {
            level = Level::Application;
        } else if (levelCanSend_(Level::Handshake)) {
            level = Level::Handshake;
        } else if (!levelCanSend_(Level::Initial)) {
            setError_(error, "QUIC connection has no keys to send CONNECTION_CLOSE");
            return false;
        }

        SwVector<SwQuicFrame> frames;
        if (m_closeIsApplication && level == Level::Application) {
            frames.push_back(SwQuicFrame::applicationClose(m_closeErrorCode, m_closeReason));
        } else if (m_closeIsApplication) {
            // An application CONNECTION_CLOSE (0x1d) MUST NOT be sent in an
            // Initial or Handshake packet (RFC 9000 10.2.3 / 12.5): convert it
            // to a transport close (0x1c) with APPLICATION_ERROR and no reason.
            frames.push_back(SwQuicFrame::connectionClose(0x0c /*APPLICATION_ERROR*/, 0,
                                                          SwString()));
        } else {
            frames.push_back(SwQuicFrame::connectionClose(m_closeErrorCode, 0, m_closeReason));
        }

        SwByteArray datagram;
        bool closeBlocked = false;
        if (!encodePacket_(level, frames, false, nowMs, datagram, closeBlocked, error)) {
            return false;
        }
        if (!closeBlocked) {
            outDatagrams.push_back(datagram);
        }
        m_closeSent = true;
        armDrainTimer_(nowMs);
        if (error) {
            *error = SwString();
        }
        return true;
    }

    bool buildDatagramsForLevel_(Level level,
                                 std::uint64_t nowMs,
                                 SwVector<SwByteArray>& outDatagrams,
                                 SwString* error) {
        if (!levelCanSend_(level)) {
            return true;
        }

        Space_& space = space_(level);

        // Application level: move sendable stream data into pending frames
        // within flow-control limits (congestion is applied per packet below).
        if (level == Level::Application) {
            stageStreamFrames_();
        }

        const bool ackDue = space.hasAckDeadline &&
                            space.ackDeadlineMs <= nowMs &&
                            !space.ackTracker.isEmpty();
        bool ackWanted = !space.ackTracker.isEmpty() &&
                         space.ackTracker.ackElicitingPending() &&
                         (ackDue || level != Level::Application ||
                          !space.pendingFrames.empty());

        while ((!space.pendingFrames.empty() || ackWanted) &&
               (m_maxDatagramsPerBuild == 0 ||
                outDatagrams.size() < m_maxDatagramsPerBuild) &&
               (m_pacingRatePerMillisecond == 0 || m_pacingTokens > 0)) {
            SwVector<SwQuicFrame> frames;
            std::size_t payloadSize = 0;
            bool ackEliciting = false;
            bool carriesGeneratedAck = false;

            if (ackWanted) {
                SwQuicFrame ackFrame = SwQuicFrame::ping();
                SwString ackError;
                // Report the real time we held the ACK, clamped to
                // max_ack_delay (RFC 9000 13.2.5), encoded with our exponent.
                std::uint64_t delayMs = 0;
                if (nowMs >= space.largestReceivedTimeMs) {
                    delayMs = nowMs - space.largestReceivedTimeMs;
                }
                if (delayMs > kLocalMaxAckDelayMs()) {
                    delayMs = kLocalMaxAckDelayMs();
                }
                const std::uint64_t encodedDelay =
                    (delayMs * 1000) >> kLocalAckDelayExponent();
                if (!space.ackTracker.buildAckFrame(ackFrame, encodedDelay, &ackError)) {
                    if (error) {
                        *error = ackError;
                    }
                    return false;
                }
                frames.push_back(ackFrame);
                payloadSize += frameEncodedSize_(ackFrame);
                carriesGeneratedAck = true;
            }

            const std::size_t payloadBudget = packetPayloadBudget_(level);
            while (!space.pendingFrames.empty()) {
                const SwQuicFrame& next = space.pendingFrames.front();
                const std::size_t nextSize = frameEncodedSize_(next);
                if (nextSize > payloadBudget ||
                    payloadSize > payloadBudget - nextSize) {
                    if (frames.empty()) {
                        setError_(error, "Pending QUIC frame exceeds the UDP payload ceiling");
                        return false;
                    }
                    break;
                }
                if (isAckElicitingFrame_(next.type())) {
                    const bool fits = m_congestion.canSend(payloadSize + nextSize + 64);
                    // Congestion control gates ack-eliciting payload, except a
                    // PTO probe, which must not be blocked (RFC 9002 6.2.4).
                    if (!fits && space.probesPending == 0) {
                        break;
                    }
                    if (!fits && space.probesPending > 0) {
                        --space.probesPending;
                    }
                    ackEliciting = true;
                }
                frames.push_back(std::move(space.pendingFrames.front()));
                payloadSize += nextSize;
                space.pendingFrames.pop_front();
            }

            if (frames.empty()) {
                break;
            }

            SwByteArray datagram;
            bool amplificationBlocked = false;
            if (!encodePacket_(level, frames, ackEliciting, nowMs, datagram,
                               amplificationBlocked, error)) {
                // Nothing reached the network. Restore every frame that came
                // from pendingFrames (including an explicitly queued ACK), but
                // not the ACK synthesized from ackTracker above. Its tracker,
                // deadline and receive counter have deliberately not been
                // committed yet, so the next flush will rebuild it exactly.
                for (std::size_t i = frames.size(); i-- > 0;) {
                    if (carriesGeneratedAck && i == 0) continue;
                    space.pendingFrames.push_front(std::move(frames[i]));
                }
                return false;
            }
            if (amplificationBlocked) {
                // Unvalidated path: cannot send more than 3x received
                // (RFC 9000 8.1 / 9.3). The packet was withheld with no sent-state
                // committed; restore its retransmittable frames to the queue so a
                // later flush (after a PATH_RESPONSE or more received bytes) resends
                // them, and stop this flush.
                for (std::size_t i = frames.size(); i-- > 0;) {
                    if (isRetransmittableFrame_(frames[i].type())) {
                        space.pendingFrames.push_front(frames[i]);
                    }
                }
                break;
            }
            const std::size_t wireSize = datagram.size();
            outDatagrams.push_back(std::move(datagram));
            if (m_pacingRatePerMillisecond > 0 && m_pacingTokens > 0) {
                --m_pacingTokens;
            }
            m_bytesSentThisPath += static_cast<std::uint64_t>(wireSize);

            // Commit receive-side ACK state only after encodePacket_ committed
            // the packet number/loss state and the datagram reached the output
            // batch. A protection, wire-ceiling or anti-amplification failure
            // must leave the ACK pending for a later flush.
            if (carriesGeneratedAck) {
                space.ackTracker.onAckSent();
                space.hasAckDeadline = false;
                space.ackElicitingSinceAck = 0; // RFC 9000 13.2.1 counter reset
                ackWanted = false;
            }

            if (space.pendingFrames.empty()) {
                break;
            }
            if (!m_congestion.canSend(64) && space.probesPending == 0) {
                break;
            }
        }

        return true;
    }

    static bool containsPathFrame_(const SwVector<SwQuicFrame>& frames) {
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::PathChallenge ||
                frames[i].type() == SwQuicFrame::Type::PathResponse) {
                return true;
            }
        }
        return false;
    }

    // RFC 9000 8.1: before a path is validated, an endpoint must not send more
    // than three times the bytes it has received on that path.
    bool amplificationBlocked_(std::size_t nextDatagramSize) const {
        if (m_pathValidated) {
            return false;
        }
        const std::uint64_t budget = 3 * m_bytesReceivedThisPath;
        return m_bytesSentThisPath + static_cast<std::uint64_t>(nextDatagramSize) > budget;
    }

    bool stageOneStreamFrame_(SwMap<std::uint64_t, SendStream_>::iterator it,
                              Space_& app) {
        SendStream_& stream = it->second;
        const std::size_t stored = static_cast<std::size_t>(stream.buffer.size());
        const std::size_t remaining =
            stream.bufferOffset < stored ? stored - stream.bufferOffset : 0;

        if (remaining > 0) {
            std::uint64_t streamLimit = peerStreamSendLimit_(it->first);
            std::uint64_t connectionLimit =
                m_connectionFlow.sendableBytes(m_totalStreamBytesSent);
            if (!m_hasPeerParams) {
                streamLimit = UINT64_MAX; // legacy sans-I/O tests
                connectionLimit = UINT64_MAX;
            }

            std::uint64_t allowance = 0;
            if (streamLimit > stream.nextOffset) {
                allowance = streamLimit - stream.nextOffset;
            }
            if (connectionLimit < allowance) allowance = connectionLimit;
            if (allowance == 0) return false;

            std::uint64_t chunkSize = static_cast<std::uint64_t>(remaining);
            if (chunkSize > allowance) chunkSize = allowance;
            if (chunkSize > kStreamChunkBytes()) chunkSize = kStreamChunkBytes();

            SwByteArray chunk = stream.buffer.mid(static_cast<int>(stream.bufferOffset),
                                                  static_cast<int>(chunkSize));
            stream.bufferOffset += static_cast<std::size_t>(chunkSize);
            m_bufferedStreamSendBytes =
                static_cast<std::size_t>(chunkSize) <= m_bufferedStreamSendBytes
                    ? m_bufferedStreamSendBytes - static_cast<std::size_t>(chunkSize)
                    : 0;
            const bool drained = stream.bufferOffset == stored;
            if (drained) {
                stream.buffer = SwByteArray();
                stream.bufferOffset = 0;
            }
            const bool fin = stream.finQueued && drained;
            app.pendingFrames.push_back(
                SwQuicFrame::stream(it->first, stream.nextOffset, std::move(chunk), fin));
            stream.nextOffset += chunkSize;
            m_totalStreamBytesSent += chunkSize;
            if (fin) stream.finSent = true;
            return true;
        }

        if (stream.finQueued && !stream.finSent) {
            app.pendingFrames.push_back(
                SwQuicFrame::stream(it->first, stream.nextOffset, SwByteArray(), true));
            stream.finSent = true;
            return true;
        }
        return false;
    }

    // Stage a bounded number of chunks in round-robin order. This both avoids
    // materialising an arbitrarily large pending-frame queue and prevents a
    // low stream id from monopolising the connection window.
    void stageStreamFrames_() {
        if (m_sendStreams.empty()) return;
        Space_& app = space_(Level::Application);
        // Repeated buildDatagrams() calls while congestion-blocked must not
        // keep materialising another 32 chunks each time. Include control and
        // retransmission frames in the cap: they have priority over new data.
        if (app.pendingFrames.size() >= kMaxStagedStreamFrames_()) return;
        const std::size_t stagingBudget =
            kMaxStagedStreamFrames_() - app.pendingFrames.size();
        const std::size_t streamCount = m_sendStreams.size();
        std::size_t cursor = m_streamSchedulingCursor % streamCount;
        SwMap<std::uint64_t, SendStream_>::iterator it = m_sendStreams.begin();
        for (std::size_t i = 0; i < cursor; ++i) ++it;

        std::size_t staged = 0;
        std::size_t withoutProgress = 0;
        while (staged < stagingBudget && withoutProgress < streamCount) {
            const bool progressed = stageOneStreamFrame_(it, app);
            ++it;
            cursor = (cursor + 1) % streamCount;
            if (it == m_sendStreams.end()) it = m_sendStreams.begin();
            if (progressed) {
                ++staged;
                withoutProgress = 0;
            } else {
                ++withoutProgress;
            }
        }
        m_streamSchedulingCursor = cursor;
    }

    static std::uint64_t kStreamChunkBytes() { return 1000; }
    static std::size_t kMaxStagedStreamFrames_() { return 32; }

    std::size_t packetPayloadBudgetForWire_(Level level, std::size_t wireBytes) const {
        // Datagram budget minus worst-case header and AEAD tag.
        std::size_t overhead = 0;
        if (level == Level::Application) {
            overhead = 1 + m_peerConnectionId.size() + kPacketNumberLength() +
                       SwQuicPacketProtector::kTagLength;
        } else {
            overhead = 1 + 4 + 1 + m_peerConnectionId.size() + 1 +
                       m_localConnectionId.size() + 4 /* length varint */ +
                       kPacketNumberLength() + SwQuicPacketProtector::kTagLength;
            if (level == Level::Initial) {
                overhead += 4 + m_initialToken.size();
            }
        }
        if (overhead + 64 >= wireBytes) {
            return 64;
        }
        return wireBytes - overhead;
    }

    std::size_t effectiveMaximumUdpPayloadSize_() const {
        std::size_t maximum = m_maximumUdpPayloadSize;
        if (maximum == 0 || maximum > kMaxUdpPayload()) maximum = kMaxUdpPayload();
        if (m_hasPeerParams &&
            m_peerParams.maxUdpPayloadSize >=
                SwQuicLimits::minimumInitialUdpPayloadBytes() &&
            m_peerParams.maxUdpPayloadSize < maximum) {
            maximum = static_cast<std::size_t>(m_peerParams.maxUdpPayloadSize);
        }
        return maximum;
    }

    std::size_t packetPayloadBudget_(Level level) const {
        return packetPayloadBudgetForWire_(level, effectiveMaximumUdpPayloadSize_());
    }

    static std::size_t frameEncodedSize_(const SwQuicFrame& frame) {
        if (frame.type() == SwQuicFrame::Type::Datagram) {
            return 1 + SwQuicVarIntCodec::encodedSize(
                           static_cast<std::uint64_t>(frame.data().size())) +
                   frame.data().size();
        }
        SwByteArray encoded;
        SwVector<SwQuicFrame> single;
        single.push_back(frame);
        SwString ignored;
        if (!SwQuicFrameCodec::encodeFrames(single, encoded, &ignored)) {
            return 64; // conservative fallback; encodePacket_ re-validates
        }
        return encoded.size();
    }

    bool encodePacket_(Level level,
                       SwVector<SwQuicFrame>& frames,
                       bool ackEliciting,
                       std::uint64_t nowMs,
                       SwByteArray& outDatagram,
                       bool& outAmplificationBlocked,
                       SwString* error,
                       std::uint64_t maxWireBytes = UINT64_MAX,
                       bool trackGlobalLoss = true) {
        outAmplificationBlocked = false;
        Space_& space = space_(level);

        SwByteArray payload;
        if (!SwQuicFrameCodec::encodeFrames(frames, payload, error)) {
            return false;
        }

        // Client Initial datagrams carrying ack-eliciting packets must fill
        // 1200 bytes (RFC 9000 14.1); pad the plaintext before protection.
        if (level == Level::Initial && m_role == Role::Client && ackEliciting) {
            const std::size_t budget = packetPayloadBudgetForWire_(
                Level::Initial, SwQuicLimits::minimumInitialUdpPayloadBytes());
            while (payload.size() < budget) {
                payload.append(static_cast<char>(0));
            }
        }

        // Datagrams carrying PATH_CHALLENGE/PATH_RESPONSE must be expanded to
        // 1200 bytes so path validation also proves the path's minimum MTU
        // (RFC 9000 8.2.1/8.2.2), unless the anti-amplification budget forbids.
        if (level == Level::Application && containsPathFrame_(frames)) {
            const std::size_t target = packetPayloadBudgetForWire_(
                Level::Application, SwQuicLimits::minimumInitialUdpPayloadBytes());
            while (payload.size() < target) {
                const std::uint64_t estimatedWire =
                    static_cast<std::uint64_t>(payload.size()) + 64U;
                if (amplificationBlocked_(payload.size() + 64) ||
                    estimatedWire > maxWireBytes) {
                    break; // do not exceed 3x received on an unvalidated path
                }
                payload.append(static_cast<char>(0));
            }
        }

        // AEAD sampling needs at least 4 bytes past the packet number.
        while (payload.size() < 4) {
            payload.append(static_cast<char>(0));
        }

        // Reserve the packet number but do not consume it until we know the
        // packet is actually going out: a datagram withheld by anti-amplification
        // must leave no phantom state (RFC 9002 — no bytes counted in flight, no
        // packet number burned) so retransmission stays consistent.
        const std::uint64_t packetNumber = space.nextTxPn;

        SwByteArray packet;
        const auto protectPayload = [&]() -> bool {
            packet.clear();
            if (level == Level::Application) {
                return SwQuicPacketProtector::protectShortHeader1Rtt(
                    space.txKeys, m_peerConnectionId, packetNumber,
                    kPacketNumberLength(), false, false, payload, packet, error);
            }
            if (space.hasTxKeys) {
                SwByteArray header;
                return buildLongHeaderBytes_(level, packetNumber, payload.size(), header, error) &&
                       SwQuicPacketProtector::protectLongHeader(
                           space.txKeys, packetNumber, kPacketNumberLength(), header,
                           payload, packet, error);
            }
            // Plaintext Initial mode (self-tests only).
            SwQuicPacketHeader header = SwQuicPacketHeader::makeInitial(m_peerConnectionId,
                                                                        m_localConnectionId);
            header.setVersion(m_version);
            header.setToken(m_initialToken);
            return header.setPacketNumberLength(kPacketNumberLength(), error) &&
                   header.setPacketNumber(packetNumber, error) &&
                   SwQuicPacketCodec::encodeInitialPacket(header, payload, packet, error);
        };
        if (!protectPayload()) {
            return false;
        }

        // The pessimistic header budget above can undershoot by a few bytes because QUIC length
        // fields are variable-width. Re-protect once with the exact missing plaintext bytes so the
        // RFC 9000 minimum is 1200 on wire, while ordinary packets may still use the 1450 ceiling.
        const std::size_t minimumWire = SwQuicLimits::minimumInitialUdpPayloadBytes();
        const bool mustReachMinimum =
            (level == Level::Initial && m_role == Role::Client && ackEliciting) ||
            (level == Level::Application && containsPathFrame_(frames) &&
             static_cast<std::uint64_t>(minimumWire) <= maxWireBytes &&
             !amplificationBlocked_(minimumWire));
        if (mustReachMinimum && packet.size() < minimumWire) {
            payload.append(SwByteArray(minimumWire - packet.size(), static_cast<char>(0)));
            if (!protectPayload()) {
                return false;
            }
        }

        // A connection/path ceiling is absolute even when it was lowered
        // after frames were queued. The caller restores the unsent frames on
        // this local failure; no packet number or loss state has been committed.
        if (packet.size() > effectiveMaximumUdpPayloadSize_()) {
            setError_(error, "Encoded QUIC packet exceeds the UDP payload ceiling");
            return false;
        }

        // Anti-amplification gate (RFC 9000 8.1) evaluated on the fully protected
        // datagram, BEFORE any state is committed. If the path is unvalidated and
        // this datagram would exceed 3x received, withhold it cleanly: the packet
        // number is not consumed and nothing is recorded in flight.
        if (amplificationBlocked_(packet.size()) ||
            static_cast<std::uint64_t>(packet.size()) > maxWireBytes) {
            outAmplificationBlocked = true;
            return true;
        }

        // Commit: consume the reserved packet number now that the packet ships.
        space.nextTxPn = packetNumber + 1;

        // Book-keeping for loss recovery / congestion control.
        SwQuicLossRecovery::SentPacket sent;
        sent.packetNumber = packetNumber;
        sent.sentTimeMs = nowMs;
        sent.ackEliciting = ackEliciting;
        sent.inFlight = ackEliciting;
        sent.sentBytes = packet.size();
        if (ackEliciting && trackGlobalLoss) {
            space.loss.onPacketSent(sent);
            m_congestion.onPacketSent(packet.size());
            space.lastAckElicitingSentMs = nowMs;
            space.hasAckElicitingInFlight = true;
            // Restart the idle timer when sending an ack-eliciting packet
            // (RFC 9000 10.1), so a send-heavy/receive-light flow is not closed.
            m_lastNetworkActivityMs = nowMs;
            m_hasNetworkActivity = true;
        }

        if (ackEliciting && trackGlobalLoss) {
            SentRecord_ record;
            record.sentBytes = packet.size();
            record.sentTimeMs = nowMs;
            record.ackEliciting = true;
            for (std::size_t i = 0; i < frames.size(); ++i) {
                if (isRetransmittableFrame_(frames[i].type())) {
                    record.retransmittable.push_back(frames[i]);
                }
            }
            space.sentPackets[packetNumber] = std::move(record);
        }

        outDatagram = std::move(packet);
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].type() == SwQuicFrame::Type::Datagram) {
                ++m_encodedDatagramFrames;
            }
        }
        return true;
    }

    static bool isRetransmittableFrame_(SwQuicFrame::Type type) {
        return type != SwQuicFrame::Type::Ack &&
               type != SwQuicFrame::Type::Padding &&
               type != SwQuicFrame::Type::Ping &&
               type != SwQuicFrame::Type::ConnectionClose &&
               type != SwQuicFrame::Type::Datagram &&
               type != SwQuicFrame::Type::PathResponse;
    }

    bool buildLongHeaderBytes_(Level level,
                               std::uint64_t packetNumber,
                               std::size_t plaintextPayloadSize,
                               SwByteArray& outHeader,
                               SwString* error) const {
        outHeader.clear();

        std::uint8_t firstByte = static_cast<std::uint8_t>(0xc0U | (kPacketNumberLength() - 1U));
        if (level == Level::Handshake) {
            firstByte = static_cast<std::uint8_t>(firstByte | 0x20U);
        }
        outHeader.append(static_cast<char>(firstByte));
        outHeader.append(static_cast<char>((m_version >> 24) & 0xffU));
        outHeader.append(static_cast<char>((m_version >> 16) & 0xffU));
        outHeader.append(static_cast<char>((m_version >> 8) & 0xffU));
        outHeader.append(static_cast<char>(m_version & 0xffU));
        outHeader.append(static_cast<char>(m_peerConnectionId.size()));
        outHeader.append(m_peerConnectionId.bytes());
        outHeader.append(static_cast<char>(m_localConnectionId.size()));
        outHeader.append(m_localConnectionId.bytes());

        if (level == Level::Initial) {
            if (!SwQuicVarIntCodec::encode(static_cast<std::uint64_t>(m_initialToken.size()),
                                           outHeader, error)) {
                return false;
            }
            outHeader.append(m_initialToken);
        }

        const std::uint64_t encodedLength =
            static_cast<std::uint64_t>(plaintextPayloadSize) +
            kPacketNumberLength() + SwQuicPacketProtector::kTagLength;
        if (!SwQuicVarIntCodec::encode(encodedLength, outHeader, error)) {
            return false;
        }

        for (std::uint8_t i = 0; i < kPacketNumberLength(); ++i) {
            const std::uint8_t shift =
                static_cast<std::uint8_t>((kPacketNumberLength() - 1 - i) * 8);
            outHeader.append(static_cast<char>((packetNumber >> shift) & 0xffU));
        }
        return true;
    }

    // ---- members -----------------------------------------------------------

    Role m_role;
    State m_state;
    std::uint32_t m_version;
    bool m_plaintextInitialTx;
    bool m_handshakeConfirmed;

    SwQuicConnectionId m_localConnectionId;
    SwQuicConnectionId m_peerConnectionId;
    SwByteArray m_initialToken;

    SwQuicTransportParameters m_localParams;
    SwQuicTransportParameters m_peerParams;
    bool m_hasPeerParams;
    std::uint64_t m_peerAckDelayExponent;
    std::uint64_t m_peerMaxAckDelayMs;

    std::uint64_t m_effectiveIdleTimeoutMs;
    std::uint64_t m_lastNetworkActivityMs;
    bool m_hasNetworkActivity;
    std::uint64_t m_drainDeadlineMs;
    bool m_hasDrainDeadline;
    std::size_t m_ptoCount;

    bool m_closeQueued;
    bool m_closeSent;
    std::uint64_t m_closeErrorCode;
    SwString m_closeReason;
    bool m_closeIsApplication;

    Space_ m_spaces[3];
    SwQuicCongestionControl m_congestion;
    SwQuicConnectionFlowControl m_connectionFlow;
    SwMap<std::uint64_t, SwQuicStreamFlowControl> m_streamFlow;
    SwMap<std::uint64_t, std::uint64_t> m_peerStreamMaxData;
    std::uint64_t m_peerMaxStreamsBidi;
    std::uint64_t m_peerMaxStreamsUni;
    std::uint64_t m_localMaxStreamsBidi = 100;
    std::uint64_t m_localMaxStreamsUni = 100;
    std::uint64_t m_totalStreamBytesSent = 0;

    SwQuicPacketHeader m_lastInitialHeader;
    SwQuicStreamMap m_streams;
    SwMap<std::uint64_t, SendStream_> m_sendStreams;
    SwMap<std::uint64_t, bool> m_localOpenedStreams;
    SwVector<std::uint64_t> m_touchedStreams;
    std::size_t m_streamSchedulingCursor = 0;
    std::size_t m_maxStreamReassemblyBytes = 1024 * 1024;
    std::size_t m_maxStreamReassemblyFragments = 1024;
    std::size_t m_bufferedStreamSendBytes = 0;
    std::size_t m_maxBufferedStreamSendBytes = 16U * 1024U * 1024U;
    SwMap<std::uint64_t, std::uint64_t> m_resetStreams;
    SwMap<std::uint64_t, PendingReliableReset_> m_pendingReliableResets;
    SwMap<std::uint64_t, std::uint64_t> m_streamFinalSize; // established final sizes
    // Compact inclusive stream-number ranges. A single low stream kept open
    // cannot force one tombstone per later completed request.
    SwMap<std::uint64_t, std::uint64_t> m_retiredPeerBidiRanges;
    SwDequeue<SwByteArray> m_datagrams;
    std::size_t m_receivedDatagramBytes = 0;
    std::size_t m_maxReceivedDatagrams = 1024;
    std::size_t m_maxReceivedDatagramBytes = 4U * 1024U * 1024U;
    std::uint64_t m_droppedReceivedDatagrams = 0;
    std::uint64_t m_queuedDatagramFrames = 0;
    std::uint64_t m_encodedDatagramFrames = 0;
    std::uint64_t m_receivedDatagramFrames = 0;
    std::size_t m_maxDatagramsPerBuild = 0;
    std::size_t m_maxPendingDatagramFrames = 0;
    std::size_t m_maximumUdpPayloadSize = SwQuicLimits::maximumUdpPayloadBytes();
    std::size_t m_pacingRatePerMillisecond = 0;
    std::size_t m_pacingMaximumBurst = 0;
    std::uint64_t m_pacingLastRefillMs = 0;
    std::size_t m_pacingTokens = 0;
    SwVector<SwByteArray> m_newTokens;
    SwMap<std::uint64_t, SwByteArray> m_peerIssuedConnectionIds;
    SwMap<std::uint64_t, SwByteArray> m_peerStatelessResetTokens;
    std::uint64_t m_currentPeerConnectionIdSequence = 0;
    SwQuicStream m_cryptoReassembly[3];
    SwByteArray m_cryptoAssembled[3];
    SwByteArray m_lastPathChallenge;
    std::size_t m_receivedPingCount;
    bool m_sawVersionNegotiation;
    bool m_sawRetry;

    // Path validation / migration state (RFC 9000 section 9).
    bool m_pathValidated = true;          // the established path is validated
    bool m_awaitingPathResponse = false;
    bool m_resetCcOnValidation = false;
    SwByteArray m_pathChallengeData;
    std::uint64_t m_bytesReceivedThisPath = 0;
    std::uint64_t m_bytesSentThisPath = 0;
    std::uint64_t m_nextLocalCidSequence = 1; // sequence 0 = the initial CID
    SwMap<std::uint64_t, SwByteArray> m_issuedConnectionIds;
    PathControlEvents* m_pathControlEventsOut = nullptr; // non-owning, seulement pendant receive
    bool m_inspectPathControlOnly = false;
    bool m_inspectedNonPathFrames = false;
    bool m_inspectedPathPacketReplay = false;
    bool m_hasLargestInspectedPathPacketNumber = false;
    std::uint64_t m_largestInspectedPathPacketNumber = 0;
    SwDequeue<std::uint64_t> m_inspectedPathPacketNumbers;
};

#endif
