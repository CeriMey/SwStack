#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwHttpServer.h"
#include "SwString.h"
#include "SwThread.h"
#include "SwThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
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

struct PerfScenarioConfig_ {
    SwString name;
    bool useThreadPool = false;
    bool remoteTarget = false;
    int workIterations = 0;
    int concurrency = 24;
    int requestsPerConnection = 80;
    int acceptBurstConnections = 240;
    int requestTimeoutMs = 8000;
    int threadPoolWorkers = 0;
    uint16_t remotePort = 0;
    std::string connectHost = "127.0.0.1";
    std::string hostHeader = "127.0.0.1";
    std::string keepAliveTarget = "/cpu";
    std::string burstTarget = "/ping";
};

struct PerfScenarioResult_ {
    SwString name;
    bool useThreadPool = false;
    bool remoteTarget = false;
    long long keepAliveRequests = 0;
    long long keepAliveErrors = 0;
    double keepAliveDurationMs = 0.0;
    double keepAliveReqPerSec = 0.0;
    double keepAliveP50Ms = 0.0;
    double keepAliveP95Ms = 0.0;
    long long burstRequests = 0;
    long long burstErrors = 0;
    double burstDurationMs = 0.0;
    double burstReqPerSec = 0.0;
    double burstP50Ms = 0.0;
    double burstP95Ms = 0.0;
};

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
    char temp[8192];
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

    bool request(const std::string& target,
                 const std::string& hostHeader,
                 bool keepAlive,
                 int timeoutMs,
                 long long& latencyUsOut,
                 int& statusCodeOut) {
        latencyUsOut = 0;
        statusCodeOut = 0;
        if (m_socket == kInvalidNativeSocket_) {
            return false;
        }
        if (!setSocketTimeouts_(m_socket, timeoutMs)) {
            return false;
        }

        std::ostringstream builder;
        builder << "GET " << target << " HTTP/1.1\r\n";
        builder << "Host: " << hostHeader << "\r\n";
        builder << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
        builder << "\r\n";

        const auto startedAt = std::chrono::steady_clock::now();
        if (!sendAll_(m_socket, builder.str())) {
            return false;
        }
        if (!readResponse_(statusCodeOut)) {
            return false;
        }
        latencyUsOut = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - startedAt)
                            .count();
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
    bool readResponse_(int& statusCodeOut) {
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

                const size_t totalNeeded = headerEnd + 4 + contentLength;
                while (m_buffer.size() < totalNeeded) {
                    if (!recvSome_(m_socket, m_buffer)) {
                        return false;
                    }
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

static unsigned long long cpuWork_(unsigned long long seed, int iterations) {
    unsigned long long value = seed + 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < iterations; ++i) {
        value ^= (value << 13);
        value ^= (value >> 7);
        value ^= (value << 17);
        value += 0xA0761D6478BD642FULL + static_cast<unsigned long long>(i);
    }
    return value;
}

static double percentileMs_(std::vector<long long> samplesUs, double ratio) {
    if (samplesUs.empty()) {
        return 0.0;
    }
    std::sort(samplesUs.begin(), samplesUs.end());
    const size_t index = static_cast<size_t>(ratio * static_cast<double>(samplesUs.size() - 1));
    return static_cast<double>(samplesUs[index]) / 1000.0;
}

class PerfServerHost_ : public SwObject {
    SW_OBJECT(PerfServerHost_, SwObject)

public:
    explicit PerfServerHost_(const PerfScenarioConfig_& config, SwObject* parent = nullptr)
        : SwObject(parent),
          m_config(config) {
    }

    bool start() {
        SwHttpLimits limits;
        limits.maxBodyBytes = 1024 * 1024;
        limits.maxChunkSize = 256 * 1024;
        limits.maxConnections = 8192;
        limits.maxInFlightRequests = 8192;
        limits.maxThreadPoolQueuedDispatches = 8192;
        m_server.setLimits(limits);

        SwHttpTimeouts timeouts;
        timeouts.headerReadTimeoutMs = 5000;
        timeouts.bodyReadTimeoutMs = 5000;
        timeouts.keepAliveIdleTimeoutMs = 5000;
        timeouts.writeTimeoutMs = 5000;
        m_server.setTimeouts(timeouts);

        if (m_config.threadPoolWorkers <= 0) {
            m_pool.setMaxThreadCount(std::max(2u, std::thread::hardware_concurrency()));
        } else {
            m_pool.setMaxThreadCount(m_config.threadPoolWorkers);
        }
        m_pool.setMaxQueuedTaskCount(8192);

        if (m_config.useThreadPool) {
            m_server.setThreadPool(&m_pool);
            m_server.setDispatchMode(SwHttpServer::DispatchMode::ThreadPool);
        } else {
            m_server.setDispatchMode(SwHttpServer::DispatchMode::Inline);
        }

        m_server.addRoute("GET", "/ping", [](const SwHttpRequest& request) {
            SwHttpResponse response = swHttpTextResponse(200, "ok");
            response.closeConnection = !request.keepAlive;
            return response;
        });

        m_server.addRoute("GET", "/cpu", [this](const SwHttpRequest& request) {
            const unsigned long long work = cpuWork_(static_cast<unsigned long long>(request.target.size()),
                                                     m_config.workIterations);
            m_sink.fetch_add(work, std::memory_order_relaxed);
            SwHttpResponse response = swHttpTextResponse(200, "ok");
            response.closeConnection = !request.keepAlive;
            return response;
        });

        for (uint16_t port = 19650; port < 19750; ++port) {
            if (m_server.listen(port)) {
                m_port = port;
                return true;
            }
        }

        m_lastError = "unable to bind benchmark port";
        return false;
    }

    void stop() {
        m_server.close();
    }

    uint16_t port() const {
        return m_port;
    }

    SwString lastError() const {
        return m_lastError;
    }

private:
    PerfScenarioConfig_ m_config;
    SwHttpServer m_server;
    SwThreadPool m_pool;
    std::atomic<unsigned long long> m_sink{0};
    uint16_t m_port = 0;
    SwString m_lastError;
};

static bool startPerfServer_(const PerfScenarioConfig_& config,
                             SwThread& serverThread,
                             PerfServerHost_*& hostOut,
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
        PerfServerHost_* host = new PerfServerHost_(config, nullptr);
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
    if (!cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; })) {
        errorOut = "server start timeout";
        serverThread.quit();
        serverThread.wait();
        return false;
    }
    return ok;
}

static bool stopPerfServer_(SwThread& serverThread, PerfServerHost_* host) {
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
    const bool stopped = cv.wait_for(lock, std::chrono::seconds(10), [&]() { return done; });
    serverThread.quit();
    serverThread.wait();
    return stopped;
}

static void appendLatency_(std::vector<long long>& target,
                           const std::vector<long long>& source,
                           std::mutex& mutex) {
    std::lock_guard<std::mutex> lock(mutex);
    target.insert(target.end(), source.begin(), source.end());
}

static bool parseIntArg_(const char* text, int& valueOut) {
    if (!text) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    valueOut = static_cast<int>(value);
    return true;
}

static bool parsePortArg_(const char* text, uint16_t& valueOut) {
    int port = 0;
    if (!parseIntArg_(text, port) || port < 1 || port > 65535) {
        return false;
    }
    valueOut = static_cast<uint16_t>(port);
    return true;
}

static PerfScenarioResult_ runPerfScenario_(const PerfScenarioConfig_& config) {
    PerfScenarioResult_ result;
    result.name = config.name;
    result.useThreadPool = config.useThreadPool;
    result.remoteTarget = config.remoteTarget;

    SwThread serverThread("HttpPerfServerThread");
    PerfServerHost_* host = nullptr;
    uint16_t port = 0;
    if (config.remoteTarget) {
        port = config.remotePort;
    } else {
        SwString error;
        if (!startPerfServer_(config, serverThread, host, port, error)) {
            swError() << "[HttpPerfSelfTest] Failed to start scenario " << config.name << ": " << error;
            result.keepAliveErrors = 1;
            result.burstErrors = 1;
            return result;
        }
    }

    if (config.remoteTarget) {
        swDebug() << "[HttpPerfSelfTest] started scenario "
                  << config.name
                  << " mode=remote"
                  << " host="
                  << SwString(config.connectHost.c_str())
                  << " port=" << static_cast<int>(port)
                  << " keepaliveTarget=" << SwString(config.keepAliveTarget.c_str())
                  << " burstTarget=" << SwString(config.burstTarget.c_str());
    } else {
        swDebug() << "[HttpPerfSelfTest] started scenario "
                  << config.name
                  << " mode="
                  << (config.useThreadPool ? SwString("threadpool") : SwString("inline"))
                  << " port=" << static_cast<int>(port);
    }

    std::vector<long long> keepAliveLatenciesUs;
    std::mutex keepAliveMutex;
    std::atomic<long long> keepAliveErrors(0);

    const auto keepAliveStartedAt = std::chrono::steady_clock::now();
    std::vector<std::thread> keepAliveWorkers;
    keepAliveWorkers.reserve(static_cast<size_t>(config.concurrency));
    for (int i = 0; i < config.concurrency; ++i) {
        keepAliveWorkers.emplace_back([&, i]() {
            NativeHttpClient_ client;
            std::vector<long long> localLatencies;
            localLatencies.reserve(static_cast<size_t>(config.requestsPerConnection));
            if (!client.connect(config.connectHost, port, config.requestTimeoutMs)) {
                keepAliveErrors.fetch_add(config.requestsPerConnection, std::memory_order_relaxed);
                return;
            }
            for (int req = 0; req < config.requestsPerConnection; ++req) {
                long long latencyUs = 0;
                int statusCode = 0;
                std::string target = config.keepAliveTarget;
                if (!config.remoteTarget) {
                    target += "?id=" + std::to_string(i) + "_" + std::to_string(req);
                }
                if (!client.request(target,
                                    config.hostHeader,
                                    true,
                                    config.requestTimeoutMs,
                                    latencyUs,
                                    statusCode)) {
                    keepAliveErrors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                localLatencies.push_back(latencyUs);
            }
            appendLatency_(keepAliveLatenciesUs, localLatencies, keepAliveMutex);
        });
    }
    for (size_t i = 0; i < keepAliveWorkers.size(); ++i) {
        keepAliveWorkers[i].join();
    }
    const auto keepAliveEndedAt = std::chrono::steady_clock::now();

    result.keepAliveRequests = static_cast<long long>(keepAliveLatenciesUs.size());
    result.keepAliveErrors = keepAliveErrors.load(std::memory_order_relaxed);
    result.keepAliveDurationMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(keepAliveEndedAt - keepAliveStartedAt).count());
    if (result.keepAliveDurationMs > 0.0) {
        result.keepAliveReqPerSec = (static_cast<double>(result.keepAliveRequests) * 1000.0) / result.keepAliveDurationMs;
    }
    result.keepAliveP50Ms = percentileMs_(keepAliveLatenciesUs, 0.50);
    result.keepAliveP95Ms = percentileMs_(keepAliveLatenciesUs, 0.95);

    swDebug() << "[HttpPerfSelfTest] keepalive completed for " << config.name
              << " requests=" << result.keepAliveRequests
              << " errors=" << result.keepAliveErrors;

    std::vector<long long> burstLatenciesUs;
    std::mutex burstMutex;
    std::atomic<long long> burstErrors(0);
    const auto burstStartedAt = std::chrono::steady_clock::now();
    std::vector<std::thread> burstWorkers;
    burstWorkers.reserve(static_cast<size_t>(config.acceptBurstConnections));
    for (int i = 0; i < config.acceptBurstConnections; ++i) {
        burstWorkers.emplace_back([&, i]() {
            NativeHttpClient_ client;
            std::vector<long long> localLatencies;
            if (!client.connect(config.connectHost, port, config.requestTimeoutMs)) {
                burstErrors.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            long long latencyUs = 0;
            int statusCode = 0;
            std::string target = config.burstTarget;
            if (!config.remoteTarget) {
                target += "?burst=" + std::to_string(i);
            }
            if (!client.request(target,
                                config.hostHeader,
                                false,
                                config.requestTimeoutMs,
                                latencyUs,
                                statusCode)) {
                burstErrors.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            localLatencies.push_back(latencyUs);
            appendLatency_(burstLatenciesUs, localLatencies, burstMutex);
        });
    }
    for (size_t i = 0; i < burstWorkers.size(); ++i) {
        burstWorkers[i].join();
    }
    const auto burstEndedAt = std::chrono::steady_clock::now();

    result.burstRequests = static_cast<long long>(burstLatenciesUs.size());
    result.burstErrors = burstErrors.load(std::memory_order_relaxed);
    result.burstDurationMs = static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(burstEndedAt - burstStartedAt).count());
    if (result.burstDurationMs > 0.0) {
        result.burstReqPerSec = (static_cast<double>(result.burstRequests) * 1000.0) / result.burstDurationMs;
    }
    result.burstP50Ms = percentileMs_(burstLatenciesUs, 0.50);
    result.burstP95Ms = percentileMs_(burstLatenciesUs, 0.95);

    swDebug() << "[HttpPerfSelfTest] burst completed for " << config.name
              << " requests=" << result.burstRequests
              << " errors=" << result.burstErrors;

    if (!config.remoteTarget && !stopPerfServer_(serverThread, host)) {
        swError() << "[HttpPerfSelfTest] server stop timeout for scenario " << config.name;
        ++result.keepAliveErrors;
        ++result.burstErrors;
    }
    return result;
}

static void printScenarioResult_(const PerfScenarioResult_& result) {
    swDebug() << "[HttpPerfSelfTest] mode="
              << (result.remoteTarget ? SwString("remote")
                                      : (result.useThreadPool ? SwString("threadpool") : SwString("inline")))
              << " scenario=" << result.name;
    swDebug() << "[HttpPerfSelfTest]   keepalive requests=" << result.keepAliveRequests
              << " errors=" << result.keepAliveErrors
              << " durationMs=" << result.keepAliveDurationMs
              << " reqPerSec=" << result.keepAliveReqPerSec
              << " p50Ms=" << result.keepAliveP50Ms
              << " p95Ms=" << result.keepAliveP95Ms;
    swDebug() << "[HttpPerfSelfTest]   burst requests=" << result.burstRequests
              << " errors=" << result.burstErrors
              << " durationMs=" << result.burstDurationMs
              << " reqPerSec=" << result.burstReqPerSec
              << " p50Ms=" << result.burstP50Ms
              << " p95Ms=" << result.burstP95Ms;
}

} // namespace

int main(int argc, char* argv[]) {
    SwCoreApplication app(argc, argv);
    SW_UNUSED(app)

    bool remoteMode = false;
    std::string remoteHost;
    std::string remotePath = "/";
    uint16_t remotePort = 80;
    int overrideConcurrency = -1;
    int overrideRequestsPerConnection = -1;
    int overrideBurstConnections = -1;
    int overrideTimeoutMs = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--remote-host" || arg == "--host") && (i + 1) < argc) {
            remoteMode = true;
            remoteHost = argv[++i];
            continue;
        }
        if ((arg == "--remote-port" || arg == "--port") && (i + 1) < argc) {
            if (!parsePortArg_(argv[++i], remotePort)) {
                swError() << "[HttpPerfSelfTest] invalid port";
                return 1;
            }
            continue;
        }
        if ((arg == "--remote-path" || arg == "--path") && (i + 1) < argc) {
            remotePath = argv[++i];
            if (remotePath.empty()) {
                remotePath = "/";
            }
            continue;
        }
        if (arg == "--concurrency" && (i + 1) < argc) {
            if (!parseIntArg_(argv[++i], overrideConcurrency) || overrideConcurrency < 1) {
                swError() << "[HttpPerfSelfTest] invalid concurrency";
                return 1;
            }
            continue;
        }
        if (arg == "--requests-per-connection" && (i + 1) < argc) {
            if (!parseIntArg_(argv[++i], overrideRequestsPerConnection) || overrideRequestsPerConnection < 1) {
                swError() << "[HttpPerfSelfTest] invalid requests-per-connection";
                return 1;
            }
            continue;
        }
        if (arg == "--burst-connections" && (i + 1) < argc) {
            if (!parseIntArg_(argv[++i], overrideBurstConnections) || overrideBurstConnections < 1) {
                swError() << "[HttpPerfSelfTest] invalid burst-connections";
                return 1;
            }
            continue;
        }
        if (arg == "--timeout-ms" && (i + 1) < argc) {
            if (!parseIntArg_(argv[++i], overrideTimeoutMs) || overrideTimeoutMs < 1) {
                swError() << "[HttpPerfSelfTest] invalid timeout-ms";
                return 1;
            }
            continue;
        }
    }

    PerfScenarioConfig_ inlineConfig;
    inlineConfig.name = "baseline";
    inlineConfig.useThreadPool = false;
    inlineConfig.workIterations = 16000;
    inlineConfig.concurrency = 24;
    inlineConfig.requestsPerConnection = 80;
    inlineConfig.acceptBurstConnections = 240;
    inlineConfig.threadPoolWorkers = std::max(2u, std::thread::hardware_concurrency());

    PerfScenarioConfig_ threadPoolConfig = inlineConfig;
    threadPoolConfig.useThreadPool = true;

    if (remoteMode) {
        PerfScenarioConfig_ remoteConfig;
        remoteConfig.name = "remote";
        remoteConfig.remoteTarget = true;
        remoteConfig.connectHost = remoteHost;
        remoteConfig.hostHeader = remoteHost;
        remoteConfig.remotePort = remotePort;
        remoteConfig.keepAliveTarget = remotePath;
        remoteConfig.burstTarget = remotePath;
        remoteConfig.concurrency = 8;
        remoteConfig.requestsPerConnection = 10;
        remoteConfig.acceptBurstConnections = 40;
        remoteConfig.requestTimeoutMs = 10000;

        if (remotePort != 80) {
            remoteConfig.hostHeader += ":" + std::to_string(static_cast<unsigned int>(remotePort));
        }
        if (overrideConcurrency > 0) {
            remoteConfig.concurrency = overrideConcurrency;
        }
        if (overrideRequestsPerConnection > 0) {
            remoteConfig.requestsPerConnection = overrideRequestsPerConnection;
        }
        if (overrideBurstConnections > 0) {
            remoteConfig.acceptBurstConnections = overrideBurstConnections;
        }
        if (overrideTimeoutMs > 0) {
            remoteConfig.requestTimeoutMs = overrideTimeoutMs;
        }

        const PerfScenarioResult_ remoteResult = runPerfScenario_(remoteConfig);
        printScenarioResult_(remoteResult);

        bool ok = true;
        ok = ok && remoteResult.keepAliveErrors == 0 && remoteResult.burstErrors == 0;
        ok = ok && remoteResult.keepAliveRequests > 0 && remoteResult.burstRequests > 0;
        if (!ok) {
            swError() << "[HttpPerfSelfTest] FAIL";
            return 1;
        }

        swDebug() << "[HttpPerfSelfTest] PASS";
        return 0;
    }

    const PerfScenarioResult_ inlineResult = runPerfScenario_(inlineConfig);
    const PerfScenarioResult_ threadPoolResult = runPerfScenario_(threadPoolConfig);

    printScenarioResult_(inlineResult);
    printScenarioResult_(threadPoolResult);

    bool ok = true;
    ok = ok && inlineResult.keepAliveErrors == 0 && inlineResult.burstErrors == 0;
    ok = ok && threadPoolResult.keepAliveErrors == 0 && threadPoolResult.burstErrors == 0;
    ok = ok && inlineResult.keepAliveRequests > 0 && threadPoolResult.keepAliveRequests > 0;
    ok = ok && inlineResult.burstRequests > 0 && threadPoolResult.burstRequests > 0;

    if (!ok) {
        swError() << "[HttpPerfSelfTest] FAIL";
        return 1;
    }

    swDebug() << "[HttpPerfSelfTest] PASS";
    return 0;
}
