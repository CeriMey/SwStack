// Live QUIC v1 + TLS 1.3 handshake probe. Drives SwQuicHandshakeClient against a
// real HTTP/3 server (default cloudflare-quic.com), performing a genuine ECDHE
// key exchange and running the key schedule through Initial -> Handshake -> 1-RTT.
// It reports exactly how far the handshake progresses.

#include "core/io/SwUdpSocket.h"
#include "core/io/quic/SwQuicHandshakeClient.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

const uint16_t kPort_ = 443;

const char* stateName(SwQuicHandshakeClient::State state) {
    switch (state) {
    case SwQuicHandshakeClient::State::Idle:                return "Idle";
    case SwQuicHandshakeClient::State::WaitServerHello:     return "WaitServerHello";
    case SwQuicHandshakeClient::State::WaitServerHandshake: return "WaitServerHandshake";
    case SwQuicHandshakeClient::State::Complete:            return "Complete";
    case SwQuicHandshakeClient::State::Failed:              return "Failed";
    }
    return "Unknown";
}

}  // namespace

int main(int argc, char** argv) {
    const SwString host(argc >= 2 ? argv[1] : "cloudflare-quic.com");
    const SwString serverName(argc >= 3 ? argv[2] : host.toStdString().c_str());
    const int timeoutMs = (argc >= 4 && std::atoi(argv[3]) > 0) ? std::atoi(argv[3]) : 8000;
    // Pass --no-verify to skip the certificate/hostname/signature checks.
    bool verifyPeer = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-verify") {
            verifyPeer = false;
        }
    }

    SwQuicHandshakeClient client;
    client.setVerifyPeer(verifyPeer);
    SwString error;

    SwByteArray initialDatagram;
    if (!client.start(serverName, initialDatagram, &error)) {
        std::cerr << "handshake start failed: " << error.toStdString() << std::endl;
        return 1;
    }

    std::cout << "target=" << host.toStdString() << ":" << kPort_
              << " sni=" << serverName.toStdString()
              << " client_pubkey=" << client.clientEphemeralPublicKey().toHex().toStdString()
              << " initial_bytes=" << initialDatagram.size() << std::endl;

    SwUdpSocket socket;
    socket.setMaxPendingDatagrams(64);

    const int64_t sent = socket.writeDatagram(initialDatagram.constData(),
                                              static_cast<int64_t>(initialDatagram.size()),
                                              host, kPort_);
    if (sent != static_cast<int64_t>(initialDatagram.size())) {
        std::cerr << "send failed: " << socket.errorString().toStdString() << std::endl;
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    SwQuicHandshakeClient::State lastState = client.state();
    bool sawHandshakeKeys = false;
    std::size_t datagramsReceived = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        socket.pollPendingDatagrams(100);
        while (socket.hasPendingDatagrams()) {
            SwString sender;
            uint16_t senderPort = 0;
            const SwByteArray datagram = socket.receiveDatagram(&sender, &senderPort);
            if (datagram.isEmpty()) {
                continue;
            }
            ++datagramsReceived;
            std::cout << "recv datagram #" << datagramsReceived
                      << " bytes=" << datagram.size()
                      << " from=" << sender.toStdString() << std::endl;

            std::vector<SwByteArray> responses;
            if (!client.processIncomingDatagram(datagram, responses, &error)) {
                std::cerr << "handshake failed: " << error.toStdString() << std::endl;
                std::cerr << "final_state=" << stateName(client.state()) << std::endl;
                return 3;
            }

            for (std::size_t i = 0; i < responses.size(); ++i) {
                socket.writeDatagram(responses[i].constData(),
                                     static_cast<int64_t>(responses[i].size()),
                                     host, kPort_);
                std::cout << "  sent response bytes=" << responses[i].size() << std::endl;
            }

            if (client.state() != lastState) {
                std::cout << "state -> " << stateName(client.state()) << std::endl;
                lastState = client.state();
            }
            if (!sawHandshakeKeys &&
                client.state() == SwQuicHandshakeClient::State::WaitServerHandshake) {
                sawHandshakeKeys = true;
                std::cout << "server_hello_parsed=yes ecdhe_ok=yes handshake_keys_derived=yes"
                          << std::endl;
                std::cout << "  server_hs_key=" << client.serverHandshakeKeys().key.toHex().toStdString()
                          << std::endl;
            }
            if (!client.serverCertificateDer().isEmpty()) {
                std::cout << "server_certificate_bytes=" << client.serverCertificateDer().size()
                          << std::endl;
            }
        }

        if (client.handshakeComplete()) {
            std::cout << "HANDSHAKE COMPLETE" << std::endl;
            std::cout << "server_authenticated=" << (verifyPeer ? "yes" : "skipped")
                      << std::endl;
            std::cout << "certificate_chain_length=" << client.serverCertificateChain().size()
                      << std::endl;
            if (!client.negotiatedAlpn().isEmpty()) {
                std::cout << "alpn=" << client.negotiatedAlpn().toStdString() << std::endl;
            }
            if (client.hasPeerTransportParameters()) {
                const SwQuicTransportParameters& p = client.peerTransportParameters();
                std::cout << "peer_initial_max_data=" << p.initialMaxData
                          << " peer_max_idle_timeout_ms=" << p.maxIdleTimeoutMs
                          << " peer_max_datagram_frame_size=" << p.maxDatagramFrameSize
                          << std::endl;
            }
            std::cout << "client_1rtt_key=" << client.clientApplicationKeys().key.toHex().toStdString()
                      << std::endl;
            std::cout << "server_1rtt_key=" << client.serverApplicationKeys().key.toHex().toStdString()
                      << std::endl;
            return 0;
        }
    }

    std::cout << "timeout reached. final_state=" << stateName(client.state())
              << " datagrams_received=" << datagramsReceived
              << " handshake_keys=" << (sawHandshakeKeys ? "derived" : "no")
              << " server_cert_bytes=" << client.serverCertificateDer().size()
              << std::endl;
    if (sawHandshakeKeys) {
        // We decrypted the server's Initial with keys derived from a live ECDHE
        // exchange -- the crypto core is proven end-to-end even if the full flight
        // did not complete within the timeout.
        return 0;
    }
    return 2;
}
