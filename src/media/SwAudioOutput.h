#pragma once

/**
 * @file src/media/SwAudioOutput.h
 * @ingroup media
 * @brief Declares the high-level audio output facade used by SwMediaPlayer.
 */

#include "media/SwAudioSink.h"
#if defined(_WIN32)
#include "media/SwWasapiAudioSink.h"
#endif

#include <memory>
#include <vector>

class SwAudioOutput {
public:
    SwAudioOutput()
        : m_sink(defaultSink_()) {}

    void setAudioSink(const std::shared_ptr<SwAudioSink>& sink) {
        if (!sink) {
            return;
        }
        m_sink = sink;
    }

    std::shared_ptr<SwAudioSink> audioSink() const { return m_sink; }

    void setDeviceId(const SwString& deviceId) { m_deviceId = deviceId; }
    SwString deviceId() const { return m_deviceId; }

    void setVolume(float volume) { m_volume = std::max(0.0f, std::min(1.0f, volume)); }
    float volume() const { return m_volume; }

    void setMuted(bool muted) { m_muted = muted; }
    bool isMuted() const { return m_muted; }

    bool start(int sampleRate, int channelCount) {
        if (!m_sink) {
            return false;
        }
        const bool opened = m_sink->open(sampleRate, channelCount);
        if (opened) {
            m_sampleRate = sampleRate;
            m_channelCount = channelCount;
            m_active = true;
        }
        return opened;
    }

    void stop() {
        if (m_sink) {
            m_sink->close();
        }
        m_sampleRate = 0;
        m_channelCount = 0;
        m_active = false;
    }

    bool pushFrame(const SwAudioFrame& frame) {
        if (!m_sink || !frame.isValid()) {
            return false;
        }
        SwAudioFrame adjusted = frame;
        if (m_muted || m_volume < 0.999f) {
            adjusted = applyVolume_(frame);
        }
        return m_sink->pushFrame(adjusted);
    }

    std::int64_t playedTimestamp() const {
        return m_sink ? m_sink->playedTimestamp() : -1;
    }

    bool isActive() const { return m_active; }
    int sampleRate() const { return m_sampleRate; }
    int channelCount() const { return m_channelCount; }
    SwString sinkName() const { return m_sink ? SwString(m_sink->name()) : SwString(); }

private:
    SwAudioFrame applyVolume_(const SwAudioFrame& frame) const {
        SwAudioFrame adjusted = frame;
        if (frame.sampleFormat() != SwAudioFrame::SampleFormat::Float32) {
            return adjusted;
        }
        const float gain = m_muted ? 0.0f : m_volume;
        const std::size_t sampleCount = frame.sampleCount() * static_cast<std::size_t>(frame.channelCount());
        if (sampleCount == 0) {
            return adjusted;
        }
        const float* src = reinterpret_cast<const float*>(frame.payload().constData());
        std::vector<float> samples(sampleCount);
        for (std::size_t i = 0; i < sampleCount; ++i) {
            samples[i] = src[i] * gain;
        }
        adjusted.setPayload(SwByteArray(reinterpret_cast<const char*>(samples.data()),
                                        samples.size() * sizeof(float)));
        return adjusted;
    }

    std::shared_ptr<SwAudioSink> m_sink;
    SwString m_deviceId{};
    float m_volume{1.0f};
    bool m_muted{false};
    int m_sampleRate{0};
    int m_channelCount{0};
    bool m_active{false};

    static std::shared_ptr<SwAudioSink> defaultSink_() {
#if defined(_WIN32)
        return std::make_shared<SwWasapiAudioSink>();
#else
        return std::make_shared<SwNullAudioSink>();
#endif
    }
};
