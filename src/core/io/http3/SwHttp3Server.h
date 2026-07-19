#ifndef SWHTTP3SERVER_H
#define SWHTTP3SERVER_H

#include "SwByteArray.h"
#include "SwFile.h"
#include "SwString.h"
#include "SwVector.h"
#include "http/SwHttpMultipart.h"
#include "http/SwHttpTypes.h"
#include "http3/SwHttp3FrameCodec.h"
#include "http3/SwQpackDecoder.h"
#include "http3/SwQpackEncoder.h"
#include "http3/SwWebTransportSession.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicVarIntCodec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Stateful server-side HTTP/3 session (RFC 9114) bound to a SwQuicConnection.
//
// It closes the gap between the transport (SwQuicConnection) and the
// application: it opens the server control stream, exchanges SETTINGS,
// demultiplexes the client's unidirectional stream types, turns each client
// request stream (HEADERS + DATA) into an SwHttpRequest, hands it to a handler
// (which can be an SwHttpRouter adapter), and encodes the SwHttpResponse back
// into HEADERS + DATA on the same stream. QPACK is static-table only, which is
// always interoperable because the QPACK dynamic table is optional.
//
// Sans-IO driving: after the owning driver feeds datagrams into the
// SwQuicConnection, call pump() to advance the HTTP/3 state machine, then flush
// the connection's outgoing datagrams as usual.
class SwHttp3Server {
private:
    struct RequestStream_;
    struct ResponseStream_;

public:
    typedef std::function<SwHttpResponse(const SwHttpRequest&)> RequestHandler;
    typedef std::function<void(SwHttpResponse)> ResponseCallback;
    typedef std::function<void(const SwHttpRequest&, const ResponseCallback&)>
        AsyncRequestHandler;
    typedef std::function<void(std::uint64_t, SwHttpResponse)>
        AsyncResponseReadyHandler;
    typedef std::function<bool(std::size_t)> PendingBytesReserveHandler;
    typedef std::function<void(std::size_t)> PendingBytesReleaseHandler;
    // Called when a client opens a WebTransport session via Extended CONNECT
    // (RFC 9220); return true to accept (204/200) or false to reject (404).
    typedef std::function<bool(const SwHttpRequest&, std::uint64_t sessionId)>
        WebTransportHandler;
    typedef std::function<bool(const SwHttpRequest&,
                               std::uint64_t sessionId,
                               SwString& selectedProtocol)>
        WebTransportProtocolHandler;
    typedef std::function<void(std::uint64_t sessionId,
                               const SwByteArray& payload)>
        WebTransportDatagramHandler;
    typedef std::function<void(std::uint64_t sessionId,
                               std::uint64_t streamId,
                               const SwByteArray& payload,
                               bool bidirectional,
                               bool fin)>
        WebTransportStreamHandler;
    typedef std::function<void(std::uint64_t sessionId)>
        WebTransportSessionClosedHandler;

    static std::uint64_t streamTypeControl() { return 0x00; }
    static std::uint64_t settingEnableConnectProtocol() { return 0x08; }

    explicit SwHttp3Server(SwQuicConnection* connection)
        : m_connection(connection),
          m_nextServerUniStreamId(3),
          m_controlStreamOpened(false),
          m_peerSettingsReceived(false),
          m_peerH3Datagram(false),
          m_peerEnableConnectProtocol(false),
          m_peerEnableWebTransport(false) {
    }

    ~SwHttp3Server();

    void setRequestHandler(const RequestHandler& handler) { m_requestHandler = handler; }
    void setAsyncRequestHandler(const AsyncRequestHandler& handler) {
        m_asyncRequestHandler = handler;
    }
    void setAsyncResponseReadyHandler(const AsyncResponseReadyHandler& handler) {
        m_asyncResponseReadyHandler = handler;
    }
    void setWebTransportHandler(const WebTransportHandler& handler) {
        m_webTransportHandler = handler;
    }
    void setWebTransportProtocolHandler(
        const WebTransportProtocolHandler& handler) {
        m_webTransportProtocolHandler = handler;
    }
    void setWebTransportDatagramHandler(const WebTransportDatagramHandler& handler) {
        m_webTransportDatagramHandler = handler;
    }
    void setWebTransportStreamHandler(const WebTransportStreamHandler& handler) {
        m_webTransportStreamHandler = handler;
    }
    void setWebTransportSessionClosedHandler(
        const WebTransportSessionClosedHandler& handler) {
        m_webTransportSessionClosedHandler = handler;
    }

    bool hasWebTransportSession(std::uint64_t sessionId) const {
        return m_webTransportSessions.find(sessionId) !=
               m_webTransportSessions.end();
    }

    bool handleWebTransportDatagram(const SwByteArray& datagram,
                                    SwString* error = nullptr) {
        std::uint64_t quarterStreamId = 0;
        SwByteArray payload;
        if (!SwWebTransportSession::decodeDatagram(
                datagram, quarterStreamId, payload, error)) return false;
        const std::uint64_t sessionId = quarterStreamId * 4U;
        if (!hasWebTransportSession(sessionId)) {
            setError_(error, "HTTP datagram references an unknown WebTransport session");
            return false;
        }
        if (m_webTransportDatagramHandler) {
            m_webTransportDatagramHandler(sessionId, payload);
        }
        clearError_(error);
        return true;
    }

    bool sendWebTransportDatagram(std::uint64_t sessionId,
                                  const SwByteArray& payload,
                                  SwString* error = nullptr) {
        if (!hasWebTransportSession(sessionId)) {
            setError_(error, "Cannot send a datagram for an unknown WebTransport session");
            return false;
        }
        SwByteArray datagram;
        if (!SwWebTransportSession::encodeDatagram(
                sessionId, payload, datagram, error)) return false;
        return m_connection->queueDatagramFrame(std::move(datagram), error);
    }

    bool sendWebTransportStreamData(std::uint64_t sessionId,
                                    std::uint64_t streamId,
                                    const SwByteArray& payload,
                                    bool bidirectional,
                                    bool firstWrite,
                                    bool fin,
                                    SwString* error = nullptr) {
        if (!hasWebTransportSession(sessionId)) {
            setError_(error, "Cannot send a stream for an unknown WebTransport session");
            return false;
        }
        SwByteArray bytes;
        if (firstWrite) {
            const bool built = bidirectional
                ? SwWebTransportSession::buildBidiStreamHeader(sessionId, bytes, error)
                : SwWebTransportSession::buildUniStreamHeader(sessionId, bytes, error);
            if (!built) return false;
        }
        bytes.append(payload);
        return m_connection->sendStreamData(streamId, bytes, fin, error);
    }

    void setLimits(const SwHttpLimits& limits) {
        m_limits = limits;
    }

    // Optional driver-wide budget hooks. SwQuicHttp3Server uses these to make
    // maxPendingRequestBytesGlobal cover all of its live HTTP/3 connections,
    // while this session always enforces maxPendingRequestBytes locally.
    void setPendingRequestBudgetHandlers(
        const PendingBytesReserveHandler& reserveHandler,
        const PendingBytesReleaseHandler& releaseHandler) {
        m_pendingBytesReserveHandler = reserveHandler;
        m_pendingBytesReleaseHandler = releaseHandler;
    }

    bool peerSettingsReceived() const { return m_peerSettingsReceived; }
    bool peerH3Datagram() const { return m_peerH3Datagram; }
    bool peerEnableConnectProtocol() const { return m_peerEnableConnectProtocol; }
    bool peerEnableWebTransport() const { return m_peerEnableWebTransport; }
    std::size_t requestsHandled() const { return m_requestsHandled; }
    std::size_t pendingRequestBytes() const { return m_pendingRequestBytes; }
    std::size_t requestStreamStateCount() const { return m_requestStreams.size(); }
    std::size_t completedRequestRangeCount() const {
        return m_completedRequestRanges.size();
    }
    std::size_t responseStreamStateCount() const {
        return m_responseStreams.size();
    }
    SwString firstPendingRequestDiagnostic() const {
        if (m_requestStreams.empty()) return SwString("none");
        const RequestStream_& state = m_requestStreams.begin()->second;
        SwString out = SwString("headers=") +
            SwString(state.headersDecoded ? "1" : "0") +
            SwString(" buffer=") + SwString(std::to_string(state.buffer.size())) +
            SwString(" body=") + SwString(std::to_string(state.body.size())) +
            SwString(" fields=") + SwString(std::to_string(state.fields.size()));
        for (std::size_t i = 0; i < state.fields.size(); ++i) {
            const SwByteArray& name = state.fields[i].first;
            if (name.isEmpty() || !name.constData() || name.constData()[0] != ':') {
                continue;
            }
            out += SwString(" ") + SwString(name.toStdString()) + SwString("=") +
                   SwString(state.fields[i].second.toStdString());
        }
        return out;
    }

    // Feed a bounded, fair window of response DATA frames into QUIC. The
    // method is intentionally public for socket drivers: ACK-only datagrams
    // and QUIC timer callbacks do not necessarily touch an HTTP/3 stream, but
    // both can release transport send-buffer capacity.
    bool pumpResponseStreams(SwString* error = nullptr) {
        return pumpResponseStreams_(error);
    }

    // Completes a response produced after the request-dispatch call returned.
    // The owning QUIC driver invokes this on its affinity thread, then flushes
    // the newly queued response frames.
    bool completeAsyncResponse(std::uint64_t streamId,
                               SwHttpResponse& response,
                               SwHttpRequest* completedRequest = nullptr,
                               SwString* error = nullptr);

    // Open the control stream and emit our SETTINGS. Call once the QUIC
    // connection can send 1-RTT application data.
    bool start(SwString* error = nullptr) {
        return ensureControlStream_(error);
    }

    // Advance: read newly-arrived stream bytes and act on them.
    bool pump(SwString* error = nullptr) {
        if (!m_connection) {
            setError_(error, "HTTP/3 server has no QUIC connection");
            return false;
        }
        return pumpStreams(m_connection->streams().streamIds(), error);
    }

    // Hot-path variant for drivers that already know which streams the latest
    // datagram touched. It avoids scanning every historical stream per packet.
    bool pumpStreams(const SwVector<std::uint64_t>& ids,
                     SwString* error = nullptr) {
        if (!m_connection) {
            setError_(error, "HTTP/3 server has no QUIC connection");
            return false;
        }
        if (!ensureControlStream_(error)) {
            return false;
        }

        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint64_t streamId = ids[i];
            const bool webTransportConnect = hasWebTransportSession(streamId);
            std::map<std::uint64_t, RequestStream_>::iterator wtDataIt =
                m_requestStreams.find(streamId);
            const bool webTransportData =
                wtDataIt != m_requestStreams.end() &&
                wtDataIt->second.webTransportDataStream;

            if (!isUnidirectional_(streamId) &&
                (webTransportConnect || webTransportData) &&
                (m_connection->isStreamReceiveReset(streamId) ||
                 m_connection->isStreamSendReset(streamId))) {
                if (webTransportConnect) closeWebTransportSession_(streamId);
                if (webTransportData) {
                    if (m_webTransportStreamHandler) {
                        m_webTransportStreamHandler(
                            wtDataIt->second.webTransportSessionId,
                            streamId, SwByteArray(), true, true);
                    }
                    m_requestStreams.erase(wtDataIt);
                }
                continue;
            }

            // A rejected request is kept only as a compact tombstone. Do not
            // keep draining and flow-crediting an unbounded body after sending
            // STOP_SENDING. If the peer closes with FIN, discard the now-finite
            // remainder once so the receive direction can become terminal.
            if (!isUnidirectional_(streamId) &&
                isCompletedRequestStream_(streamId)) {
                // A response may still be feeding DATA into QUIC after the
                // request direction reached FIN. STOP_SENDING makes QUIC
                // queue RESET_STREAM; drop the source (and close its file)
                // instead of continuing to read data the peer cancelled.
                if (m_connection->isStreamSendReset(streamId)) {
                    std::map<std::uint64_t, ResponseStream_>::iterator response =
                        m_responseStreams.find(streamId);
                    if (response != m_responseStreams.end()) {
                        if (!m_connection->releasePeerBidirectionalStream(
                                streamId, error)) {
                            return false;
                        }
                        m_responseStreams.erase(response);
                    }
                }
                if (!m_connection->isStreamReceiveReset(streamId) &&
                    m_connection->streamHasFinalSize(streamId)) {
                    (void)m_connection->readStream(streamId);
                }
                continue;
            }

            // RESET_STREAM is receive-terminal without requiring a read. It
            // can arrive before any bytes (so no SwQuicStream exists) or while
            // an async request/buffered body is live; clean that state now and
            // reset the response direction as well.
            if (!isUnidirectional_(streamId) &&
                m_connection->isStreamReceiveReset(streamId)) {
                if (!abortRequestStream_(streamId, h3RequestCancelled_(), true,
                                         error)) {
                    return false;
                }
                continue;
            }


            // A peer STOP_SENDING cancels the response direction. Cancel any
            // still-buffering/async request too, ask the peer to stop its
            // request direction, and let the normal two-sided reset lifecycle
            // decide when stream credit can be returned.
            if (!isUnidirectional_(streamId) &&
                m_connection->isStreamSendReset(streamId)) {
                if (!abortRequestStream_(streamId, h3RequestCancelled_(), false,
                                         error)) {
                    return false;
                }
                continue;
            }

            const SwByteArray chunk = m_connection->readStream(streamId);

            // The FIN can arrive in a payload-less STREAM frame, so it must be
            // checked even when this read returned no new bytes: a bodyless GET
            // or a request half-closed after its HEADERS would otherwise never
            // be dispatched (RFC 9114 4.1 -- request completion is the FIN).
            const SwQuicStream* quicStream = m_connection->streams().stream(streamId);
            const bool fin = quicStream && quicStream->isReceiveComplete();

            if (webTransportConnect) {
                // The CONNECT stream carries WebTransport capsules, not an
                // application bidirectional stream. Capsule parsing remains
                // transport-owned and its FIN/reset terminates the session.
                if (fin) closeWebTransportSession_(streamId);
                continue;
            }

            if (isUnidirectional_(streamId)) {
                // A closed control stream is a fatal H3_CLOSED_CRITICAL_STREAM
                // (RFC 9114 6.2.1).
                if (fin && m_haveControlStream && streamId == m_controlStreamId) {
                    setError_(error, "HTTP/3 control stream closed (H3_CLOSED_CRITICAL_STREAM)");
                    return false;
                }
                if (chunk.isEmpty() && !fin) {
                    continue;
                }
                if (!handleUniStream_(streamId, chunk, fin, error)) {
                    return false;
                }
            } else {
                if (chunk.isEmpty() && !fin) {
                    continue;
                }
                if (!handleRequestStream_(streamId, chunk, fin, error)) {
                    return false;
                }
            }
        }

        // Stream IDs are commonly ordered with request stream 0 before the
        // client control stream 2. A CONNECT received in the same packet must
        // therefore get one deferred pass after SETTINGS has been consumed;
        // draft WebTransport forbids processing it before that SETTINGS frame.
        if (m_peerSettingsReceived) {
            for (std::size_t i = 0; i < ids.size(); ++i) {
                const std::uint64_t streamId = ids[i];
                if (isUnidirectional_(streamId) ||
                    hasWebTransportSession(streamId)) continue;
                std::map<std::uint64_t, RequestStream_>::iterator it =
                    m_requestStreams.find(streamId);
                if (it == m_requestStreams.end() || it->second.responded ||
                    it->second.awaitingResponse ||
                    it->second.webTransportDataStream) continue;
                const SwQuicStream* quicStream =
                    m_connection->streams().stream(streamId);
                const bool fin = quicStream && quicStream->isReceiveComplete();
                if (!handleRequestStream_(
                        streamId, SwByteArray(), fin, error)) return false;
            }
        }

        return pumpResponseStreams_(error);
    }

private:
    struct UniStream_ {
        bool typeKnown;
        std::uint64_t type;
        bool webTransportSessionKnown;
        std::uint64_t webTransportSessionId;
        SwByteArray buffer;
        std::size_t accountedBytes;
        UniStream_()
            : typeKnown(false), type(0), webTransportSessionKnown(false),
              webTransportSessionId(0), accountedBytes(0) {}
    };

    struct RequestStream_ {
        SwByteArray buffer;
        SwByteArray body;
        std::vector<std::pair<SwByteArray, SwByteArray> > fields;
        std::unique_ptr<SwHttpRequest> activeRequest;
        std::size_t accountedBytes;
        bool headersDecoded;
        bool trailersDecoded;
        bool responded;
        bool awaitingResponse;
        bool resourceLimitExceeded;
        bool webTransportDataStream;
        std::uint64_t webTransportSessionId;
        bool frameOrderError; // DATA before HEADERS, or a control frame on this stream
        RequestStream_()
            : accountedBytes(0),
              headersDecoded(false),
              trailersDecoded(false),
              responded(false),
              awaitingResponse(false),
              resourceLimitExceeded(false),
              webTransportDataStream(false),
              webTransportSessionId(0),
              frameOrderError(false) {}
    };

    struct ResponseStream_ {
        enum class Source {
            None,
            Body,
            ChunkParts,
            File
        };

        Source source = Source::None;
        SwByteArray body;
        std::size_t bodyOffset = 0;
        SwList<SwByteArray> chunkParts;
        std::size_t chunkPartIndex = 0;
        std::size_t chunkPartOffset = 0;
        std::unique_ptr<SwFile> file;
        std::size_t fileBytesRemaining = 0;
        std::size_t chunkBytes = 64U * 1024U;
        SwByteArray pendingFrame;
        bool hasPendingFrame = false;
        bool pendingFin = false;
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

    static bool isUnidirectional_(std::uint64_t streamId) {
        return (streamId & 0x2U) != 0;
    }

    static std::uint64_t h3ExcessiveLoad_() { return 0x107; }
    static std::uint64_t h3RequestCancelled_() { return 0x10c; }
    static std::uint64_t h3InternalError_() { return 0x102; }

    bool abortRequestStream_(std::uint64_t streamId,
                             std::uint64_t applicationErrorCode,
                             bool peerAlreadyReset,
                             SwString* error) {
        std::map<std::uint64_t, RequestStream_>::iterator it =
            m_requestStreams.find(streamId);
        if (it != m_requestStreams.end()) {
            it->second.responded = true;
            if (it->second.activeRequest) {
                swHttpCleanupMultipartTemporaryFiles(*it->second.activeRequest);
                it->second.activeRequest.reset();
            }
            releaseRequestStorage_(it->second);
        }

        bool stopped = true;
        if (!peerAlreadyReset) {
            stopped = m_connection->stopReceivingStream(
                streamId, applicationErrorCode, error);
        }
        const bool reset = stopped && m_connection->resetStream(
            streamId, applicationErrorCode, error);
        const bool released = reset && m_connection->releasePeerBidirectionalStream(
            streamId, error);
        if (reset && released) {
            markCompletedRequestStream_(streamId);
            m_requestStreams.erase(streamId);
            clearError_(error);
            return true;
        }
        return false;
    }

    bool isCompletedRequestStream_(std::uint64_t streamId) const {
        const std::uint64_t number = streamId >> 2;
        std::map<std::uint64_t, std::uint64_t>::const_iterator next =
            m_completedRequestRanges.upper_bound(number);
        if (next == m_completedRequestRanges.begin()) {
            return false;
        }
        --next;
        return number <= next->second;
    }

    void markCompletedRequestStream_(std::uint64_t streamId) {
        std::uint64_t first = streamId >> 2;
        std::uint64_t last = first;
        std::map<std::uint64_t, std::uint64_t>::iterator next =
            m_completedRequestRanges.lower_bound(first);
        if (next != m_completedRequestRanges.begin()) {
            std::map<std::uint64_t, std::uint64_t>::iterator previous = next;
            --previous;
            if (previous->second == (std::numeric_limits<std::uint64_t>::max)() ||
                previous->second + 1 >= first) {
                first = previous->first;
                if (previous->second > last) last = previous->second;
                m_completedRequestRanges.erase(previous);
            }
        }
        next = m_completedRequestRanges.lower_bound(first);
        while (next != m_completedRequestRanges.end() &&
               (last == (std::numeric_limits<std::uint64_t>::max)() ||
                next->first <= last + 1)) {
            if (next->second > last) last = next->second;
            std::map<std::uint64_t, std::uint64_t>::iterator doomed = next++;
            m_completedRequestRanges.erase(doomed);
        }
        m_completedRequestRanges[first] = last;
    }

    bool reservePendingBytes_(std::size_t bytes) {
        if (bytes == 0) return true;
        if (m_limits.maxPendingRequestBytes > 0 &&
            (m_pendingRequestBytes > m_limits.maxPendingRequestBytes ||
             bytes > m_limits.maxPendingRequestBytes - m_pendingRequestBytes)) {
            return false;
        }
        if (m_pendingBytesReserveHandler && !m_pendingBytesReserveHandler(bytes)) {
            return false;
        }
        m_pendingRequestBytes += bytes;
        return true;
    }

    bool appendUniStreamBytes_(UniStream_& stream,
                               const SwByteArray& chunk,
                               SwString* error) {
        const std::size_t bytes = static_cast<std::size_t>(chunk.size());
        if (bytes == 0) return true;
        if (bytes > (std::numeric_limits<std::size_t>::max)() -
                        stream.accountedBytes ||
            !reservePendingBytes_(bytes)) {
            setError_(error,
                      "HTTP/3 unidirectional/control stream buffering limit exceeded");
            return false;
        }
        try {
            stream.buffer.append(chunk);
        } catch (...) {
            releasePendingBytes_(bytes);
            setError_(error,
                      "Unable to buffer HTTP/3 unidirectional/control stream data");
            return false;
        }
        stream.accountedBytes += bytes;
        return true;
    }

    void synchronizeUniStreamAccounting_(UniStream_& stream) {
        const std::size_t retained = static_cast<std::size_t>(stream.buffer.size());
        if (retained < stream.accountedBytes) {
            releasePendingBytes_(stream.accountedBytes - retained);
            stream.accountedBytes = retained;
        }
    }

    void releaseUniStreamStorage_(UniStream_& stream) {
        releasePendingBytes_(stream.accountedBytes);
        stream.accountedBytes = 0;
        stream.buffer = SwByteArray();
    }

    void releasePendingBytes_(std::size_t bytes) {
        if (bytes == 0) return;
        const std::size_t released = bytes < m_pendingRequestBytes
            ? bytes
            : m_pendingRequestBytes;
        m_pendingRequestBytes -= released;
        if (released > 0 && m_pendingBytesReleaseHandler) {
            m_pendingBytesReleaseHandler(released);
        }
    }

    static std::size_t retainedRequestBytes_(const RequestStream_& state) {
        std::size_t retained = state.buffer.size() + state.body.size();
        for (std::size_t i = 0; i < state.fields.size(); ++i) {
            const std::size_t fieldBytes =
                state.fields[i].first.size() + state.fields[i].second.size();
            if (retained > (std::numeric_limits<std::size_t>::max)() - fieldBytes) {
                return (std::numeric_limits<std::size_t>::max)();
            }
            retained += fieldBytes;
        }
        return retained;
    }

    static bool multipartExpansionBytes_(const SwHttpRequest& request,
                                         std::size_t& outBytes) {
        outBytes = 0;
        if (!request.isMultipartFormData) return true;
        const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
            const std::size_t scalarBytes[] = {
                static_cast<std::size_t>(part.data.size()), part.name.size(),
                part.fileName.size(), part.contentType.size(),
                part.tempFilePath.size()
            };
            for (std::size_t scalar = 0;
                 scalar < sizeof(scalarBytes) / sizeof(scalarBytes[0]); ++scalar) {
                if (scalarBytes[scalar] > maximum - outBytes) return false;
                outBytes += scalarBytes[scalar];
            }
            for (SwMap<SwString, SwString>::const_iterator header =
                     part.headers.begin();
                 header != part.headers.end(); ++header) {
                if (header.key().size() > maximum - outBytes) return false;
                outBytes += header.key().size();
                if (header.value().size() > maximum - outBytes) return false;
                outBytes += header.value().size();
            }
        }
        for (SwMap<SwString, SwString>::const_iterator it =
                 request.formFields.begin();
             it != request.formFields.end(); ++it) {
            const std::size_t keyBytes = it.key().size();
            const std::size_t valueBytes = it.value().size();
            if (keyBytes > maximum - outBytes) return false;
            outBytes += keyBytes;
            if (valueBytes > maximum - outBytes) return false;
            outBytes += valueBytes;
        }
        return true;
    }

    bool reserveMultipartParseBudget_(RequestStream_& state,
                                      const SwHttpRequest& request,
                                      std::size_t& reservedBytes) {
        reservedBytes = 0;
        const SwString contentType =
            request.headers.value(SwString("content-type")).trimmed().toLower();
        if (!contentType.startsWith(SwString("multipart/form-data"))) {
            return true;
        }

        // The non-streaming parser can simultaneously retain its input ring,
        // parsed parts/metadata and a form-field projection. All are derived
        // from the original body, so three body lengths conservatively cover
        // the transient expansion before any allocation is attempted.
        const std::size_t bodyBytes =
            static_cast<std::size_t>(request.body.size());
        const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        if (bodyBytes > maximum / 3U) return false;
        reservedBytes = bodyBytes * 3U;
        if (reservedBytes > maximum - state.accountedBytes ||
            !reservePendingBytes_(reservedBytes)) {
            reservedBytes = 0;
            return false;
        }
        state.accountedBytes += reservedBytes;
        return true;
    }

    bool reconcileMultipartParseBudget_(RequestStream_& state,
                                        const SwHttpRequest& request,
                                        std::size_t reservedBytes) {
        std::size_t expansion = 0;
        if (!multipartExpansionBytes_(request, expansion)) {
            return false;
        }
        if (expansion > reservedBytes) {
            const std::size_t extra = expansion - reservedBytes;
            if (extra > (std::numeric_limits<std::size_t>::max)() -
                            state.accountedBytes ||
                !reservePendingBytes_(extra)) {
                return false;
            }
            state.accountedBytes += extra;
        } else if (reservedBytes > expansion) {
            const std::size_t unused = reservedBytes - expansion;
            releasePendingBytes_(unused);
            state.accountedBytes = unused < state.accountedBytes
                ? state.accountedBytes - unused
                : 0;
        }
        return true;
    }

    void releaseRequestStorage_(RequestStream_& state) {
        releasePendingBytes_(state.accountedBytes);
        state.accountedBytes = 0;
        // Assignment/swap releases capacity too; clear() alone intentionally
        // retains it in SwByteArray/std::vector and caused completed uploads to
        // pin their peak allocation for the lifetime of the connection.
        state.buffer = SwByteArray();
        state.body = SwByteArray();
        std::vector<std::pair<SwByteArray, SwByteArray> >().swap(state.fields);
    }

    void markResourceLimitExceeded_(RequestStream_& state) {
        releaseRequestStorage_(state);
        state.resourceLimitExceeded = true;
    }

    bool synchronizeRequestAccounting_(RequestStream_& state) {
        const std::size_t retained = retainedRequestBytes_(state);
        if (retained > state.accountedBytes) {
            const std::size_t extra = retained - state.accountedBytes;
            if (!reservePendingBytes_(extra)) {
                markResourceLimitExceeded_(state);
                return false;
            }
            state.accountedBytes += extra;
        } else if (retained < state.accountedBytes) {
            const std::size_t released = state.accountedBytes - retained;
            releasePendingBytes_(released);
            state.accountedBytes = retained;
        }
        return true;
    }

    bool finishRequestStream_(std::uint64_t streamId,
                              RequestStream_& state,
                              const SwHttpResponse& response,
                              bool finishStream,
                              SwString* error) {
        const bool sent = finishStream
            ? beginResponseStream_(streamId, response, error)
            : sendResponseHeadersOnly_(streamId, response, false, error);
        state.responded = true;
        releaseRequestStorage_(state);
        if (finishStream) {
            markCompletedRequestStream_(streamId);
            m_requestStreams.erase(streamId);
        }
        return sent;
    }

    static bool statusForbidsResponseBody_(int status) {
        return (status >= 100 && status < 200) || status == 204 ||
               status == 205 || status == 304;
    }

    static std::size_t responseChunkBytes_(std::size_t requested) {
        std::size_t bytes = requested > 0 ? requested : 64U * 1024U;
        if (bytes < 4096U) bytes = 4096U;
        if (bytes > 64U * 1024U) bytes = 64U * 1024U;
        return bytes;
    }

    static bool chunkPartsHaveBytes_(const ResponseStream_& state) {
        for (std::size_t i = state.chunkPartIndex;
             i < state.chunkParts.size(); ++i) {
            const std::size_t offset = i == state.chunkPartIndex
                ? state.chunkPartOffset
                : 0;
            if (offset < state.chunkParts[i].size()) return true;
        }
        return false;
    }

    // HTTP/3 has no Transfer-Encoding framing. Even responses produced through
    // the HTTP/1.x-style chunk API therefore have a deterministic representation
    // length on the wire: explicit parts win, with body as the legacy fallback
    // only when no parts were supplied.
    static bool responseRepresentationLength_(const SwHttpResponse& response,
                                              std::size_t& outLength) {
        if (response.hasFile) {
            outLength = response.fileLength;
            return true;
        }
        if (!response.useChunkedTransfer || response.chunkedParts.isEmpty()) {
            outLength = static_cast<std::size_t>(response.body.size());
            return true;
        }

        outLength = 0;
        const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        for (std::size_t i = 0; i < response.chunkedParts.size(); ++i) {
            const std::size_t bytes =
                static_cast<std::size_t>(response.chunkedParts[i].size());
            if (bytes > maximum - outLength) {
                return false;
            }
            outLength += bytes;
        }
        return true;
    }

    bool encodeResponseHeaders_(const SwHttpResponse& response,
                                SwByteArray& out,
                                SwString* error) const {
        std::vector<std::pair<SwByteArray, SwByteArray> > fields;
        pushStatus_(fields, response.status);
        if (!statusForbidsResponseBody_(response.status)) {
            std::size_t representationLength = 0;
            if (responseRepresentationLength_(response, representationLength)) {
                fields.push_back(std::make_pair(
                    SwByteArray("content-length"),
                    SwByteArray(
                        SwString::number(
                            static_cast<unsigned long long>(representationLength))
                            .toStdString())));
            }
        }

        std::uint64_t fieldSectionBytes = 0;
        if (!fieldSectionSize_(fields, fieldSectionBytes) ||
            fieldSectionBytes > m_peerMaxFieldSectionSize) {
            setError_(error,
                      "HTTP/3 mandatory response fields exceed the peer limit");
            return false;
        }

        std::vector<std::pair<SwByteArray, SwByteArray> > applicationFields;
        appendHeaderMap_(response.headers, applicationFields);
        for (std::size_t i = 0; i < applicationFields.size(); ++i) {
            std::uint64_t lineBytes = 0;
            if (!fieldLineSize_(applicationFields[i], lineBytes)) continue;
            if (lineBytes > m_peerMaxFieldSectionSize - fieldSectionBytes) {
                continue; // never exceed SETTINGS_MAX_FIELD_SECTION_SIZE
            }
            fieldSectionBytes += lineBytes;
            fields.push_back(applicationFields[i]);
        }

        SwByteArray headerBlock;
        if (!SwQpackEncoder::encodeFieldSection(fields, headerBlock, error)) {
            return false;
        }
        return SwHttp3FrameCodec::encodeFrame(
            SwHttp3Frame::headers(headerBlock), out, error);
    }

    bool beginResponseStream_(std::uint64_t streamId,
                              const SwHttpResponse& originalResponse,
                              SwString* error) {
        if (m_responseStreams.find(streamId) != m_responseStreams.end()) {
            setError_(error, "HTTP/3 response stream is already active");
            return false;
        }

        // Keep the application's response as the source of truth. Files remain
        // incremental; in-memory body/part payloads are copied into the stream
        // state so asynchronous response objects can safely go out of scope.
        const SwHttpResponse* response = &originalResponse;
        SwHttpResponse replacementResponse;
        ResponseStream_ stream;
        const bool suppressBody = response->headOnly ||
            statusForbidsResponseBody_(response->status);

        if (!suppressBody && response->hasFile) {
            stream.file.reset(new SwFile(response->filePath));
            if (!stream.file->openBinary(SwFile::Read)) {
                // No response bytes have been queued yet, so a deterministic
                // 404 can still replace an unavailable file safely.
                replacementResponse =
                    swHttpTextResponse(404, SwString("Not Found"));
                response = &replacementResponse;
                stream.file.reset();
            } else {
                try {
                    stream.file->seek(
                        static_cast<std::streampos>(response->fileOffset));
                } catch (...) {
                    stream.file.reset();
                    replacementResponse = swHttpTextResponse(
                        500, SwString("Internal Server Error"));
                    response = &replacementResponse;
                }
            }
        }

        const bool normalizedSuppressBody = response->headOnly ||
            statusForbidsResponseBody_(response->status);
        if (!normalizedSuppressBody && response->hasFile && stream.file) {
            stream.source = response->fileLength > 0
                ? ResponseStream_::Source::File
                : ResponseStream_::Source::None;
            stream.fileBytesRemaining = response->fileLength;
            stream.chunkBytes = responseChunkBytes_(response->streamChunkBytes);
            if (stream.fileBytesRemaining == 0) stream.file.reset();
        } else if (!normalizedSuppressBody && response->useChunkedTransfer) {
            stream.chunkBytes = responseChunkBytes_(response->streamChunkBytes);
            // Match SwHttpSession exactly: explicit chunk parts are the payload;
            // body is used only as the fallback when the part list is empty.
            if (!response->chunkedParts.isEmpty()) {
                stream.chunkParts = response->chunkedParts;
                stream.source = ResponseStream_::Source::ChunkParts;
                if (!chunkPartsHaveBytes_(stream)) {
                    stream.source = ResponseStream_::Source::None;
                    stream.chunkParts.clear();
                }
            } else if (!response->body.isEmpty()) {
                stream.body = response->body;
                stream.source = ResponseStream_::Source::Body;
            }
        } else if (!normalizedSuppressBody && !response->body.isEmpty()) {
            stream.source = ResponseStream_::Source::Body;
            stream.body = response->body;
            stream.chunkBytes = responseChunkBytes_(response->streamChunkBytes);
        }

        if (!encodeResponseHeaders_(*response, stream.pendingFrame, error)) {
            return false;
        }
        stream.hasPendingFrame = true;
        stream.pendingFin = stream.source == ResponseStream_::Source::None;
        m_responseStreams.insert(std::make_pair(streamId, std::move(stream)));
        return pumpResponseStreams_(error);
    }

    bool failResponseStream_(std::uint64_t streamId,
                             SwString* error) {
        SwString resetError;
        const bool reset = m_connection->resetStream(
            streamId, h3InternalError_(), &resetError);
        const bool released = reset && m_connection->releasePeerBidirectionalStream(
            streamId, &resetError);
        m_responseStreams.erase(streamId);
        if (!reset || !released) {
            if (error) *error = resetError;
            return false;
        }
        clearError_(error);
        return true;
    }

    bool encodeDataFrame_(const SwByteArray& payload,
                          ResponseStream_& state,
                          bool fin,
                          SwString* error) {
        state.pendingFrame = SwByteArray();
        if (!SwHttp3FrameCodec::encodeFrame(
                SwHttp3Frame::data(payload), state.pendingFrame, error)) {
            return false;
        }
        state.hasPendingFrame = true;
        state.pendingFin = fin;
        return true;
    }

    bool prepareNextResponseFrame_(std::uint64_t streamId,
                                   ResponseStream_& state,
                                   std::size_t maxPayloadBytes,
                                   SwString* error) {
        if (state.hasPendingFrame) return true;

        if (state.source == ResponseStream_::Source::None) {
            state.pendingFrame = SwByteArray();
            state.hasPendingFrame = true;
            state.pendingFin = true;
            return true;
        }

        if (state.source == ResponseStream_::Source::Body) {
            if (state.bodyOffset >= state.body.size()) {
                state.body = SwByteArray();
                state.source = ResponseStream_::Source::None;
                return prepareNextResponseFrame_(
                    streamId, state, maxPayloadBytes, error);
            }
            const std::size_t remaining = state.body.size() - state.bodyOffset;
            const std::size_t block = (std::min)(
                remaining, (std::min)(state.chunkBytes, maxPayloadBytes));
            const SwByteArray payload = state.body.mid(
                static_cast<int>(state.bodyOffset), static_cast<int>(block));
            state.bodyOffset += block;
            const bool bodyComplete = state.bodyOffset == state.body.size();
            const bool fin = bodyComplete;
            if (bodyComplete) {
                state.body = SwByteArray();
                state.bodyOffset = 0;
                state.source = ResponseStream_::Source::None;
            }
            return encodeDataFrame_(payload, state, fin, error);
        }

        if (state.source == ResponseStream_::Source::ChunkParts) {
            while (state.chunkPartIndex < state.chunkParts.size() &&
                   state.chunkPartOffset >=
                       state.chunkParts[state.chunkPartIndex].size()) {
                state.chunkParts[state.chunkPartIndex] = SwByteArray();
                ++state.chunkPartIndex;
                state.chunkPartOffset = 0;
            }
            if (state.chunkPartIndex >= state.chunkParts.size()) {
                state.chunkParts.clear();
                state.source = ResponseStream_::Source::None;
                return prepareNextResponseFrame_(
                    streamId, state, maxPayloadBytes, error);
            }

            const SwByteArray& part = state.chunkParts[state.chunkPartIndex];
            const std::size_t remaining = part.size() - state.chunkPartOffset;
            const std::size_t block = (std::min)(
                remaining, (std::min)(state.chunkBytes, maxPayloadBytes));
            const SwByteArray payload = part.mid(
                static_cast<int>(state.chunkPartOffset), static_cast<int>(block));
            state.chunkPartOffset += block;
            const bool fin = !chunkPartsHaveBytes_(state);
            if (fin) {
                state.chunkParts.clear();
                state.chunkPartIndex = 0;
                state.chunkPartOffset = 0;
                state.source = ResponseStream_::Source::None;
            }
            return encodeDataFrame_(payload, state, fin, error);
        }

        if (!state.file || state.fileBytesRemaining == 0) {
            state.file.reset();
            state.source = ResponseStream_::Source::None;
            return prepareNextResponseFrame_(
                streamId, state, maxPayloadBytes, error);
        }
        const std::size_t block = (std::min)(
            state.fileBytesRemaining,
            (std::min)(state.chunkBytes, maxPayloadBytes));
        SwByteArray payload;
        try {
            payload = state.file->read(static_cast<std::int64_t>(block));
        } catch (...) {
            return failResponseStream_(streamId, error);
        }
        if (payload.isEmpty() || payload.size() > state.fileBytesRemaining) {
            return failResponseStream_(streamId, error);
        }
        state.fileBytesRemaining -= payload.size();
        const bool fin = state.fileBytesRemaining == 0;
        if (fin) {
            state.file.reset();
            state.source = ResponseStream_::Source::None;
        }
        return encodeDataFrame_(payload, state, fin, error);
    }

    bool pumpOneResponseStream_(std::uint64_t streamId,
                                bool& progressed,
                                SwString* error) {
        progressed = false;
        std::map<std::uint64_t, ResponseStream_>::iterator it =
            m_responseStreams.find(streamId);
        if (it == m_responseStreams.end()) return true;

        const std::size_t available = m_connection->availableStreamSendBytes();
        const std::size_t unstaged =
            m_connection->unstagedStreamSendBytes(streamId);
        const std::size_t configuredMaximum =
            m_connection->maxBufferedStreamSendBytes();
        static const std::size_t kPerStreamUnstagedBytes = 64U * 1024U;
        const std::size_t perStreamMaximum =
            configuredMaximum > 0
                ? (std::min)(configuredMaximum, kPerStreamUnstagedBytes)
                : kPerStreamUnstagedBytes;
        std::size_t maxPayload = it->second.chunkBytes;
        if (!it->second.hasPendingFrame &&
            it->second.source != ResponseStream_::Source::None) {
            // A DATA frame uses at most nine bytes for its type/length. Keep a
            // conservative margin so reading a file chunk never creates a
            // second, unaccounted backlog beside QUIC's bounded send queue.
            static const std::size_t kFrameEnvelopeBytes = 16;
            const std::size_t perStreamAvailable =
                unstaged >= perStreamMaximum ? 0 : perStreamMaximum - unstaged;
            if (available <= kFrameEnvelopeBytes ||
                perStreamAvailable <= kFrameEnvelopeBytes) {
                if (configuredMaximum > 0 &&
                    configuredMaximum <= kFrameEnvelopeBytes) {
                    progressed = true;
                    return failResponseStream_(streamId, error);
                }
                clearError_(error);
                return true;
            }
            maxPayload = (std::min)(maxPayload,
                                    available - kFrameEnvelopeBytes);
            maxPayload = (std::min)(maxPayload,
                                    perStreamAvailable - kFrameEnvelopeBytes);
        }

        if (!prepareNextResponseFrame_(
                streamId, it->second, maxPayload, error)) {
            return false;
        }
        // A stream-local file failure erases the state after queueing RESET.
        it = m_responseStreams.find(streamId);
        if (it == m_responseStreams.end()) {
            progressed = true;
            return true;
        }

        ResponseStream_& state = it->second;
        const std::size_t frameBytes = state.pendingFrame.size();
        if (configuredMaximum > 0 && frameBytes > configuredMaximum) {
            progressed = true;
            return failResponseStream_(streamId, error);
        }
        // An oversized HEADERS frame is allowed when it is the sole unstaged
        // item; otherwise each stream gets a bounded share of the global send
        // budget. A peer withholding MAX_STREAM_DATA on one request therefore
        // cannot prevent another credited request from queuing its response.
        if (unstaged > 0 &&
            (unstaged >= perStreamMaximum ||
             frameBytes > perStreamMaximum - unstaged)) {
            clearError_(error);
            return true;
        }
        if (frameBytes > m_connection->availableStreamSendBytes()) {
            clearError_(error);
            return true;
        }

        const bool fin = state.pendingFin;
        if (!m_connection->sendStreamData(
                streamId, state.pendingFrame, fin, error)) {
            return false;
        }
        state.pendingFrame = SwByteArray();
        state.hasPendingFrame = false;
        state.pendingFin = false;
        progressed = true;

        if (fin) {
            if (!m_connection->releasePeerBidirectionalStream(streamId, error)) {
                return false;
            }
            m_responseStreams.erase(it);
        }
        return true;
    }

    bool pumpResponseStreams_(SwString* error) {
        if (!m_connection) {
            setError_(error, "HTTP/3 server has no QUIC connection");
            return false;
        }
        if (m_responseStreams.empty()) {
            clearError_(error);
            return true;
        }

        std::vector<std::uint64_t> ids;
        ids.reserve(m_responseStreams.size());
        for (std::map<std::uint64_t, ResponseStream_>::const_iterator it =
                 m_responseStreams.begin(); it != m_responseStreams.end(); ++it) {
            ids.push_back(it->first);
        }
        std::size_t cursor = m_responseSchedulingCursor % ids.size();
        std::size_t framesQueued = 0;
        static const std::size_t kFramesPerPump = 64;

        while (framesQueued < kFramesPerPump) {
            bool roundProgress = false;
            std::size_t scanCursor = cursor;
            for (std::size_t visited = 0;
                 visited < ids.size() && framesQueued < kFramesPerPump;
                 ++visited) {
                const std::uint64_t streamId = ids[scanCursor];
                scanCursor = (scanCursor + 1) % ids.size();
                bool progressed = false;
                if (!pumpOneResponseStream_(streamId, progressed, error)) {
                    return false;
                }
                if (progressed) {
                    ++framesQueued;
                    roundProgress = true;
                    // Persist the position after the last stream which really
                    // consumed capacity. Merely visiting blocked streams must
                    // not wrap the cursor back to the same winner on every ACK.
                    cursor = scanCursor;
                }
            }
            if (!roundProgress) break;
        }
        m_responseSchedulingCursor = cursor;
        clearError_(error);
        return true;
    }

    bool ensureControlStream_(SwString* error) {
        if (m_controlStreamOpened) {
            return true;
        }

        SwHttp3Frame::SettingList settings;
        settings.push_back(std::make_pair(SwHttp3Frame::settingQpackMaxTableCapacity(),
                                          std::uint64_t(0)));
        settings.push_back(std::make_pair(SwHttp3Frame::settingQpackBlockedStreams(),
                                          std::uint64_t(0)));
        if (m_limits.maxHeaderBytes > 0) {
            settings.push_back(std::make_pair(
                SwHttp3Frame::settingMaxFieldSectionSize(),
                static_cast<std::uint64_t>(m_limits.maxHeaderBytes)));
        }
        settings.push_back(std::make_pair(SwHttp3Frame::settingH3Datagram(), std::uint64_t(1)));
        settings.push_back(std::make_pair(settingEnableConnectProtocol(), std::uint64_t(1)));
        settings.push_back(std::make_pair(
            SwHttp3Frame::settingEnableWebTransportDraft02(), std::uint64_t(1)));
        settings.push_back(std::make_pair(SwHttp3Frame::settingEnableWebTransport(),
                                          std::uint64_t(1)));

        SwByteArray streamData;
        if (!SwQuicVarIntCodec::encode(streamTypeControl(), streamData, error)) {
            return false;
        }
        if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::settings(settings), streamData, error)) {
            return false;
        }

        const std::uint64_t controlStreamId = m_nextServerUniStreamId;
        m_nextServerUniStreamId += 4;
        if (!m_connection->sendStreamData(controlStreamId, streamData, false, error)) {
            return false;
        }

        m_controlStreamOpened = true;
        return true;
    }

    bool handleUniStream_(std::uint64_t streamId, const SwByteArray& chunk,
                          bool fin, SwString* error) {
        UniStream_& stream = m_uniStreams[streamId];
        // Non-control streams are intentionally ignored in this static-QPACK
        // implementation. Once their type is known, discard new bytes without
        // retaining or flow-budgeting them. A control stream, by contrast, can
        // carry a frame whose declared payload never completes; charge every
        // retained byte before appending so readStream() flow-credit cannot turn
        // that partial frame into an unbounded connection-lifetime allocation.
        if (stream.typeKnown && stream.type != streamTypeControl() &&
            stream.type != SwWebTransportSession::uniStreamType()) {
            clearError_(error);
            return true;
        }
        if (!appendUniStreamBytes_(stream, chunk, error)) {
            return false;
        }

        if (!stream.typeKnown) {
            std::size_t offset = 0;
            std::uint64_t type = 0;
            if (!SwQuicVarIntCodec::decode(stream.buffer, offset, type, nullptr)) {
                return true; // await the full type varint
            }
            stream.type = type;
            stream.typeKnown = true;
            stream.buffer = stream.buffer.mid(static_cast<int>(offset),
                                              static_cast<int>(stream.buffer.size() - offset));
            synchronizeUniStreamAccounting_(stream);
        }

        if (stream.type == streamTypeControl()) {
            // Only one peer control stream is allowed (RFC 9114 6.2.1); a
            // second is H3_STREAM_CREATION_ERROR.
            if (m_haveControlStream && m_controlStreamId != streamId) {
                setError_(error, "Second HTTP/3 control stream (H3_STREAM_CREATION_ERROR)");
                return false;
            }
            m_haveControlStream = true;
            m_controlStreamId = streamId;
            if (!parseControlStream_(stream.buffer, error)) {
                return false;
            }
            synchronizeUniStreamAccounting_(stream);
            return true;
        }
        if (stream.type == SwWebTransportSession::uniStreamType()) {
            if (!stream.webTransportSessionKnown) {
                std::size_t sessionOffset = 0;
                std::uint64_t sessionId = 0;
                if (!SwQuicVarIntCodec::decode(
                        stream.buffer, sessionOffset, sessionId, nullptr)) {
                    return true;
                }
                if (!hasWebTransportSession(sessionId)) {
                    setError_(error,
                              "WebTransport unidirectional stream references an unknown session");
                    return false;
                }
                stream.webTransportSessionKnown = true;
                stream.webTransportSessionId = sessionId;
                stream.buffer = stream.buffer.mid(
                    static_cast<int>(sessionOffset),
                    static_cast<int>(stream.buffer.size() - sessionOffset));
                synchronizeUniStreamAccounting_(stream);
            }
            if ((!stream.buffer.isEmpty() || fin) && m_webTransportStreamHandler) {
                m_webTransportStreamHandler(
                    stream.webTransportSessionId, streamId,
                    stream.buffer, false, fin);
            }
            releaseUniStreamStorage_(stream);
            clearError_(error);
            return true;
        }
        // Push (0x01) is server-initiated so it never arrives here; QPACK
        // encoder/decoder streams (0x02/0x03) carry no instructions in a
        // static-table deployment. Consume and ignore.
        releaseUniStreamStorage_(stream);
        clearError_(error);
        return true;
    }

    bool parseControlStream_(SwByteArray& buffer, SwString* error) {
        std::size_t offset = 0;
        while (offset < buffer.size()) {
            // First establish whether the complete outer frame is present.
            // decodeFrame() also validates type-specific payloads (notably the
            // SETTINGS identifier/value pairs), so treating every false return
            // as "need more bytes" would keep a complete malformed frame alive
            // forever. Only an actually incomplete type/length/payload waits.
            std::size_t payloadOffset = offset;
            std::uint64_t rawType = 0;
            std::uint64_t payloadLength = 0;
            if (!SwQuicVarIntCodec::decode(
                    buffer, payloadOffset, rawType, nullptr) ||
                !SwQuicVarIntCodec::decode(
                    buffer, payloadOffset, payloadLength, nullptr) ||
                payloadLength > static_cast<std::uint64_t>(
                    buffer.size() - payloadOffset)) {
                break;
            }
            (void)rawType;

            SwHttp3Frame frame;
            SwString decodeError;
            if (!SwHttp3FrameCodec::decodeFrame(buffer, offset, frame, &decodeError)) {
                if (error) {
                    *error = SwString("Malformed HTTP/3 control-stream frame: ") +
                             decodeError;
                }
                return false;
            }
            // The first frame on the control stream MUST be SETTINGS
            // (RFC 9114 6.2.1); a later/duplicate SETTINGS is H3_FRAME_UNEXPECTED.
            if (frame.type() == SwHttp3Frame::Type::Settings) {
                if (m_peerSettingsReceived) {
                    setError_(error, "Duplicate HTTP/3 SETTINGS (H3_FRAME_UNEXPECTED)");
                    return false;
                }
                m_peerSettingsReceived = true;
                for (std::size_t i = 0; i < frame.settings().size(); ++i) {
                    if (frame.settings()[i].first ==
                        SwHttp3Frame::settingEnableWebTransportDraft02()) {
                        m_peerEnableWebTransport =
                            m_peerEnableWebTransport ||
                            (frame.settings()[i].second > 0);
                        // Draft-02 predates SETTINGS_ENABLE_CONNECT_PROTOCOL;
                        // its WebTransport setting is itself the peer opt-in
                        // for the extended CONNECT request.
                        m_peerEnableConnectProtocol =
                            m_peerEnableConnectProtocol ||
                            (frame.settings()[i].second > 0);
                    } else if (frame.settings()[i].first ==
                               SwHttp3Frame::settingEnableWebTransport()) {
                        m_peerEnableWebTransport =
                            m_peerEnableWebTransport ||
                            (frame.settings()[i].second > 0);
                    } else if (frame.settings()[i].first ==
                               SwHttp3Frame::settingH3Datagram()) {
                        m_peerH3Datagram = (frame.settings()[i].second == 1);
                    } else if (frame.settings()[i].first ==
                               settingEnableConnectProtocol()) {
                        m_peerEnableConnectProtocol =
                            (frame.settings()[i].second == 1);
                    } else if (frame.settings()[i].first ==
                               SwHttp3Frame::settingMaxFieldSectionSize()) {
                        m_peerMaxFieldSectionSize = frame.settings()[i].second;
                    }
                }
            } else if (!m_peerSettingsReceived) {
                setError_(error, "First HTTP/3 control-stream frame is not SETTINGS (H3_MISSING_SETTINGS)");
                return false;
            }
        }
        buffer = buffer.mid(static_cast<int>(offset),
                            static_cast<int>(buffer.size() - offset));
        clearError_(error);
        return true;
    }

    bool handleRequestStream_(std::uint64_t streamId,
                              const SwByteArray& chunk,
                              bool fin,
                              SwString* error) {
        if (isCompletedRequestStream_(streamId)) {
            return true;
        }
        RequestStream_& state = m_requestStreams[streamId];
        if (state.webTransportDataStream) {
            if ((!chunk.isEmpty() || fin) && m_webTransportStreamHandler) {
                m_webTransportStreamHandler(
                    state.webTransportSessionId, streamId, chunk, true, fin);
            }
            if (fin) {
                releaseRequestStorage_(state);
                m_requestStreams.erase(streamId);
            }
            clearError_(error);
            return true;
        }
        // Completed request streams remain as tiny tombstones because pump()
        // may scan historical QUIC streams. Never recreate or refill their
        // request buffers after a response has been queued.
        if (state.responded || state.awaitingResponse) {
            return true;
        }

        if (!state.resourceLimitExceeded && !chunk.isEmpty()) {
            const std::size_t incomingBytes = static_cast<std::size_t>(chunk.size());
            if (!reservePendingBytes_(incomingBytes)) {
                markResourceLimitExceeded_(state);
            } else {
                state.accountedBytes += incomingBytes;
                state.buffer.append(chunk);
            }
        }

        if (state.resourceLimitExceeded) {
            if (!fin) {
                return abortRequestStream_(streamId, h3ExcessiveLoad_(), false,
                                           error);
            }
            ++m_requestsHandled;
            return finishRequestStream_(
                streamId, state,
                swHttpTextResponse(503, SwString("Request buffering limit exceeded")),
                true, error);
        }

        // A peer-created bidirectional WebTransport stream starts with the
        // WT_STREAM signal (0x41) and the CONNECT stream/session ID. Classify
        // it before feeding bytes to the HTTP/3 frame decoder.
        if (!state.headersDecoded && state.fields.empty() && state.body.isEmpty() &&
            !state.buffer.isEmpty()) {
            std::size_t prefixOffset = 0;
            std::uint64_t signal = 0;
            if (!SwQuicVarIntCodec::decode(
                    state.buffer, prefixOffset, signal, nullptr)) {
                if (fin) {
                    setError_(error, "Truncated WebTransport stream signal");
                    return false;
                }
                return true;
            }
            if (signal == SwWebTransportSession::bidiStreamFrameType()) {
                std::uint64_t sessionId = 0;
                if (!SwQuicVarIntCodec::decode(
                        state.buffer, prefixOffset, sessionId, nullptr)) {
                    if (fin) {
                        setError_(error, "Truncated WebTransport session id");
                        return false;
                    }
                    return true;
                }
                if (!hasWebTransportSession(sessionId)) {
                    setError_(error,
                              "WebTransport bidirectional stream references an unknown session");
                    return false;
                }
                state.webTransportDataStream = true;
                state.webTransportSessionId = sessionId;
                const SwByteArray payload = state.buffer.mid(
                    static_cast<int>(prefixOffset),
                    static_cast<int>(state.buffer.size() - prefixOffset));
                state.buffer = SwByteArray();
                synchronizeRequestAccounting_(state);
                if ((!payload.isEmpty() || fin) && m_webTransportStreamHandler) {
                    m_webTransportStreamHandler(
                        sessionId, streamId, payload, true, fin);
                }
                if (fin) {
                    releaseRequestStorage_(state);
                    m_requestStreams.erase(streamId);
                }
                clearError_(error);
                return true;
            }
        }

        std::size_t offset = 0;
        while (offset < state.buffer.size()) {
            const std::size_t frameStart = offset;
            SwHttp3Frame frame;
            SwString decodeError;
            if (!SwHttp3FrameCodec::decodeFrame(state.buffer, offset, frame, &decodeError)) {
                offset = frameStart;
                break;
            }
            if (frame.type() == SwHttp3Frame::Type::Headers) {
                // Only the first HEADERS section carries the request pseudo-
                // headers; a later HEADERS is trailers (RFC 9114 4.1) and must
                // not overwrite :method/:path (SwQpackDecoder clears its out).
                if (!state.headersDecoded) {
                    if (!SwQpackDecoder::decodeFieldSection(
                            frame.payload(), state.fields,
                            m_limits.maxHeaderCount, m_limits.maxHeaderBytes, error)) {
                        return false;
                    }
                    if (m_limits.maxHeaderCount > 0 &&
                        state.fields.size() > m_limits.maxHeaderCount) {
                        state.frameOrderError = true;
                    }
                    std::size_t headerBytes = 0;
                    for (std::size_t i = 0; i < state.fields.size(); ++i) {
                        headerBytes += state.fields[i].first.size();
                        headerBytes += state.fields[i].second.size();
                    }
                    if (m_limits.maxHeaderBytes > 0 &&
                        headerBytes > m_limits.maxHeaderBytes) {
                        state.frameOrderError = true;
                    }
                    state.headersDecoded = true;
                } else {
                    std::vector<std::pair<SwByteArray, SwByteArray> > trailers;
                    if (!SwQpackDecoder::decodeFieldSection(
                            frame.payload(), trailers,
                            m_limits.maxHeaderCount, m_limits.maxHeaderBytes,
                            error)) {
                        return false;
                    }
                    if (state.trailersDecoded ||
                        !validateTrailerFields_(trailers)) {
                        state.frameOrderError = true;
                    }
                    state.trailersDecoded = true;
                }
            } else if (frame.type() == SwHttp3Frame::Type::Data) {
                // DATA before any HEADERS is an invalid frame sequence
                // (RFC 9114 4.1, H3_FRAME_UNEXPECTED).
                if (!state.headersDecoded || state.trailersDecoded) {
                    state.frameOrderError = true;
                }
                if (state.trailersDecoded) {
                    continue; // trailers terminate the HTTP message body
                } else if (m_limits.maxBodyBytes > 0 &&
                    (frame.payload().size() > m_limits.maxBodyBytes ||
                     state.body.size() > m_limits.maxBodyBytes - frame.payload().size())) {
                    if (!fin) {
                        return abortRequestStream_(streamId, h3ExcessiveLoad_(),
                                                   false, error);
                    }
                    state.frameOrderError = true;
                } else {
                    state.body.append(frame.payload());
                }
            } else if (frame.type() == SwHttp3Frame::Type::Settings ||
                       frame.type() == SwHttp3Frame::Type::GoAway ||
                       frame.type() == SwHttp3Frame::Type::MaxPushId ||
                       frame.type() == SwHttp3Frame::Type::CancelPush ||
                       frame.type() == SwHttp3Frame::Type::PushPromise) {
                // Control-only frames MUST NOT appear on a request stream
                // (RFC 9114 7.2). GREASE/Unknown frames are ignored per s9.
                state.frameOrderError = true;
            }
        }
        state.buffer = state.buffer.mid(static_cast<int>(offset),
                                        static_cast<int>(state.buffer.size() - offset));

        // QPACK may expand indexed fields, while consuming frame bytes may
        // shrink the raw buffer. Reconcile the charge with the data actually
        // retained by this request stream.
        if (!synchronizeRequestAccounting_(state)) {
            if (!fin) {
                return abortRequestStream_(streamId, h3ExcessiveLoad_(), false,
                                           error);
            }
            ++m_requestsHandled;
            return finishRequestStream_(
                streamId, state,
                swHttpTextResponse(503, SwString("Request buffering limit exceeded")),
                true, error);
        }

        // Ordinary HTTP requests are complete at FIN. An extended CONNECT is
        // deliberately long-lived: dispatch it as soon as its complete
        // HEADERS frame and the peer SETTINGS are available, without waiting
        // for FIN (which terminates a WebTransport session).
        if (!fin) {
            if (!state.headersDecoded || !state.buffer.isEmpty() ||
                !m_peerSettingsReceived) {
                return true;
            }
            SwHttpRequest candidate;
            // DATA following an Extended CONNECT HEADERS section carries
            // capsules and may arrive in the same flight. Inspect a copy so
            // version/routing validation cannot consume those early bytes.
            SwByteArray candidateBody = state.body;
            bool candidateMalformed = false;
            if (!fieldsToRequest_(state.fields, candidateBody, candidate,
                                  candidateMalformed) || candidateMalformed) {
                return true;
            }
        }

        // A FIN makes the byte sequence final. Any undecoded suffix therefore
        // is a truncated HTTP/3 frame, not an ignorable partial frame that may
        // be routed as if the request ended cleanly.
        if (!state.buffer.isEmpty()) {
            state.frameOrderError = true;
        }

        if (!state.headersDecoded) {
            ++m_requestsHandled;
            return finishRequestStream_(
                streamId, state, swHttpTextResponse(400, SwString("Bad Request")),
                true, error);
        }

        SwHttpRequest request;
        std::uint64_t sessionProtocol = 0;
        bool malformed = false;
        const bool isConnect = fieldsToRequest_(state.fields, state.body, request, malformed);
        // The parsed request now owns the body and normalized headers. Drop the
        // raw QPACK/frame storage immediately, while retaining its accounting
        // charge until the application response completes.
        state.buffer = SwByteArray();
        std::vector<std::pair<SwByteArray, SwByteArray> >().swap(state.fields);

        ++m_requestsHandled;

        // A malformed request (RFC 9114 4.1.2) or an invalid frame sequence
        // (4.1) is answered with 400 rather than routed. (A stricter
        // implementation would reset the stream / close with H3_FRAME_UNEXPECTED
        // or H3_MESSAGE_ERROR; this driver has no stream-reset path yet.)
        if (malformed || state.frameOrderError) {
            return finishRequestStream_(
                streamId, state, swHttpTextResponse(400, SwString("Bad Request")),
                true, error);
        }

        SwString multipartError;
        std::size_t multipartParseReservation = 0;
        if (!reserveMultipartParseBudget_(
                state, request, multipartParseReservation)) {
            const bool sent = finishRequestStream_(
                streamId, state,
                swHttpTextResponse(503, SwString("Request buffering limit exceeded")),
                true, error);
            swHttpCleanupMultipartTemporaryFiles(request);
            return sent;
        }
        if (!swHttpParseMultipartRequest(request, m_limits, multipartError)) {
            const bool sent = finishRequestStream_(
                streamId, state, swHttpTextResponse(400, SwString("Bad Request")),
                true, error);
            swHttpCleanupMultipartTemporaryFiles(request);
            return sent;
        }
        if (!reconcileMultipartParseBudget_(
                state, request, multipartParseReservation)) {
            const bool sent = finishRequestStream_(
                streamId, state,
                swHttpTextResponse(503, SwString("Request buffering limit exceeded")),
                true, error);
            swHttpCleanupMultipartTemporaryFiles(request);
            return sent;
        }

        if (isConnect) {
            const SwQuicTransportParameters& peerTransport =
                m_connection->peerTransportParameters();
            const bool webTransportNegotiated =
                m_peerEnableWebTransport && m_peerEnableConnectProtocol &&
                m_peerH3Datagram && m_connection->hasPeerTransportParameters() &&
                peerTransport.maxDatagramFrameSize > 0;
            // WebTransport Extended CONNECT (RFC 9220): the request stream ID
            // is the session ID.
            SwString selectedProtocol;
            bool accept = false;
            if (webTransportNegotiated && m_webTransportProtocolHandler) {
                accept = m_webTransportProtocolHandler(
                    request, streamId, selectedProtocol);
            } else if (webTransportNegotiated && m_webTransportHandler) {
                accept = m_webTransportHandler(request, streamId);
            }
            (void)sessionProtocol;
            SwHttpResponse response;
            response.status = accept ? 200 : 404;
            response.reason = swHttpStatusReason(response.status);
            if (accept && !selectedProtocol.isEmpty()) {
                response.headers[SwString("wt-protocol")] =
                    SwString("\"") + selectedProtocol + SwString("\"");
            }
            const bool sent = finishRequestStream_(streamId, state, response, !accept, error);
            if (sent && accept) m_webTransportSessions[streamId] = true;
            swHttpCleanupMultipartTemporaryFiles(request);
            return sent;
        }

        if (m_asyncRequestHandler && m_asyncResponseReadyHandler) {
            state.awaitingResponse = true;
            state.activeRequest.reset(new SwHttpRequest(std::move(request)));
            const AsyncResponseReadyHandler responseReady = m_asyncResponseReadyHandler;
            const ResponseCallback complete =
                [responseReady, streamId](SwHttpResponse response) mutable {
                    responseReady(streamId, std::move(response));
                };
            try {
                m_asyncRequestHandler(*state.activeRequest, complete);
            } catch (const std::exception&) {
                complete(swHttpTextResponse(500, SwString("Internal Server Error")));
            } catch (...) {
                complete(swHttpTextResponse(500, SwString("Internal Server Error")));
            }
            return true;
        }

        SwHttpResponse response;
        if (m_requestHandler) {
            response = m_requestHandler(request);
        } else {
            response = swHttpTextResponse(404, SwString("Not Found"));
        }
        if (!prepareResponseForHttp3_(request, response, error)) {
            swHttpCleanupMultipartTemporaryFiles(request);
            releaseRequestStorage_(state);
            return false;
        }
        const bool sent = finishRequestStream_(streamId, state, response, true, error);
        swHttpCleanupMultipartTemporaryFiles(request);
        return sent;
    }

    static bool prepareResponseForHttp3_(const SwHttpRequest& request,
                                         SwHttpResponse& response,
                                         SwString* error) {
        // A TCP socket hand-over (classic WebSocket/CONNECT upgrade) cannot be
        // represented on a QUIC request stream. WebTransport has its dedicated
        // Extended CONNECT path above.
        if (response.switchToRawSocket || response.switchToRawSocketWithoutHttpResponse) {
            response = swHttpTextResponse(
                501, SwString("This protocol upgrade is not available over HTTP/3"));
        }

        // This writer emits exactly one final response. A 1xx followed by FIN
        // is not a valid response sequence, and an out-of-range status cannot
        // form the mandatory three-digit :status pseudo-field.
        if (response.status < 200 || response.status > 599) {
            response = swHttpTextResponse(
                500, SwString("Invalid response status"));
        }

        // HEAD carries the same representation headers as GET but never the
        // payload. The incremental response writer checks headOnly before it
        // opens a file or copies any body/chunk source.
        if (request.method.toUpper() == SwString("HEAD") || response.headOnly) {
            response.headOnly = true;
        }
        clearError_(error);
        return true;
    }

    // For WebTransport CONNECT the response has no body; a rejected session
    // finishes the stream, an accepted one keeps it open as the session stream.
    bool sendResponseHeadersOnly_(std::uint64_t streamId,
                                  const SwHttpResponse& response,
                                  bool finish,
                                  SwString* error) {
        std::vector<std::pair<SwByteArray, SwByteArray> > fields;
        pushStatus_(fields, response.status);

        std::vector<std::pair<SwByteArray, SwByteArray> > applicationFields;
        appendHeaderMap_(response.headers, applicationFields);
        for (std::size_t i = 0; i < applicationFields.size(); ++i) {
            fields.push_back(applicationFields[i]);
        }

        std::uint64_t fieldSectionBytes = 0;
        if (!fieldSectionSize_(fields, fieldSectionBytes) ||
            fieldSectionBytes > m_peerMaxFieldSectionSize) {
            setError_(error,
                      "HTTP/3 response status exceeds the peer field-section limit");
            return false;
        }

        SwByteArray headerBlock;
        if (!SwQpackEncoder::encodeFieldSection(fields, headerBlock, error)) {
            return false;
        }
        SwByteArray streamData;
        if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::headers(headerBlock), streamData, error)) {
            return false;
        }
        return m_connection->sendStreamData(streamId, streamData, finish, error);
    }

    static void pushStatus_(std::vector<std::pair<SwByteArray, SwByteArray> >& fields,
                            int status) {
        fields.push_back(std::make_pair(SwByteArray(":status"),
                                        SwByteArray(SwString::number(status).toStdString())));
    }

    void closeWebTransportSession_(std::uint64_t sessionId) {
        if (m_webTransportSessions.erase(sessionId) == 0) return;
        std::map<std::uint64_t, RequestStream_>::iterator connectState =
            m_requestStreams.find(sessionId);
        if (connectState != m_requestStreams.end()) {
            releaseRequestStorage_(connectState->second);
            m_requestStreams.erase(connectState);
        }
        if (m_webTransportSessionClosedHandler) {
            m_webTransportSessionClosedHandler(sessionId);
        }
    }

    static bool fieldLineSize_(
        const std::pair<SwByteArray, SwByteArray>& field,
        std::uint64_t& outBytes) {
        const std::uint64_t maximum =
            (std::numeric_limits<std::uint64_t>::max)();
        const std::uint64_t nameBytes =
            static_cast<std::uint64_t>(field.first.size());
        const std::uint64_t valueBytes =
            static_cast<std::uint64_t>(field.second.size());
        if (nameBytes > maximum - 32U ||
            valueBytes > maximum - 32U - nameBytes) {
            return false;
        }
        outBytes = 32U + nameBytes + valueBytes;
        return true;
    }

    static bool fieldSectionSize_(
        const std::vector<std::pair<SwByteArray, SwByteArray> >& fields,
        std::uint64_t& outBytes) {
        outBytes = 0;
        const std::uint64_t maximum =
            (std::numeric_limits<std::uint64_t>::max)();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            std::uint64_t lineBytes = 0;
            if (!fieldLineSize_(fields[i], lineBytes) ||
                lineBytes > maximum - outBytes) {
                return false;
            }
            outBytes += lineBytes;
        }
        return true;
    }

    static bool isValidResponseHeaderName_(const SwString& name) {
        if (name.isEmpty() || name.startsWith(SwString(":"))) return false;
        const std::string bytes = name.toStdString();
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (!isTokenCharacter_(static_cast<std::uint8_t>(bytes[i]))) {
                return false;
            }
        }
        return true;
    }

    static bool isValidResponseHeaderValue_(const SwString& value) {
        const std::string bytes = value.toStdString();
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const std::uint8_t current = static_cast<std::uint8_t>(bytes[i]);
            if ((current < 0x20U && current != 0x09U) || current == 0x7fU) {
                return false;
            }
        }
        return true;
    }

    static void appendHeaderMap_(const SwMap<SwString, SwString>& headers,
                                 std::vector<std::pair<SwByteArray, SwByteArray> >& fields) {
        for (SwMap<SwString, SwString>::const_iterator it = headers.begin();
             it != headers.end(); ++it) {
            const SwString nameLower = it.key().trimmed().toLower();
            if (!isValidResponseHeaderName_(nameLower) ||
                !isValidResponseHeaderValue_(it.value()) ||
                isConnectionSpecificHeader_(nameLower) ||
                nameLower == SwString("content-length") ||
                nameLower == SwString("te")) {
                // Connection-specific/framing fields are generated by the
                // transport. Invalid application fields are dropped rather
                // than emitting an H3_MESSAGE_ERROR to an otherwise good peer.
                continue;
            }
            fields.push_back(std::make_pair(SwByteArray(nameLower.toStdString()),
                                            SwByteArray(it.value().toStdString())));
        }
    }

    static bool isConnectionSpecificHeader_(const SwString& lower) {
        return lower == SwString("connection") || lower == SwString("keep-alive") ||
               lower == SwString("proxy-connection") || lower == SwString("transfer-encoding") ||
               lower == SwString("upgrade");
    }

    static bool validateTrailerFields_(
        const std::vector<std::pair<SwByteArray, SwByteArray> >& fields) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (!isValidRequestFieldName_(fields[i].first, false) ||
                !isValidRequestFieldValue_(fields[i].second)) {
                return false;
            }
            const SwString lower(fields[i].first.toStdString());
            if (isConnectionSpecificHeader_(lower) ||
                lower == SwString("content-length") ||
                lower == SwString("host") || lower == SwString("te")) {
                return false;
            }
        }
        return true;
    }

    static bool isTokenCharacter_(std::uint8_t value) {
        if ((value >= static_cast<std::uint8_t>('a') &&
             value <= static_cast<std::uint8_t>('z')) ||
            (value >= static_cast<std::uint8_t>('A') &&
             value <= static_cast<std::uint8_t>('Z')) ||
            (value >= static_cast<std::uint8_t>('0') &&
             value <= static_cast<std::uint8_t>('9'))) {
            return true;
        }
        switch (value) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
        case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
        }
    }

    // HTTP/3 field names are HTTP tokens encoded entirely in lowercase. A
    // request pseudo-header has one leading ':' followed by such a token.
    static bool isValidRequestFieldName_(const SwByteArray& name, bool pseudo) {
        const std::size_t size = name.size();
        const std::size_t start = pseudo ? 1U : 0U;
        if (size <= start || !name.constData() ||
            (pseudo && static_cast<std::uint8_t>(name.constData()[0]) !=
                           static_cast<std::uint8_t>(':'))) {
            return false;
        }
        for (std::size_t i = start; i < size; ++i) {
            const std::uint8_t value =
                static_cast<std::uint8_t>(name.constData()[i]);
            if ((value >= static_cast<std::uint8_t>('A') &&
                 value <= static_cast<std::uint8_t>('Z')) ||
                !isTokenCharacter_(value)) {
                return false;
            }
        }
        return true;
    }

    static bool isValidRequestFieldValue_(const SwByteArray& value) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            const std::uint8_t byte =
                static_cast<std::uint8_t>(value.constData()[i]);
            // Reject every C0 control (including CR/LF and HTAB) plus DEL at
            // the H3 boundary. This keeps downstream maps/logging free from
            // ambiguous line folding and request-smuggling delimiters.
            if (byte <= 0x1fU || byte == 0x7fU) {
                return false;
            }
        }
        return true;
    }

    static bool isValidRequestMethod_(const SwByteArray& method) {
        if (method.isEmpty() || !method.constData()) return false;
        for (std::size_t i = 0; i < method.size(); ++i) {
            if (!isTokenCharacter_(
                    static_cast<std::uint8_t>(method.constData()[i]))) {
                return false;
            }
        }
        return true;
    }

    static bool parseContentLength_(const SwByteArray& value,
                                    std::uint64_t& outLength) {
        if (value.isEmpty() || !value.constData()) {
            return false;
        }
        std::uint64_t parsed = 0;
        for (std::size_t i = 0; i < value.size(); ++i) {
            const std::uint8_t byte =
                static_cast<std::uint8_t>(value.constData()[i]);
            if (byte < static_cast<std::uint8_t>('0') ||
                byte > static_cast<std::uint8_t>('9')) {
                return false;
            }
            const std::uint64_t digit =
                static_cast<std::uint64_t>(byte - static_cast<std::uint8_t>('0'));
            if (parsed > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10U) {
                return false;
            }
            parsed = parsed * 10U + digit;
        }
        outLength = parsed;
        return true;
    }

    static bool authoritiesMatch_(const SwString& authority,
                                  const SwString& host) {
        return authority.trimmed().toLower() == host.trimmed().toLower();
    }

    // Returns true when the request is an Extended CONNECT (WebTransport). Sets
    // outMalformed per RFC 9114 4.1.2/4.2/4.3.1 (connection-specific fields,
    // bad TE, unknown/duplicate/misplaced pseudo-headers, missing mandatory
    // pseudo-headers).
    static bool fieldsToRequest_(const std::vector<std::pair<SwByteArray, SwByteArray> >& fields,
                                 SwByteArray& body,
                                 SwHttpRequest& request,
                                 bool& outMalformed) {
        request.protocol = SwString("HTTP/3");
        request.body = std::move(body);
        outMalformed = false;
        bool isConnect = false;
        bool hasProtocolPseudo = false;
        bool isWebTransport = false;
        bool sawRegular = false;
        bool hasMethod = false, hasScheme = false, hasPath = false;
        bool seenMethod = false, seenPath = false, seenAuthority = false,
             seenScheme = false, seenProtocol = false;
        bool seenHost = false, seenContentLength = false;
        std::uint64_t contentLength = 0;
        SwString authorityValue;
        SwString hostValue;

        for (std::size_t i = 0; i < fields.size(); ++i) {
            const bool isPseudo = !fields[i].first.isEmpty() &&
                static_cast<std::uint8_t>(fields[i].first.constData()[0]) ==
                    static_cast<std::uint8_t>(':');
            if (!isValidRequestFieldName_(fields[i].first, isPseudo) ||
                !isValidRequestFieldValue_(fields[i].second)) {
                outMalformed = true;
                return false;
            }
            const SwString name(fields[i].first.toStdString());
            const SwString value(fields[i].second.toStdString());

            if (isPseudo && sawRegular) {
                outMalformed = true; // pseudo-header after a regular field
                return false;
            }
            if (name == SwString(":method")) {
                if (seenMethod) { outMalformed = true; return false; }
                if (!isValidRequestMethod_(fields[i].second)) {
                    outMalformed = true;
                    return false;
                }
                seenMethod = true; hasMethod = true;
                // SwHttpParser normalizes HTTP/1.x methods before pre-route
                // authorization runs. Keep the transport-neutral contract:
                // otherwise `post` could skip a case-sensitive POST guard and
                // still match a POST route, because SwHttpRouter uppercases for
                // route lookup.
                request.method = value.toUpper();
                if (request.method == SwString("CONNECT")) {
                    isConnect = true;
                }
            } else if (name == SwString(":path")) {
                if (seenPath) { outMalformed = true; return false; }
                seenPath = true; hasPath = true;
                request.target = value;
                const int queryOffset = value.indexOf('?');
                if (queryOffset >= 0) {
                    request.path = value.left(queryOffset);
                    request.queryString = value.mid(queryOffset + 1);
                    swHttpParseQueryString(request.queryString, request.queryParams);
                } else {
                    request.path = value;
                }
                SwString decodedPath;
                if (!swHttpPercentDecode(request.path, decodedPath, false)) {
                    outMalformed = true;
                    return false;
                }
                request.path = swHttpNormalizePath(decodedPath);
            } else if (name == SwString(":authority")) {
                if (seenAuthority) { outMalformed = true; return false; }
                seenAuthority = true;
                authorityValue = value;
            } else if (name == SwString(":scheme")) {
                if (seenScheme) { outMalformed = true; return false; }
                seenScheme = true; hasScheme = true;
                request.isTls = (value == SwString("https"));
            } else if (name == SwString(":protocol")) {
                if (seenProtocol) { outMalformed = true; return false; }
                seenProtocol = true;
                hasProtocolPseudo = true;
                isWebTransport = (value == SwString("webtransport"));
            } else if (isPseudo) {
                outMalformed = true; // unknown pseudo-header
                return false;
            } else {
                sawRegular = true;
                const SwString lower = name;
                // Connection-specific fields are malformed in HTTP/3 (4.2);
                // TE may only carry "trailers".
                if (isConnectionSpecificHeader_(lower) ||
                    (lower == SwString("te") && !(value == SwString("trailers")))) {
                    outMalformed = true;
                    return false;
                }
                if (lower == SwString("host")) {
                    if (seenHost) { outMalformed = true; return false; }
                    seenHost = true;
                    hostValue = value;
                } else if (lower == SwString("content-length")) {
                    if (seenContentLength ||
                        !parseContentLength_(fields[i].second, contentLength)) {
                        outMalformed = true;
                        return false;
                    }
                    seenContentLength = true;
                    request.headers[lower] = value;
                } else {
                    request.headers[lower] = value;
                }
            }
        }

        // :authority defines the target authority in HTTP/3. A legacy Host
        // field may agree with it, but it must never overwrite it; conflicting
        // values are rejected instead of creating transport-dependent routing.
        if (seenAuthority) {
            if (seenHost && !authoritiesMatch_(authorityValue, hostValue)) {
                outMalformed = true;
                return false;
            }
            request.headers[SwString("host")] = authorityValue;
        } else if (seenHost) {
            request.headers[SwString("host")] = hostValue;
        }

        if (seenContentLength &&
            contentLength != static_cast<std::uint64_t>(request.body.size())) {
            outMalformed = true;
            return false;
        }

        // Mandatory pseudo-headers (4.3.1): CONNECT needs :method (+:authority);
        // Extended CONNECT also needs :scheme/:path, while ordinary requests
        // need :method/:scheme/:path and cannot carry :protocol.
        if (isConnect) {
            if (!hasMethod || !seenAuthority || authorityValue.isEmpty()) {
                outMalformed = true;
            } else if (hasProtocolPseudo) {
                if (!hasScheme || !hasPath || request.target.isEmpty()) {
                    outMalformed = true;
                }
            } else if (hasScheme || hasPath) {
                outMalformed = true;
            }
        } else if (!hasMethod || !hasScheme || !hasPath ||
                   request.target.isEmpty() || hasProtocolPseudo) {
            outMalformed = true;
        }
        return isConnect && isWebTransport;
    }

    SwQuicConnection* m_connection;
    std::uint64_t m_nextServerUniStreamId;
    bool m_controlStreamOpened;
    bool m_peerSettingsReceived;
    bool m_peerH3Datagram;
    bool m_peerEnableConnectProtocol;
    bool m_peerEnableWebTransport;
    std::uint64_t m_peerMaxFieldSectionSize =
        (std::numeric_limits<std::uint64_t>::max)();
    bool m_haveControlStream = false;
    std::uint64_t m_controlStreamId = 0;
    std::size_t m_requestsHandled = 0;

    RequestHandler m_requestHandler;
    AsyncRequestHandler m_asyncRequestHandler;
    AsyncResponseReadyHandler m_asyncResponseReadyHandler;
    WebTransportHandler m_webTransportHandler;
    WebTransportProtocolHandler m_webTransportProtocolHandler;
    WebTransportDatagramHandler m_webTransportDatagramHandler;
    WebTransportStreamHandler m_webTransportStreamHandler;
    WebTransportSessionClosedHandler m_webTransportSessionClosedHandler;
    PendingBytesReserveHandler m_pendingBytesReserveHandler;
    PendingBytesReleaseHandler m_pendingBytesReleaseHandler;
    SwHttpLimits m_limits;
    std::size_t m_pendingRequestBytes = 0;
    std::map<std::uint64_t, UniStream_> m_uniStreams;
    std::map<std::uint64_t, RequestStream_> m_requestStreams;
    std::map<std::uint64_t, ResponseStream_> m_responseStreams;
    std::map<std::uint64_t, bool> m_webTransportSessions;
    std::map<std::uint64_t, std::uint64_t> m_completedRequestRanges;
    std::size_t m_responseSchedulingCursor = 0;
};

inline SwHttp3Server::~SwHttp3Server() {
    for (std::map<std::uint64_t, RequestStream_>::iterator it =
             m_requestStreams.begin();
         it != m_requestStreams.end(); ++it) {
        if (it->second.activeRequest) {
            swHttpCleanupMultipartTemporaryFiles(*it->second.activeRequest);
        }
    }
    if (m_pendingRequestBytes > 0 && m_pendingBytesReleaseHandler) {
        m_pendingBytesReleaseHandler(m_pendingRequestBytes);
    }
}

inline bool SwHttp3Server::completeAsyncResponse(
    std::uint64_t streamId,
    SwHttpResponse& response,
    SwHttpRequest* completedRequest,
    SwString* error) {
    if (completedRequest) {
        *completedRequest = SwHttpRequest();
    }
    std::map<std::uint64_t, RequestStream_>::iterator it =
        m_requestStreams.find(streamId);
    if (it == m_requestStreams.end() || it->second.responded ||
        !it->second.awaitingResponse) {
        clearError_(error);
        return true; // stale/duplicate completion after close or timeout
    }

    RequestStream_& state = it->second;
    if (!state.activeRequest) {
        setError_(error, "HTTP/3 asynchronous request state is missing");
        return false;
    }
    state.awaitingResponse = false;
    SwHttpRequest request = std::move(*state.activeRequest);
    state.activeRequest.reset();

    if (!prepareResponseForHttp3_(request, response, error)) {
        swHttpCleanupMultipartTemporaryFiles(request);
        state.responded = true;
        releaseRequestStorage_(state);
        return false;
    }
    const bool sent = finishRequestStream_(streamId, state, response, true, error);
    swHttpCleanupMultipartTemporaryFiles(request);
    if (completedRequest) {
        *completedRequest = std::move(request);
    }
    return sent;
}

#endif
