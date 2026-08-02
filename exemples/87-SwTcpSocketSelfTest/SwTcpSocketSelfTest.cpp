#include "core/io/SwTcpServer.h"
#include "core/io/SwTcpSocket.h"
#include "core/runtime/SwCoreApplication.h"
#include "core/runtime/SwIoDispatcher.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// This executable is also run in Release configurations. Standard assert() would erase
// expressions such as listen()/connectToHost() under NDEBUG and make the test itself invalid.
#ifdef assert
#undef assert
#endif
#define assert(condition)                                                                  \
    do {                                                                                   \
        if (!(condition)) {                                                                \
            std::cerr << "requirement failed: " #condition << " at " << __FILE__ << ':'   \
                      << __LINE__ << std::endl;                                             \
            std::abort();                                                                  \
        }                                                                                  \
    } while (false)

#if !defined(_WIN32)
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace {

class InspectableTcpSocket : public SwTcpSocket {
public:
    using SwTcpSocket::SwTcpSocket;

    int nativeIntOption(int level, int option) const {
        if (!isSocketValid_()) {
            return -1;
        }
        int value = 0;
#if defined(_WIN32)
        int length = sizeof(value);
        if (::getsockopt(m_socket,
                         level,
                         option,
                         reinterpret_cast<char*>(&value),
                         &length) != 0) {
            return -1;
        }
#else
        socklen_t length = sizeof(value);
        if (::getsockopt(m_socket, level, option, &value, &length) != 0) {
            return -1;
        }
#endif
        return value;
    }

#if !defined(_WIN32)
    void resetDispatcherInterestObservations() {
        m_dispatcherInterestUpdateCount = 0;
        m_lastDispatcherInterest = SwIoDispatcher::None;
    }

    std::size_t dispatcherInterestUpdateCount() const {
        return m_dispatcherInterestUpdateCount;
    }

    bool lastDispatcherInterestIncludesWritable() const {
        return (m_lastDispatcherInterest & SwIoDispatcher::Writable) != 0;
    }

    bool dispatchWritableEventForTest() {
        return handleTransportWritableEvent_();
    }

protected:
    SwIoDispatcher::EventMask desiredDispatcherEvents_() const override {
        const SwIoDispatcher::EventMask events = SwTcpSocket::desiredDispatcherEvents_();
        ++m_dispatcherInterestUpdateCount;
        m_lastDispatcherInterest = events;
        return events;
    }

private:
    mutable std::size_t m_dispatcherInterestUpdateCount = 0;
    mutable SwIoDispatcher::EventMask m_lastDispatcherInterest = SwIoDispatcher::None;
#endif
};

template <typename Predicate>
bool pumpUntil(SwCoreApplication& app, Predicate predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        (void)app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

void pumpFor(SwCoreApplication& app, int durationMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(durationMs);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void testReliableDispatcherRetry() {
    SwIoDispatcher dispatcher;
    std::atomic<int> postAttempts{0};
    std::atomic<int> callbacks{0};

#if defined(_WIN32)
    HANDLE eventHandle = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(eventHandle != NULL);
    const SwIoDispatcher::Token token = dispatcher.watchHandleReliable(
        eventHandle,
        [&postAttempts](std::function<void()> task) -> bool {
            if (postAttempts.fetch_add(1, std::memory_order_relaxed) == 0) {
                return false;
            }
            task();
            return true;
        },
        [eventHandle, &callbacks]() {
            ::ResetEvent(eventHandle);
            callbacks.fetch_add(1, std::memory_order_relaxed);
        });
    assert(token != 0);
    assert(::SetEvent(eventHandle) != FALSE);
#else
    const int eventHandle = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(eventHandle >= 0);
    const SwIoDispatcher::Token token = dispatcher.watchFdReliable(
        eventHandle,
        SwIoDispatcher::Readable | SwIoDispatcher::Error,
        [&postAttempts](std::function<void()> task) -> bool {
            if (postAttempts.fetch_add(1, std::memory_order_relaxed) == 0) {
                return false;
            }
            task();
            return true;
        },
        [eventHandle, &callbacks](SwIoDispatcher::EventMask) {
            std::uint64_t value = 0;
            const ssize_t readResult = ::read(eventHandle, &value, sizeof(value));
            (void)readResult;
            callbacks.fetch_add(1, std::memory_order_relaxed);
        });
    assert(token != 0);
    const std::uint64_t one = 1;
    assert(::write(eventHandle, &one, sizeof(one)) == sizeof(one));
#endif

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (callbacks.load(std::memory_order_relaxed) == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(postAttempts.load(std::memory_order_relaxed) >= 2);
    assert(callbacks.load(std::memory_order_relaxed) == 1);

    dispatcher.remove(token);
#if defined(_WIN32)
    ::CloseHandle(eventHandle);
#else
    ::close(eventHandle);
#endif
}

void testColdDnsIsAsyncAndCancellationSafe(SwCoreApplication& app) {
    SwTcpSocket client;
    client.setConnectTimeout(2000);
    int errors = 0;
    int disconnects = 0;
    SwObject::connect(&client, &SwAbstractSocket::errorOccurred, [&](int) {
        ++errors;
    });
    SwObject::connect(&client, &SwAbstractSocket::disconnected, [&]() {
        ++disconnects;
    });

    const long long nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const SwString host(std::string("swstack-cold-") + std::to_string(nonce) + ".invalid");
    const auto startedAt = std::chrono::steady_clock::now();
    assert(client.connectToHost(host, 443));
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startedAt)
                               .count();
    // The first lookup is queued on the resolver worker; the event-loop caller never runs
    // getaddrinfo and remains cancelable while the request is outstanding.
    assert(elapsedMs < 200);
    assert(client.state() == SwAbstractSocket::HostLookupState);

    client.cancelConnect();
    assert(client.state() == SwAbstractSocket::UnconnectedState);
    assert(disconnects == 1);
    pumpFor(app, 100);
    assert(errors == 0);
    assert(disconnects == 1);
}

void testResolverCacheSemantics() {
    SwHostResolver& resolver = SwHostResolver::instance();
    resolver.store("SwStack-Negative.Test", SwString());
    bool negativeHit = false;
    const SwHostResolver::AddressList negative =
        resolver.cachedAddresses("swstack-negative.test", &negativeHit);
    assert(negative.empty());
    assert(negativeHit);

    resolver.store("SwStack-Positive.Test", "127.0.0.1");
    negativeHit = true;
    const SwHostResolver::AddressList positive =
        resolver.cachedAddresses("swstack-positive.test", &negativeHit);
    assert(positive.size() == 1);
    assert(positive.front().family == AF_INET);
    assert(!negativeHit);
}

void testHappyEyeballsAndTcpOptions(SwCoreApplication& app) {
    SwTcpServer server;
    assert(server.listen("127.0.0.1", 0));

    InspectableTcpSocket client;
    client.setHappyEyeballsDelay(10);
    client.setConnectTimeout(3000);
    client.setTcpNoDelay(true);
    client.setKeepAlive(true);
    client.setTcpUserTimeout(12345);
    int connectedCount = 0;
    int errorCount = 0;
    SwObject::connect(&client, &SwAbstractSocket::connected, [&]() {
        ++connectedCount;
    });
    SwObject::connect(&client, &SwAbstractSocket::errorOccurred, [&](int) {
        ++errorCount;
    });

    assert(client.connectToHost("localhost", server.localPort()));
    assert(pumpUntil(app,
                     [&]() {
                         return client.state() == SwAbstractSocket::ConnectedState &&
                                server.pendingConnectionCount() == 1;
                     },
                     5000));
    assert(connectedCount == 1);
    assert(errorCount == 0);
    assert(client.nativeIntOption(IPPROTO_TCP, TCP_NODELAY) == 1);
    assert(client.nativeIntOption(SOL_SOCKET, SO_KEEPALIVE) == 1);
#if defined(TCP_USER_TIMEOUT)
    assert(client.nativeIntOption(IPPROTO_TCP, TCP_USER_TIMEOUT) == 12345);
#endif
    assert(client.tcpNoDelay());
    assert(client.keepAlive());
    assert(client.tcpUserTimeout() == 12345);

    SwTcpSocket* peer = server.nextPendingConnection();
    assert(peer != nullptr);
    delete peer;
    client.abort();
    server.close();
}

void testConnectDeadlineCompletesOnce(SwCoreApplication& app) {
    SwTcpSocket client;
    client.setConnectTimeout(20);
    int errors = 0;
    int disconnects = 0;
    SwObject::connect(&client, &SwAbstractSocket::errorOccurred, [&](int) {
        ++errors;
    });
    SwObject::connect(&client, &SwAbstractSocket::disconnected, [&]() {
        ++disconnects;
    });

    const auto startedAt = std::chrono::steady_clock::now();
    const bool started = client.connectToHost("198.51.100.1", 65000);
    if (started) {
        assert(pumpUntil(app,
                         [&]() {
                             return client.state() == SwAbstractSocket::UnconnectedState;
                         },
                         1500));
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startedAt)
                               .count();
    assert(elapsedMs < 1500);
    assert(client.state() == SwAbstractSocket::UnconnectedState);
    assert(errors == 1);
    assert(disconnects == 1);
    pumpFor(app, 50);
    assert(errors == 1);
    assert(disconnects == 1);
}

void testTcpBackpressureAndDeferredShutdown(SwCoreApplication& app) {
    SwTcpServer server;
    server.setMaxPendingConnections(8);
    assert(server.listen("127.0.0.1", 0));
    assert(server.localPort() != 0);

    InspectableTcpSocket client;
    client.setSendBufferSize(4096);
    client.setWriteBufferWatermarks(128U * 1024U, 32U * 1024U);

    bool readyWriteSeen = false;
    bool clientDisconnected = false;
    SwObject::connect(&client, &SwIODevice::readyWrite, [&readyWriteSeen]() {
        readyWriteSeen = true;
    });
    SwObject::connect(&client, &SwAbstractSocket::disconnected, [&]() {
        clientDisconnected = true;
        assert(client.state() == SwAbstractSocket::UnconnectedState);
    });

    assert(client.connectToHost("127.0.0.1", server.localPort()));
    assert(pumpUntil(app,
                     [&]() {
                         return client.state() == SwAbstractSocket::ConnectedState &&
                                server.pendingConnectionCount() > 0;
                     },
                     3000));

    SwTcpSocket* peer = server.nextPendingConnection();
    assert(peer != nullptr);

    std::vector<char> chunk(16U * 1024U);
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        chunk[i] = static_cast<char>((i * 31U + 7U) & 0xFFU);
    }

    std::vector<char> expected;
    bool sawWouldBlock = false;
    for (int attempt = 0; attempt < 512; ++attempt) {
        const SwTcpSocket::WriteResult result = client.tryWrite(chunk.data(), chunk.size());
        if (result == SwTcpSocket::WriteResult::WouldBlock) {
            sawWouldBlock = true;
            break;
        }
        assert(result == SwTcpSocket::WriteResult::Accepted);
        expected.insert(expected.end(), chunk.begin(), chunk.end());
        assert(client.bytesToWrite() <= client.writeHighWatermark());
    }
    assert(sawWouldBlock);
    assert(client.state() == SwAbstractSocket::ConnectedState);
#if !defined(_WIN32)
    assert(client.bytesToWrite() > 0);
    assert(client.lastDispatcherInterestIncludesWritable());
#endif

    assert(client.shutdownWrite());
    assert(client.tryWrite("x", 1) == SwTcpSocket::WriteResult::WriteClosed);

    std::vector<char> received;
    received.reserve(expected.size());
    char readBuffer[32U * 1024U];
    assert(pumpUntil(app,
                     [&]() {
                         while (true) {
                             const int64_t bytes = peer->readInto(readBuffer, sizeof(readBuffer));
                             if (bytes <= 0) {
                                 break;
                             }
                             received.insert(received.end(), readBuffer, readBuffer + bytes);
                         }
                         return peer->isRemoteClosed() && received.size() == expected.size();
                     },
                     10000));

    assert(received == expected);
    assert(client.bytesToWrite() == 0);
    assert(readyWriteSeen);
    assert(peer->isRemoteClosed());
#if !defined(_WIN32)
    assert(!client.lastDispatcherInterestIncludesWritable());

    // Exercise the writable handler directly: it owns the responsibility for
    // recalculating the epoll mask after every flush, including an empty one.
    client.resetDispatcherInterestObservations();
    assert(client.dispatchWritableEventForTest());
    assert(client.dispatcherInterestUpdateCount() == 1);
    assert(!client.lastDispatcherInterestIncludesWritable());
#endif

    // Complete the opposite half-close. Both sockets may now release their native handles.
    assert(peer->shutdownWrite());
    assert(pumpUntil(app,
                     [&]() {
                         (void)client.readInto(readBuffer, sizeof(readBuffer));
                         return client.isRemoteClosed() && clientDisconnected;
                     },
                     3000));
    assert(client.state() == SwAbstractSocket::UnconnectedState);

    delete peer;
    client.abort();
    server.close();
}

void testDeferredCloseDeliversAllBytes(SwCoreApplication& app) {
    SwTcpServer server;
    assert(server.listen("127.0.0.1", 0));

    SwTcpSocket client;
    client.setSendBufferSize(4096);
    client.setWriteBufferWatermarks(2U * 1024U * 1024U, 256U * 1024U);
    bool disconnected = false;
    SwObject::connect(&client, &SwAbstractSocket::disconnected, [&]() {
        disconnected = true;
        assert(client.state() == SwAbstractSocket::UnconnectedState);
    });

    assert(client.connectToHost("127.0.0.1", server.localPort()));
    assert(pumpUntil(app,
                     [&]() {
                         return client.state() == SwAbstractSocket::ConnectedState &&
                                server.pendingConnectionCount() == 1;
                     },
                     3000));
    SwTcpSocket* peer = server.nextPendingConnection();
    assert(peer != nullptr);

    std::vector<char> payload(1024U * 1024U);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>((i * 13U + 19U) & 0xFFU);
    }
    assert(client.write(payload.data(), payload.size()));
    client.close();

    std::vector<char> received;
    received.reserve(payload.size());
    char buffer[32U * 1024U];
    const bool closeCompleted = pumpUntil(app,
                     [&]() {
                         while (true) {
                             const int64_t bytes = peer->readInto(buffer, sizeof(buffer));
                             if (bytes <= 0) {
                                 break;
                             }
                             received.insert(received.end(), buffer, buffer + bytes);
                         }
                         return disconnected && peer->isRemoteClosed() &&
                                received.size() == payload.size();
                     },
                     10000);
    if (!closeCompleted) {
        std::cerr << "deferred close timeout: disconnected=" << disconnected
                  << " clientState=" << static_cast<int>(client.state())
                  << " bytesToWrite=" << client.bytesToWrite()
                  << " peerRemoteClosed=" << peer->isRemoteClosed()
                  << " received=" << received.size()
                  << " expected=" << payload.size() << std::endl;
    }
    assert(closeCompleted);
    assert(received == payload);
    assert(client.state() == SwAbstractSocket::UnconnectedState);

    delete peer;
    server.close();
}

void testPendingAcceptCapAndCloseDrain(SwCoreApplication& app) {
    SwTcpServer server;
    server.setMaxPendingConnections(2);
    server.setAcceptBudget(2);
    assert(server.listen("127.0.0.1", 0));

    std::vector<std::unique_ptr<SwTcpSocket>> clients;
    for (int i = 0; i < 6; ++i) {
        clients.emplace_back(new SwTcpSocket());
        assert(clients.back()->connectToHost("127.0.0.1", server.localPort()));
    }

    assert(pumpUntil(app,
                     [&]() {
                         return server.pendingConnectionCount() == server.maxPendingConnections();
                     },
                     3000));
    assert(server.pendingConnectionCount() <= 2);

    server.close();
    assert(server.pendingConnectionCount() == 0);
    for (std::size_t i = 0; i < clients.size(); ++i) {
        clients[i]->abort();
    }
}

} // namespace

int main(int argc, char** argv) {
    testReliableDispatcherRetry();

    SwCoreApplication app(argc, argv);
    testColdDnsIsAsyncAndCancellationSafe(app);
    testResolverCacheSemantics();
    testHappyEyeballsAndTcpOptions(app);
    testConnectDeadlineCompletesOnce(app);
    testTcpBackpressureAndDeferredShutdown(app);
    testDeferredCloseDeliversAllBytes(app);
    testPendingAcceptCapAndCloseDrain(app);
    return 0;
}
