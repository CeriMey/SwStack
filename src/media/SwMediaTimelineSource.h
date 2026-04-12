#pragma once

/**
 * @file src/media/SwMediaTimelineSource.h
 * @ingroup media
 * @brief Declares optional timeline/seek capabilities for media sources.
 */

#include <cstdint>

class SwMediaTimelineSource {
public:
    virtual ~SwMediaTimelineSource() = default;

    virtual bool isSeekable() const { return false; }
    virtual std::int64_t durationMs() const { return -1; }
    virtual std::int64_t positionMs() const { return -1; }
    virtual bool seek(std::int64_t positionMs) {
        (void)positionMs;
        return false;
    }
};
