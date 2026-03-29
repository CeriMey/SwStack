#pragma once

#include "SwList.h"
#include "SwString.h"
#include "core/runtime/SwRuntimeProfiler.h"

struct RuntimeProfilerDashboardStallEntry {
    unsigned long long sequence{0};
    SwRuntimeTimingKind kind{SwRuntimeTimingKind::ManualScope};
    SwString label;
    long long elapsedUs{0};
    long long sampleTimeNs{0};
    SwFiberLane lane{SwFiberLane::Normal};
    unsigned long long threadId{0};
    SwList<unsigned long long> frames;
    SwList<SwRuntimeResolvedFrame> resolvedFrames;
    SwList<SwString> symbols;
    SwString symbolBackend;
    SwString symbolSearchPath;
};

struct RuntimeProfilerDashboardLoadSample {
    long long sampleTimeNs{0};
    double loadPercentage{0.0};
};

struct RuntimeProfilerStackInspectorData {
    unsigned long long sequence{0};
    SwRuntimeTimingKind kind{SwRuntimeTimingKind::ManualScope};
    SwString label;
    long long elapsedUs{0};
    long long thresholdUs{0};
    long long sampleTimeNs{0};
    long long launchTimeNs{0};
    SwFiberLane lane{SwFiberLane::Normal};
    unsigned long long threadId{0};
    SwList<unsigned long long> frames;
    SwList<SwRuntimeResolvedFrame> resolvedFrames;
    SwList<SwString> symbols;
    SwString symbolBackend;
    SwString symbolSearchPath;
};
