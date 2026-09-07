#pragma once

/** Central native IO dispatcher. Include this header on every platform. */
#if defined(_WIN32)
#include "SwIoDispatcherWin.h"
#elif defined(__linux__)
#include "SwIoDispatcherLinux.h"
#else
#error "SwIoDispatcher requires Windows or Linux"
#endif
