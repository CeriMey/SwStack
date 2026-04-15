#pragma once

/**
 * @file src/media/SwLinuxMovieSource.h
 * @ingroup media
 * @brief Declares the Linux movie source placeholder used by platform-neutral source factories.
 */

#include "media/SwMediaTimelineSource.h"
#include "media/SwVideoSource.h"

#if defined(__linux__)

class SwLinuxMovieSource : public SwVideoSource, public SwMediaTimelineSource {
public:
    explicit SwLinuxMovieSource(const std::wstring&) {}

    SwString name() const override { return "SwLinuxMovieSource"; }
    bool initialize() { return false; }
    void start() override {}
    void stop() override {}

    bool isSeekable() const override { return false; }
    std::int64_t durationMs() const override { return -1; }
    std::int64_t positionMs() const override { return -1; }
    bool seek(std::int64_t) override { return false; }
};

#endif
