#ifndef SWHTTP3CONNECTION_H
#define SWHTTP3CONNECTION_H

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
// SwHttp3Connection
//
// Client-oriented HTTP/3 connection helper (RFC 9114). It converts HTTP requests/responses to and
// from the raw byte payloads that ride on QUIC streams, delegating framing to SwHttp3FrameCodec and
// header (de)compression to SwQpack{Encoder,Decoder}. It operates purely on byte buffers: the QUIC
// stream layer is responsible for actually moving those bytes. This class owns no sockets and holds
// no connection state.
//
// Every fallible operation returns bool and reports a human-readable message through the trailing
// SwString* error out-parameter (nullptr = discard).
//--------------------------------------------------------------------------------------------------

class SwHttp3Connection {
public:
    // Unidirectional stream type for the HTTP/3 control stream (RFC 9114 section 6.2.1).
    static std::uint64_t controlStreamType() { return 0x00; }

    // Builds the byte payload of the client control stream: the leading unidirectional stream-type
    // varint (0x00) followed by a single SETTINGS frame carrying the supplied settings.
    static bool buildControlStream(const SwHttp3Frame::SettingList& settings,
                                   SwByteArray& out,
                                   SwString* error = nullptr) {
        SwByteArray buffer;
        if (!SwQuicVarIntCodec::encode(controlStreamType(), buffer, error)) {
            return false;
        }
        if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::settings(settings), buffer, error)) {
            return false;
        }
        out = buffer;
        clearError_(error);
        return true;
    }

    // Reads the leading stream-type varint of a unidirectional stream, advancing 'offset' past it.
    static bool parseControlStreamPrefix(const SwByteArray& in,
                                         std::size_t& offset,
                                         std::uint64_t& streamType,
                                         SwString* error = nullptr) {
        if (!SwQuicVarIntCodec::decode(in, offset, streamType, error)) {
            return false;
        }
        clearError_(error);
        return true;
    }

    //----------------------------------------------------------------------------------------------
    // Requests
    //----------------------------------------------------------------------------------------------

    struct Request {
        SwByteArray method;
        SwByteArray scheme;
        SwByteArray authority;
        SwByteArray path;
        std::vector<std::pair<SwByteArray, SwByteArray> > extraHeaders;
        SwByteArray body;
    };

    // Builds a request stream: a HEADERS frame whose payload is the QPACK-encoded field section of
    // the four pseudo-headers (:method, :scheme, :authority, :path, in that order) followed by any
    // extra headers, then a DATA frame if the body is non-empty.
    static bool buildRequest(const Request& req,
                             SwByteArray& outRequestStream,
                             SwString* error = nullptr) {
        std::vector<std::pair<SwByteArray, SwByteArray> > headers;
        headers.push_back(std::make_pair(pseudoMethod_(), req.method));
        headers.push_back(std::make_pair(pseudoScheme_(), req.scheme));
        headers.push_back(std::make_pair(pseudoAuthority_(), req.authority));
        headers.push_back(std::make_pair(pseudoPath_(), req.path));
        for (std::size_t i = 0; i < req.extraHeaders.size(); ++i) {
            headers.push_back(req.extraHeaders[i]);
        }

        SwByteArray fieldSection;
        if (!SwQpackEncoder::encodeFieldSection(headers, fieldSection, error)) {
            return false;
        }

        SwByteArray buffer;
        if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::headers(fieldSection), buffer, error)) {
            return false;
        }

        if (!req.body.isEmpty()) {
            if (!SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::data(req.body), buffer, error)) {
                return false;
            }
        }

        outRequestStream = buffer;
        clearError_(error);
        return true;
    }

    //----------------------------------------------------------------------------------------------
    // Responses
    //----------------------------------------------------------------------------------------------

    struct Response {
        SwByteArray status;
        std::vector<std::pair<SwByteArray, SwByteArray> > headers;
        SwByteArray body;
    };

    // Walks the HTTP/3 frames on a response stream: HEADERS frames are QPACK-decoded (the :status
    // pseudo-header is lifted out into Response::status, all others go into Response::headers), DATA
    // frames are appended to Response::body, and unrecognized frame types are ignored (RFC 9114
    // section 9).
    static bool parseResponse(const SwByteArray& in,
                              Response& out,
                              SwString* error = nullptr) {
        out.status = SwByteArray();
        out.headers.clear();
        out.body = SwByteArray();

        std::size_t offset = 0;
        while (offset < in.size()) {
            SwHttp3Frame frame;
            if (!SwHttp3FrameCodec::decodeFrame(in, offset, frame, error)) {
                return false;
            }

            if (frame.type() == SwHttp3Frame::Type::Headers) {
                std::vector<std::pair<SwByteArray, SwByteArray> > fields;
                if (!SwQpackDecoder::decodeFieldSection(frame.payload(), fields, error)) {
                    return false;
                }
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    if (fields[i].first == pseudoStatus_()) {
                        out.status = fields[i].second;
                    } else {
                        out.headers.push_back(fields[i]);
                    }
                }
            } else if (frame.type() == SwHttp3Frame::Type::Data) {
                out.body.append(frame.payload());
            }
            // Any other frame type (Settings, GoAway, GREASE/Unknown, ...) is ignored here.
        }

        clearError_(error);
        return true;
    }

private:
    static void clearError_(SwString* error) {
        if (error) {
            *error = SwString();
        }
    }

    // Pseudo-header field names (RFC 9114 section 4.3). Constructed on demand to keep this class
    // header-only and free of static storage.
    static SwByteArray pseudoMethod_() { return SwByteArray(std::string(":method")); }
    static SwByteArray pseudoScheme_() { return SwByteArray(std::string(":scheme")); }
    static SwByteArray pseudoAuthority_() { return SwByteArray(std::string(":authority")); }
    static SwByteArray pseudoPath_() { return SwByteArray(std::string(":path")); }
    static SwByteArray pseudoStatus_() { return SwByteArray(std::string(":status")); }
};

#endif
