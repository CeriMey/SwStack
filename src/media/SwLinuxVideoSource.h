#pragma once

/**
 * @file src/media/SwLinuxVideoSource.h
 * @ingroup media
 * @brief Declares the Linux capture source placeholder used by platform-neutral APIs.
 */

#include "media/SwVideoSource.h"

#if defined(__linux__)

class SwLinuxVideoSource : public SwVideoSource {
public:
    explicit SwLinuxVideoSource(unsigned int /*deviceIndex*/ = 0) {}

    SwString name() const override { return "SwLinuxVideoSource"; }
    bool initialize() { return false; }
    void start() override {}
    void stop() override {}
};

#endif
