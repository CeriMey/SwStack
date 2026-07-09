// SwQuicSocket self-test — event-loop driven (readyRead), loopback + ephemeral
// ports only. Two SwQuicSocket on real UDP sockets (127.0.0.1):
//   - the server registers a demux prefix {0x3F,'V','G','D'} and listens (RPK);
//   - the client connect()s to the server AND sends a nude datagram starting
//     with 0x3F"VGD".
// SwCoreApplication::exec() drives everything; a periodic checker app.quit()s
// once (a) the QUIC handshake completed, (b) a QUIC DATAGRAM A->B was received,
// and (c) the 0x3F prefix handler fired. NO manual pump — all I/O flows through
// SwUdpSocket::readyRead.

#include "SwCoreApplication.h"
#include "quic/SwQuicSocket.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

int main() {
    SwCoreApplication app;

    // ---- shared test state --------------------------------------------------
    bool serverAccepted = false;
    bool gotQuicDatagram = false;
    bool gotPrefix = false;
    bool sentDatagram = false;
    std::string quicPayloadSeen;
    std::string prefixPayloadSeen;
    std::shared_ptr<SwQuicConnectionHandle> serverConn;

    // ---- server -------------------------------------------------------------
    SwQuicSocket server;
    if (!server.bind(SwString("127.0.0.1"), 0)) {
        std::printf("FAIL: server bind failed\n");
        return 2;
    }
    const std::uint16_t serverPort = server.localPort();

    // Demux prefix 0x3F 'V' 'G' 'D' -> handler (never reaches the QUIC engine).
    SwByteArray prefix;
    prefix.append(static_cast<char>(0x3F));
    prefix.append('V');
    prefix.append('G');
    prefix.append('D');
    server.onPrefix(prefix, [&](const std::uint8_t* data, std::size_t len,
                                const SwString& from, std::uint16_t port) {
        gotPrefix = true;
        prefixPayloadSeen.assign(reinterpret_cast<const char*>(data), len);
        std::printf("[server] 0x3F prefix handler fired: %zu bytes from %s:%u\n",
                    len, from.toStdString().c_str(), static_cast<unsigned>(port));
    });

    server.endpoint().listen(SwString("h3"), SwQuicAuthMode::RawPublicKey,
                             [&](std::shared_ptr<SwQuicConnectionHandle> conn) {
        serverAccepted = true;
        serverConn = conn;
        SwQuicCallbacks cbs;
        cbs.onDatagram = [&](const SwQuicPathHandle& /*path*/,
                             const std::uint8_t* data, std::size_t len) {
            gotQuicDatagram = true;
            quicPayloadSeen.assign(reinterpret_cast<const char*>(data), len);
            std::printf("[server] QUIC DATAGRAM received: %zu bytes ('%s')\n",
                        len, quicPayloadSeen.c_str());
        };
        conn->setCallbacks(cbs);
        std::printf("[server] handshake accepted (established=%d)\n",
                    conn->isEstablished() ? 1 : 0);
    });

    // ---- client -------------------------------------------------------------
    SwQuicSocket client;
    if (!client.bind(SwString("127.0.0.1"), 0)) {
        std::printf("FAIL: client bind failed\n");
        return 2;
    }

    SwQuicCallbacks clientCbs;
    clientCbs.verifyPeerKey = [](const SwByteArray& spkiDer) -> bool {
        // Delegated trust: accept the server's presented key for the test.
        std::printf("[client] verifyPeerKey: %d bytes presented -> accept\n",
                    static_cast<int>(spkiDer.size()));
        return true;
    };

    std::shared_ptr<SwQuicConnectionHandle> clientConn =
        client.endpoint().connect(SwString("127.0.0.1"), serverPort,
                                  SwString("h3"), SwQuicAuthMode::RawPublicKey, clientCbs);
    if (!clientConn) {
        std::printf("FAIL: connect() returned null\n");
        return 2;
    }

    // Nude datagram beginning with the demux prefix 0x3F"VGD".
    SwByteArray nude;
    nude.append(static_cast<char>(0x3F));
    nude.append('V');
    nude.append('G');
    nude.append('D');
    nude.append("hello-demux", 11);
    client.writeDatagram(nude, SwString("127.0.0.1"), serverPort);
    std::printf("[client] sent nude 0x3F prefix datagram (%d bytes)\n",
                static_cast<int>(nude.size()));

    // ---- checker: periodic, event-loop timer (no manual pump) ---------------
    int ticks = 0;
    const int kMaxTicks = 800; // 800 * 10ms = 8s guard
    bool passed = false;

    app.addTimer([&]() {
        ++ticks;

        // Once the client handshake completes, send a QUIC DATAGRAM A->B once.
        if (clientConn->isEstablished() && !sentDatagram) {
            const char* msg = "quic-datagram-A-to-B";
            const bool ok = clientConn->sendDatagram(
                reinterpret_cast<const std::uint8_t*>(msg),
                static_cast<std::size_t>(std::string(msg).size()));
            sentDatagram = true;
            std::printf("[client] handshake established; sendDatagram ok=%d\n", ok ? 1 : 0);
        }

        const bool handshakeOk = clientConn->isEstablished() && serverAccepted &&
                                 (serverConn && serverConn->isEstablished());
        if (handshakeOk && gotQuicDatagram && gotPrefix) {
            passed = true;
            app.quit();
            return;
        }
        if (ticks >= kMaxTicks) {
            app.quit();
        }
    }, 10000 /* us = 10ms */, false);

    app.exec();

    // ---- assertions ---------------------------------------------------------
    const bool handshakeOk = clientConn->isEstablished() && serverAccepted &&
                             (serverConn && serverConn->isEstablished());
    std::printf("\n==== RESULTS (ticks=%d) ====\n", ticks);
    std::printf("  QUIC handshake complete (client+server established): %s\n",
                handshakeOk ? "YES" : "NO");
    std::printf("  QUIC DATAGRAM A->B delivered to onDatagram:          %s%s\n",
                gotQuicDatagram ? "YES" : "NO",
                gotQuicDatagram ? (" ('" + quicPayloadSeen + "')").c_str() : "");
    std::printf("  0x3F demux prefix routed to prefix handler:          %s%s\n",
                gotPrefix ? "YES" : "NO",
                gotPrefix ? (" ('" + prefixPayloadSeen + "')").c_str() : "");

    if (!passed || !handshakeOk || !gotQuicDatagram || !gotPrefix) {
        std::printf("\nRESULT: FAIL\n");
        return 1;
    }

    // Sanity on payload correctness.
    if (quicPayloadSeen != "quic-datagram-A-to-B") {
        std::printf("\nRESULT: FAIL (QUIC datagram payload mismatch)\n");
        return 1;
    }
    if (prefixPayloadSeen != std::string("\x3F""VGDhello-demux", 15)) {
        std::printf("\nRESULT: FAIL (prefix payload mismatch)\n");
        return 1;
    }

    std::printf("\nRESULT: PASS (event-loop-driven via readyRead)\n");
    return 0;
}
