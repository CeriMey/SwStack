#include "SwCoreApplication.h"
#include "SwEventLoop.h"
#include "SwProxyObject.h"
#include "SwProcess.h"
#include "SwRemoteObject.h"
#include "SwString.h"
#include "SwThread.h"
#include "SwTimer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

static uint64_t nowUs() {
    const auto tp = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(tp).count());
}

static int toInt(const SwString& s, int def) {
    try {
        const std::string t = s.toStdString();
        if (t.empty()) return def;
        return std::atoi(t.c_str());
    } catch (...) {
        return def;
    }
}

static uint64_t toU64(const SwString& s, uint64_t def) {
    try {
        const std::string t = s.toStdString();
        if (t.empty()) return def;
        return static_cast<uint64_t>(std::strtoull(t.c_str(), nullptr, 10));
    } catch (...) {
        return def;
    }
}

static bool splitTarget(const SwString& fqn, SwString& domainOut, SwString& objectOut) {
    SwString x = fqn;
    x.replace("\\", "/");
    std::string s = x.toStdString();
    while (!s.empty() && s.front() == '/') s.erase(s.begin());
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return false;
    const size_t slash = s.find('/');
    if (slash == std::string::npos) return false;
    domainOut = SwString(s.substr(0, slash));
    objectOut = SwString(s.substr(slash + 1));
    return !domainOut.isEmpty() && !objectOut.isEmpty();
}

static std::vector<SwString> splitList(const SwString& s, char sep) {
    std::vector<SwString> out;
    const SwList<SwString> parts = s.split(sep);
    for (size_t i = 0; i < parts.size(); ++i) {
        SwString t = parts[i].trimmed();
        if (!t.isEmpty()) out.push_back(t);
    }
    return out;
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

static void printStats(const std::string& label, const std::vector<uint64_t>& samplesUs) {
    if (samplesUs.empty()) {
        std::cout << "[perf] " << label << " no samples\n";
        return;
    }
    const uint64_t mn = *std::min_element(samplesUs.begin(), samplesUs.end());
    const uint64_t mx = *std::max_element(samplesUs.begin(), samplesUs.end());
    const long double sum = std::accumulate(samplesUs.begin(), samplesUs.end(), static_cast<long double>(0.0));
    const uint64_t mean = static_cast<uint64_t>(sum / static_cast<long double>(samplesUs.size()));
    const uint64_t p50 = percentileUs(samplesUs, 50.0);
    const uint64_t p95 = percentileUs(samplesUs, 95.0);
    const uint64_t p99 = percentileUs(samplesUs, 99.0);

    std::cout
        << "[perf] " << label
        << " samples=" << samplesUs.size()
        << " min=" << mn
        << " mean=" << mean
        << " p50=" << p50
        << " p95=" << p95
        << " p99=" << p99
        << " max=" << mx
        << "\n";
}

static uint64_t threadTagNow() {
    const size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return static_cast<uint64_t>(h);
}

static void usage() {
    std::cout
        << "IpcThreadStress (example 26)\n"
        << "\n"
        << "Bench (spawns N client processes, in-process server):\n"
        << "  IpcThreadStress.exe --mode=bench [--ns=bench] [--clients=4] [--count=200] [--warmup=20]\n"
        << "                     [--timeout_ms=1000] [--signal_samples=50] [--tick_ms=5]\n"
        << "\n"
        << "Server only:\n"
        << "  IpcThreadStress.exe --mode=server [--ns=bench] [--tick_ms=5]\n"
        << "\n"
        << "Client only:\n"
        << "  IpcThreadStress.exe --mode=client --targets=bench/A,bench/B,bench/W1 --bench_target=bench/W1\n"
        << "                     [--count=200] [--warmup=20] [--timeout_ms=1000]\n"
        << "                     [--signal_target=bench/W1] [--signal_samples=50]\n";
}

} // namespace

class BenchObject : public SwRemoteObject {
public:
    SW_REGISTER_SHM_SIGNAL(tick, uint64_t, uint64_t, SwString);

    BenchObject(const SwString& ns,
                const SwString& objectName,
                SwObject* parent = nullptr)
        : SwRemoteObject(ns, /*nameSpace=*/SwString(), objectName, parent) {
        ipcExposeRpc(add, [](int a, int b) { return a + b; });
        ipcExposeRpc(who, [this]() { return this->getObjectName(); });
        ipcExposeRpc(threadTag, []() { return threadTagNow(); });
    }

    void startTicking(int intervalMs) {
        auto self = this;
        ThreadHandle* targetThread = this->threadHandle();
        ThreadHandle* currentThread = ThreadHandle::currentThread();

        auto startFn = [self, intervalMs]() {
            if (!self->tickTimer_) {
                self->tickTimer_ = new SwTimer(intervalMs, self);
                self->tickTimer_->setSingleShot(false);
                self->tickTimer_->connect(self->tickTimer_, SIGNAL(timeout), [self]() {
                    const uint64_t seq = self->tickSeq_.fetch_add(1, std::memory_order_relaxed) + 1;
                    self->tick.publish(seq, nowUs(), self->getObjectName());
                });
            }
            self->tickTimer_->start(intervalMs);
        };

        if (!targetThread || targetThread == currentThread) {
            startFn();
        } else {
            targetThread->postTask(startFn);
        }
    }

    void stopTicking() {
        if (tickTimer_) tickTimer_->stop();
    }

private:
    SwTimer* tickTimer_ = nullptr;
    std::atomic<uint64_t> tickSeq_{0};
};

SW_PROXY_OBJECT_CLASS_BEGIN(BenchRemote)
    SW_PROXY_OBJECT_RPC(int, add, int, int)
    SW_PROXY_OBJECT_RPC(SwString, who)
    SW_PROXY_OBJECT_RPC(uint64_t, threadTag)
SW_PROXY_OBJECT_CLASS_END()

static int clientMain(SwCoreApplication& app) {
    const SwString targetsArg = app.getArgument("targets", "");
    const SwString benchTargetArg = app.getArgument("bench_target", "");
    const SwString clientId = app.getArgument("client_id", "0");

    const int count = (std::max)(1, toInt(app.getArgument("count", "200"), 200));
    const int warmup = (std::max)(0, toInt(app.getArgument("warmup", "20"), 20));
    const int timeoutMs = (std::max)(1, toInt(app.getArgument("timeout_ms", "1000"), 1000));

    const int signalSamples = (std::max)(0, toInt(app.getArgument("signal_samples", "0"), 0));
    const SwString signalTargetArg = app.getArgument("signal_target", benchTargetArg);

    if (targetsArg.isEmpty()) {
        usage();
        return 1;
    }

    const std::vector<SwString> targets = splitList(targetsArg, ',');
    if (targets.empty()) {
        usage();
        return 1;
    }

    SwString benchTarget = benchTargetArg;
    if (benchTarget.isEmpty()) {
        benchTarget = targets.front();
    }

    app.postEvent([&app, targets, benchTarget, clientId, count, warmup, timeoutMs, signalSamples, signalTargetArg]() mutable {
        const std::string prefix = std::string("[client ") + clientId.toStdString() + "] ";

        std::cout << prefix << "targets=" << targets.size()
                  << " bench_target=" << benchTarget.toStdString()
                  << " count=" << count
                  << " warmup=" << warmup
                  << " timeout_ms=" << timeoutMs
                  << " signal_samples=" << signalSamples
                  << "\n";

        // Quick routing validation on all targets.
        for (size_t i = 0; i < targets.size(); ++i) {
            SwString dom, obj;
            if (!splitTarget(targets[i], dom, obj)) {
                std::cout << prefix << "bad target: " << targets[i].toStdString() << "\n";
                continue;
            }

            BenchRemote remote(dom, obj, SwString("IpcThreadStress#") + clientId);
            const SwString who = remote.who(timeoutMs);
            const uint64_t tag = remote.threadTag(timeoutMs);
            const SwString whoErr = remote.whoLastError();
            const SwString tagErr = remote.threadTagLastError();

            std::cout << prefix << "check " << targets[i].toStdString()
                      << " who=" << who.toStdString()
                      << " threadTag=" << tag
                      << (whoErr.isEmpty() ? "" : (" whoErr=" + whoErr.toStdString()))
                      << (tagErr.isEmpty() ? "" : (" tagErr=" + tagErr.toStdString()))
                      << "\n";

            if (!whoErr.isEmpty() || !tagErr.isEmpty() || who != obj) {
                std::cout << prefix << "ROUTING_MISMATCH expected=" << obj.toStdString() << "\n";
            }
        }

        // Optional signal benchmark (server -> clients).
        if (signalSamples > 0 && !signalTargetArg.isEmpty()) {
            SwString dom, obj;
            if (splitTarget(signalTargetArg, dom, obj)) {
                sw::ipc::Registry reg(dom, obj);
                sw::ipc::Signal<uint64_t, uint64_t, SwString> tick(reg, "tick");

                std::vector<uint64_t> latUs;
                latUs.reserve(static_cast<size_t>(signalSamples));
                uint64_t lastSeq = 0;
                int missed = 0;

                SwEventLoop loop;
                auto sub = tick.connect([&](uint64_t seq, uint64_t sendUs, SwString sender) {
                    const uint64_t t = nowUs();
                    const uint64_t d = (t >= sendUs) ? (t - sendUs) : 0;
                    latUs.push_back(d);
                    if (lastSeq != 0 && seq != lastSeq + 1) {
                        missed += static_cast<int>(seq - (lastSeq + 1));
                    }
                    lastSeq = seq;
                    if (static_cast<int>(latUs.size()) >= signalSamples) {
                        loop.quit();
                    }
                }, /*fireInitial=*/false);

                loop.exec(0);
                sub.stop();

                std::cout << prefix << "signal tick from " << signalTargetArg.toStdString()
                          << " samples=" << latUs.size()
                          << " missed=" << missed
                          << "\n";
                printStats(prefix + std::string("tick_latency_us ") + signalTargetArg.toStdString(), latUs);
            }
        }

        // RPC bench on benchTarget.
        SwString dom, obj;
        if (!splitTarget(benchTarget, dom, obj)) {
            std::cout << prefix << "bad bench_target: " << benchTarget.toStdString() << "\n";
            app.quit();
            return;
        }

        BenchRemote remote(dom, obj, SwString("IpcThreadStress#") + clientId);

        // Warmup (stabilize caches).
        for (int i = 0; i < warmup; ++i) {
            (void)remote.add(1, 2, timeoutMs);
            (void)remote.who(timeoutMs);
        }

        // add()
        int addErrors = 0;
        std::vector<uint64_t> addRtt;
        addRtt.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            (void)remote.add(123, 456, timeoutMs);
            const uint64_t t1 = nowUs();
            addRtt.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.addLastError().isEmpty()) ++addErrors;
        }
        std::cout << prefix << "rpc " << benchTarget.toStdString() << "#add"
                  << " errors=" << addErrors << "\n";
        printStats(prefix + std::string("rpc_rtt_us ") + benchTarget.toStdString() + "#add", addRtt);

        // who()
        int whoErrors = 0;
        std::vector<uint64_t> whoRtt;
        whoRtt.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const uint64_t t0 = nowUs();
            (void)remote.who(timeoutMs);
            const uint64_t t1 = nowUs();
            whoRtt.push_back((t1 >= t0) ? (t1 - t0) : 0);
            if (!remote.whoLastError().isEmpty()) ++whoErrors;
        }
        std::cout << prefix << "rpc " << benchTarget.toStdString() << "#who"
                  << " errors=" << whoErrors << "\n";
        printStats(prefix + std::string("rpc_rtt_us ") + benchTarget.toStdString() + "#who", whoRtt);

        app.quit();
    });

    return app.exec();
}

static int serverMain(SwCoreApplication& app) {
    const SwString ns = app.getArgument("ns", "bench");
    const int tickMs = (std::max)(1, toInt(app.getArgument("tick_ms", "5"), 5));

    SwThread worker("BenchWorker");
    worker.start();

    BenchObject a(ns, "A");
    BenchObject b(ns, "B");

    BenchObject w1(ns, "W1");
    BenchObject w2(ns, "W2");
    BenchObject moved(ns, "MOVED");

    w1.moveToThread(&worker);
    w2.moveToThread(&worker);
    moved.moveToThread(&worker);

    w1.startTicking(tickMs);

    std::cout << "[server] ns=" << ns.toStdString()
              << " targets=" << ns.toStdString() << "/A,"
              << ns.toStdString() << "/B,"
              << ns.toStdString() << "/W1,"
              << ns.toStdString() << "/W2,"
              << ns.toStdString() << "/MOVED"
              << " tick_ms=" << tickMs
              << "\n";

    const int rc = app.exec();

    worker.quit();
    worker.wait();
    return rc;
}

static int benchMain(SwCoreApplication& app, const SwString& program) {
    const SwString ns = app.getArgument("ns", "bench");
    const int clients = (std::max)(1, toInt(app.getArgument("clients", "4"), 4));
    const int count = (std::max)(1, toInt(app.getArgument("count", "200"), 200));
    const int warmup = (std::max)(0, toInt(app.getArgument("warmup", "20"), 20));
    const int timeoutMs = (std::max)(1, toInt(app.getArgument("timeout_ms", "1000"), 1000));
    const int signalSamples = (std::max)(0, toInt(app.getArgument("signal_samples", "50"), 50));
    const int tickMs = (std::max)(1, toInt(app.getArgument("tick_ms", "5"), 5));

    SwThread worker("BenchWorker");
    worker.start();

    BenchObject a(ns, "A");
    BenchObject b(ns, "B");

    BenchObject w1(ns, "W1");
    BenchObject w2(ns, "W2");
    BenchObject moved(ns, "MOVED");

    w1.moveToThread(&worker);
    w2.moveToThread(&worker);
    moved.moveToThread(&worker);

    w1.startTicking(tickMs);

    const SwString targets =
        ns + "/A," +
        ns + "/B," +
        ns + "/W1," +
        ns + "/W2," +
        ns + "/MOVED";

    const SwString benchTarget = ns + "/W1";

    std::cout << "[bench] spawning clients=" << clients
              << " targets=" << targets.toStdString()
              << " bench_target=" << benchTarget.toStdString()
              << " count=" << count
              << " warmup=" << warmup
              << " timeout_ms=" << timeoutMs
              << " signal_samples=" << signalSamples
              << " tick_ms=" << tickMs
              << "\n";

    struct BenchState {
        int remaining = 0;
        std::vector<std::unique_ptr<SwProcess>> procs;
    };
    auto state = std::make_shared<BenchState>();
    state->remaining = clients;

    app.postEvent([&app, state, program, ns, clients, targets, benchTarget, count, warmup, timeoutMs, signalSamples]() mutable {
        for (int i = 0; i < clients; ++i) {
            std::unique_ptr<SwProcess> p(new SwProcess());
            SwProcess* raw = p.get();
            const std::string pid = std::to_string(i);

            SwObject::connect(raw, SIGNAL(readyReadStdOut), std::function<void()>([raw, pid]() {
                SwString out = raw->read();
                const std::string s = out.toStdString();
                if (!s.empty()) std::cout << "[client " << pid << " stdout] " << s;
            }));

            SwObject::connect(raw, SIGNAL(readyReadStdErr), std::function<void()>([raw, pid]() {
                SwString out = raw->readStdErr();
                const std::string s = out.toStdString();
                if (!s.empty()) std::cerr << "[client " << pid << " stderr] " << s;
            }));

            SwObject::connect(raw, SIGNAL(processTerminated), std::function<void(int)>([&app, state, pid](int exitCode) {
                std::cout << "[client " << pid << "] exited code=" << exitCode << "\n";
                state->remaining -= 1;
                if (state->remaining <= 0) {
                    app.quit();
                }
            }));

            SwStringList args;
            args.append("--mode=client");
            args.append(SwString("--client_id=") + SwString(pid));
            args.append(SwString("--targets=") + targets);
            args.append(SwString("--bench_target=") + benchTarget);
            args.append(SwString("--count=") + SwString::number(count));
            args.append(SwString("--warmup=") + SwString::number(warmup));
            args.append(SwString("--timeout_ms=") + SwString::number(timeoutMs));
            if (signalSamples > 0) {
                args.append(SwString("--signal_target=") + benchTarget);
                args.append(SwString("--signal_samples=") + SwString::number(signalSamples));
            }

#if defined(_WIN32)
            raw->start(program, args, ProcessFlags::CreateNoWindow);
#else
            raw->start(program, args);
#endif

            state->procs.push_back(std::move(p));
        }
    });

    const int rc = app.exec();
    worker.quit();
    worker.wait();
    return rc;
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    const SwString mode = app.getArgument("mode", "bench");

    if (mode == "client") {
        return clientMain(app);
    }
    if (mode == "server") {
        return serverMain(app);
    }
    if (mode == "bench") {
        const SwString program = (argc > 0) ? SwString(argv[0]) : SwString("IpcThreadStress.exe");
        if (program.isEmpty()) {
            usage();
            return 1;
        }
        return benchMain(app, program);
    }

    usage();
    return 1;
}
