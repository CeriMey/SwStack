#include "SwMap.h"
#ifndef SWQUICSTREAM_H
#define SWQUICSTREAM_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicFrame.h"

#include <cstdint>
#include <limits>
#include <map>

class SwQuicStream {
public:
    explicit SwQuicStream(std::uint64_t streamId = 0)
        : m_streamId(streamId),
          m_readOffset(0),
          m_finReceived(false),
          m_finalOffset(0) {
    }

    std::uint64_t streamId() const { return m_streamId; }
    std::uint64_t readOffset() const { return m_readOffset; }

    bool receive(std::uint64_t offset,
                 const SwByteArray& data,
                 bool fin,
                 SwString* error = nullptr) {
        const std::uint64_t dataSize = static_cast<std::uint64_t>(data.size());
        if (addWouldOverflow_(offset, dataSize)) {
            setError_(error, "QUIC stream fragment offset overflows");
            return false;
        }

        const std::uint64_t endOffset = offset + dataSize;
        if (fin) {
            if (m_finReceived && m_finalOffset != endOffset) {
                setError_(error, "QUIC stream received conflicting final offsets");
                return false;
            }
            m_finReceived = true;
            m_finalOffset = endOffset;
        }

        if (m_finReceived && endOffset > m_finalOffset) {
            setError_(error, "QUIC stream fragment exceeds final offset");
            return false;
        }

        if (endOffset <= m_readOffset) {
            if (error) {
                *error = SwString();
            }
            return true;
        }

        std::uint64_t fragmentOffset = offset;
        SwByteArray fragment = data;
        if (fragmentOffset < m_readOffset) {
            const std::uint64_t trim = m_readOffset - fragmentOffset;
            if (!byteArraySliceFits_(trim, error) ||
                !byteArraySliceFits_(dataSize - trim, error)) {
                return false;
            }
            fragment = data.mid(static_cast<int>(trim), static_cast<int>(dataSize - trim));
            fragmentOffset = m_readOffset;
        }

        if (fragment.size() > 0) {
            SwMap<std::uint64_t, SwByteArray>::iterator existing =
                m_fragments.find(fragmentOffset);
            if (existing == m_fragments.end() || existing->second.size() < fragment.size()) {
                m_fragments[fragmentOffset] = fragment;
            }
        }

        if (error) {
            *error = SwString();
        }
        return true;
    }

    bool receiveFrame(const SwQuicFrame& frame, SwString* error = nullptr) {
        if (frame.type() != SwQuicFrame::Type::Stream) {
            setError_(error, "QUIC stream can only receive STREAM frames");
            return false;
        }
        if (frame.streamId() != m_streamId) {
            setError_(error, "QUIC STREAM frame belongs to another stream");
            return false;
        }

        return receive(frame.offset(), frame.data(), frame.fin(), error);
    }

    bool hasReadableData() const {
        for (SwMap<std::uint64_t, SwByteArray>::const_iterator it = m_fragments.begin();
             it != m_fragments.end();
             ++it) {
            const std::uint64_t start = it->first;
            const std::uint64_t end = start + static_cast<std::uint64_t>(it->second.size());
            if (start <= m_readOffset && end > m_readOffset) {
                return true;
            }
            if (start > m_readOffset) {
                return false;
            }
        }
        return false;
    }

    SwByteArray readContiguous() {
        SwByteArray out;

        while (!m_fragments.empty()) {
            SwMap<std::uint64_t, SwByteArray>::iterator it = m_fragments.begin();
            const std::uint64_t start = it->first;
            const std::uint64_t size = static_cast<std::uint64_t>(it->second.size());
            const std::uint64_t end = start + size;

            if (end <= m_readOffset) {
                m_fragments.erase(it);
                continue;
            }
            if (start > m_readOffset) {
                break;
            }

            const std::uint64_t trim = m_readOffset - start;
            const std::uint64_t readable = size - trim;
            if (readable > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                break;
            }

            const SwByteArray slice = it->second.mid(static_cast<int>(trim),
                                                     static_cast<int>(readable));
            out.append(slice);
            m_readOffset += readable;
            m_fragments.erase(it);
        }

        return out;
    }

    bool isReceiveComplete() const {
        return m_finReceived && m_readOffset >= m_finalOffset;
    }

private:
    static void setError_(SwString* error, const char* message) {
        if (error) {
            *error = SwString(message);
        }
    }

    static bool addWouldOverflow_(std::uint64_t lhs, std::uint64_t rhs) {
        return lhs > std::numeric_limits<std::uint64_t>::max() - rhs;
    }

    static bool byteArraySliceFits_(std::uint64_t value, SwString* error) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            setError_(error, "QUIC stream fragment is too large for SwByteArray slicing");
            return false;
        }
        return true;
    }

    std::uint64_t m_streamId;
    std::uint64_t m_readOffset;
    bool m_finReceived;
    std::uint64_t m_finalOffset;
    SwMap<std::uint64_t, SwByteArray> m_fragments;
};

#endif
