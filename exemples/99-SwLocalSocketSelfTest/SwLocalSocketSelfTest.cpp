// SwLocalSocketSelfTest — end-to-end checks for SwLocalSocket/SwLocalServer over the real
// platform endpoint (named pipe on Windows, Unix domain socket on POSIX): listen/connect,
// bidirectional exchange, peer credentials, large buffered transfer, EOF with buffered bytes,
// connect failure, multiple clients, and endpoint reclaim after close.

#include "core/io/SwLocalServer.h"
#include "core/io/SwLocalSocket.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <unistd.h>
#endif

// This executable is also run in Release configurations. Standard assert() would erase
// expressions under NDEBUG, so redefine it as an always-on requirement check.
#ifdef assert
#undef assert
#endif
#define assert(condition)                                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::cerr << "requirement failed: " #condition << " at " << __FILE__ << ':'            \
                      << __LINE__ << std::endl;                                                    \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

namespace {

std::string uniqueServerName(const char* suffix) {
#if defined(_WIN32)
    const unsigned long pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    return std::string("sw-local-selftest-") + std::to_string(pid) + "-" + suffix;
}

template <typename Predicate>
bool pumpUntil(SwCoreApplication& app, Predicate predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

void pumpFor(SwCoreApplication& app, int durationMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
    while (std::chrono::steady_clock::now() < deadline) {
        app.processEvent(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string drainToString(SwLocalSocket* socket) {
    const SwByteArray bytes = socket->read();
    return std::string(bytes.constData(), bytes.size());
}

void testListenConnectExchange(SwCoreApplication& app) {
    const std::string name = uniqueServerName("basic");
    SwLocalServer::removeServer(SwString(name));

    SwLocalServer server;
    assert(server.listen(SwString(name)));
    assert(server.isListening());

    SwLocalSocket client;
    bool clientConnected = false;
    SwObject::connect(&client, &SwLocalSocket::connected, [&clientConnected]() { clientConnected = true; });
    assert(client.connectToServer(SwString(name)));
    assert(pumpUntil(app, [&]() { return clientConnected; }, 5000));

    SwLocalSocket* accepted = nullptr;
    assert(pumpUntil(app, [&]() {
        if (!accepted) {
            accepted = server.nextPendingConnection();
        }
        return accepted != nullptr;
    }, 5000));
    assert(accepted->state() == SwAbstractSocket::ConnectedState);

    // Client → server.
    assert(client.write("hello-local", 11));
    assert(pumpUntil(app, [&]() { return accepted->bytesAvailable() >= 11; }, 5000));
    assert(drainToString(accepted) == "hello-local");

    // Server → client.
    assert(accepted->write("welcome", 7));
    assert(pumpUntil(app, [&]() { return client.bytesAvailable() >= 7; }, 5000));
    assert(drainToString(&client) == "welcome");

    // Same process on both ends: the attested peer pid must be ours.
    const SwLocalSocket::PeerCredentials credentials = accepted->peerCredentials();
#if defined(_WIN32)
    assert(credentials.pid == static_cast<std::int64_t>(GetCurrentProcessId()));
#else
    assert(credentials.pid == static_cast<std::int64_t>(::getpid()));
    assert(credentials.uid == static_cast<std::int64_t>(::getuid()));
#endif

    delete accepted;
    server.close();
    pumpFor(app, 20);
}

void testLargeBufferedTransfer(SwCoreApplication& app) {
    const std::string name = uniqueServerName("large");
    SwLocalServer::removeServer(SwString(name));

    SwLocalServer server;
    assert(server.listen(SwString(name)));

    SwLocalSocket client;
    assert(client.connectToServer(SwString(name)));

    SwLocalSocket* accepted = nullptr;
    assert(pumpUntil(app, [&]() {
        if (!accepted) {
            accepted = server.nextPendingConnection();
        }
        return accepted != nullptr && client.state() == SwAbstractSocket::ConnectedState;
    }, 5000));

    const std::size_t totalBytes = 1024 * 1024;
    std::vector<char> payload(totalBytes);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('A' + (i % 23));
    }

    bool writeDrained = false;
    SwObject::connect(&client, &SwLocalSocket::writeFinished, [&writeDrained]() { writeDrained = true; });
    assert(client.write(payload.data(), payload.size()));

    std::vector<char> received;
    received.reserve(totalBytes);
    assert(pumpUntil(app, [&]() {
        while (accepted->bytesAvailable() > 0) {
            const SwByteArray chunk = accepted->read();
            received.insert(received.end(), chunk.constData(), chunk.constData() + chunk.size());
        }
        return received.size() >= totalBytes;
    }, 15000));
    assert(received.size() == totalBytes);
    for (std::size_t i = 0; i < received.size(); ++i) {
        if (received[i] != payload[i]) {
            std::cerr << "payload mismatch at offset " << i << std::endl;
            std::abort();
        }
    }
    assert(pumpUntil(app, [&]() { return writeDrained && !client.hasPendingWrites(); }, 5000));

    delete accepted;
    server.close();
    pumpFor(app, 20);
}

void testEofKeepsBufferedBytes(SwCoreApplication& app) {
    const std::string name = uniqueServerName("eof");
    SwLocalServer::removeServer(SwString(name));

    SwLocalServer server;
    assert(server.listen(SwString(name)));

    SwLocalSocket client;
    assert(client.connectToServer(SwString(name)));

    SwLocalSocket* accepted = nullptr;
    assert(pumpUntil(app, [&]() {
        if (!accepted) {
            accepted = server.nextPendingConnection();
        }
        return accepted != nullptr && client.state() == SwAbstractSocket::ConnectedState;
    }, 5000));

    bool serverSideDisconnected = false;
    SwObject::connect(accepted, &SwLocalSocket::disconnected,
            [&serverSideDisconnected]() { serverSideDisconnected = true; });

    assert(client.write("bye", 3));
    assert(pumpUntil(app, [&]() { return !client.hasPendingWrites(); }, 5000));
    client.close();

    assert(pumpUntil(app, [&]() { return serverSideDisconnected; }, 5000));
    assert(accepted->state() == SwAbstractSocket::UnconnectedState);
    // Bytes received before the EOF stay readable after disconnected().
    assert(drainToString(accepted) == "bye");

    delete accepted;
    server.close();
    pumpFor(app, 20);
}

void testConnectFailure(SwCoreApplication& app) {
    SwLocalSocket client;
    client.setConnectTimeout(300);
    bool sawError = false;
    SwObject::connect(&client, &SwLocalSocket::errorOccurred, [&sawError](int) { sawError = true; });

    const std::string name = uniqueServerName("missing");
    client.connectToServer(SwString(name));
    assert(pumpUntil(app, [&]() { return sawError; }, 5000));
    assert(client.state() == SwAbstractSocket::UnconnectedState);
}

void testMultipleClients(SwCoreApplication& app) {
    const std::string name = uniqueServerName("multi");
    SwLocalServer::removeServer(SwString(name));

    SwLocalServer server;
    assert(server.listen(SwString(name)));

    constexpr int clientCount = 3;
    std::vector<SwLocalSocket*> clients;
    for (int i = 0; i < clientCount; ++i) {
        SwLocalSocket* client = new SwLocalSocket();
        assert(client->connectToServer(SwString(name)));
        clients.push_back(client);
    }

    std::vector<SwLocalSocket*> accepted;
    assert(pumpUntil(app, [&]() {
        while (SwLocalSocket* socket = server.nextPendingConnection()) {
            accepted.push_back(socket);
        }
        return static_cast<int>(accepted.size()) == clientCount;
    }, 5000));

    // Each accepted socket answers with its index; each client must get exactly its answer.
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        const char digit = static_cast<char>('0' + i);
        assert(accepted[i]->write(&digit, 1));
    }
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        assert(pumpUntil(app, [&]() {
            for (std::size_t c = 0; c < clients.size(); ++c) {
                if (clients[c]->bytesAvailable() == 0) {
                    return false;
                }
            }
            return true;
        }, 5000));
    }
    std::vector<bool> seen(clientCount, false);
    for (std::size_t c = 0; c < clients.size(); ++c) {
        const std::string answer = drainToString(clients[c]);
        assert(answer.size() == 1);
        const int index = answer[0] - '0';
        assert(index >= 0 && index < clientCount);
        assert(!seen[static_cast<std::size_t>(index)]);
        seen[static_cast<std::size_t>(index)] = true;
    }

    for (std::size_t i = 0; i < accepted.size(); ++i) {
        delete accepted[i];
    }
    for (std::size_t i = 0; i < clients.size(); ++i) {
        delete clients[i];
    }
    server.close();
    pumpFor(app, 20);
}

void testCloseAndReclaim(SwCoreApplication& app) {
    const std::string name = uniqueServerName("reclaim");
    SwLocalServer::removeServer(SwString(name));

    {
        SwLocalServer server;
        assert(server.listen(SwString(name)));
        server.close();
    }
    // The endpoint must be reclaimable and reusable right away.
    assert(SwLocalServer::removeServer(SwString(name)));

    SwLocalServer server;
    assert(server.listen(SwString(name)));
    SwLocalSocket client;
    assert(client.connectToServer(SwString(name)));
    assert(pumpUntil(app, [&]() {
        return client.state() == SwAbstractSocket::ConnectedState;
    }, 5000));
    server.close();
    pumpFor(app, 20);
}

} // namespace

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);

    testListenConnectExchange(app);
    testLargeBufferedTransfer(app);
    testEofKeepsBufferedBytes(app);
    testConnectFailure(app);
    testMultipleClients(app);
    testCloseAndReclaim(app);

    return 0;
}
