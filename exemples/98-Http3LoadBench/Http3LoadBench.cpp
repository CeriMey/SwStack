// HTTP/3 load bench: drives many concurrent *normal* SwHttp3Client requests
// against a target and measures throughput + latency PURELY BY USING the
// client's async API -- start a GET, time it with a wall clock from the outside,
// stop on the finished()/errorOccurred() signal. Nothing inside the client is
// instrumented.
//
// Model: N worker threads, each running its own SwCoreApplication event loop and
// holding K in-flight SwHttp3Client connections. When one settles, its latency
// is recorded (steady_clock delta) and a fresh GET is fired to keep K in flight,
// until the duration elapses. Total offered concurrency = N * K connections.
//
// Usage:
//   Http3LoadBench [https://host:port/path] [options]
//   (no target)                 -> spins up a loopback SwQuicHttp3Server and
//                                  benches against it (the stack's own ceiling)
// Options:
//   --host H   --port P   --path /p
//   --threads N        worker threads / event loops   (default 4)
//   --conns   K        in-flight connections per thread (default 32)
//   --duration S       seconds of load                (default 5)
//   --timeout MS       per-request timeout            (default 10000)
//   --verify           verify the certificate chain   (default: off, self-signed)

#include "SwCoreApplication.h"
#include "SwTimer.h"
#include "core/io/http/SwHttpRouter.h"
#include "core/io/http3/SwHttp3Client.h"
#include "core/io/http3/SwQuicHttp3Server.h"
#include "core/io/quic/SwQuicServerCredential.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

typedef std::chrono::steady_clock Clock;

struct Config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::string path = "/hello";
    int threads = 4;
    int connsPerThread = 32;
    int durationSec = 5;
    int timeoutMs = 10000;
    bool verifyChain = false; // self-signed loopback peer by default
    bool loopback = true;     // no target given -> run our own server
};

double toMs(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

// Drives K concurrent async clients on one event loop for the load window.
// All measurement is external: it times each request between get() and the
// settling signal, and never reads any client-internal counter.
class WorkerRunner {
public:
    WorkerRunner(const Config& cfg, Clock::time_point endAt,
                 std::atomic<std::uint64_t>& liveOk)
        : m_cfg(cfg), m_endAt(endAt), m_liveOk(liveOk) {
        m_lat.reserve(1 << 16);
    }

    void run() {
        SwCoreApplication app; // event loop bound to this worker thread
        m_app = &app;

        const int k = m_cfg.connsPerThread;
        m_clients.resize(k, nullptr);
        m_relaunch.resize(k, nullptr);
        m_starts.resize(k);
        m_active = k;

        for (int i = 0; i < k; ++i) {
            // No SwObject parent (SwCoreApplication is not an SwObject); this
            // worker owns and deletes its clients explicitly. Constructing them
            // here binds their affinity to this thread's event loop.
            SwHttp3Client* client = new SwHttp3Client();
            client->setVerifyPeer(true);
            client->setVerifyCertificateChain(m_cfg.verifyChain);
            SwObject::connect(client, &SwHttp3Client::finished, client,
                              [this, i](const SwByteArray&) { onSettle_(i, true); });
            SwObject::connect(client, &SwHttp3Client::errorOccurred, client,
                              [this, i](const SwString&) { onSettle_(i, false); });
            m_clients[i] = client;

            // Relaunch is posted through this timer, never called inline from a
            // settle slot: a synchronous get() start-failure emits errorOccurred
            // inline, so an inline relaunch would recurse without bound.
            SwTimer* timer = new SwTimer();
            timer->setSingleShot(true);
            SwObject::connect(timer, &SwTimer::timeout, [this, i]() { launch_(i); });
            m_relaunch[i] = timer;
        }

        // Safety net: leave the loop even if some request never settles.
        SwTimer watchdog(m_cfg.durationSec * 1000 + m_cfg.timeoutMs + 2000);
        watchdog.setSingleShot(true);
        SwObject::connect(&watchdog, &SwTimer::timeout, [this]() {
            if (m_app) m_app->exit(0);
        });
        watchdog.start();

        for (int i = 0; i < k; ++i) {
            launch_(i);
        }
        app.exec();

        for (int i = 0; i < k; ++i) {
            delete m_relaunch[i];
            delete m_clients[i];
        }
        m_app = nullptr;
    }

    std::uint64_t ok() const { return m_ok; }
    std::uint64_t fail() const { return m_fail; }
    const std::vector<float>& latenciesMs() const { return m_lat; }

private:
    void launch_(int i) {
        if (Clock::now() >= m_endAt) {
            stopSlot_();
            return;
        }
        m_starts[i] = Clock::now();
        // Ignore the return: a start-failure has already emitted errorOccurred
        // synchronously, so onSettle_ counts it and schedules the next attempt.
        m_clients[i]->get(SwString(m_cfg.host), m_cfg.port,
                          SwString(m_cfg.path), m_cfg.timeoutMs);
    }

    void onSettle_(int i, bool ok) {
        const double ms = toMs(Clock::now() - m_starts[i]);
        if (ok) {
            ++m_ok;
            ++m_liveOk;
            if (m_lat.size() < kMaxSamples_) {
                m_lat.push_back(static_cast<float>(ms));
            }
        } else {
            ++m_fail;
        }
        if (Clock::now() >= m_endAt) {
            stopSlot_();
            return;
        }
        // Post the relaunch through the loop (0 ms) rather than calling launch_
        // inline: on a synchronous start-failure this slot would otherwise
        // recurse get()->errorOccurred->onSettle_->get() without bound.
        m_relaunch[i]->start(0);
    }

    void stopSlot_() {
        if (--m_active == 0 && m_app) {
            m_app->exit(0);
        }
    }

    static const std::size_t kMaxSamples_ = 3000000; // bound latency memory/thread

    const Config& m_cfg;
    Clock::time_point m_endAt;
    std::atomic<std::uint64_t>& m_liveOk;
    SwCoreApplication* m_app = nullptr;
    std::vector<SwHttp3Client*> m_clients;
    std::vector<SwTimer*> m_relaunch;
    std::vector<Clock::time_point> m_starts;
    int m_active = 0;
    std::uint64_t m_ok = 0;
    std::uint64_t m_fail = 0;
    std::vector<float> m_lat;
};

double percentile(const std::vector<float>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    std::size_t idx = static_cast<std::size_t>((sorted.size() - 1) * p);
    return sorted[idx];
}

void parseUrl(const std::string& url, Config& cfg) {
    std::string s = url;
    const std::size_t scheme = s.find("://");
    if (scheme != std::string::npos) {
        s = s.substr(scheme + 3);
    }
    std::string hostport = s;
    std::string path = "/";
    const std::size_t slash = s.find('/');
    if (slash != std::string::npos) {
        hostport = s.substr(0, slash);
        path = s.substr(slash);
    }
    std::uint16_t port = 443;
    const std::size_t colon = hostport.find(':');
    std::string host = hostport;
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = static_cast<std::uint16_t>(std::atoi(hostport.substr(colon + 1).c_str()));
    }
    cfg.host = host;
    cfg.port = port;
    cfg.path = path;
    cfg.loopback = false;
    cfg.verifyChain = true; // a real remote target has a real chain
}

bool parseArgs(int argc, char** argv, Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* def) -> std::string {
            if (i + 1 < argc) return argv[++i];
            return def;
        };
        if (a == "--host") { cfg.host = next("127.0.0.1"); cfg.loopback = false; }
        else if (a == "--port") { cfg.port = static_cast<std::uint16_t>(std::atoi(next("0").c_str())); }
        else if (a == "--path") { cfg.path = next("/hello"); }
        else if (a == "--threads") { cfg.threads = std::atoi(next("4").c_str()); }
        else if (a == "--conns") { cfg.connsPerThread = std::atoi(next("32").c_str()); }
        else if (a == "--duration") { cfg.durationSec = std::atoi(next("5").c_str()); }
        else if (a == "--timeout") { cfg.timeoutMs = std::atoi(next("10000").c_str()); }
        else if (a == "--verify") { cfg.verifyChain = true; }
        else if (a == "--loopback") { cfg.loopback = true; }
        else if (!a.empty() && a[0] != '-') { parseUrl(a, cfg); }
        else {
            std::cerr << "unknown option: " << a << std::endl;
            return false;
        }
    }
    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.connsPerThread < 1) cfg.connsPerThread = 1;
    if (cfg.durationSec < 1) cfg.durationSec = 1;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Config cfg;
    if (!parseArgs(argc, argv, cfg)) {
        return 2;
    }

    // Loopback mode: stand up our own HTTP/3 server so the bench measures the
    // stack's own ceiling rather than a network path.
    SwQuicHttp3Server server;
    SwHttpRouter router;
    std::thread serverThread;
    std::atomic<bool> stopServer(false);
    if (cfg.loopback) {
        SwString error;
        SwQuicServerCredential credential;
        if (!SwQuicEcdsaCredential::createSelfSigned(SwString("localhost"), credential, &error)) {
            std::cerr << "credential generation failed: " << error.toStdString() << std::endl;
            return 1;
        }
        router.addRoute(SwString("GET"), SwString(cfg.path),
                        [](const SwHttpRequest&) -> SwHttpResponse {
                            SwHttpResponse response;
                            response.status = 200;
                            response.body = SwByteArray("ok");
                            return response;
                        });
        server.setCredential(credential);
        server.setRouter(&router);
        if (!server.listen(SwString("127.0.0.1"), 0, &error)) {
            std::cerr << "server listen failed: " << error.toStdString() << std::endl;
            return 1;
        }
        cfg.host = "127.0.0.1";
        cfg.port = server.localPort();
        cfg.verifyChain = false;
        serverThread = std::thread([&server, &stopServer]() {
            SwString pollError;
            while (!stopServer.load()) {
                server.poll(1, &pollError);
            }
        });
    }

    const int totalConcurrency = cfg.threads * cfg.connsPerThread;
    std::cout << "== HTTP/3 load bench ==\n"
              << "target       : https://" << cfg.host << ":" << cfg.port << cfg.path
              << (cfg.loopback ? "  (loopback self-server)" : "  (remote)") << "\n"
              << "threads      : " << cfg.threads << "\n"
              << "conns/thread : " << cfg.connsPerThread
              << "   (total in-flight = " << totalConcurrency << ")\n"
              << "duration     : " << cfg.durationSec << " s\n"
              << "verify chain : " << (cfg.verifyChain ? "yes" : "no") << "\n"
              << std::flush;

    std::atomic<std::uint64_t> liveOk(0);
    const Clock::time_point start = Clock::now();
    const Clock::time_point endAt = start + std::chrono::seconds(cfg.durationSec);

    std::vector<WorkerRunner*> runners;
    std::vector<std::thread> workers;
    for (int t = 0; t < cfg.threads; ++t) {
        WorkerRunner* runner = new WorkerRunner(cfg, endAt, liveOk);
        runners.push_back(runner);
        workers.push_back(std::thread([runner]() { runner->run(); }));
    }

    // Live progress (instantaneous RPS) once per second during the load window.
    std::uint64_t prev = 0;
    for (int s = 0; s < cfg.durationSec; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const std::uint64_t cur = liveOk.load();
        std::cout << "  t+" << (s + 1) << "s  " << (cur - prev) << " req/s"
                  << "   (total ok " << cur << ")\n" << std::flush;
        prev = cur;
    }

    for (std::size_t i = 0; i < workers.size(); ++i) {
        workers[i].join();
    }
    const double wallSec = std::chrono::duration<double>(Clock::now() - start).count();

    std::uint64_t totalOk = 0, totalFail = 0;
    std::vector<float> allLat;
    for (std::size_t i = 0; i < runners.size(); ++i) {
        totalOk += runners[i]->ok();
        totalFail += runners[i]->fail();
        const std::vector<float>& l = runners[i]->latenciesMs();
        allLat.insert(allLat.end(), l.begin(), l.end());
    }
    std::sort(allLat.begin(), allLat.end());

    double sum = 0.0;
    for (std::size_t i = 0; i < allLat.size(); ++i) sum += allLat[i];
    const double meanMs = allLat.empty() ? 0.0 : sum / allLat.size();

    const std::uint64_t total = totalOk + totalFail;
    const double rps = totalOk / static_cast<double>(cfg.durationSec);

    std::cout << "\n== results ==\n"
              << "completed    : " << totalOk << "   failed: " << totalFail
              << "   (" << (total ? (100.0 * totalOk / total) : 0.0) << "% ok)\n"
              << "throughput   : " << rps << " req/s   (over " << cfg.durationSec
              << "s window; wall " << wallSec << "s)\n";
    if (!allLat.empty()) {
        std::cout << "latency (ms) : mean " << meanMs
                  << "  p50 " << percentile(allLat, 0.50)
                  << "  p90 " << percentile(allLat, 0.90)
                  << "  p99 " << percentile(allLat, 0.99)
                  << "  max " << allLat.back() << "\n";
    }
    std::cout << std::flush;

    if (cfg.loopback) {
        stopServer.store(true);
        serverThread.join();
    }
    for (std::size_t i = 0; i < runners.size(); ++i) {
        delete runners[i];
    }

    return (totalOk > 0) ? 0 : 1;
}
