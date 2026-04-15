#pragma once

/**
 * @file src/media/SwPlatformVideoDecoder.h
 * @ingroup media
 * @brief Aggregates the active platform video decoder backends behind a neutral include.
 */

#if defined(_WIN32)
#include "media/SwMediaFoundationVideoDecoder.h"
#elif defined(__linux__)
#include "media/SwLinuxVideoDecoder.h"
#endif
