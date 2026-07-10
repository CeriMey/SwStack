// HttpAppDbBench — measures the server-side cost of serving SwEmbeddedDb data
// through SwHttpApp, against direct in-process DB baselines.
//
// Phases:
//   direct-db-write1   one write() per record, in-process (no HTTP)
//   direct-db-get      random get(), in-process (no HTTP)
//   direct-db-scan     first N rows of scanPrimary(), in-process (no HTTP)
//   http-ping          GET with no DB work (HTTP framework floor)
//   http-echo-post     POST with body, no DB work (HTTP POST floor)
//   http-db-get        GET /db/:id -> db.get()
//   http-db-post       POST /db/:id -> db.write()
//   http-db-list       GET /db/list/:n -> scanPrimary() page
//
// Each HTTP phase runs once at concurrency 1 (clean latency profile) and once
// at higher concurrency (throughput). The summary derives the marginal cost of
// the DB inside the server (http-db-* minus the matching no-DB floor) and
// compares it with the direct in-process baseline.

#include "SwCoreApplication.h"
#include "SwDir.h"
#include "SwEmbeddedDb.h"
#include "SwHttpApp.h"
#include "SwStandardLocation.h"
#include "SwString.h"
#include "SwThread.h"
#include "SwThreadPool.h"
#include "platform/SwDbPlatform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NativeSocket_;
static const NativeSocket_ kInvalidNativeSocket_ = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int NativeSocket_;
static const NativeSocket_ kInvalidNativeSocket_ = -1;
#endif

namespace {

struct BenchOptions {
    SwString dbPath;
    unsigned long long records{20000};
    unsigned long long valueBytes{512};
    unsigned long long directGetSamples{5000};
    unsigned long long directWriteSamples{2000};
    unsigned long long directScanReps{100};
    unsigned long long listLimit{100};
    int latencyRequests{1000};
    int concurrency{8};
    int requestsPerConnection{250};
    int timeoutMs{8000};
    int writeThreads{8};
    bool threadPool{false};
    bool lazyWrite{false};
    bool keepDb{false};
};

struct PhaseStats {
    SwString name;
    unsigned long long operations{0};
    unsigned long long errors{0};
    unsigned long long bytes{0};
    double wallMs{0.0};
    std::vector<long long> latenciesUs;

    double p50{0.0};
    double p95{0.0};
    double p99{0.0};
    double maxUs{0.0};
    double opsPerSec{0.0};

    void finalize() {
        if (!latenciesUs.empty()) {
            std::vector<long long> sorted = latenciesUs;
            std::sort(sorted.begin(), sorted.end());
            const auto at = [&sorted](double ratio) {
                const std::size_t index = static_cast<std::size_t>(
                    ratio * static_cast<double>(sorted.size() - 1));
                return static_cast<double>(sorted[index]);
            };
            p50 = at(0.50);
            p95 = at(0.95);
            p99 = at(0.99);
            maxUs = static_cast<double>(sorted.back());
        }
        if (wallMs > 0.0) {
            opsPerSec = static_cast<double>(operations) * 1000.0 / wallMs;
        }
    }
};

static void printPhase_(const PhaseStats& stats) {
    std::cout << std::fixed << std::setprecision(2)
              << "[PHASE] " << stats.name.toStdString()
              << " ops=" << stats.operations
              << " errors=" << stats.errors
              << " wall-ms=" << stats.wallMs
              << " req-per-sec=" << stats.opsPerSec
              << " p50-us=" << stats.p50
              << " p95-us=" << stats.p95
              << " p99-us=" << stats.p99
              << " max-us=" << stats.maxUs
              << std::endl;
}

static double elapsedMs_(const std::chrono::steady_clock::duration& duration) {
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli> >(duration).count();
}

static long long elapsedUs_(const std::chrono::steady_clock::duration& duration) {
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

static unsigned long long splitMix64_(unsigned long long x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static SwByteArray makePrimaryKey_(unsigned long long recordId) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "row:%012llu", recordId);
    return SwByteArray(buffer);
}

static std::string makePayloadStd_(unsigned long long recordId, unsigned long long bytes) {
    static const char alphabet[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
    static const std::size_t alphabetSize = sizeof(alphabet) - 1;

    std::string payload(static_cast<std::size_t>(bytes), '\0');
    for (unsigned long long i = 0; i < bytes; ++i) {
        const unsigned long long mixed = splitMix64_(recordId ^ (i * 1315423911ull));
        payload[static_cast<std::size_t>(i)] = alphabet[mixed % alphabetSize];
    }
    return payload;
}

static SwByteArray makePayload_(unsigned long long recordId, unsigned long long bytes) {
    return SwByteArray(makePayloadStd_(recordId, bytes));
}

// ---------------------------------------------------------------------------
// Native blocking HTTP/1.1 client (same approach as 55-HttpPerfSelfTest, with
// method + request-body support so POST routes can be exercised).
// ---------------------------------------------------------------------------

static void closeNativeSocket_(NativeSocket_ socketFd) {
    if (socketFd == kInvalidNativeSocket_) {
        return;
    }
#if defined(_WIN32)
    ::closesocket(socketFd);
#else
    ::close(socketFd);
#endif
}

static bool initializeNativeSockets_() {
#if defined(_WIN32)
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, []() {
        WSADATA data {};
        ok = (::WSAStartup(MAKEWORD(2, 2), &data) == 0);
    });
    return ok;
#else
    return true;
#endif
}

static bool setSocketTimeouts_(NativeSocket_ socketFd, int timeoutMs) {
#if defined(_WIN32)
    const DWORD value = timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : 0;
    return ::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)) == 0 &&
           ::setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
#else
    struct timeval value;
    value.tv_sec = timeoutMs / 1000;
    value.tv_usec = (timeoutMs % 1000) * 1000;
    return ::setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) == 0 &&
           ::setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) == 0;
#endif
}

static bool sendAll_(NativeSocket_ socketFd, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
#if defined(_WIN32)
        const int sent = ::send(socketFd,
                                data.data() + static_cast<int>(offset),
                                static_cast<int>(data.size() - offset),
                                0);
        if (sent == SOCKET_ERROR || sent <= 0) {
            return false;
        }
#else
        const ssize_t sent = ::send(socketFd, data.data() + offset, data.size() - offset, 0);
        if (sent <= 0) {
            return false;
        }
#endif
        offset += static_cast<size_t>(sent);
    }
    return true;
}

static bool recvSome_(NativeSocket_ socketFd, std::string& bufferOut) {
    char temp[16384];
#if defined(_WIN32)
    const int received = ::recv(socketFd, temp, static_cast<int>(sizeof(temp)), 0);
    if (received == SOCKET_ERROR || received <= 0) {
        return false;
    }
#else
    const ssize_t received = ::recv(socketFd, temp, sizeof(temp), 0);
    if (received <= 0) {
        return false;
    }
#endif
    bufferOut.append(temp, static_cast<size_t>(received));
    return true;
}

static bool parseContentLength_(const std::string& headers, size_t& lengthOut) {
    std::string lowered = headers;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const std::string key = "content-length:";
    const size_t pos = lowered.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    size_t cursor = pos + key.size();
    while (cursor < lowered.size() && (lowered[cursor] == ' ' || lowered[cursor] == '\t')) {
        ++cursor;
    }
    size_t end = cursor;
    while (end < lowered.size() && lowered[end] >= '0' && lowered[end] <= '9') {
        ++end;
    }
    if (end == cursor) {
        return false;
    }
    lengthOut = static_cast<size_t>(std::strtoull(lowered.substr(cursor, end - cursor).c_str(), nullptr, 10));
    return true;
}

static bool parseStatusCode_(const std::string& statusLine, int& statusCodeOut) {
    const size_t firstSpace = statusLine.find(' ');
    if (firstSpace == std::string::npos) {
        return false;
    }
    const size_t secondSpace = statusLine.find(' ', firstSpace + 1);
    const std::string token = statusLine.substr(firstSpace + 1, secondSpace == std::string::npos
                                                                    ? std::string::npos
                                                                    : secondSpace - firstSpace - 1);
    statusCodeOut = std::atoi(token.c_str());
    return statusCodeOut > 0;
}

class NativeHttpClient_ {
public:
    NativeHttpClient_() = default;

    ~NativeHttpClient_() {
        close();
    }

    bool connect(const std::string& host, uint16_t port, int timeoutMs) {
        close();
        if (!initializeNativeSockets_()) {
            return false;
        }

        struct addrinfo hints {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* results = nullptr;
        const std::string service = std::to_string(static_cast<unsigned int>(port));
        if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &results) != 0 || !results) {
            return false;
        }

        bool connected = false;
        for (struct addrinfo* it = results; it; it = it->ai_next) {
            NativeSocket_ candidate = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (candidate == kInvalidNativeSocket_) {
                continue;
            }
            m_socket = candidate;
            if (!setSocketTimeouts_(m_socket, timeoutMs)) {
                close();
                continue;
            }
#if defined(_WIN32)
            if (::connect(m_socket, it->ai_addr, static_cast<int>(it->ai_addrlen)) == SOCKET_ERROR) {
                close();
                continue;
            }
#else
            if (::connect(m_socket, it->ai_addr, static_cast<socklen_t>(it->ai_addrlen)) != 0) {
                close();
                continue;
            }
#endif
            connected = true;
            break;
        }

        if (results) {
            ::freeaddrinfo(results);
        }
        if (!connected) {
            close();
            return false;
        }
        return true;
    }

    bool request(const std::string& method,
                 const std::string& target,
                 const std::string& hostHeader,
                 const std::string& body,
                 int timeoutMs,
                 long long& latencyUsOut,
                 int& statusCodeOut,
                 std::string* responseBodyOut = nullptr,
                 long long expectedBodyBytes = -1) {
        latencyUsOut = 0;
        statusCodeOut = 0;
        if (m_socket == kInvalidNativeSocket_) {
            return false;
        }

        std::ostringstream builder;
        builder << method << " " << target << " HTTP/1.1\r\n";
        builder << "Host: " << hostHeader << "\r\n";
        builder << "Connection: keep-alive\r\n";
        if (!body.empty() || method == "POST" || method == "PUT") {
            builder << "Content-Type: application/octet-stream\r\n";
            builder << "Content-Length: " << body.size() << "\r\n";
        }
        builder << "\r\n";
        builder << body;

        const auto startedAt = std::chrono::steady_clock::now();
        if (!sendAll_(m_socket, builder.str())) {
            return false;
        }
        if (!readResponse_(statusCodeOut, responseBodyOut, expectedBodyBytes)) {
            return false;
        }
        latencyUsOut = elapsedUs_(std::chrono::steady_clock::now() - startedAt);
        (void)timeoutMs;
        return statusCodeOut >= 200 && statusCodeOut < 400;
    }

    void close() {
        if (m_socket != kInvalidNativeSocket_) {
            closeNativeSocket_(m_socket);
            m_socket = kInvalidNativeSocket_;
        }
        m_buffer.clear();
    }

private:
    bool readResponse_(int& statusCodeOut, std::string* bodyOut, long long expectedBodyBytes) {
        while (true) {
            const size_t headerEnd = m_buffer.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const std::string headerBlock = m_buffer.substr(0, headerEnd);
                std::istringstream stream(headerBlock);
                std::string statusLine;
                if (!std::getline(stream, statusLine)) {
                    return false;
                }
                if (!statusLine.empty() && statusLine[statusLine.size() - 1] == '\r') {
                    statusLine.erase(statusLine.size() - 1);
                }
                if (!parseStatusCode_(statusLine, statusCodeOut)) {
                    return false;
                }

                size_t contentLength = 0;
                if (!parseContentLength_(headerBlock, contentLength)) {
                    return false;
                }
                if (expectedBodyBytes >= 0 &&
                    statusCodeOut >= 200 && statusCodeOut < 300 &&
                    contentLength != static_cast<size_t>(expectedBodyBytes)) {
                    return false;
                }

                const size_t totalNeeded = headerEnd + 4 + contentLength;
                while (m_buffer.size() < totalNeeded) {
                    if (!recvSome_(m_socket, m_buffer)) {
                        return false;
                    }
                }
                if (bodyOut) {
                    *bodyOut = m_buffer.substr(headerEnd + 4, contentLength);
                }
                m_buffer.erase(0, totalNeeded);
                return true;
            }

            if (!recvSome_(m_socket, m_buffer)) {
                return false;
            }
        }
    }

    NativeSocket_ m_socket = kInvalidNativeSocket_;
    std::string m_buffer;
};

// ---------------------------------------------------------------------------
// Server host: SwHttpApp + SwEmbeddedDb living on a dedicated SwThread.
// ---------------------------------------------------------------------------

static SwEmbeddedDbOptions makeDbOptions_(const SwString& dbPath, bool lazyWrite) {
    SwEmbeddedDbOptions dbOptions;
    dbOptions.dbPath = dbPath;
    dbOptions.readOnly = false;
    dbOptions.lazyWrite = lazyWrite;
    dbOptions.enableShmNotifications = false;
    return dbOptions;
}

class BenchServerHost_ : public SwObject {
    SW_OBJECT(BenchServerHost_, SwObject)

public:
    BenchServerHost_(const BenchOptions& options, const SwString& dbPath, SwObject* parent = nullptr)
        : SwObject(parent),
          m_options(options),
          m_dbPath(dbPath) {
    }

    bool start() {
        const SwDbStatus openStatus = m_db.open(makeDbOptions_(m_dbPath, m_options.lazyWrite));
        if (!openStatus.ok()) {
            m_lastError = SwString("db open failed: ") + openStatus.message();
            return false;
        }

        SwHttpLimits limits;
        limits.maxBodyBytes = 4 * 1024 * 1024;
        limits.maxConnections = 4096;
        limits.maxInFlightRequests = 4096;
        m_app.setLimits(limits);

        SwHttpTimeouts timeouts;
        timeouts.headerReadTimeoutMs = 5000;
        timeouts.bodyReadTimeoutMs = 5000;
        timeouts.keepAliveIdleTimeoutMs = 15000;
        timeouts.writeTimeoutMs = 5000;
        m_app.setTimeouts(timeouts);

        if (m_options.threadPool) {
            m_pool.setMaxThreadCount(std::max(2u, std::thread::hardware_concurrency()));
            m_pool.setMaxQueuedTaskCount(8192);
            m_app.setThreadPool(&m_pool);
            m_app.setDispatchMode(SwHttpServer::DispatchMode::ThreadPool);
        } else {
            m_app.setDispatchMode(SwHttpServer::DispatchMode::Inline);
        }

        m_app.get("/ping", [](SwHttpContext& context) {
            context.send(SwByteArray("ok"));
        });

        m_app.post("/echo", [](SwHttpContext& context) {
            (void)context.request().body;
            context.send(SwByteArray("ok"));
        });

        m_app.get("/db/:id", [this](SwHttpContext& context) {
            const unsigned long long id =
                std::strtoull(context.pathValue("id").toStdString().c_str(), nullptr, 10);
            SwByteArray value;
            const SwDbStatus status = m_db.get(makePrimaryKey_(id), &value);
            if (!status.ok()) {
                context.text("missing", 404);
                return;
            }
            context.send(value);
        });

        m_app.post("/db/:id", [this](SwHttpContext& context) {
            const unsigned long long id =
                std::strtoull(context.pathValue("id").toStdString().c_str(), nullptr, 10);
            SwDbWriteBatch batch;
            batch.put(makePrimaryKey_(id), context.request().body);
            const SwDbStatus status = m_db.write(std::move(batch));
            if (!status.ok()) {
                context.text("db-error", 500);
                return;
            }
            context.send(SwByteArray("ok"));
        });

        m_app.get("/db/list/:limit", [this](SwHttpContext& context) {
            const unsigned long long limit =
                std::strtoull(context.pathValue("limit").toStdString().c_str(), nullptr, 10);
            SwByteArray out;
            out.reserve(static_cast<std::size_t>(limit * m_options.valueBytes));
            unsigned long long rows = 0;
            SwDbIterator iterator = m_db.scanPrimary();
            while (iterator.isValid() && rows < limit) {
                out.append(iterator.current().value);
                ++rows;
                iterator.next();
            }
            context.send(out);
        });

        for (uint16_t port = 19750; port < 19850; ++port) {
            if (m_app.listenHttp("127.0.0.1", port)) {
                m_port = port;
                return true;
            }
        }

        m_lastError = "unable to bind benchmark port";
        m_db.close();
        return false;
    }

    void stop() {
        m_app.close();
        m_db.close();
    }

    uint16_t port() const {
        return m_port;
    }

    SwString lastError() const {
        return m_lastError;
    }

private:
    BenchOptions m_options;
    SwString m_dbPath;
    SwHttpApp m_app;
    SwThreadPool m_pool;
    SwEmbeddedDb m_db;
    uint16_t m_port = 0;
    SwString m_lastError;
};

static bool startBenchServer_(const BenchOptions& options,
                              const SwString& dbPath,
                              SwThread& serverThread,
                              BenchServerHost_*& hostOut,
                              uint16_t& portOut,
                              SwString& errorOut) {
    hostOut = nullptr;
    portOut = 0;
    errorOut.clear();

    if (!serverThread.start()) {
        errorOut = "unable to start server thread";
        return false;
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;

    serverThread.postTaskOnLane([&]() {
        BenchServerHost_* host = new BenchServerHost_(options, dbPath, nullptr);
        ok = host->start();
        if (ok) {
            hostOut = host;
            portOut = host->port();
        } else {
            errorOut = host->lastError();
            host->deleteLater();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
    }, SwFiberLane::Control);

    std::unique_lock<std::mutex> lock(mutex);
    if (!cv.wait_for(lock, std::chrono::seconds(15), [&]() { return done; })) {
        errorOut = "server start timeout";
        serverThread.quit();
        serverThread.wait();
        return false;
    }
    return ok;
}

static bool stopBenchServer_(SwThread& serverThread, BenchServerHost_* host) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;

    serverThread.postTaskOnLane([&]() {
        if (host) {
            host->stop();
            host->deleteLater();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
    }, SwFiberLane::Control);

    std::unique_lock<std::mutex> lock(mutex);
    const bool stopped = cv.wait_for(lock, std::chrono::seconds(15), [&]() { return done; });
    serverThread.quit();
    serverThread.wait();
    return stopped;
}

// ---------------------------------------------------------------------------
// HTTP scenario runner
// ---------------------------------------------------------------------------

struct HttpRequestSpec_ {
    std::string method;
    std::string target;
    std::string body;
    long long expectedBodyBytes{-1};
};

using RequestFactory_ = std::function<HttpRequestSpec_(int connectionIndex, int requestIndex)>;

static PhaseStats runHttpScenario_(const SwString& name,
                                   uint16_t port,
                                   int concurrency,
                                   int requestsPerConnection,
                                   int timeoutMs,
                                   const RequestFactory_& factory) {
    PhaseStats stats;
    stats.name = name;

    std::mutex latencyMutex;
    std::atomic<unsigned long long> errors(0);
    std::atomic<unsigned long long> bytes(0);

    const auto startedAt = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(concurrency));
    for (int c = 0; c < concurrency; ++c) {
        workers.emplace_back([&, c]() {
            NativeHttpClient_ client;
            std::vector<long long> localLatencies;
            localLatencies.reserve(static_cast<size_t>(requestsPerConnection));
            if (!client.connect("127.0.0.1", port, timeoutMs)) {
                errors.fetch_add(static_cast<unsigned long long>(requestsPerConnection),
                                 std::memory_order_relaxed);
                return;
            }
            for (int req = 0; req < requestsPerConnection; ++req) {
                const HttpRequestSpec_ spec = factory(c, req);
                long long latencyUs = 0;
                int statusCode = 0;
                if (!client.request(spec.method, spec.target, "127.0.0.1", spec.body,
                                    timeoutMs, latencyUs, statusCode, nullptr,
                                    spec.expectedBodyBytes)) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                localLatencies.push_back(latencyUs);
                if (spec.expectedBodyBytes > 0) {
                    bytes.fetch_add(static_cast<unsigned long long>(spec.expectedBodyBytes),
                                    std::memory_order_relaxed);
                }
            }
            std::lock_guard<std::mutex> lock(latencyMutex);
            stats.latenciesUs.insert(stats.latenciesUs.end(),
                                     localLatencies.begin(), localLatencies.end());
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    stats.wallMs = elapsedMs_(std::chrono::steady_clock::now() - startedAt);
    stats.operations = static_cast<unsigned long long>(stats.latenciesUs.size());
    stats.errors = errors.load(std::memory_order_relaxed);
    stats.bytes = bytes.load(std::memory_order_relaxed);
    stats.finalize();
    return stats;
}

// ---------------------------------------------------------------------------
// Direct (in-process) DB phases
// ---------------------------------------------------------------------------

static bool prepopulateDb_(SwEmbeddedDb& db, const BenchOptions& options) {
    const auto startedAt = std::chrono::steady_clock::now();
    SwDbWriteBatch batch;
    unsigned long long recordId = 0;
    while (recordId < options.records) {
        batch.clear();
        unsigned long long batchCount = 0;
        for (; recordId < options.records && batchCount < 200; ++recordId, ++batchCount) {
            batch.put(makePrimaryKey_(recordId), makePayload_(recordId, options.valueBytes));
        }
        const SwDbStatus status = db.write(std::move(batch));
        if (!status.ok()) {
            std::cerr << "Prepopulate write failed at record " << recordId << ": "
                      << status.message().toStdString() << std::endl;
            return false;
        }
    }
    std::cout << std::fixed << std::setprecision(2)
              << "[SETUP] prepopulated records=" << options.records
              << " value-bytes=" << options.valueBytes
              << " in-ms=" << elapsedMs_(std::chrono::steady_clock::now() - startedAt)
              << std::endl;
    return true;
}

static bool runDirectWritePhase_(SwEmbeddedDb& db, const BenchOptions& options, PhaseStats& stats) {
    stats.name = "direct-db-write1";
    const auto startedAt = std::chrono::steady_clock::now();
    SwDbWriteBatch batch;
    for (unsigned long long i = 0; i < options.directWriteSamples; ++i) {
        const unsigned long long recordId = options.records + i;
        batch.clear();
        batch.put(makePrimaryKey_(recordId), makePayload_(recordId, options.valueBytes));
        const auto opStart = std::chrono::steady_clock::now();
        const SwDbStatus status = db.write(std::move(batch));
        const auto opEnd = std::chrono::steady_clock::now();
        if (!status.ok()) {
            std::cerr << "Direct write failed: " << status.message().toStdString() << std::endl;
            return false;
        }
        stats.latenciesUs.push_back(elapsedUs_(opEnd - opStart));
        ++stats.operations;
        stats.bytes += options.valueBytes;
    }
    stats.wallMs = elapsedMs_(std::chrono::steady_clock::now() - startedAt);
    stats.finalize();
    return true;
}

// Concurrent durable writers on one handle: measures the group-commit ceiling
// (throughput ~= writeThreads / fsync-latency when commits coalesce).
static bool runDirectWriteMtPhase_(SwEmbeddedDb& db, const BenchOptions& options, PhaseStats& stats) {
    stats.name = SwString("direct-db-write-mt") + SwString::number(options.writeThreads);
    const int threads = std::max(1, options.writeThreads);
    const unsigned long long perThread =
        std::max<unsigned long long>(1ull, options.directWriteSamples / static_cast<unsigned long long>(threads));

    std::mutex latencyMutex;
    std::atomic<unsigned long long> errors(0);
    const unsigned long long idBase = 5000000ull;

    const auto startedAt = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t]() {
            std::vector<long long> localLatencies;
            localLatencies.reserve(static_cast<size_t>(perThread));
            SwDbWriteBatch batch;
            for (unsigned long long i = 0; i < perThread; ++i) {
                const unsigned long long recordId =
                    idBase + static_cast<unsigned long long>(t) * perThread + i;
                batch.clear();
                batch.put(makePrimaryKey_(recordId), makePayload_(recordId, options.valueBytes));
                const auto opStart = std::chrono::steady_clock::now();
                const SwDbStatus status = db.write(std::move(batch));
                const auto opEnd = std::chrono::steady_clock::now();
                if (!status.ok()) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                localLatencies.push_back(elapsedUs_(opEnd - opStart));
            }
            std::lock_guard<std::mutex> lock(latencyMutex);
            stats.latenciesUs.insert(stats.latenciesUs.end(),
                                     localLatencies.begin(), localLatencies.end());
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    stats.wallMs = elapsedMs_(std::chrono::steady_clock::now() - startedAt);
    stats.operations = static_cast<unsigned long long>(stats.latenciesUs.size());
    stats.errors = errors.load(std::memory_order_relaxed);
    stats.bytes = stats.operations * options.valueBytes;
    stats.finalize();
    return stats.errors == 0;
}

static bool runDirectGetPhase_(SwEmbeddedDb& db, const BenchOptions& options, PhaseStats& stats) {
    stats.name = "direct-db-get";
    const auto startedAt = std::chrono::steady_clock::now();
    for (unsigned long long i = 0; i < options.directGetSamples; ++i) {
        const unsigned long long recordId = splitMix64_(i ^ 0xfeedfaceull) % options.records;
        SwByteArray value;
        const auto opStart = std::chrono::steady_clock::now();
        const SwDbStatus status = db.get(makePrimaryKey_(recordId), &value);
        const auto opEnd = std::chrono::steady_clock::now();
        if (!status.ok()) {
            std::cerr << "Direct get failed for record " << recordId << ": "
                      << status.message().toStdString() << std::endl;
            return false;
        }
        if (value.size() != static_cast<std::size_t>(options.valueBytes)) {
            std::cerr << "Direct get size mismatch for record " << recordId << std::endl;
            return false;
        }
        stats.latenciesUs.push_back(elapsedUs_(opEnd - opStart));
        ++stats.operations;
        stats.bytes += value.size();
    }
    stats.wallMs = elapsedMs_(std::chrono::steady_clock::now() - startedAt);
    stats.finalize();
    return true;
}

static bool runDirectScanPhase_(SwEmbeddedDb& db, const BenchOptions& options, PhaseStats& stats) {
    stats.name = SwString("direct-db-scan") + SwString::number(
        static_cast<long long>(options.listLimit));
    const auto startedAt = std::chrono::steady_clock::now();
    for (unsigned long long rep = 0; rep < options.directScanReps; ++rep) {
        const auto opStart = std::chrono::steady_clock::now();
        SwByteArray out;
        out.reserve(static_cast<std::size_t>(options.listLimit * options.valueBytes));
        unsigned long long rows = 0;
        SwDbIterator iterator = db.scanPrimary();
        while (iterator.isValid() && rows < options.listLimit) {
            out.append(iterator.current().value);
            ++rows;
            iterator.next();
        }
        const auto opEnd = std::chrono::steady_clock::now();
        if (rows != options.listLimit) {
            std::cerr << "Direct scan returned " << rows << " rows, expected "
                      << options.listLimit << std::endl;
            return false;
        }
        stats.latenciesUs.push_back(elapsedUs_(opEnd - opStart));
        ++stats.operations;
        stats.bytes += out.size();
    }
    stats.wallMs = elapsedMs_(std::chrono::steady_clock::now() - startedAt);
    stats.finalize();
    return true;
}

// Correctness of the incremental writer read cache (base + overlay): writes
// made after a scan must be visible to the next scans — including overwrites
// shadowing base rows, erases suppressing them, and secondary-index moves.
// Keys use the "zovl:" prefix so they sort after every "row:" key and never
// disturb the HTTP list-page expectations.
static bool runOverlayCorrectnessCheck_(SwEmbeddedDb& db) {
    const SwString indexName("ovl_group");
    const auto key = [](char suffix) { return SwByteArray(std::string("zovl:k") + suffix); };
    const auto val = [](const char* text) { return SwByteArray(std::string(text)); };
    const auto group = [&indexName](const char* name) {
        SwMap<SwString, SwList<SwByteArray>> keys;
        SwList<SwByteArray> values;
        values.append(SwByteArray(std::string(name)));
        keys[indexName] = values;
        return keys;
    };
    const auto fail = [](const char* what) {
        std::cerr << "[CHECK] FAIL overlay-correctness: " << what << std::endl;
        return false;
    };
    const auto collectPrimary = [&db](SwList<SwByteArray>& keysOut, SwList<SwByteArray>& valuesOut) {
        keysOut.clear();
        valuesOut.clear();
        SwDbIterator it = db.scanPrimary(SwByteArray(std::string("zovl:")), SwByteArray(std::string("zovl;")));
        while (it.isValid()) {
            keysOut.append(it.current().primaryKey);
            valuesOut.append(it.current().value);
            it.next();
        }
    };
    const auto collectIndex = [&db, &indexName](const char* g, SwList<SwByteArray>& keysOut) {
        keysOut.clear();
        const std::string start(g);
        std::string end(g);
        end[end.size() - 1] = static_cast<char>(end[end.size() - 1] + 1);
        SwDbIterator it = db.scanIndex(indexName, SwByteArray(start), SwByteArray(end));
        while (it.isValid()) {
            keysOut.append(it.current().primaryKey);
            it.next();
        }
    };

    SwDbWriteBatch batch;
    batch.put(key('1'), val("v1"), group("ga"));
    batch.put(key('2'), val("v2"), group("ga"));
    batch.put(key('3'), val("v3"), group("ga"));
    batch.put(key('4'), val("v4"), group("gb"));
    batch.put(key('5'), val("v5"), group("gb"));
    if (!db.write(std::move(batch)).ok()) {
        return fail("seed write");
    }

    SwList<SwByteArray> keys, values;
    collectPrimary(keys, values);
    if (keys.size() != 5) {
        return fail("seed scan should see 5 rows");
    }

    // These writes land in the overlay (a base exists from the scan above).
    batch.clear();
    batch.put(key('2'), val("v2-bis"), group("gb")); // overwrite + index move ga -> gb
    batch.put(key('6'), val("v6"), group("ga"));     // brand-new key
    if (!db.write(std::move(batch)).ok()) {
        return fail("overlay write");
    }
    batch.clear();
    batch.erase(key('3'));                            // tombstone over a base row
    if (!db.write(std::move(batch)).ok()) {
        return fail("overlay erase");
    }

    collectPrimary(keys, values);
    if (keys.size() != 5 ||
        keys[0] != key('1') || keys[1] != key('2') || keys[2] != key('4') ||
        keys[3] != key('5') || keys[4] != key('6')) {
        return fail("merged primary scan keys mismatch");
    }
    if (values[1] != val("v2-bis") || values[0] != val("v1") || values[4] != val("v6")) {
        return fail("merged primary scan values mismatch");
    }

    SwList<SwByteArray> indexKeys;
    collectIndex("ga", indexKeys);
    if (indexKeys.size() != 2 || indexKeys[0] != key('1') || indexKeys[1] != key('6')) {
        return fail("index scan ga should be {k1,k6}");
    }
    collectIndex("gb", indexKeys);
    if (indexKeys.size() != 3 || indexKeys[0] != key('2') || indexKeys[1] != key('4') ||
        indexKeys[2] != key('5')) {
        return fail("index scan gb should be {k2,k4,k5}");
    }

    SwDbSnapshot snapshot = db.createSnapshot();
    SwByteArray value;
    if (!snapshot.get(key('2'), &value).ok() || value != val("v2-bis")) {
        return fail("snapshot get overwritten key");
    }
    if (snapshot.get(key('3'), &value).ok()) {
        return fail("snapshot get erased key should miss");
    }

    std::cout << "[CHECK] PASS overlay correctness (shadow/tombstone/index-move through base+overlay)"
              << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// Options / main
// ---------------------------------------------------------------------------

static bool parseUll_(const char* text, unsigned long long& valueOut) {
    if (!text || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    valueOut = value;
    return true;
}

static bool parseInt_(const char* text, int& valueOut) {
    unsigned long long parsed = 0;
    if (!parseUll_(text, parsed) || parsed > 0x7fffffffull) {
        return false;
    }
    valueOut = static_cast<int>(parsed);
    return true;
}

static bool parseArgs_(int argc, char* argv[], BenchOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = (i + 1) < argc;
        if (arg == "--records" && hasValue) {
            if (!parseUll_(argv[++i], options.records) || options.records == 0) return false;
        } else if (arg == "--value-bytes" && hasValue) {
            if (!parseUll_(argv[++i], options.valueBytes) || options.valueBytes == 0) return false;
        } else if (arg == "--direct-get-samples" && hasValue) {
            if (!parseUll_(argv[++i], options.directGetSamples)) return false;
        } else if (arg == "--direct-write-samples" && hasValue) {
            if (!parseUll_(argv[++i], options.directWriteSamples)) return false;
        } else if (arg == "--direct-scan-reps" && hasValue) {
            if (!parseUll_(argv[++i], options.directScanReps)) return false;
        } else if (arg == "--list-limit" && hasValue) {
            if (!parseUll_(argv[++i], options.listLimit) || options.listLimit == 0) return false;
        } else if (arg == "--latency-requests" && hasValue) {
            if (!parseInt_(argv[++i], options.latencyRequests) || options.latencyRequests < 1) return false;
        } else if (arg == "--concurrency" && hasValue) {
            if (!parseInt_(argv[++i], options.concurrency) || options.concurrency < 1) return false;
        } else if (arg == "--requests-per-connection" && hasValue) {
            if (!parseInt_(argv[++i], options.requestsPerConnection) || options.requestsPerConnection < 1) return false;
        } else if (arg == "--timeout-ms" && hasValue) {
            if (!parseInt_(argv[++i], options.timeoutMs) || options.timeoutMs < 1) return false;
        } else if (arg == "--write-threads" && hasValue) {
            if (!parseInt_(argv[++i], options.writeThreads) || options.writeThreads < 1) return false;
        } else if (arg == "--threadpool") {
            options.threadPool = true;
        } else if (arg == "--db-path" && hasValue) {
            options.dbPath = SwString(argv[++i]);
        } else if (arg == "--lazy-write") {
            options.lazyWrite = true;
        } else if (arg == "--keep-db") {
            options.keepDb = true;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << std::endl;
            return false;
        }
    }
    if (options.listLimit > options.records) {
        std::cerr << "--list-limit must be <= --records" << std::endl;
        return false;
    }
    return true;
}

struct SummaryRow_ {
    const char* label;
    double directP50;
    double httpP50;
    double floorP50;
};

static void printSummaryRow_(const SummaryRow_& row) {
    const double marginal = row.httpP50 - row.floorP50;
    const double overheadRatio = row.directP50 > 0.0 ? marginal / row.directP50 : 0.0;
    std::cout << std::fixed << std::setprecision(2)
              << "[SUMMARY] " << row.label
              << " direct-db-p50-us=" << row.directP50
              << " http-total-p50-us=" << row.httpP50
              << " http-floor-p50-us=" << row.floorP50
              << " marginal-db-in-server-p50-us=" << marginal
              << " marginal-vs-direct=x" << overheadRatio
              << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    SwCoreApplication core(argc, argv);
    SW_UNUSED(core)

    BenchOptions options;
    if (!parseArgs_(argc, argv, options)) {
        std::cerr << "Usage: HttpAppDbBench [--records N] [--value-bytes N]\n"
                     "  [--direct-get-samples N] [--direct-write-samples N] [--direct-scan-reps N]\n"
                     "  [--list-limit N] [--latency-requests N] [--concurrency N]\n"
                     "  [--requests-per-connection N] [--timeout-ms N] [--write-threads N]\n"
                     "  [--threadpool] [--db-path PATH] [--lazy-write] [--keep-db]" << std::endl;
        return 1;
    }

    // --- DB path -----------------------------------------------------------
    SwString dbPath;
    bool autoCleanup = !options.keepDb;
    if (!options.dbPath.isEmpty()) {
        dbPath = swDbPlatform::normalizePath(options.dbPath);
        autoCleanup = false;
        if (swDbPlatform::directoryExists(dbPath) || swDbPlatform::fileExists(dbPath)) {
            std::cerr << "Refusing to reuse explicit --db-path because the path already exists: "
                      << dbPath.toStdString() << std::endl;
            return 1;
        }
    } else {
        const SwString baseTemp = SwStandardLocation::standardLocation(SwStandardLocationId::Temp);
        const unsigned long long token = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        dbPath = swDbPlatform::joinPath(baseTemp, SwString("SwHttpAppDbBench-") + SwString::number(token));
        (void)SwDir::removeRecursively(dbPath);
    }
    if (!SwDir::mkpathAbsolute(dbPath)) {
        std::cerr << "Failed to create benchmark directory: " << dbPath.toStdString() << std::endl;
        return 1;
    }

    std::cout << "[CONFIG] db-path=" << dbPath.toStdString() << std::endl;
    std::cout << "[CONFIG] records=" << options.records
              << " value-bytes=" << options.valueBytes
              << " list-limit=" << options.listLimit
              << " lazy-write=" << (options.lazyWrite ? "true" : "false") << std::endl;
    std::cout << "[CONFIG] latency-requests=" << options.latencyRequests
              << " concurrency=" << options.concurrency
              << " requests-per-connection=" << options.requestsPerConnection
              << " write-threads=" << options.writeThreads
              << " dispatch=" << (options.threadPool ? "threadpool" : "inline") << std::endl;

    bool ok = true;

    // --- Direct in-process baselines ----------------------------------------
    PhaseStats directWrite, directWriteMt, directGet, directScan;
    {
        SwEmbeddedDb db;
        const SwDbStatus openStatus = db.open(makeDbOptions_(dbPath, options.lazyWrite));
        if (!openStatus.ok()) {
            std::cerr << "DB open failed: " << openStatus.message().toStdString() << std::endl;
            return 1;
        }
        ok = prepopulateDb_(db, options) &&
             runDirectWritePhase_(db, options, directWrite) &&
             runDirectWriteMtPhase_(db, options, directWriteMt) &&
             runDirectGetPhase_(db, options, directGet) &&
             runDirectScanPhase_(db, options, directScan) &&
             runOverlayCorrectnessCheck_(db);
        db.close();
        if (!ok) {
            if (autoCleanup) {
                (void)SwDir::removeRecursively(dbPath);
            }
            return 1;
        }
        printPhase_(directWrite);
        printPhase_(directWriteMt);
        printPhase_(directGet);
        printPhase_(directScan);
    }

    // --- Server ------------------------------------------------------------
    SwThread serverThread("HttpAppDbBenchServer");
    BenchServerHost_* host = nullptr;
    uint16_t port = 0;
    {
        SwString error;
        const auto startedAt = std::chrono::steady_clock::now();
        if (!startBenchServer_(options, dbPath, serverThread, host, port, error)) {
            std::cerr << "Server start failed: " << error.toStdString() << std::endl;
            if (autoCleanup) {
                (void)SwDir::removeRecursively(dbPath);
            }
            return 1;
        }
        std::cout << std::fixed << std::setprecision(2)
                  << "[SETUP] server listening on 127.0.0.1:" << port
                  << " start-ms=" << elapsedMs_(std::chrono::steady_clock::now() - startedAt)
                  << std::endl;
    }

    // --- Correctness pre-checks (untimed) -----------------------------------
    {
        NativeHttpClient_ probe;
        long long latencyUs = 0;
        int statusCode = 0;
        std::string body;
        bool checksOk = probe.connect("127.0.0.1", port, options.timeoutMs);
        checksOk = checksOk &&
                   probe.request("GET", "/db/0", "127.0.0.1", "", options.timeoutMs,
                                 latencyUs, statusCode, &body) &&
                   body == makePayloadStd_(0, options.valueBytes);
        if (checksOk) {
            const std::string payload = makePayloadStd_(9000000ull, options.valueBytes);
            checksOk = probe.request("POST", "/db/9000000", "127.0.0.1", payload,
                                     options.timeoutMs, latencyUs, statusCode, nullptr) &&
                       probe.request("GET", "/db/9000000", "127.0.0.1", "", options.timeoutMs,
                                     latencyUs, statusCode, &body) &&
                       body == payload;
        }
        if (!checksOk) {
            std::cerr << "[CHECK] FAIL round-trip through /db routes (status="
                      << statusCode << ")" << std::endl;
            (void)stopBenchServer_(serverThread, host);
            if (autoCleanup) {
                (void)SwDir::removeRecursively(dbPath);
            }
            return 1;
        }
        std::cout << "[CHECK] PASS GET/POST round-trip matches expected payloads" << std::endl;
    }

    // --- HTTP scenarios ------------------------------------------------------
    const long long valueBytes = static_cast<long long>(options.valueBytes);
    const long long listBytes = static_cast<long long>(options.listLimit * options.valueBytes);
    const std::string postPayload = makePayloadStd_(0xabcdef1234ull, options.valueBytes);
    const unsigned long long records = options.records;
    const std::string listTarget = "/db/list/" + std::to_string(options.listLimit);

    const RequestFactory_ pingFactory = [](int, int) {
        return HttpRequestSpec_{"GET", "/ping", "", 2};
    };
    const RequestFactory_ echoFactory = [&postPayload](int, int) {
        return HttpRequestSpec_{"POST", "/echo", postPayload, 2};
    };
    const RequestFactory_ dbGetFactory = [records, valueBytes](int connectionIndex, int requestIndex) {
        const unsigned long long sample = static_cast<unsigned long long>(connectionIndex) * 1000003ull +
                                          static_cast<unsigned long long>(requestIndex);
        const unsigned long long id = splitMix64_(sample) % records;
        return HttpRequestSpec_{"GET", "/db/" + std::to_string(id), "", valueBytes};
    };
    // Unique ids far above both the prepopulated range and the direct-write range.
    const auto makeDbPostFactory = [&postPayload](unsigned long long base, int requestsPerConnection) {
        return [&postPayload, base, requestsPerConnection](int connectionIndex, int requestIndex) {
            const unsigned long long id = base +
                static_cast<unsigned long long>(connectionIndex) *
                    static_cast<unsigned long long>(requestsPerConnection) +
                static_cast<unsigned long long>(requestIndex);
            return HttpRequestSpec_{"POST", "/db/" + std::to_string(id), postPayload, 2};
        };
    };
    const RequestFactory_ dbListFactory = [&listTarget, listBytes](int, int) {
        return HttpRequestSpec_{"GET", listTarget, "", listBytes};
    };
    // Worst case for any snapshot caching: every write invalidates, every list
    // rebuilds. Alternates POST (even requests) and list (odd requests).
    const auto makeMixedFactory = [&postPayload, &listTarget, listBytes](unsigned long long base,
                                                                         int requestsPerConnection) {
        return [&postPayload, &listTarget, listBytes, base, requestsPerConnection](int connectionIndex,
                                                                                   int requestIndex) {
            if ((requestIndex % 2) == 0) {
                const unsigned long long id = base +
                    static_cast<unsigned long long>(connectionIndex) *
                        static_cast<unsigned long long>(requestsPerConnection) +
                    static_cast<unsigned long long>(requestIndex);
                return HttpRequestSpec_{"POST", "/db/" + std::to_string(id), postPayload, 2};
            }
            return HttpRequestSpec_{"GET", listTarget, "", listBytes};
        };
    };

    struct ScenarioDef_ {
        const char* name;
        RequestFactory_ factory;
        int listDivisor; // list responses are heavy; run fewer of them
    };
    const ScenarioDef_ scenarios[] = {
        {"http-ping", pingFactory, 1},
        {"http-echo-post", echoFactory, 1},
        {"http-db-get", dbGetFactory, 1},
        {"http-db-post", RequestFactory_(), 1}, // installed per run below (unique id base)
        {"http-db-list", dbListFactory, 5},
        {"http-db-mixed", RequestFactory_(), 5}, // installed per run below (unique id base)
    };

    std::vector<PhaseStats> latencyRuns;
    std::vector<PhaseStats> throughputRuns;
    unsigned long long postBase = 10000000ull;

    for (const ScenarioDef_& scenario : scenarios) {
        const bool isPost = std::strcmp(scenario.name, "http-db-post") == 0;
        const bool isMixed = std::strcmp(scenario.name, "http-db-mixed") == 0;

        const int latencyCount = std::max(1, options.latencyRequests / scenario.listDivisor);
        RequestFactory_ latencyFactory = scenario.factory;
        if (isPost) {
            latencyFactory = makeDbPostFactory(postBase, latencyCount);
            postBase += static_cast<unsigned long long>(latencyCount) + 1000ull;
        } else if (isMixed) {
            latencyFactory = makeMixedFactory(postBase, latencyCount);
            postBase += static_cast<unsigned long long>(latencyCount) + 1000ull;
        }
        PhaseStats latencyStats = runHttpScenario_(
            SwString(scenario.name) + "-c1", port, 1, latencyCount,
            options.timeoutMs, latencyFactory);
        printPhase_(latencyStats);
        ok = ok && latencyStats.errors == 0 && latencyStats.operations > 0;
        latencyRuns.push_back(latencyStats);

        const int throughputCount = std::max(1, options.requestsPerConnection / scenario.listDivisor);
        RequestFactory_ throughputFactory = scenario.factory;
        if (isPost) {
            throughputFactory = makeDbPostFactory(postBase, throughputCount);
        } else if (isMixed) {
            throughputFactory = makeMixedFactory(postBase, throughputCount);
        }
        if (isPost || isMixed) {
            postBase += static_cast<unsigned long long>(options.concurrency) *
                            static_cast<unsigned long long>(throughputCount) +
                        1000ull;
        }
        PhaseStats throughputStats = runHttpScenario_(
            SwString(scenario.name) + "-c" + SwString::number(options.concurrency),
            port, options.concurrency, throughputCount,
            options.timeoutMs, throughputFactory);
        printPhase_(throughputStats);
        ok = ok && throughputStats.errors == 0 && throughputStats.operations > 0;
        throughputRuns.push_back(throughputStats);
    }

    // --- Teardown ------------------------------------------------------------
    if (!stopBenchServer_(serverThread, host)) {
        std::cerr << "Server stop timeout" << std::endl;
        ok = false;
    }
    if (autoCleanup) {
        (void)SwDir::removeRecursively(dbPath);
    }

    // --- Summary -------------------------------------------------------------
    // latencyRuns order: ping, echo-post, db-get, db-post, db-list, db-mixed (all at c=1)
    if (latencyRuns.size() >= 5) {
        const PhaseStats& ping = latencyRuns[0];
        const PhaseStats& echoPost = latencyRuns[1];
        const PhaseStats& dbGet = latencyRuns[2];
        const PhaseStats& dbPost = latencyRuns[3];
        const PhaseStats& dbList = latencyRuns[4];

        printSummaryRow_({"get ", directGet.p50, dbGet.p50, ping.p50});
        printSummaryRow_({"post", directWrite.p50, dbPost.p50, echoPost.p50});
        printSummaryRow_({"list", directScan.p50, dbList.p50, ping.p50});
        std::cout << std::fixed << std::setprecision(2)
                  << "[SUMMARY] throughput-c" << options.concurrency
                  << " ping=" << throughputRuns[0].opsPerSec
                  << " db-get=" << throughputRuns[2].opsPerSec
                  << " db-post=" << throughputRuns[3].opsPerSec
                  << " db-list=" << throughputRuns[4].opsPerSec;
        if (throughputRuns.size() > 5) {
            std::cout << " db-mixed=" << throughputRuns[5].opsPerSec;
        }
        std::cout << " req-per-sec" << std::endl;
        if (latencyRuns.size() > 5) {
            std::cout << std::fixed << std::setprecision(2)
                      << "[SUMMARY] mixed-c1 p50-us=" << latencyRuns[5].p50
                      << " p95-us=" << latencyRuns[5].p95 << std::endl;
        }
    }

    if (!ok) {
        std::cerr << "[HttpAppDbBench] FAIL" << std::endl;
        return 1;
    }
    std::cout << "[HttpAppDbBench] PASS" << std::endl;
    return 0;
}
