#pragma once

/**
 * @file src/media/SwAudioSink.h
 * @ingroup media
 * @brief Declares low-level audio sink backends used by SwAudioOutput.
 */

#include "SwDebug.h"
#include "media/SwAudioFrame.h"

class SwAudioSink {
public:
    virtual ~SwAudioSink() = default;

    virtual const char* name() const = 0;
    virtual bool open(int sampleRate, int channelCount) = 0;
    virtual void close() = 0;
    virtual bool pushFrame(const SwAudioFrame& frame) = 0;
    virtual std::int64_t playedTimestamp() const { return -1; }
};

class SwNullAudioSink : public SwAudioSink {
public:
    const char* name() const override { return "SwNullAudioSink"; }

    bool open(int sampleRate, int channelCount) override {
        SW_UNUSED(sampleRate);
        SW_UNUSED(channelCount);
        return true;
    }

    void close() override {}

    bool pushFrame(const SwAudioFrame& frame) override {
        m_lastTimestamp = frame.timestamp();
        return frame.isValid();
    }

    std::int64_t playedTimestamp() const override { return m_lastTimestamp; }

private:
    std::int64_t m_lastTimestamp{-1};
};
