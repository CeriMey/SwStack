/***************************************************************************************************
 * Self-contained validation for:
 *   - DeliveryMode::LatestOnly on RingQueueDynamic (a late joiner gets ONLY the last value)
 *   - DeliveryMode::Replay     (a late joiner replays the bounded backlog)
 *   - SwIpcSignal auto type-sizing (IpcWireBound) + explicit maxBytes override
 *
 * This exercises the new IPC "signal/slot" layer in-process (one publisher + one late
 * subscriber driven by the same event loop), which is enough to prove the cursor/replay
 * semantics deterministically without spawning peers.
 *
 * Copyright (C) 2025 Ariya Consulting — Author: Eymeric O'Neill
 * Apache License 2.0.
 ***************************************************************************************************/

#include "SwCoreApplication.h"
#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"
#include "SwTimer.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace sw::ipc;

// Pump the event loop for ~durationMs WITHOUT calling quit() (quit() sets running=false
// permanently, so a second exec() would return immediately). processEvent() drains the
// LoopPoller (IPC deliveries) each iteration.
static void pump(SwCoreApplication& app, int durationMs) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        app.processEvent(false);
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() >= durationMs) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

static int g_failures = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_failures; }        \
        else { std::printf("ok  : %s\n", msg); }                              \
    } while (0)

// ---- A. Compile-time: auto type-sizing -----------------------------------------------------------
static void testWireSizeTraits() {
    // All-bounded packs report a finite size.
    // NOTE: template-argument commas must not leak into the CHECK() macro, so each trait
    // result is captured in a local first.
    const bool bInt      = size::IpcWireBound<int>::bounded;
    const bool bPod3     = size::IpcWireBound<int, double, bool>::bounded;
    const size_t szPod3  = size::IpcWireBound<int, double, bool>::value;
    const bool uStr      = size::IpcWireBound<SwString>::bounded;
    const bool uByteArr  = size::IpcWireBound<int, SwByteArray>::bounded;
    const bool uList     = size::IpcWireBound<SwList<int> >::bounded;
    const bool uMap      = size::IpcWireBound<SwMap<SwString, int> >::bounded;

    // All-bounded packs report a finite size.
    CHECK(bInt, "IpcWireBound<int> is bounded");
    CHECK(bPod3, "IpcWireBound<int,double,bool> is bounded");
    CHECK(szPod3 == sizeof(int) + sizeof(double) + sizeof(bool),
          "auto size == sum of sizeof for POD pack");

    // Variable-length types are reported unbounded -> they require an explicit maxBytes.
    CHECK(!uStr, "SwString is unbounded");
    CHECK(!uByteArr, "pack with SwByteArray is unbounded");
    CHECK(!uList, "SwList<int> is unbounded");
    CHECK(!uMap, "SwMap is unbounded");
}

// A minimal object that owns IPC signals via the new macros.
class Probe : public SwRemoteObject {
public:
    Probe(const SwString& sys, const SwString& ns, const SwString& obj, SwObject* parent = nullptr)
        : SwRemoteObject(sys, ns, obj, parent) {}

    // Auto-sized POD signal (no maxBytes needed).
    SW_IPC_SIGNAL(counter, int);
    // Sized signal carrying a variable-length payload (override required).
    SW_IPC_SIGNAL_SIZED(label, 128, int, SwString);
    // Latch: only the last value is delivered on connect (capacity 1 + LatestOnly).
    SW_IPC_LATCH_SIZED(state, 128, uint64_t, SwString);
};

// An object exposing remote properties (Q_PROPERTY-like, shared across processes).
class Gadget : public SwRemoteObject {
public:
    Gadget(const SwString& sys, const SwString& ns, const SwString& obj, SwObject* parent = nullptr)
        : SwRemoteObject(sys, ns, obj, parent) {}

    SW_IPC_PROPERTY(int, speed, 0);                              // POD: auto-sized
    SW_IPC_PROPERTY_SIZED(SwString, label, SwString("idle"), 128); // variable: explicit size
};

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    testWireSizeTraits();

    Probe p("selfA", "demo", "probe");

    // Sanity on auto-sizing reaching the ring.
    CHECK(p.counter.maxBytes() >= sizeof(int), "counter ring reserved >= sizeof(int)");
    CHECK(p.label.maxBytes() == 128u, "label ring reserved == explicit 128");
    CHECK(p.state.capacity() == 1u, "latch capacity == 1");

    // ---- B. Publish a backlog BEFORE anyone subscribes -----------------------------------------
    // Use a Replay queue with capacity 16 to hold several messages.
    Registry reg(SwString("selfA"), SwString("probe"));
    RingQueueDynamic<int> replayQ(reg, SwString("replayQ"), 16u, 64u);
    RingQueueDynamic<int> latchQ(reg, SwString("latchQ"), 16u, 64u);

    for (int i = 1; i <= 5; ++i) { replayQ.push(i); latchQ.push(i); }

    std::vector<int> replayGot;
    std::vector<int> latestGot;

    // Late joiners connect AFTER the 5 messages were already published.
    RingQueueDynamic<int>::Subscription subReplay =
        replayQ.connect([&replayGot](int v) { replayGot.push_back(v); },
                        /*fireInitial=*/true, /*timeoutMs=*/0, DeliveryMode::Replay);

    RingQueueDynamic<int>::Subscription subLatest =
        latchQ.connect([&latestGot](int v) { latestGot.push_back(v); },
                       /*fireInitial=*/true, /*timeoutMs=*/0, DeliveryMode::LatestOnly);

    // Pump the event loop briefly so the LoopPoller delivers the initial drain.
    pump(app, 300);

    // Replay: a late joiner gets the bounded backlog (here all 5, capacity allows it),
    // and the most recent value is present.
    CHECK(!replayGot.empty(), "Replay late joiner received the backlog");
    CHECK(!replayGot.empty() && replayGot.back() == 5, "Replay last value == 5");
    CHECK(replayGot.size() >= 2, "Replay delivered more than one message (backlog)");

    // LatestOnly: a late joiner gets EXACTLY the last value, never the older backlog.
    CHECK(latestGot.size() == 1, "LatestOnly delivered exactly ONE message");
    CHECK(!latestGot.empty() && latestGot.back() == 5, "LatestOnly value == last (5)");

    // ---- B2. capacity=1 + LatestOnly latch, two distinct instances over the SAME shm ------------
    // This is exactly the property transport scenario: a producer publishes, then a separate
    // late-joining instance subscribes and must receive the current value.
    {
        Registry r1(SwString("selfA"), SwString("latchobj"));
        RingQueueDynamic<uint64_t, int> prod(r1, SwString("latch1"), 1u, 64u);
        prod.push(999ull, 42);

        Registry r2(SwString("selfA"), SwString("latchobj"));
        RingQueueDynamic<uint64_t, int> cons(r2, SwString("latch1"), 1u, 64u);
        int gotTwo = -1;
        RingQueueDynamic<uint64_t, int>::Subscription sTwo =
            cons.connect([&gotTwo](uint64_t, int v) { gotTwo = v; },
                         true, 0, DeliveryMode::LatestOnly);

        pump(app, 200);
        CHECK(gotTwo == 42, "capacity=1 LatestOnly latch delivers last value cross-instance");
    }

    // ---- C. Remote properties --------------------------------------------------------------------
    Gadget owner("selfA", "props", "gadget");

    // Default is local-only (never published): a fresh property reads its default.
    CHECK(owner.speed() == 0, "property default read == 0");
    CHECK(owner.label() == SwString("idle"), "string property default == 'idle'");

    // Local set: getter reflects the new value, and the local Qt signal fires.
    int speedSeen = -1;
    SwObject::connect(&owner, &Gadget::speedChanged,
                      std::function<void(const int&)>([&speedSeen](const int& v) { speedSeen = v; }));
    owner.set_speed(120);
    CHECK(owner.speed() == 120, "property get reflects set (120)");
    CHECK(speedSeen == 120, "local speedChanged signal fired with 120");

    owner.set_label(SwString("running"));
    CHECK(owner.label() == SwString("running"), "string property set reflected");

    // Late joiner: a second object with the SAME object name shares the SHM latch and must
    // receive the CURRENT value on connect (LatestOnly), proving last-value-on-connect.
    Gadget reader("selfA", "props", "gadget");
    int readerSpeed = -1;
    SwObject::connect(&reader, &Gadget::speedChanged,
                      std::function<void(const int&)>([&readerSpeed](const int& v) { readerSpeed = v; }));

    pump(app, 300);

    CHECK(reader.speed() == 120, "late-joiner property synced to current value (120)");
    CHECK(readerSpeed == 120, "late-joiner received speedChanged on connect (120)");

    // Bidirectional write: reader writes, owner converges (different publisherId -> no echo loop).
    reader.set_speed(7);
    pump(app, 300);
    CHECK(owner.speed() == 7, "bidirectional write: owner converged to reader's value (7)");

    if (g_failures == 0) {
        std::printf("\nALL CHECKS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
