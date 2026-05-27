#pragma once

/**
 * @file src/media/server/SwMediaServerConfig.h
 * @brief Top-level media server configuration.
 */

#include "core/types/SwString.h"
#include "media/transport/SwTransportEndpoint.h"

#include <cstdint>

struct SwMediaServerConfig {
    SwString name{"SwMediaServer"};
    SwTransportEndpoint endpoint{};
    bool lowLatency{true};
    uint16_t defaultLatencyBudgetMs{90};
    uint16_t maxClients{1};
    uint16_t mtuBytes{1200};
};
