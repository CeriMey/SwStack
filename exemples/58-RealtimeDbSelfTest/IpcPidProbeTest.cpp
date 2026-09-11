#include "SwSharedMemorySignal.h"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    try {
        using sw::ipc::detail::PidProbePass_;
        using sw::ipc::detail::PidState;
        const uint32_t self = 100;
        std::map<uint32_t, unsigned> calls;
        std::map<uint32_t, PidState> states;
        states[200] = PidState::Alive;
        states[300] = PidState::Dead;
        states[400] = PidState::Unknown;
        auto probe = [&](uint32_t pid) { ++calls[pid]; return states.at(pid); };
        PidProbePass_<512> pass(self);
        for (int i = 0; i < 512; ++i) {
            require(!pass.stale(200, 1000, 2000, 15000, true, probe), "live duplicate removed");
            require(pass.stale(300, 1000, 2000, 15000, true, probe), "dead duplicate retained");
            require(!pass.stale(400, 1000, 2000, 15000, true, probe), "unknown peer removed");
            require(!pass.stale(self, 1000, 2000, 15000, true, probe), "own process removed");
        }
        require(calls.size() == 3 && calls[200] == 1 && calls[300] == 1 && calls[400] == 1,
                "each distinct peer must be probed once, never self");
        require(pass.stale(0, 1000, 2000, 15000, true, probe), "zero PID retained");
        require(pass.stale(500, 0, 2000, 15000, true, probe), "missing heartbeat retained");
        require(pass.stale(500, 1000, 16001, 15000, true, probe), "expired peer retained");
        require(pass.stale(self, 1000, 16001, 15000, true, probe), "expired self bypassed TTL");
        require(calls.size() == 3, "invalid/expired entries must not query process state");
        require(!pass.stale(200, 1000, 16000, 15000, true, probe), "TTL equality expired early");
        require(!pass.stale(200, 3000, 2000, 15000, true, probe), "future heartbeat underflowed");
        require(!pass.stale(200, 1000, 16001, 15000, false, probe), "foreign time base used TTL");
        require(pass.stale(200, 1000, 16001, 15000, true, probe), "cached alive result bypassed TTL");
        states[200] = PidState::Dead;
        states[300] = PidState::Alive; // a reused PID must be observed on a new pass
        states[400] = PidState::Alive;
        PidProbePass_<512> next(self);
        require(next.stale(200, 1000, 2000, 15000, true, probe), "new pass missed peer exit");
        require(!next.stale(300, 1000, 2000, 15000, true, probe), "new pass retained dead PID state");
        require(!next.stale(400, 1000, 2000, 15000, true, probe), "new pass missed unknown recovery");
        require(calls[200] == 2 && calls[300] == 2 && calls[400] == 2,
                "liveness leaked between cleanup passes");
        std::cout << "PID probes preserve cleanup rules with one observation per peer/pass\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
