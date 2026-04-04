#pragma once

#include "SwList.h"
#include "core/io/SwSocketTrafficTelemetry.h"

struct SocketTrafficDashboardHistoryPoint {
    long long sampleTimeNs{0};
    unsigned long long totalRxRateBytesPerSecond{0};
    unsigned long long totalTxRateBytesPerSecond{0};
    unsigned long long selectedConsumerId{0};
    unsigned long long selectedRxRateBytesPerSecond{0};
    unsigned long long selectedTxRateBytesPerSecond{0};
};

struct SocketTrafficInspectorData {
    long long sampleTimeNs{0};
    long long launchTimeNs{0};
    SwSocketTrafficTelemetryConsumerSnapshot consumer;
};
