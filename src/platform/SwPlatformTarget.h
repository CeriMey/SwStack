#pragma once

// Centralize platform feature selection so Android does not accidentally
// follow the Linux/X11 desktop path.
#if defined(_WIN32)
#define SW_PLATFORM_WIN32 1
#else
#define SW_PLATFORM_WIN32 0
#endif

#if defined(__ANDROID__)
#define SW_PLATFORM_ANDROID 1
#else
#define SW_PLATFORM_ANDROID 0
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#define SW_PLATFORM_X11 1
#else
#define SW_PLATFORM_X11 0
#endif

#if defined(__APPLE__)
#define SW_PLATFORM_APPLE 1
#else
#define SW_PLATFORM_APPLE 0
#endif

