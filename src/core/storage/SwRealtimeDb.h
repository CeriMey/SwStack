#pragma once

#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwRealtimeDbOptions.h"

#include <functional>
#include <memory>


// Serialized, bounded in-memory state. Call only from its owning runtime thread.
// Protocol failures throw std::runtime_error; execute returns the response data.
class SwRealtimeDb {
public:
    using Transform = std::function<SwJsonArray(const SwString& script,
                                               const SwJsonObject& tables)>;

    using ViewProgram = std::function<SwJsonArray(const SwJsonObject& tables)>;
    using PrepareView = std::function<ViewProgram(const SwString& script)>;

    explicit SwRealtimeDb(Transform transform = {}, const SwRealtimeDbOptions& options = {}, PrepareView prepare = {});
    virtual ~SwRealtimeDb();
    SwRealtimeDb(const SwRealtimeDb&) = delete;
    SwRealtimeDb& operator=(const SwRealtimeDb&) = delete;

    // mutate_many executes up to 64 write/erase operations in order. Its
    // complete/results response describes partial commits; it is not a transaction.
    SwJsonObject execute(const SwJsonObject& request);
    void expire();

private:
    struct State;
    std::unique_ptr<State> state_;
};
