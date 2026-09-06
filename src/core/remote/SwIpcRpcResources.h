#pragma once
#include "SwSharedMemorySignal.h"
#include "SwSet.h"
#include "SwMutex.h"
#include <cstdlib>

namespace sw { namespace ipc { namespace detail {
// Response queues are addressed to one PID; their names must not survive that
// process indefinitely. Request queues remain stable for application restarts.
class RpcResponseResources {
public:
    static void own(const SwString& name) {
#ifndef _WIN32
        auto& state = instance();
        SwMutexLocker lock(state.mutex);
        if (state.pid != currentPid()) {
            state.names.clear(); state.pid = currentPid();
        }
        state.names.insert(name);
#endif
    }
    static bool deadClient(const SwString& response) {
        if (!response.startsWith("__rpc_ret__|")) return false;
        const auto suffix = response.mid(static_cast<int>(response.lastIndexOf('|') + 1));
        if (!suffix.isInt()) return false;
        bool valid = false;
        const auto pid = suffix.toUInt(&valid);
        return valid && pid > 0 && pidStateBestEffort_(pid) == PidState::Dead;
    }
    template<class Map> static void prune(Map& queues, const Registry& registry) {
        for (auto it = queues.begin(); it != queues.end();) {
            if (!deadClient(it.key())) { ++it; continue; }
#ifndef _WIN32
            ::shm_unlink(make_shm_name(registry.domain(), registry.object(), it.key()).c_str());
#endif
            it = queues.erase(it);
        }
    }
private:
    struct State {
        SwMutex mutex;
        uint32_t pid{currentPid()};
        SwSet<SwString> names;
    };
    static State& instance() {
        static auto* state = new State;
        static const bool registered = [] { std::atexit(&cleanup); return true; }();
        (void)registered;
        return *state;
    }
    static void cleanup() {
#ifndef _WIN32
        auto& state = instance();
        SwMutexLocker lock(state.mutex);
        if (state.pid != currentPid()) return;
        for (const auto& name : state.names) ::shm_unlink(name.c_str());
#endif
    }
};
}}}
