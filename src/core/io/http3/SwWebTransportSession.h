#ifndef SWWEBTRANSPORTSESSION_H
#define SWWEBTRANSPORTSESSION_H

#include "SwByteArray.h"
#include "SwString.h"
#include "http3/SwHttp3FrameCodec.h"
#include "http3/SwQpackEncoder.h"
#include "http3/SwQpackDecoder.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

//--------------------------------------------------------------------------------------------------
// SwWebTransportSession
//
// WebTransport-over-HTTP/3 session framing (draft-ietf-webtrans-http3). Client-oriented and
// stateless: every routine operates on byte buffers so the caller owns the actual QUIC streams.
//
// A WebTransport session is established with an HTTP/3 Extended CONNECT request (RFC 9220) that
// carries the :protocol pseudo-header set to "webtransport". Once the server answers 2xx, the
// client may open WebTransport streams and send HTTP/3 datagrams associated with the session:
//   - Unidirectional WT stream : stream type 0x54 (varint) then Session ID (varint).
//   - Bidirectional  WT stream : frame  type 0x41 WEBTRANSPORT_STREAM (varint) then Session ID.
//   - HTTP/3 datagram          : Quarter Stream ID (varint = Session ID / 4) then the payload.
//
// Every fallible operation returns bool and reports a human-readable message through the trailing
// SwString* error out-parameter (nullptr accepted). Reuses SwHttp3FrameCodec + SwQpackEncoder for
// the CONNECT HEADERS frame and SwQuicVarIntCodec for every QUIC/HTTP3 varint.
//--------------------------------------------------------------------------------------------------

class SwWebTransportSession {
public:
    // Wire constants (draft-ietf-webtrans-http3, RFC 9220, RFC 9297) ------------------------------

    // WebTransport unidirectional stream type prefix.
    static std::uint64_t uniStreamType() { return 0x54; }
    // WEBTRANSPORT_STREAM frame type, opening a bidirectional WebTransport stream.
    static std::uint64_t bidiStreamFrameType() { return 0x41; }
    // SETTINGS_ENABLE_CONNECT_PROTOCOL (RFC 9220): permits Extended CONNECT in HTTP/3.
    static std::uint64_t settingEnableConnectProtocol() { return 0x08; }

    // SETTINGS a WebTransport client must advertise on its HTTP/3 control stream ------------------
    static SwHttp3Frame::SettingList requiredClientSettings() {
        SwHttp3Frame::SettingList list;
        list.push_back(std::make_pair(SwHttp3Frame::settingH3Datagram(), std::uint64_t(1)));
        list.push_back(std::make_pair(SwHttp3Frame::settingEnableWebTransportDraft02(),
                                      std::uint64_t(1)));
        list.push_back(std::make_pair(SwHttp3Frame::settingEnableWebTransport(), std::uint64_t(1)));
        list.push_back(std::make_pair(settingEnableConnectProtocol(), std::uint64_t(1)));
        return list;
    }

    struct ConnectRequest {
        SwByteArray authority; // e.g. "example.com:443"
        SwByteArray path;      // e.g. "/wt"
    };

    // Builds the Extended CONNECT request as a single HTTP/3 HEADERS frame carrying the QPACK
    // encoded pseudo-header set required to open a WebTransport session.
    static bool buildConnect(const ConnectRequest& req,
                             SwByteArray& outRequestStream,
                             SwString* err = nullptr) {
        if (req.authority.isEmpty()) {
            setError_(err, "WebTransport CONNECT requires a non-empty :authority");
            return false;
        }
        if (req.path.isEmpty()) {
            setError_(err, "WebTransport CONNECT requires a non-empty :path");
            return false;
        }

        std::vector<std::pair<SwByteArray, SwByteArray> > headers;
        headers.push_back(std::make_pair(SwByteArray(":method"), SwByteArray("CONNECT")));
        headers.push_back(std::make_pair(SwByteArray(":protocol"), SwByteArray("webtransport")));
        headers.push_back(std::make_pair(SwByteArray(":scheme"), SwByteArray("https")));
        headers.push_back(std::make_pair(SwByteArray(":authority"), req.authority));
        headers.push_back(std::make_pair(SwByteArray(":path"), req.path));

        SwByteArray fieldSection;
        if (!SwQpackEncoder::encodeFieldSection(headers, fieldSection, err)) {
            return false;
        }

        outRequestStream.clear();
        if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::headers(fieldSection),
                                            outRequestStream, err)) {
            return false;
        }

        clearError_(err);
        return true;
    }

    // Inspects the server's decoded response field section (QPACK encoded field-section bytes) and
    // reports whether the WebTransport session was accepted, i.e. :status is 2xx.
    static bool isSessionAccepted(const SwByteArray& responseHeadersDecoded,
                                  bool& accepted,
                                  SwString* err = nullptr) {
        accepted = false;

        std::vector<std::pair<SwByteArray, SwByteArray> > headers;
        if (!SwQpackDecoder::decodeFieldSection(responseHeadersDecoded, headers, err)) {
            return false;
        }

        const SwByteArray statusName(":status");
        bool found = false;
        for (std::size_t i = 0; i < headers.size(); ++i) {
            if (headers[i].first == statusName) {
                const SwByteArray& value = headers[i].second;
                accepted = (value.size() >= 1 && value.constData()[0] == '2');
                found = true;
                break;
            }
        }

        if (!found) {
            setError_(err, "HTTP/3 response is missing the :status pseudo-header");
            return false;
        }

        clearError_(err);
        return true;
    }

    // Emits the leading bytes of a WebTransport unidirectional stream: type 0x54 then session id.
    static bool buildUniStreamHeader(std::uint64_t sessionId,
                                     SwByteArray& out,
                                     SwString* err = nullptr) {
        out.clear();
        if (!SwQuicVarIntCodec::encode(uniStreamType(), out, err)) {
            return false;
        }
        if (!SwQuicVarIntCodec::encode(sessionId, out, err)) {
            return false;
        }
        clearError_(err);
        return true;
    }

    // Emits the leading bytes of a WebTransport bidirectional stream: the WEBTRANSPORT_STREAM frame
    // type 0x41 then the session id.
    static bool buildBidiStreamHeader(std::uint64_t sessionId,
                                      SwByteArray& out,
                                      SwString* err = nullptr) {
        out.clear();
        if (!SwQuicVarIntCodec::encode(bidiStreamFrameType(), out, err)) {
            return false;
        }
        if (!SwQuicVarIntCodec::encode(sessionId, out, err)) {
            return false;
        }
        clearError_(err);
        return true;
    }

    // Encodes an HTTP/3 datagram bound to a WebTransport session: the Quarter Stream ID
    // (session id / 4, RFC 9297 section 2.1) as a varint, followed by the raw payload.
    static bool encodeDatagram(std::uint64_t sessionId,
                               const SwByteArray& payload,
                               SwByteArray& out,
                               SwString* err = nullptr) {
        out.clear();
        if (!SwQuicVarIntCodec::encode(sessionId / 4, out, err)) {
            return false;
        }
        out.append(payload);
        clearError_(err);
        return true;
    }

    // Decodes an HTTP/3 datagram: the leading Quarter Stream ID varint, then the trailing payload.
    static bool decodeDatagram(const SwByteArray& in,
                               std::uint64_t& quarterStreamId,
                               SwByteArray& payload,
                               SwString* err = nullptr) {
        std::size_t offset = 0;
        if (!SwQuicVarIntCodec::decode(in, offset, quarterStreamId, err)) {
            return false;
        }
        if (offset >= in.size()) {
            payload = SwByteArray();
        } else {
            payload = in.mid(static_cast<int>(offset),
                             static_cast<int>(in.size() - offset));
        }
        clearError_(err);
        return true;
    }

private:
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
};

#endif
