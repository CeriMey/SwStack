#pragma once

/**
 * @file src/media/SwPlatformVideoSource.h
 * @ingroup media
 * @brief Exposes the platform capture source through a neutral name.
 */

#if defined(_WIN32)
#include "media/SwMediaFoundationVideoSource.h"
using SwPlatformVideoSource = SwMediaFoundationVideoSource;
#elif defined(__linux__)
#include "media/SwLinuxVideoSource.h"
using SwPlatformVideoSource = SwLinuxVideoSource;
#else
#include "media/SwMediaFoundationVideoSource.h"
using SwPlatformVideoSource = SwMediaFoundationVideoSource;
#endif
