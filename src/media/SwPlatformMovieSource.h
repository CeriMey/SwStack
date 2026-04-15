#pragma once

/**
 * @file src/media/SwPlatformMovieSource.h
 * @ingroup media
 * @brief Exposes the platform movie source through a neutral name.
 */

#if defined(_WIN32)
#include "media/SwMediaFoundationMovieSource.h"
using SwPlatformMovieSource = SwMediaFoundationMovieSource;
#elif defined(__linux__)
#include "media/SwLinuxMovieSource.h"
using SwPlatformMovieSource = SwLinuxMovieSource;
#else
#include "media/SwMediaFoundationMovieSource.h"
using SwPlatformMovieSource = SwMediaFoundationMovieSource;
#endif
