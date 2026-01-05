#include "SwCoreApplication.h"
#include "SwEventLoop.h"
#include "SwIpcRpc.h"
#include "SwSharedMemorySignal.h"
#include "SwString.h"
#include "SwTimer.h"
#include "SwRegularExpression.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

namespace {

static uint64_t nowUs() {
    const auto tp = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(tp).count());
}

static bool splitTarget(const SwString& fqn, SwString& domainOut, SwString& objectOut)
{
    SwString x = fqn;
    x.replace('\\', '/');
    x.remove(SwRegularExpression("^/+|/+$"));   // enlève / au début et à la fin

    const int slash = x.indexOf('/');
    if (slash <= 0 || slash >= x.size() - 1) return false;

    domainOut = x.left(slash);
    objectOut = x.mid(slash + 1);
    return true;
}


static SwByteArray makePayload(size_t n) {
    SwByteArray b(n, '\0');
    for (size_t i = 0; i < n; ++i) b.data()[i] = static_cast<char>('a' + (i % 26));
    return b;
}

static uint64_t percentileUs(std::vector<uint64_t> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double x = (p / 100.0) * static_cast<double>(v.size() - 1);
    const size_t i0 = static_cast<size_t>(x);
    const size_t i1 = (i0 + 1 < v.size()) ? (i0 + 1) : i0;
    const double t = x - static_cast<double>(i0);
    const double y = (1.0 - t) * static_cast<double>(v[i0]) + t * static_cast<double>(v[i1]);
    return static_cast<uint64_t>(y);
}

static void printStats(const std::vector<uint64_t>& rttUs, uint64_t totalUs) {
    if (rttUs.empty()) {
        std::cout << "[perf] no samples\n";
        return;
    }
    const uint64_t mn = *std::min_element(rttUs.begin(), rttUs.end());
    const uint64_t mx = *std::max_element(rttUs.begin(), rttUs.end());
    const long double sum = std::accumulate(rttUs.begin(), rttUs.end(), static_cast<long double>(0.0));
    const uint64_t mean = static_cast<uint64_t>(sum / static_cast<long double>(rttUs.size()));
    const uint64_t p50 = percentileUs(rttUs, 50.0);
    const uint64_t p95 = percentileUs(rttUs, 95.0);
    const uint64_t p99 = percentileUs(rttUs, 99.0);
    const double secs = totalUs ? (static_cast<double>(totalUs) / 1e6) : 0.0;
    const double rate = secs > 0.0 ? (static_cast<double>(rttUs.size()) / secs) : 0.0;

    std::cout
        << "[perf] samples=" << rttUs.size()
        << " duration_s=" << secs
        << " rate_hz=" << rate
        << "\n"
        << "[perf] rtt_us min=" << mn
        << " mean=" << mean
        << " p50=" << p50
        << " p95=" << p95
        << " p99=" << p99
        << " max=" << mx
        << "\n";
}

static void usage() {
    std::cout
        << "IpcPerfMonitor (example 25)\n"
        << "\n"
        << "Server:\n"
        << "  IpcPerfMonitor.exe --mode=server --self=<domain/object> --peer=<domain/object> [--work_us=0]\n"
        << "\n"
        << "Client:\n"
        << "  IpcPerfMonitor.exe --mode=client --self=<domain/object> --peer=<domain/object>\n"
        << "                    [--count=1000] [--warmup=50] [--payload=0] [--timeout_ms=1000] [--interval_us=0]\n"
        << "\n"
        << "RPC server:\n"
        << "  IpcPerfMonitor.exe --mode=rpc_server --self=<domain/object> --peer=<domain/object> [--work_us=0]\n"
        << "\n"
        << "RPC client:\n"
        << "  IpcPerfMonitor.exe --mode=rpc_client --self=<domain/object> --peer=<domain/object>\n"
        << "                    [--count=1000] [--warmup=50] [--payload=0] [--timeout_ms=1000] [--interval_us=0]\n";
}

} // namespace

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    const SwString mode = app.getArgument("mode", "");
    if (mode.isEmpty() || mode == "help" || mode == "-h" || mode == "--help") {
        usage();
        return 0;
    }

    const SwString selfFqn = app.getArgument("self", "");
    const SwString peerFqn = app.getArgument("peer", "");

    SwString selfDomain, selfObject, peerDomain, peerObject;
    if (!splitTarget(selfFqn, selfDomain, selfObject)) {
        std::cerr << "[IpcPerfMonitor] invalid --self (expected domain/object)\n";
        usage();
        return 2;
    }
    if (!splitTarget(peerFqn, peerDomain, peerObject)) {
        std::cerr << "[IpcPerfMonitor] invalid --peer (expected domain/object)\n";
        usage();
        return 2;
    }

    const SwString rpcMethod = "perfRpc";

    if (mode == "server") {
        const uint64_t workUs = static_cast<uint64_t>((std::max)(0, app.getArgument("work_us", "0").toInt()));

        sw::ipc::Registry selfReg(selfDomain, selfObject);
        sw::ipc::Registry peerReg(peerDomain, peerObject);

        sw::ipc::Signal<uint64_t, uint64_t, SwByteArray> ping(selfReg, "perfPing");
        sw::ipc::Signal<uint64_t, uint64_t, uint64_t> pong(peerReg, "perfPong");

        std::cout << "[IpcPerfMonitor] server online: self=" << selfFqn.toStdString()
                  << " peer=" << peerFqn.toStdString()
                  << " work_us=" << workUs << "\n";

        auto sub = ping.connect([&](uint64_t seq, uint64_t sendUs, SwByteArray payload) {
            const uint64_t t0 = nowUs();

            // Minimal predictable CPU work (optional).
            if (workUs) {
                const uint64_t end = t0 + workUs;
                while (nowUs() < end) {
                    // busy wait (example purpose)
                }
            } else {
                // Touch the payload to avoid being optimized away in release builds.
                volatile uint64_t acc = 0;
                for (size_t i = 0; i < payload.size(); ++i) acc += static_cast<unsigned char>(payload.data()[i]);
                (void)acc;
            }

            const uint64_t t1 = nowUs();
            const uint64_t serverWork = (t1 >= t0) ? (t1 - t0) : 0;
            pong.publish(seq, sendUs, serverWork);
        }, /*fireInitial=*/false);

        return app.exec();
    }

    if (mode == "rpc_server") {
        const uint64_t workUs = static_cast<uint64_t>((std::max)(0, app.getArgument("work_us", "0").toInt()));

        sw::ipc::Registry selfReg(selfDomain, selfObject);

        sw::ipc::RingQueue<10, uint64_t, uint32_t, SwString, uint64_t, SwByteArray> req(
            selfReg,
            sw::ipc::rpcRequestQueueName(rpcMethod));

        typedef sw::ipc::RingQueue<10, uint64_t, bool, SwString, uint64_t> RespQueue;
        std::mutex respMutex;
        std::map<uint32_t, std::shared_ptr<RespQueue>> respByPid;

        auto getRespQueue = [&](uint32_t clientPid) -> std::shared_ptr<RespQueue> {
            std::lock_guard<std::mutex> lk(respMutex);
            auto it = respByPid.find(clientPid);
            if (it != respByPid.end()) return it->second;
            std::shared_ptr<RespQueue> q(new RespQueue(selfReg, sw::ipc::rpcResponseQueueName(rpcMethod, clientPid)));
            respByPid[clientPid] = q;
            return q;
        };

        std::cout << "[IpcPerfMonitor] rpc server online: self=" << selfFqn.toStdString()
                  << " peer=" << peerFqn.toStdString()
                  << " method=" << rpcMethod.toStdString()
                  << " work_us=" << workUs << "\n";

        auto sub = req.connect([&](uint64_t callId,
                                   uint32_t clientPid,
                                   SwString /*clientInfo*/,
                                   uint64_t /*sendUs*/,
                                   SwByteArray payload) {
            const uint64_t t0 = nowUs();

            // Minimal predictable CPU work (optional).
            if (workUs) {
                const uint64_t end = t0 + workUs;
                while (nowUs() < end) {
                    // busy wait (example purpose)
                }
            } else {
                // Touch the payload to avoid being optimized away in release builds.
                volatile uint64_t acc = 0;
                for (size_t i = 0; i < payload.size(); ++i) acc += static_cast<unsigned char>(payload.data()[i]);
                (void)acc;
            }

            const uint64_t t1 = nowUs();
            const uint64_t serverWork = (t1 >= t0) ? (t1 - t0) : 0;

            const std::shared_ptr<RespQueue> resp = getRespQueue(clientPid);
            if (resp) {
                (void)resp->push(callId, /*ok=*/true, SwString(), serverWork);
            }
        }, /*fireInitial=*/false);

        return app.exec();
    }

    if (mode == "client") {
        const int count = (std::max)(1, app.getArgument("count", "1000").toInt());
        const int warmup = (std::max)(0, app.getArgument("warmup", "50").toInt());
        const int timeoutMs = (std::max)(1, app.getArgument("timeout_ms", "1000").toInt());
        const int intervalUs = (std::max)(0, app.getArgument("interval_us", "0").toInt());
        const int payloadBytes = (std::max)(0, app.getArgument("payload", "0").toInt());

        const size_t maxPayload = 3500;
        const size_t payloadN = static_cast<size_t>((payloadBytes > static_cast<int>(maxPayload)) ? maxPayload : payloadBytes);
        const SwByteArray payload = makePayload(payloadN);

        sw::ipc::Registry selfReg(selfDomain, selfObject);
        sw::ipc::Registry peerReg(peerDomain, peerObject);

        sw::ipc::Signal<uint64_t, uint64_t, SwByteArray> ping(peerReg, "perfPing");
        sw::ipc::Signal<uint64_t, uint64_t, uint64_t> pong(selfReg, "perfPong");

        struct State {
            std::atomic<uint64_t> expectedSeq{0};
            std::atomic_bool gotAck{false};
            std::atomic<uint64_t> lastRttUs{0};
            std::atomic<uint64_t> lastServerWorkUs{0};
            std::atomic<int> waitId{0};
            std::atomic<int> timeoutTimerId{-1};
        };
        State st;

        auto sub = pong.connect([&](uint64_t seq, uint64_t sendUs, uint64_t serverWorkUs) {
            if (seq != st.expectedSeq.load(std::memory_order_acquire)) return;
            const uint64_t t = nowUs();
            const uint64_t rtt = (t >= sendUs) ? (t - sendUs) : 0;
            st.lastRttUs.store(rtt, std::memory_order_release);
            st.lastServerWorkUs.store(serverWorkUs, std::memory_order_release);
            st.gotAck.store(true, std::memory_order_release);

            // Cancel timeout if still pending.
            const int tid = st.timeoutTimerId.exchange(-1, std::memory_order_acq_rel);
            if (tid != -1) {
                SwCoreApplication* a = SwCoreApplication::instance(false);
                if (a) a->removeTimer(tid);
            }

            const int id = st.waitId.load(std::memory_order_acquire);
            if (id != 0) {
                SwCoreApplication::unYieldFiber(id);
            }
        }, /*fireInitial=*/false);

        std::cout << "[IpcPerfMonitor] client start: self=" << selfFqn.toStdString()
                  << " peer=" << peerFqn.toStdString()
                  << " count=" << count
                  << " warmup=" << warmup
                  << " payload=" << payloadN
                  << " timeout_ms=" << timeoutMs
                  << " interval_us=" << intervalUs
                  << "\n";

        SwCoreApplication::instance()->postEvent([&]() {
            // Warmup + measured loops run in a fiber.
            std::vector<uint64_t> rtts;
            rtts.reserve(static_cast<size_t>(count));

            const uint64_t tBegin = nowUs();

            const int total = warmup + count;
            for (int i = 0; i < total; ++i) {
                const uint64_t seq = static_cast<uint64_t>(i);
                st.expectedSeq.store(seq, std::memory_order_release);
                st.gotAck.store(false, std::memory_order_release);

                const int waitId = SwCoreApplication::generateYieldId();
                st.waitId.store(waitId, std::memory_order_release);

                const uint64_t sendUs = nowUs();
                SwCoreApplication* a = SwCoreApplication::instance(false);
                const int timerId = a ? a->addTimer([waitId]() { SwCoreApplication::unYieldFiber(waitId); },
                                                    timeoutMs * 1000,
                                                    /*singleShot=*/true)
                                      : -1;
                st.timeoutTimerId.store(timerId, std::memory_order_release);

                ping.publish(seq, sendUs, payload);

                // Wait until pong callback or timeout un-yields this fiber.
                SwCoreApplication::yieldFiber(waitId);

                // Best-effort cleanup if the timer is still armed.
                const int tid = st.timeoutTimerId.exchange(-1, std::memory_order_acq_rel);
                if (tid != -1 && a) {
                    a->removeTimer(tid);
                }
                st.waitId.store(0, std::memory_order_release);

                if (!st.gotAck.load(std::memory_order_acquire)) {
                    std::cerr << "[IpcPerfMonitor] timeout seq=" << seq << "\n";
                    continue;
                }

                const uint64_t rtt = st.lastRttUs.load(std::memory_order_acquire);
                if (i >= warmup) rtts.push_back(rtt);

                if (intervalUs > 0) {
                    SwEventLoop::swsleep((intervalUs + 999) / 1000);
                }
            }

            const uint64_t tEnd = nowUs();
            printStats(rtts, (tEnd >= tBegin) ? (tEnd - tBegin) : 0);
            // Exit the event loop cleanly so normal destructors run.
            SwCoreApplication* a = SwCoreApplication::instance(false);
            if (a) a->exit(0);
        });

        return app.exec();
    }

    if (mode == "rpc_client") {
        const int count = (std::max)(1, app.getArgument("count", "1000").toInt());
        const int warmup = (std::max)(0, app.getArgument("warmup", "50").toInt());
        const int timeoutMs = (std::max)(1, app.getArgument("timeout_ms", "1000").toInt());
        const int intervalUs = (std::max)(0, app.getArgument("interval_us", "0").toInt());
        const int payloadBytes = (std::max)(0, app.getArgument("payload", "0").toInt());

        const size_t maxPayload = 3500;
        const size_t payloadN = static_cast<size_t>((payloadBytes > static_cast<int>(maxPayload)) ? maxPayload : payloadBytes);
        const SwByteArray payload = makePayload(payloadN);

        sw::ipc::RpcMethodClient<uint64_t, uint64_t, SwByteArray> rpc(peerDomain,
                                                                      peerObject,
                                                                      rpcMethod,
                                                                      /*clientInfo=*/selfFqn);

        struct State {
            std::atomic_bool gotAck{false};
            std::atomic<uint64_t> lastRttUs{0};
            std::atomic<int> waitId{0};
            std::atomic<int> timeoutTimerId{-1};
        };
        State st;

        std::cout << "[IpcPerfMonitor] rpc client start: self=" << selfFqn.toStdString()
                  << " peer=" << peerFqn.toStdString()
                  << " method=" << rpcMethod.toStdString()
                  << " count=" << count
                  << " warmup=" << warmup
                  << " payload=" << payloadN
                  << " timeout_ms=" << timeoutMs
                  << " interval_us=" << intervalUs
                  << "\n";

        SwCoreApplication::instance()->postEvent([&]() {
            // Warmup + measured loops run in a fiber.
            std::vector<uint64_t> rtts;
            rtts.reserve(static_cast<size_t>(count));

            const uint64_t tBegin = nowUs();

            const int total = warmup + count;
            for (int i = 0; i < total; ++i) {
                st.gotAck.store(false, std::memory_order_release);

                const int waitId = SwCoreApplication::generateYieldId();
                st.waitId.store(waitId, std::memory_order_release);

                const uint64_t sendUs = nowUs();
                SwCoreApplication* a = SwCoreApplication::instance(false);
                const int timerId = a ? a->addTimer([waitId]() { SwCoreApplication::unYieldFiber(waitId); },
                                                    timeoutMs * 1000,
                                                    /*singleShot=*/true)
                                      : -1;
                st.timeoutTimerId.store(timerId, std::memory_order_release);

                rpc.callAsync(sendUs,
                              payload,
                              [&, sendUs](const uint64_t& /*serverWorkUs*/) {
                    const uint64_t t = nowUs();
                    const uint64_t rtt = (t >= sendUs) ? (t - sendUs) : 0;
                    st.lastRttUs.store(rtt, std::memory_order_release);
                    st.gotAck.store(true, std::memory_order_release);

                    // Cancel timeout if still pending.
                    const int tid = st.timeoutTimerId.exchange(-1, std::memory_order_acq_rel);
                    if (tid != -1) {
                        SwCoreApplication* a = SwCoreApplication::instance(false);
                        if (a) a->removeTimer(tid);
                    }

                    const int id = st.waitId.load(std::memory_order_acquire);
                    if (id != 0) {
                        SwCoreApplication::unYieldFiber(id);
                    }
                },
                              timeoutMs);

                // Wait until rpc callback or timeout un-yields this fiber.
                SwCoreApplication::yieldFiber(waitId);

                // Best-effort cleanup if the timer is still armed.
                const int tid = st.timeoutTimerId.exchange(-1, std::memory_order_acq_rel);
                if (tid != -1 && a) {
                    a->removeTimer(tid);
                }
                st.waitId.store(0, std::memory_order_release);

                if (!st.gotAck.load(std::memory_order_acquire)) {
                    std::cerr << "[IpcPerfMonitor] rpc timeout i=" << i << "\n";
                    continue;
                }

                const uint64_t rtt = st.lastRttUs.load(std::memory_order_acquire);
                if (i >= warmup) rtts.push_back(rtt);

                if (intervalUs > 0) {
                    SwEventLoop::swsleep((intervalUs + 999) / 1000);
                }
            }

            const uint64_t tEnd = nowUs();
            printStats(rtts, (tEnd >= tBegin) ? (tEnd - tBegin) : 0);
            // Exit the event loop cleanly so normal destructors run.
            SwCoreApplication* a = SwCoreApplication::instance(false);
            if (a) a->exit(0);
        });

        return app.exec();
    }

    std::cerr << "[IpcPerfMonitor] unknown --mode (use server|client|rpc_server|rpc_client)\n";
    usage();
    return 2;
}
