#pragma once
#include "SwEmbeddedDb.h"
#include <cstddef>

// Storage is persistent by default. Applications select storage.persistent=false
// for a process-local, disposable state database.
struct SwRealtimeDbOptions {
    SwEmbeddedDbOptions storage;
    std::size_t defaultMaxRows{1024};
    SwString defaultOverflowPolicy{"reject"};
    // Limits for the built-in evaluator used by SwRealtimeDbNode.
    int viewBudgetMs{5};
    std::size_t viewHeapLimitBytes{4 * 1024 * 1024};
    // Opt-in: custom Transform callbacks must be thread-safe when nonzero.
    // Only independent evaluations run concurrently; storage commits stay serialized.
    unsigned viewWorkerCount{0};
    SwRealtimeDbOptions() { storage.dbPath = "rtdb"; }
};
