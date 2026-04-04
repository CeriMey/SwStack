#pragma once

/**
 * @file src/core/runtime/SwRuntimeTelemetry.h
 * @ingroup core_runtime
 * @brief Generic iteration-level runtime telemetry primitives for SwCoreApplication.
 */

#include "SwFiberPool.h"
#include "SwString.h"

#include <chrono>

class SwCoreApplication;

struct SwRuntimeApplicationDescriptor {
    unsigned long long applicationId;
    unsigned long long threadId;
    SwString runtimeKind;
    SwString label;
    bool isGuiRuntime;

    SwRuntimeApplicationDescriptor()
        : applicationId(0),
          threadId(0),
          isGuiRuntime(false) {}
};

struct SwRuntimeIterationSnapshot {
    unsigned long long busyMicroseconds;
    unsigned long long totalMicroseconds;
    double loadPercentage;
    double lastSecondLoadPercentage;
    int postedEventCount;
    int priorityPostedEventCount;
    int timerCount;
    SwFiberPoolStats fiberPoolStats;

    SwRuntimeIterationSnapshot()
        : busyMicroseconds(0),
          totalMicroseconds(0),
          loadPercentage(0.0),
          lastSecondLoadPercentage(0.0),
          postedEventCount(0),
          priorityPostedEventCount(0),
          timerCount(0) {}
};

class SwRuntimeTelemetrySession {
public:
    virtual ~SwRuntimeTelemetrySession() {}

    virtual void onAttached(SwCoreApplication* app) = 0;
    virtual void onDetached() = 0;
    virtual void bindToCurrentThread() {}
    virtual void updateIterationSnapshot(const SwRuntimeIterationSnapshot& snapshot) = 0;
};

struct SwRuntimeApplicationIterationSample {
    long long sampleTimeNs;
    SwRuntimeApplicationDescriptor application;
    SwRuntimeIterationSnapshot snapshot;

    SwRuntimeApplicationIterationSample()
        : sampleTimeNs(0) {}
};
