#pragma once

/**
 * @file src/media/SwAudioFrame.h
 * @ingroup media
 * @brief Declares the normalized PCM audio frame used by SwAudioOutput and SwAudioSink.
 */

#include "core/types/SwByteArray.h"

#include <cstdint>

class SwAudioFrame {
public:
    enum class SampleFormat {
        Unknown,
        Float32
    };

    SwAudioFrame() = default;

    bool isValid() const {
        return m_sampleRate > 0 &&
               m_channelCount > 0 &&
               m_format == SampleFormat::Float32 &&
               !m_payload.isEmpty();
    }

    SampleFormat sampleFormat() const { return m_format; }
    void setSampleFormat(SampleFormat format) { m_format = format; }

    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int sampleRate) { m_sampleRate = sampleRate; }

    int channelCount() const { return m_channelCount; }
    void setChannelCount(int channelCount) { m_channelCount = channelCount; }

    const SwByteArray& payload() const { return m_payload; }
    void setPayload(const SwByteArray& payload) { m_payload = payload; }
    void setPayload(SwByteArray&& payload) { m_payload = std::move(payload); }

    std::int64_t timestamp() const { return m_timestamp; }
    void setTimestamp(std::int64_t timestamp) { m_timestamp = timestamp; }

    std::size_t sampleCount() const {
        if (m_format != SampleFormat::Float32 || m_channelCount <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(m_payload.size()) /
               (sizeof(float) * static_cast<std::size_t>(m_channelCount));
    }

private:
    SampleFormat m_format{SampleFormat::Unknown};
    int m_sampleRate{0};
    int m_channelCount{0};
    SwByteArray m_payload{};
    std::int64_t m_timestamp{-1};
};
