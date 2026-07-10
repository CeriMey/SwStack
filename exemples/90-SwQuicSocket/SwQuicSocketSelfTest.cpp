// SwQuicSocket self-test — SIGNAL-DRIVEN, event-loop driven, loopback + ephemeral
// ports only. "Data on the UDP -> a signal climbs the layers": nothing is polled
// by hand and NO std::function notifies data — every event travels through
// DECLARE_SIGNAL/connect. Two SwQuicSocket on real UDP sockets (127.0.0.1):
//   - the server registers a demux prefix {0x3F,'V','G','D'} (nude, non-QUIC) and
//     listen()s (RPK). It connects a slot to SwQuicEndpoint::connectionAccepted;
//     inside that slot it connects a slot to the accepted connection's
//     datagramReceived SIGNAL.
//   - the client connect()s; it connects slots to ITS connection's established and
//     datagramReceived SIGNALs. On established (SIGNAL) it sends one QUIC DATAGRAM
//     A->B and one nude 0x3F"VGD" datagram.
// SwCoreApplication::exec() drives everything; a SwTimer checker (its timeout
// SIGNAL) app.quit()s once (a) the handshake completed, (b) a QUIC DATAGRAM A->B
// arrived via datagramReceived, and (c) the 0x3F prefix handler fired.
// The ONLY std::function is verifyPeerKey — a DECISION (returns bool), not a
// notification. NO manual pump — all I/O flows through SwUdpSocket::readyRead.

#include "SwCoreApplication.h"
#include "SwTimer.h"
#include "quic/SwQuicSocket.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

int main() {
    SwCoreApplication app;

    // A plain SwObject used as the slot receiver for the signal connections.
    SwObject bus;

    // ---- shared test state (written only from slots) ------------------------
    bool clientEstablished = false;   // set by clientConn->established SIGNAL
    bool serverAccepted    = false;   // set by endpoint->connectionAccepted SIGNAL
    bool serverGotDatagram = false;   // set by serverConn->datagramReceived SIGNAL
    bool gotPrefix         = false;   // set by the 0x3F onPrefix handler
    bool sentQuicDatagram  = false;
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

    // Nude demux prefix 0x3F 'V' 'G' 'D' -> handler (never reaches the QUIC engine).
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

    // The endpoint accepts inbound connections: SIGNAL connectionAccepted.
    server.endpoint().listen(SwString("h3"), SwQuicAuthMode::RawPublicKey);
    SwObject::connect(&server.endpoint(), &SwQuicEndpoint::connectionAccepted, &bus,
                      [&](std::shared_ptr<SwQuicConnectionHandle> conn) {
        serverAccepted = true;
        serverConn = conn;
        std::printf("[server] connectionAccepted SIGNAL (established=%d)\n",
                    conn->isEstablished() ? 1 : 0);
        // Data climbs from THIS connection through its datagramReceived SIGNAL.
        SwObject::connect(conn.get(), &SwQuicConnectionHandle::datagramReceived, &bus,
                          [&](const SwByteArray& bytes) {
            serverGotDatagram = true;
            quicPayloadSeen.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
            std::printf("[server] datagramReceived SIGNAL: %d bytes ('%s')\n",
                        static_cast<int>(bytes.size()), quicPayloadSeen.c_str());
        });
    });

    // ---- client -------------------------------------------------------------
    SwQuicSocket client;
    if (!client.bind(SwString("127.0.0.1"), 0)) {
        std::printf("FAIL: client bind failed\n");
        return 2;
    }

    // Delegated-trust decision hook (the only surviving std::function; returns bool).
    client.endpoint().setVerifyPeerKey([](const SwByteArray& spkiDer) -> bool {
        std::printf("[client] verifyPeerKey: %d bytes presented -> accept\n",
                    static_cast<int>(spkiDer.size()));
        return true;
    });

    std::shared_ptr<SwQuicConnectionHandle> clientConn =
        client.endpoint().connect(SwString("127.0.0.1"), serverPort,
                                  SwString("h3"), SwQuicAuthMode::RawPublicKey);
    if (!clientConn) {
        std::printf("FAIL: connect() returned null\n");
        return 2;
    }

    // Client-side signals: established (0-arg) drives the two sends; datagramReceived
    // is wired for symmetry (unused by this scenario, but proves the plumbing).
    SwObject::connect(clientConn.get(), &SwQuicConnectionHandle::datagramReceived, &bus,
                      [&](const SwByteArray& bytes) {
        std::printf("[client] datagramReceived SIGNAL: %d bytes\n",
                    static_cast<int>(bytes.size()));
    });
    SwObject::connect(clientConn.get(), &SwQuicConnectionHandle::established, &bus, [&]() {
        clientEstablished = true;
        std::printf("[client] established SIGNAL\n");
        if (!sentQuicDatagram) {
            sentQuicDatagram = true;
            const std::string msg = "quic-datagram-A-to-B";
            const bool ok = clientConn->sendDatagram(
                reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
            std::printf("[client] sendDatagram ok=%d\n", ok ? 1 : 0);

            // Nude datagram beginning with the demux prefix 0x3F"VGD".
            SwByteArray nude;
            nude.append(static_cast<char>(0x3F));
            nude.append('V');
            nude.append('G');
            nude.append('D');
            nude.append("hello-demux", 11);
            client.writeDatagram(nude, SwString("127.0.0.1"), serverPort);
            std::printf("[client] sent nude 0x3F datagram (%d bytes)\n",
                        static_cast<int>(nude.size()));
        }
    });

    // ---- checker: SwTimer, its timeout SIGNAL quits the loop ----------------
    int ticks = 0;
    const int kMaxTicks = 800; // 800 * 10ms = 8s guard
    bool passed = false;

    SwTimer checker(10, &bus);
    SwObject::connect(&checker, &SwTimer::timeout, &bus, [&]() {
        ++ticks;
        const bool handshakeOk =
            clientEstablished && serverAccepted && serverConn && serverConn->isEstablished();
        if (handshakeOk && serverGotDatagram && gotPrefix) {
            passed = true;
            app.quit();
            return;
        }
        if (ticks >= kMaxTicks) {
            app.quit();
        }
    });
    checker.start();

    app.exec();

    // ---- assertions ---------------------------------------------------------
    const bool handshakeOk =
        clientEstablished && serverAccepted && serverConn && serverConn->isEstablished();
    std::printf("\n==== RESULTS (ticks=%d) ====\n", ticks);
    std::printf("  established SIGNAL (client) + connectionAccepted (server): %s\n",
                handshakeOk ? "YES" : "NO");
    std::printf("  QUIC DATAGRAM A->B via datagramReceived SIGNAL:            %s%s\n",
                serverGotDatagram ? "YES" : "NO",
                serverGotDatagram ? (" ('" + quicPayloadSeen + "')").c_str() : "");
    std::printf("  0x3F demux prefix routed to prefix handler:               %s%s\n",
                gotPrefix ? "YES" : "NO",
                gotPrefix ? (" ('" + prefixPayloadSeen + "')").c_str() : "");

    if (!passed || !handshakeOk || !serverGotDatagram || !gotPrefix) {
        std::printf("\nRESULT: FAIL\n");
        return 1;
    }
    if (quicPayloadSeen != "quic-datagram-A-to-B") {
        std::printf("\nRESULT: FAIL (QUIC datagram payload mismatch)\n");
        return 1;
    }
    if (prefixPayloadSeen != std::string("\x3F""VGDhello-demux", 15)) {
        std::printf("\nRESULT: FAIL (prefix payload mismatch)\n");
        return 1;
    }

    std::printf("\nRESULT: PASS (100%% signal-driven: DECLARE_SIGNAL/connect + event loop)\n");
    return 0;
}
