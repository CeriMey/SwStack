#ifndef SWQUICSTREAMMAP_H
#define SWQUICSTREAMMAP_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicFrame.h"
#include "quic/SwQuicStream.h"

#include <cstdint>
#include <map>
#include <cstddef>
#include <vector>

class SwQuicStreamMap {
public:
    bool receiveFrame(const SwQuicFrame& frame, SwString* error = nullptr) {
        if (frame.type() != SwQuicFrame::Type::Stream) {
            setError_(error, "QUIC stream map can only receive STREAM frames");
            return false;
        }

        std::map<std::uint64_t, SwQuicStream>::iterator it = m_streams.find(frame.streamId());
        if (it == m_streams.end()) {
            it = m_streams.insert(std::make_pair(frame.streamId(),
                                                 SwQuicStream(frame.streamId()))).first;
        }

        return it->second.receiveFrame(frame, error);
    }

    bool hasStream(std::uint64_t streamId) const {
        return m_streams.find(streamId) != m_streams.end();
    }

    SwQuicStream* stream(std::uint64_t streamId) {
        std::map<std::uint64_t, SwQuicStream>::iterator it = m_streams.find(streamId);
        if (it == m_streams.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const SwQuicStream* stream(std::uint64_t streamId) const {
        std::map<std::uint64_t, SwQuicStream>::const_iterator it = m_streams.find(streamId);
        if (it == m_streams.end()) {
            return nullptr;
        }
        return &it->second;
    }

    SwByteArray readContiguous(std::uint64_t streamId) {
        SwQuicStream* existing = stream(streamId);
        if (!existing) {
            return SwByteArray();
        }
        return existing->readContiguous();
    }

    std::size_t streamCount() const {
        return m_streams.size();
    }

    // Stream IDs currently tracked, ascending. Lets a higher layer (HTTP/3)
    // iterate received streams without exposing the internal container.
    std::vector<std::uint64_t> streamIds() const {
        std::vector<std::uint64_t> ids;
        ids.reserve(m_streams.size());
        for (std::map<std::uint64_t, SwQuicStream>::const_iterator it = m_streams.begin();
             it != m_streams.end(); ++it) {
            ids.push_back(it->first);
        }
        return ids;
    }

    void clear() {
        m_streams.clear();
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    std::map<std::uint64_t, SwQuicStream> m_streams;
};

#endif
