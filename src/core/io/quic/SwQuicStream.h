#include "SwMap.h"
#include "SwVector.h"
#ifndef SWQUICSTREAM_H
#define SWQUICSTREAM_H

#include "SwByteArray.h"
#include "SwString.h"
#include "quic/SwQuicFrame.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

class SwQuicStream {
public:
    explicit SwQuicStream(std::uint64_t streamId = 0,
                          std::size_t maxBufferedBytes = 1024 * 1024,
                          std::size_t maxFragments = 1024)
        : m_streamId(streamId),
          m_readOffset(0),
          m_finReceived(false),
          m_finalOffset(0),
          m_bufferedBytes(0),
          m_maxBufferedBytes(maxBufferedBytes),
          m_maxFragments(maxFragments) {
    }

    std::uint64_t streamId() const { return m_streamId; }
    std::uint64_t readOffset() const { return m_readOffset; }
    std::size_t bufferedBytes() const { return m_bufferedBytes; }
    std::size_t fragmentCount() const { return m_fragments.size(); }

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
            if (endOffset < m_readOffset) {
                setError_(error, "QUIC stream final offset precedes consumed data");
                return false;
            }
            if (!m_finReceived && !m_fragments.empty()) {
                SwMap<std::uint64_t, SwByteArray>::const_iterator last = m_fragments.end();
                --last;
                const std::uint64_t retainedEnd =
                    last->first + static_cast<std::uint64_t>(last->second.size());
                if (retainedEnd > endOffset) {
                    setError_(error, "QUIC stream final offset precedes received data");
                    return false;
                }
            }
        }

        if (m_finReceived && endOffset > m_finalOffset) {
            setError_(error, "QUIC stream fragment exceeds final offset");
            return false;
        }

        if (endOffset <= m_readOffset) {
            if (fin) {
                m_finReceived = true;
                m_finalOffset = endOffset;
            }
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
            if (fragment.size() >
                static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
                setError_(error, "QUIC stream fragment exceeds SwByteArray slice limits");
                return false;
            }
            const std::uint64_t fragmentEnd =
                fragmentOffset + static_cast<std::uint64_t>(fragment.size());
            struct PendingFragment_ {
                std::uint64_t offset;
                SwByteArray data;
            };
            SwVector<PendingFragment_> additions;
            std::uint64_t cursor = fragmentOffset;
            std::size_t addedBytes = 0;

            // Retain disjoint intervals without repeatedly concatenating the
            // whole reassembly buffer. Incremental out-of-order delivery is
            // therefore O(total bytes), not O(total bytes squared). Only the
            // uncovered portions of this frame are copied into new fragments.
            for (SwMap<std::uint64_t, SwByteArray>::const_iterator it = m_fragments.begin();
                 it != m_fragments.end(); ++it) {
                const std::uint64_t existingStart = it->first;
                const std::uint64_t existingEnd =
                    existingStart + static_cast<std::uint64_t>(it->second.size());
                if (existingEnd <= fragmentOffset) continue;
                if (existingStart >= fragmentEnd) break;

                if (existingStart > cursor) {
                    const std::uint64_t uncoveredEnd =
                        existingStart < fragmentEnd ? existingStart : fragmentEnd;
                    const std::size_t uncoveredLength =
                        static_cast<std::size_t>(uncoveredEnd - cursor);
                    PendingFragment_ pending;
                    pending.offset = cursor;
                    pending.data = fragment.mid(
                        static_cast<int>(cursor - fragmentOffset),
                        static_cast<int>(uncoveredLength));
                    additions.push_back(std::move(pending));
                    addedBytes += uncoveredLength;
                }

                const std::uint64_t overlapStart =
                    existingStart > fragmentOffset ? existingStart : fragmentOffset;
                const std::uint64_t overlapEnd =
                    existingEnd < fragmentEnd ? existingEnd : fragmentEnd;
                if (overlapStart < overlapEnd) {
                    const std::size_t existingIndex =
                        static_cast<std::size_t>(overlapStart - existingStart);
                    const std::size_t fragmentIndex =
                        static_cast<std::size_t>(overlapStart - fragmentOffset);
                    const std::size_t overlapLength =
                        static_cast<std::size_t>(overlapEnd - overlapStart);
                    if (std::memcmp(it->second.constData() + existingIndex,
                                    fragment.constData() + fragmentIndex,
                                    overlapLength) != 0) {
                        setError_(error, "QUIC stream received conflicting overlapping data");
                        return false;
                    }
                }
                if (existingEnd > cursor) cursor = existingEnd;
            }

            if (cursor < fragmentEnd) {
                const std::size_t uncoveredLength =
                    static_cast<std::size_t>(fragmentEnd - cursor);
                PendingFragment_ pending;
                pending.offset = cursor;
                if (cursor == fragmentOffset && uncoveredLength == fragment.size()) {
                    pending.data = std::move(fragment);
                } else {
                    pending.data = fragment.mid(
                        static_cast<int>(cursor - fragmentOffset),
                        static_cast<int>(uncoveredLength));
                }
                additions.push_back(std::move(pending));
                addedBytes += uncoveredLength;
            }

            if (m_bufferedBytes > m_maxBufferedBytes ||
                addedBytes > m_maxBufferedBytes - m_bufferedBytes ||
                m_fragments.size() > m_maxFragments ||
                additions.size() > m_maxFragments - m_fragments.size()) {
                setError_(error, "QUIC stream reassembly budget exceeded");
                return false;
            }

            for (std::size_t i = 0; i < additions.size(); ++i) {
                m_fragments[additions[i].offset] = std::move(additions[i].data);
            }
            m_bufferedBytes += addedBytes;
        }

        if (fin) {
            m_finReceived = true;
            m_finalOffset = endOffset;
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
                m_bufferedBytes -= static_cast<std::size_t>(it->second.size());
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

            if (out.isEmpty() && trim == 0) {
                out = std::move(it->second);
            } else {
                const SwByteArray slice = it->second.mid(static_cast<int>(trim),
                                                         static_cast<int>(readable));
                out.append(slice);
            }
            m_readOffset += readable;
            m_bufferedBytes -= static_cast<std::size_t>(size);
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
    std::size_t m_bufferedBytes;
    std::size_t m_maxBufferedBytes;
    std::size_t m_maxFragments;
    SwMap<std::uint64_t, SwByteArray> m_fragments;
};

#endif
