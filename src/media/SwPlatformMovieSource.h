#pragma once

/**
 * @file src/media/SwPlatformMovieSource.h
 * @ingroup media
 * @brief Exposes the platform movie source through a neutral name.
 */

#if defined(_WIN32)
#include "media/SwMediaFoundationMovieSource.h"
using SwPlatformMovieSource = SwMediaFoundationMovieSource;
#else
// Every non-Windows platform gets the dependency-free native MP4 movie source.
#include "media/SwMp4MovieSource.h"
using SwPlatformMovieSource = SwMp4MovieSource;
#endif
