#ifndef SWQUICCONNECTIONID_H
#define SWQUICCONNECTIONID_H

#include "SwByteArray.h"
#include "SwString.h"

#include <cstddef>

class SwQuicConnectionId {
public:
    static const std::size_t kMaxLength = 20;

    SwQuicConnectionId() = default;

    explicit SwQuicConnectionId(const SwByteArray& bytes) {
        setBytes(bytes);
    }

    static bool fromBytes(const SwByteArray& bytes,
                          SwQuicConnectionId& outConnectionId,
                          SwString* error = nullptr) {
        SwQuicConnectionId id;
        if (!id.setBytes(bytes, error)) {
            return false;
        }
        outConnectionId = id;
        return true;
    }

    bool setBytes(const SwByteArray& bytes, SwString* error = nullptr) {
        if (bytes.size() > kMaxLength) {
            setError_(error, "QUIC connection ID is longer than 20 bytes");
            return false;
        }

        m_bytes = bytes;
        if (error) {
            *error = SwString();
        }
        return true;
    }

    const SwByteArray& bytes() const { return m_bytes; }
    std::size_t size() const { return m_bytes.size(); }
    bool isEmpty() const { return m_bytes.size() == 0; }

    bool operator==(const SwQuicConnectionId& other) const {
        return m_bytes == other.m_bytes;
    }

    bool operator!=(const SwQuicConnectionId& other) const {
        return !(*this == other);
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    SwByteArray m_bytes;
};

#endif
