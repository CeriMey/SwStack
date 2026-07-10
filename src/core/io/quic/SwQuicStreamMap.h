#include "SwPair.h"
#include "SwMap.h"
#ifndef SWQUICSTREAMMAP_H
#define SWQUICSTREAMMAP_H

#include "SwVector.h"
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
        return receiveFrame(frame, 1024 * 1024, 1024, error);
    }

    bool receiveFrame(const SwQuicFrame& frame,
                      std::size_t maxBufferedBytes,
                      std::size_t maxFragments,
                      SwString* error = nullptr) {
        if (frame.type() != SwQuicFrame::Type::Stream) {
            setError_(error, "QUIC stream map can only receive STREAM frames");
            return false;
        }

        SwMap<std::uint64_t, SwQuicStream>::iterator it = m_streams.find(frame.streamId());
        if (it == m_streams.end()) {
            it = m_streams.insert(SwMakePair(frame.streamId(),
                                             SwQuicStream(frame.streamId(), maxBufferedBytes,
                                                          maxFragments))).first;
        }

        return it->second.receiveFrame(frame, error);
    }

    bool hasStream(std::uint64_t streamId) const {
        return m_streams.find(streamId) != m_streams.end();
    }

    SwQuicStream* stream(std::uint64_t streamId) {
        SwMap<std::uint64_t, SwQuicStream>::iterator it = m_streams.find(streamId);
        if (it == m_streams.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const SwQuicStream* stream(std::uint64_t streamId) const {
        SwMap<std::uint64_t, SwQuicStream>::const_iterator it = m_streams.find(streamId);
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

    bool removeStream(std::uint64_t streamId) {
        return m_streams.erase(streamId) != 0;
    }

    // Stream IDs currently tracked, ascending. Lets a higher layer (HTTP/3)
    // iterate received streams without exposing the internal container.
    SwVector<std::uint64_t> streamIds() const {
        SwVector<std::uint64_t> ids;
        ids.reserve(m_streams.size());
        for (SwMap<std::uint64_t, SwQuicStream>::const_iterator it = m_streams.begin();
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

    SwMap<std::uint64_t, SwQuicStream> m_streams;
};

#endif
