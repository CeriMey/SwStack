#ifndef SWHTTP3SERVER_H
#define SWHTTP3SERVER_H

#include "SwByteArray.h"
#include "SwString.h"
#include "http/SwHttpTypes.h"
#include "http3/SwHttp3FrameCodec.h"
#include "http3/SwQpackDecoder.h"
#include "http3/SwQpackEncoder.h"
#include "http3/SwWebTransportSession.h"
#include "quic/SwQuicConnection.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
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
public:
    typedef std::function<SwHttpResponse(const SwHttpRequest&)> RequestHandler;
    // Called when a client opens a WebTransport session via Extended CONNECT
    // (RFC 9220); return true to accept (204/200) or false to reject (404).
    typedef std::function<bool(const SwHttpRequest&, std::uint64_t sessionId)>
        WebTransportHandler;

    static std::uint64_t streamTypeControl() { return 0x00; }
    static std::uint64_t settingEnableConnectProtocol() { return 0x08; }

    explicit SwHttp3Server(SwQuicConnection* connection)
        : m_connection(connection),
          m_nextServerUniStreamId(3),
          m_controlStreamOpened(false),
          m_peerSettingsReceived(false),
          m_peerEnableWebTransport(false) {
    }

    void setRequestHandler(const RequestHandler& handler) { m_requestHandler = handler; }
    void setWebTransportHandler(const WebTransportHandler& handler) {
        m_webTransportHandler = handler;
    }

    bool peerSettingsReceived() const { return m_peerSettingsReceived; }
    bool peerEnableWebTransport() const { return m_peerEnableWebTransport; }
    std::size_t requestsHandled() const { return m_requestsHandled; }

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
        if (!ensureControlStream_(error)) {
            return false;
        }

        const std::vector<std::uint64_t> ids = m_connection->streams().streamIds();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const std::uint64_t streamId = ids[i];
            const SwByteArray chunk = m_connection->readStream(streamId);

            // The FIN can arrive in a payload-less STREAM frame, so it must be
            // checked even when this read returned no new bytes: a bodyless GET
            // or a request half-closed after its HEADERS would otherwise never
            // be dispatched (RFC 9114 4.1 -- request completion is the FIN).
            const SwQuicStream* quicStream = m_connection->streams().stream(streamId);
            const bool fin = quicStream && quicStream->isReceiveComplete();

            if (isUnidirectional_(streamId)) {
                // A closed control stream is a fatal H3_CLOSED_CRITICAL_STREAM
                // (RFC 9114 6.2.1).
                if (fin && m_haveControlStream && streamId == m_controlStreamId) {
                    setError_(error, "HTTP/3 control stream closed (H3_CLOSED_CRITICAL_STREAM)");
                    return false;
                }
                if (chunk.isEmpty()) {
                    continue;
                }
                if (!handleUniStream_(streamId, chunk, error)) {
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

        clearError_(error);
        return true;
    }

private:
    struct UniStream_ {
        bool typeKnown;
        std::uint64_t type;
        SwByteArray buffer;
        UniStream_() : typeKnown(false), type(0) {}
    };

    struct RequestStream_ {
        SwByteArray buffer;
        SwByteArray body;
        std::vector<std::pair<SwByteArray, SwByteArray> > fields;
        bool headersDecoded;
        bool responded;
        bool frameOrderError; // DATA before HEADERS, or a control frame on this stream
        RequestStream_() : headersDecoded(false), responded(false), frameOrderError(false) {}
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

    bool ensureControlStream_(SwString* error) {
        if (m_controlStreamOpened) {
            return true;
        }

        SwHttp3Frame::SettingList settings;
        settings.push_back(std::make_pair(SwHttp3Frame::settingQpackMaxTableCapacity(),
                                          std::uint64_t(0)));
        settings.push_back(std::make_pair(SwHttp3Frame::settingQpackBlockedStreams(),
                                          std::uint64_t(0)));
        settings.push_back(std::make_pair(SwHttp3Frame::settingMaxFieldSectionSize(),
                                          std::uint64_t(65536)));
        settings.push_back(std::make_pair(SwHttp3Frame::settingH3Datagram(), std::uint64_t(1)));
        settings.push_back(std::make_pair(settingEnableConnectProtocol(), std::uint64_t(1)));
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

    bool handleUniStream_(std::uint64_t streamId, const SwByteArray& chunk, SwString* error) {
        UniStream_& stream = m_uniStreams[streamId];
        stream.buffer.append(chunk);

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
            return parseControlStream_(stream.buffer, error);
        }
        // Push (0x01) is server-initiated so it never arrives here; QPACK
        // encoder/decoder streams (0x02/0x03) carry no instructions in a
        // static-table deployment. Consume and ignore.
        stream.buffer.clear();
        return true;
    }

    bool parseControlStream_(SwByteArray& buffer, SwString* error) {
        std::size_t offset = 0;
        while (offset < buffer.size()) {
            const std::size_t frameStart = offset;
            SwHttp3Frame frame;
            SwString decodeError;
            if (!SwHttp3FrameCodec::decodeFrame(buffer, offset, frame, &decodeError)) {
                offset = frameStart;
                break;
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
                        SwHttp3Frame::settingEnableWebTransport()) {
                        m_peerEnableWebTransport = (frame.settings()[i].second != 0);
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
        RequestStream_& state = m_requestStreams[streamId];
        state.buffer.append(chunk);

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
                    if (!SwQpackDecoder::decodeFieldSection(frame.payload(), state.fields, error)) {
                        return false;
                    }
                    state.headersDecoded = true;
                }
            } else if (frame.type() == SwHttp3Frame::Type::Data) {
                // DATA before any HEADERS is an invalid frame sequence
                // (RFC 9114 4.1, H3_FRAME_UNEXPECTED).
                if (!state.headersDecoded) {
                    state.frameOrderError = true;
                }
                state.body.append(frame.payload());
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

        if (!fin || state.responded || !state.headersDecoded) {
            return true;
        }

        SwHttpRequest request;
        std::uint64_t sessionProtocol = 0;
        bool malformed = false;
        const bool isConnect = fieldsToRequest_(state.fields, state.body, request, malformed);

        state.responded = true;
        ++m_requestsHandled;

        // A malformed request (RFC 9114 4.1.2) or an invalid frame sequence
        // (4.1) is answered with 400 rather than routed. (A stricter
        // implementation would reset the stream / close with H3_FRAME_UNEXPECTED
        // or H3_MESSAGE_ERROR; this driver has no stream-reset path yet.)
        if (malformed || state.frameOrderError) {
            return sendResponse_(streamId, swHttpTextResponse(400, SwString("Bad Request")),
                                 error);
        }

        if (isConnect) {
            // WebTransport Extended CONNECT (RFC 9220): the request stream ID
            // is the session ID.
            const bool accept = m_webTransportHandler
                                    ? m_webTransportHandler(request, streamId)
                                    : false;
            (void)sessionProtocol;
            SwHttpResponse response;
            response.status = accept ? 200 : 404;
            response.reason = swHttpStatusReason(response.status);
            return sendResponseHeadersOnly_(streamId, response, !accept, error);
        }

        SwHttpResponse response;
        if (m_requestHandler) {
            response = m_requestHandler(request);
        } else {
            response = swHttpTextResponse(404, SwString("Not Found"));
        }
        return sendResponse_(streamId, response, error);
    }

    bool sendResponse_(std::uint64_t streamId, const SwHttpResponse& response, SwString* error) {
        std::vector<std::pair<SwByteArray, SwByteArray> > fields;
        pushStatus_(fields, response.status);
        appendHeaderMap_(response.headers, fields);

        SwByteArray headerBlock;
        if (!SwQpackEncoder::encodeFieldSection(fields, headerBlock, error)) {
            return false;
        }

        SwByteArray streamData;
        if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::headers(headerBlock), streamData, error)) {
            return false;
        }
        if (!response.body.isEmpty()) {
            if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::data(response.body), streamData,
                                                error)) {
                return false;
            }
        }
        return m_connection->sendStreamData(streamId, streamData, true, error);
    }

    // For WebTransport CONNECT the response has no body; a rejected session
    // finishes the stream, an accepted one keeps it open as the session stream.
    bool sendResponseHeadersOnly_(std::uint64_t streamId,
                                  const SwHttpResponse& response,
                                  bool finish,
                                  SwString* error) {
        std::vector<std::pair<SwByteArray, SwByteArray> > fields;
        pushStatus_(fields, response.status);

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

    static void appendHeaderMap_(const SwMap<SwString, SwString>& headers,
                                 std::vector<std::pair<SwByteArray, SwByteArray> >& fields) {
        for (SwMap<SwString, SwString>::const_iterator it = headers.begin();
             it != headers.end(); ++it) {
            const SwString nameLower = it.key().toLower();
            if (nameLower == SwString("connection") || nameLower == SwString("keep-alive") ||
                nameLower == SwString("transfer-encoding") || nameLower == SwString("upgrade")) {
                continue; // connection-specific headers are forbidden in HTTP/3
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

    // Returns true when the request is an Extended CONNECT (WebTransport). Sets
    // outMalformed per RFC 9114 4.1.2/4.2/4.3.1 (connection-specific fields,
    // bad TE, unknown/duplicate/misplaced pseudo-headers, missing mandatory
    // pseudo-headers).
    static bool fieldsToRequest_(const std::vector<std::pair<SwByteArray, SwByteArray> >& fields,
                                 const SwByteArray& body,
                                 SwHttpRequest& request,
                                 bool& outMalformed) {
        request.protocol = SwString("HTTP/3");
        request.body = body;
        outMalformed = false;
        bool isConnect = false;
        bool hasProtocol = false;
        bool sawRegular = false;
        bool hasMethod = false, hasScheme = false, hasPath = false;
        bool seenMethod = false, seenPath = false, seenAuthority = false,
             seenScheme = false, seenProtocol = false;

        for (std::size_t i = 0; i < fields.size(); ++i) {
            const SwString name(fields[i].first.toStdString());
            const SwString value(fields[i].second.toStdString());
            const bool isPseudo = name.startsWith(SwString(":"));

            if (isPseudo && sawRegular) {
                outMalformed = true; // pseudo-header after a regular field
                return false;
            }
            if (name == SwString(":method")) {
                if (seenMethod) { outMalformed = true; return false; }
                seenMethod = true; hasMethod = true;
                request.method = value;
                if (value == SwString("CONNECT")) {
                    isConnect = true;
                }
            } else if (name == SwString(":path")) {
                if (seenPath) { outMalformed = true; return false; }
                seenPath = true; hasPath = true;
                request.target = value;
                request.path = value;
            } else if (name == SwString(":authority")) {
                if (seenAuthority) { outMalformed = true; return false; }
                seenAuthority = true;
                request.headers[SwString("host")] = value;
            } else if (name == SwString(":scheme")) {
                if (seenScheme) { outMalformed = true; return false; }
                seenScheme = true; hasScheme = true;
                request.isTls = (value == SwString("https"));
            } else if (name == SwString(":protocol")) {
                if (seenProtocol) { outMalformed = true; return false; }
                seenProtocol = true;
                hasProtocol = (value == SwString("webtransport"));
            } else if (isPseudo) {
                outMalformed = true; // unknown pseudo-header
                return false;
            } else {
                sawRegular = true;
                const SwString lower = name.toLower();
                // Connection-specific fields are malformed in HTTP/3 (4.2);
                // TE may only carry "trailers".
                if (isConnectionSpecificHeader_(lower) ||
                    (lower == SwString("te") && !(value == SwString("trailers")))) {
                    outMalformed = true;
                    return false;
                }
                request.headers[name] = value;
            }
        }

        // Mandatory pseudo-headers (4.3.1): CONNECT needs :method (+:authority);
        // all other requests need :method, :scheme, :path.
        if (isConnect) {
            if (!hasMethod) { outMalformed = true; }
        } else if (!hasMethod || !hasScheme || !hasPath) {
            outMalformed = true;
        }
        return isConnect && hasProtocol;
    }

    SwQuicConnection* m_connection;
    std::uint64_t m_nextServerUniStreamId;
    bool m_controlStreamOpened;
    bool m_peerSettingsReceived;
    bool m_peerEnableWebTransport;
    bool m_haveControlStream = false;
    std::uint64_t m_controlStreamId = 0;
    std::size_t m_requestsHandled = 0;

    RequestHandler m_requestHandler;
    WebTransportHandler m_webTransportHandler;
    std::map<std::uint64_t, UniStream_> m_uniStreams;
    std::map<std::uint64_t, RequestStream_> m_requestStreams;
};

#endif
