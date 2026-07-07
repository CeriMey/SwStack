#ifndef SWHTTP3FRAMECODEC_H
#define SWHTTP3FRAMECODEC_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicVarIntCodec.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

//--------------------------------------------------------------------------------------------------
// SwHttp3Frame
//
// Models a single HTTP/3 frame as defined in RFC 9114 section 7:
//   HTTP/3 Frame { Type (i), Length (i), Frame Payload (..) }
// where Type and Length are QUIC variable-length integers (RFC 9000 section 16).
//--------------------------------------------------------------------------------------------------

class SwHttp3Frame {
public:
    enum class Type {
        Data,        // 0x00
        Headers,     // 0x01
        CancelPush,  // 0x03
        Settings,    // 0x04
        PushPromise, // 0x05
        GoAway,      // 0x07
        MaxPushId,   // 0x0d
        Unknown      // reserved / GREASE / unrecognized
    };

    // Wire type codes (RFC 9114 section 11.2.1).
    static std::uint64_t typeData() { return 0x00; }
    static std::uint64_t typeHeaders() { return 0x01; }
    static std::uint64_t typeCancelPush() { return 0x03; }
    static std::uint64_t typeSettings() { return 0x04; }
    static std::uint64_t typePushPromise() { return 0x05; }
    static std::uint64_t typeGoAway() { return 0x07; }
    static std::uint64_t typeMaxPushId() { return 0x0d; }

    // Known SETTINGS identifiers (RFC 9114 section 7.2.4.1, RFC 9204, RFC 9297, WebTransport).
    static std::uint64_t settingQpackMaxTableCapacity() { return 0x01; }
    static std::uint64_t settingMaxFieldSectionSize() { return 0x06; }
    static std::uint64_t settingQpackBlockedStreams() { return 0x07; }
    static std::uint64_t settingH3Datagram() { return 0x33; }
    static std::uint64_t settingEnableWebTransport() { return 0x2b57; }

    typedef std::pair<std::uint64_t, std::uint64_t> Setting;
    typedef std::vector<Setting> SettingList;

    SwHttp3Frame()
        : m_type(Type::Unknown),
          m_rawType(0),
          m_id(0) {
    }

    // Factories ----------------------------------------------------------------------------------

    static SwHttp3Frame data(const SwByteArray& payload) {
        SwHttp3Frame frame(Type::Data);
        frame.m_rawType = typeData();
        frame.m_payload = payload;
        return frame;
    }

    static SwHttp3Frame headers(const SwByteArray& payload) {
        SwHttp3Frame frame(Type::Headers);
        frame.m_rawType = typeHeaders();
        frame.m_payload = payload;
        return frame;
    }

    static SwHttp3Frame settings(const SettingList& pairs) {
        SwHttp3Frame frame(Type::Settings);
        frame.m_rawType = typeSettings();
        frame.m_settings = pairs;
        return frame;
    }

    static SwHttp3Frame goAway(std::uint64_t id) {
        SwHttp3Frame frame(Type::GoAway);
        frame.m_rawType = typeGoAway();
        frame.m_id = id;
        return frame;
    }

    static SwHttp3Frame maxPushId(std::uint64_t id) {
        SwHttp3Frame frame(Type::MaxPushId);
        frame.m_rawType = typeMaxPushId();
        frame.m_id = id;
        return frame;
    }

    static SwHttp3Frame cancelPush(std::uint64_t id) {
        SwHttp3Frame frame(Type::CancelPush);
        frame.m_rawType = typeCancelPush();
        frame.m_id = id;
        return frame;
    }

    static SwHttp3Frame pushPromise(std::uint64_t pushId, const SwByteArray& encodedFieldSection) {
        SwHttp3Frame frame(Type::PushPromise);
        frame.m_rawType = typePushPromise();
        frame.m_id = pushId;
        frame.m_payload = encodedFieldSection;
        return frame;
    }

    // Constructs a frame that carries an unrecognized / reserved (GREASE) type unchanged.
    static SwHttp3Frame unknown(std::uint64_t rawType, const SwByteArray& payload) {
        SwHttp3Frame frame(Type::Unknown);
        frame.m_rawType = rawType;
        frame.m_payload = payload;
        return frame;
    }

    // Accessors ----------------------------------------------------------------------------------

    Type type() const { return m_type; }
    std::uint64_t rawType() const { return m_rawType; }
    const SwByteArray& payload() const { return m_payload; }
    std::uint64_t id() const { return m_id; }
    std::uint64_t pushId() const { return m_id; }
    const SettingList& settings() const { return m_settings; }

private:
    explicit SwHttp3Frame(Type type)
        : m_type(type),
          m_rawType(0),
          m_id(0) {
    }

    Type m_type;
    std::uint64_t m_rawType;
    std::uint64_t m_id;
    SwByteArray m_payload;
    SettingList m_settings;
};

//--------------------------------------------------------------------------------------------------
// SwHttp3FrameCodec
//
// Stateless encoder/decoder for HTTP/3 frames. Every fallible operation returns bool and reports
// a human-readable message through the trailing SwString* error out-parameter.
//--------------------------------------------------------------------------------------------------

class SwHttp3FrameCodec {
public:
    static bool encodeFrame(const SwHttp3Frame& frame,
                            SwByteArray& out,
                            SwString* error = nullptr) {
        SwByteArray payload;
        std::uint64_t typeValue = 0;
        if (!buildPayload_(frame, typeValue, payload, error)) {
            return false;
        }

        if (!appendVarInt_(typeValue, out, error)) {
            return false;
        }
        if (!appendVarInt_(static_cast<std::uint64_t>(payload.size()), out, error)) {
            return false;
        }
        out.append(payload);

        clearError_(error);
        return true;
    }

    static bool decodeFrame(const SwByteArray& buffer,
                            std::size_t& offset,
                            SwHttp3Frame& out,
                            SwString* error = nullptr) {
        std::uint64_t typeValue = 0;
        std::uint64_t length = 0;
        if (!readVarInt_(buffer, offset, typeValue, error)) {
            return false;
        }
        if (!readVarInt_(buffer, offset, length, error)) {
            return false;
        }

        SwByteArray payload;
        if (!readSlice_(buffer, offset, length, payload, error)) {
            return false;
        }

        if (typeValue == SwHttp3Frame::typeData()) {
            out = SwHttp3Frame::data(payload);
        } else if (typeValue == SwHttp3Frame::typeHeaders()) {
            out = SwHttp3Frame::headers(payload);
        } else if (typeValue == SwHttp3Frame::typeCancelPush()) {
            std::uint64_t id = 0;
            if (!readSinglePayloadVarInt_(payload, id, error)) {
                return false;
            }
            out = SwHttp3Frame::cancelPush(id);
        } else if (typeValue == SwHttp3Frame::typeSettings()) {
            SwHttp3Frame::SettingList pairs;
            if (!parseSettings_(payload, pairs, error)) {
                return false;
            }
            out = SwHttp3Frame::settings(pairs);
        } else if (typeValue == SwHttp3Frame::typePushPromise()) {
            std::size_t inner = 0;
            std::uint64_t pushId = 0;
            if (!readVarInt_(payload, inner, pushId, error)) {
                return false;
            }
            SwByteArray fieldSection = payload.mid(static_cast<int>(inner),
                                                   static_cast<int>(payload.size() - inner));
            out = SwHttp3Frame::pushPromise(pushId, fieldSection);
        } else if (typeValue == SwHttp3Frame::typeGoAway()) {
            std::uint64_t id = 0;
            if (!readSinglePayloadVarInt_(payload, id, error)) {
                return false;
            }
            out = SwHttp3Frame::goAway(id);
        } else if (typeValue == SwHttp3Frame::typeMaxPushId()) {
            std::uint64_t id = 0;
            if (!readSinglePayloadVarInt_(payload, id, error)) {
                return false;
            }
            out = SwHttp3Frame::maxPushId(id);
        } else {
            // Reserved / GREASE / unrecognized frame type: skip it (payload already consumed) and
            // surface it tagged Unknown so callers can ignore it (RFC 9114 section 9).
            out = SwHttp3Frame::unknown(typeValue, payload);
        }

        clearError_(error);
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

    static bool appendVarInt_(std::uint64_t value, SwByteArray& out, SwString* error) {
        return SwQuicVarIntCodec::encode(value, out, error);
    }

    static bool readVarInt_(const SwByteArray& buffer,
                            std::size_t& offset,
                            std::uint64_t& outValue,
                            SwString* error) {
        return SwQuicVarIntCodec::decode(buffer, offset, outValue, error);
    }

    static bool readSlice_(const SwByteArray& buffer,
                           std::size_t& offset,
                           std::uint64_t length,
                           SwByteArray& outSlice,
                           SwString* error) {
        if (length > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            setError_(error, "HTTP/3 frame length is too large for SwByteArray slicing");
            return false;
        }
        if (offset > buffer.size() ||
            length > static_cast<std::uint64_t>(buffer.size() - offset)) {
            setError_(error, "HTTP/3 frame payload is truncated");
            return false;
        }

        if (length == 0) {
            outSlice = SwByteArray();
        } else {
            outSlice = buffer.mid(static_cast<int>(offset), static_cast<int>(length));
        }
        offset += static_cast<std::size_t>(length);
        return true;
    }

    static bool buildPayload_(const SwHttp3Frame& frame,
                              std::uint64_t& typeValue,
                              SwByteArray& payload,
                              SwString* error) {
        switch (frame.type()) {
        case SwHttp3Frame::Type::Data:
            typeValue = SwHttp3Frame::typeData();
            payload.append(frame.payload());
            return true;
        case SwHttp3Frame::Type::Headers:
            typeValue = SwHttp3Frame::typeHeaders();
            payload.append(frame.payload());
            return true;
        case SwHttp3Frame::Type::CancelPush:
            typeValue = SwHttp3Frame::typeCancelPush();
            return appendVarInt_(frame.id(), payload, error);
        case SwHttp3Frame::Type::Settings:
            typeValue = SwHttp3Frame::typeSettings();
            for (std::size_t i = 0; i < frame.settings().size(); ++i) {
                if (!appendVarInt_(frame.settings()[i].first, payload, error) ||
                    !appendVarInt_(frame.settings()[i].second, payload, error)) {
                    return false;
                }
            }
            return true;
        case SwHttp3Frame::Type::PushPromise:
            typeValue = SwHttp3Frame::typePushPromise();
            if (!appendVarInt_(frame.id(), payload, error)) {
                return false;
            }
            payload.append(frame.payload());
            return true;
        case SwHttp3Frame::Type::GoAway:
            typeValue = SwHttp3Frame::typeGoAway();
            return appendVarInt_(frame.id(), payload, error);
        case SwHttp3Frame::Type::MaxPushId:
            typeValue = SwHttp3Frame::typeMaxPushId();
            return appendVarInt_(frame.id(), payload, error);
        case SwHttp3Frame::Type::Unknown:
            typeValue = frame.rawType();
            payload.append(frame.payload());
            return true;
        }

        setError_(error, "Unsupported HTTP/3 frame type");
        return false;
    }

    static bool readSinglePayloadVarInt_(const SwByteArray& payload,
                                         std::uint64_t& outValue,
                                         SwString* error) {
        std::size_t inner = 0;
        if (!readVarInt_(payload, inner, outValue, error)) {
            return false;
        }
        if (inner != payload.size()) {
            setError_(error, "HTTP/3 frame has trailing bytes after its value");
            return false;
        }
        return true;
    }

    static bool parseSettings_(const SwByteArray& payload,
                               SwHttp3Frame::SettingList& outPairs,
                               SwString* error) {
        outPairs.clear();
        std::size_t inner = 0;
        while (inner < payload.size()) {
            std::uint64_t identifier = 0;
            std::uint64_t value = 0;
            if (!readVarInt_(payload, inner, identifier, error) ||
                !readVarInt_(payload, inner, value, error)) {
                return false;
            }
            outPairs.push_back(std::make_pair(identifier, value));
        }
        return true;
    }
};

#endif
