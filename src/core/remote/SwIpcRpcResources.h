#pragma once
#include "SwSharedMemorySignal.h"
#include "SwSet.h"
#include "SwMutex.h"
#include <cstdlib>

namespace sw { namespace ipc { namespace detail {
// Linux mapping leases unlink queues only after their last live mapping.
// Other POSIX targets retain the process-exit response cleanup.
class RpcResponseResources {
public:
    static void own(const SwString& name) {
#if !defined(_WIN32) && !defined(__linux__)
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
#if !defined(_WIN32) && !defined(__linux__)
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
#if !defined(_WIN32) && !defined(__linux__)
        auto& state = instance();
        SwMutexLocker lock(state.mutex);
        if (state.pid != currentPid()) return;
        for (const auto& name : state.names) ::shm_unlink(name.c_str());
#endif
    }
};
}}}
