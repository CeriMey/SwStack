// Full QUIC v1 + TLS 1.3 handshake between the two real drivers, no synthetic
// scaffolding: SwQuicHandshakeClient <-> SwQuicHandshakeServer over an in-memory
// datagram loop. The server presents a freshly generated self-signed ECDSA
// P-256 credential and signs a real CertificateVerify; the client verifies that
// signature (chain policy is disabled because the cert is self-signed) and the
// server Finished, then both sides must derive identical 1-RTT keys and the
// server must verify the client Finished.

#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicHandshakeServer.h"
#include "core/io/quic/SwQuicServerCredential.h"

#include <cstdint>
#include <iostream>
#include "core/types/SwVector.h"

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool testLoopbackHandshake() {
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("loopback.test"),
                                                             credential, &error),
                     "self-signed credential generation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicHandshakeClient client;
    // Self-signed cert: verify the real CertificateVerify signature, but do not
    // require the chain to reach a trusted root.
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false);

    SwQuicHandshakeServer server;
    server.setCredential(credential);

    // RFC 8446 7.5: exporter material is unavailable until the handshake has
    // authenticated the peer Finished, and failed calls must clear their output.
    SwByteArray prematureClient("sentinel");
    SwByteArray prematureServer("sentinel");
    const bool clientExportedPrematurely = client.exportKeyingMaterial(
        SwString("EXPORTER-swstack-loopback"), SwByteArray("context"), 48,
        prematureClient, &error);
    const bool serverExportedPrematurely = server.exportKeyingMaterial(
        SwString("EXPORTER-swstack-loopback"), SwByteArray("context"), 48,
        prematureServer, &error);
    if (!requireTrue(!clientExportedPrematurely && prematureClient.isEmpty(),
                     "client exporter was available before handshake completion") ||
        !requireTrue(!serverExportedPrematurely && prematureServer.isEmpty(),
                     "server exporter was available before handshake completion")) {
        return false;
    }

    SwByteArray clientInitial;
    if (!requireTrue(client.start(SwString("loopback.test"), clientInitial, &error),
                     "client.start failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    // Drive the datagram exchange to completion (bounded number of rounds).
    SwVector<SwByteArray> clientToServer;
    clientToServer.push_back(clientInitial);

    for (int round = 0; round < 8; ++round) {
        SwVector<SwByteArray> serverToClient;
        for (std::size_t i = 0; i < clientToServer.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!requireTrue(server.processIncomingDatagram(clientToServer[i], replies, &error),
                             "server.processIncomingDatagram failed")) {
                std::cerr << error << std::endl;
                return false;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                serverToClient.push_back(replies[j]);
            }
        }
        clientToServer.clear();

        for (std::size_t i = 0; i < serverToClient.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!requireTrue(client.processIncomingDatagram(serverToClient[i], replies, &error),
                             "client.processIncomingDatagram failed")) {
                std::cerr << error << std::endl;
                return false;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                clientToServer.push_back(replies[j]);
            }
        }

        if (client.handshakeComplete() && server.handshakeComplete()) {
            break;
        }
    }

    if (!requireTrue(client.handshakeComplete(), "client handshake did not complete") ||
        !requireTrue(server.handshakeComplete(), "server handshake did not complete")) {
        std::cerr << "client_error=" << client.errorString()
                  << " server_error=" << server.errorString() << std::endl;
        return false;
    }

    // Both endpoints must agree on the 1-RTT keys.
    const bool clientKeysMatch =
        client.clientApplicationKeys().key == server.clientApplicationKeys().key &&
        client.serverApplicationKeys().key == server.serverApplicationKeys().key &&
        client.clientApplicationKeys().iv == server.clientApplicationKeys().iv &&
        client.serverApplicationKeys().iv == server.serverApplicationKeys().iv;

    const bool handshakeKeysMatch =
        client.clientHandshakeKeys().key == server.clientHandshakeKeys().key &&
        client.serverHandshakeKeys().key == server.serverHandshakeKeys().key;

    SwByteArray clientExporter;
    SwByteArray serverExporter;
    SwByteArray otherContextExporter;
    const SwString exporterLabel("EXPORTER-swstack-loopback");
    const SwByteArray exporterContext("authenticated-context");
    if (!requireTrue(client.exportKeyingMaterial(exporterLabel, exporterContext, 48,
                                                 clientExporter, &error),
                     "client exporter derivation failed") ||
        !requireTrue(server.exportKeyingMaterial(exporterLabel, exporterContext, 48,
                                                 serverExporter, &error),
                     "server exporter derivation failed") ||
        !requireTrue(client.exportKeyingMaterial(exporterLabel,
                                                 SwByteArray("other-context"), 48,
                                                 otherContextExporter, &error),
                     "context-separated exporter derivation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(handshakeKeysMatch, "handshake keys differ between client and server") &&
           requireTrue(clientKeysMatch, "1-RTT keys differ between client and server") &&
           requireTrue(clientExporter.size() == 48,
                       "exporter returned the wrong amount of keying material") &&
           requireTrue(clientExporter == serverExporter,
                       "client and server exporters derived different material") &&
           requireTrue(clientExporter != otherContextExporter,
                       "exporter context did not separate derived material") &&
           requireTrue(!client.negotiatedAlpn().isEmpty() &&
                           client.negotiatedAlpn() == SwByteArray("h3"),
                       "client did not negotiate ALPN h3") &&
           requireTrue(client.hasPeerTransportParameters(),
                       "client did not receive server transport parameters") &&
           requireTrue(server.hasPeerTransportParameters(),
                       "server did not receive client transport parameters");
}

// RFC 8446 4.4.2.2: the server MUST NOT sign CertificateVerify with a scheme the
// client did not advertise in signature_algorithms. If the credential's scheme is
// not offered, the server must abort the handshake at the ClientHello rather than
// send a signature the client will reject.
bool testServerRejectsUnofferedSignatureScheme() {
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("loopback.test"),
                                                             credential, &error),
                     "self-signed credential generation failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    // The client offers {0x0403, 0x0804, 0x0805, 0x0401}; force a scheme outside
    // that set so the server cannot authenticate with a scheme the client accepts.
    credential.signatureScheme = 0x0603; // ecdsa_secp521r1_sha512 — not offered by the client

    SwQuicHandshakeClient client;
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false);

    SwQuicHandshakeServer server;
    server.setCredential(credential);

    SwByteArray clientInitial;
    if (!requireTrue(client.start(SwString("loopback.test"), clientInitial, &error),
                     "client.start failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    // Drive the exchange; the server must reject the ClientHello once assembled.
    SwVector<SwByteArray> clientToServer;
    clientToServer.push_back(clientInitial);
    bool serverRejected = false;
    for (int round = 0; round < 8 && !serverRejected; ++round) {
        SwVector<SwByteArray> serverToClient;
        for (std::size_t i = 0; i < clientToServer.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!server.processIncomingDatagram(clientToServer[i], replies, &error)) {
                serverRejected = true;
                break;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                serverToClient.push_back(replies[j]);
            }
        }
        if (serverRejected) {
            break;
        }
        clientToServer.clear();
        for (std::size_t i = 0; i < serverToClient.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!client.processIncomingDatagram(serverToClient[i], replies, &error)) {
                break; // client-side abort is also acceptable evidence of failure
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                clientToServer.push_back(replies[j]);
            }
        }
    }

    return requireTrue(serverRejected,
                       "server accepted a ClientHello that does not offer its signature scheme") &&
           requireTrue(!server.handshakeComplete(),
                       "server completed despite an unusable signature scheme");
}

// RFC 9001 4.9.1: the client MUST discard its Initial keys as soon as it first
// sends a Handshake packet. After that, Initial packets are no longer processed.
bool testClientDiscardsInitialKeys() {
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("loopback.test"),
                                                             credential, &error),
                     "self-signed credential generation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicHandshakeClient client;
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false);
    SwQuicHandshakeServer server;
    server.setCredential(credential);

    SwByteArray clientInitial;
    if (!requireTrue(client.start(SwString("loopback.test"), clientInitial, &error),
                     "client.start failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    // Before sending any Handshake packet the client still holds Initial keys.
    if (!requireTrue(!client.initialKeysDiscarded(),
                     "client discarded Initial keys before sending a Handshake packet")) {
        return false;
    }

    SwVector<SwByteArray> clientToServer;
    clientToServer.push_back(clientInitial);
    SwByteArray firstServerFlight; // the server's Initial (coalesced with Handshake)

    for (int round = 0; round < 8; ++round) {
        SwVector<SwByteArray> serverToClient;
        for (std::size_t i = 0; i < clientToServer.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!requireTrue(server.processIncomingDatagram(clientToServer[i], replies, &error),
                             "server.processIncomingDatagram failed")) {
                return false;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                serverToClient.push_back(replies[j]);
            }
        }
        if (firstServerFlight.isEmpty() && !serverToClient.empty()) {
            firstServerFlight = serverToClient[0];
        }
        clientToServer.clear();
        for (std::size_t i = 0; i < serverToClient.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!requireTrue(client.processIncomingDatagram(serverToClient[i], replies, &error),
                             "client.processIncomingDatagram failed")) {
                return false;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                clientToServer.push_back(replies[j]);
            }
        }
        if (client.handshakeComplete() && server.handshakeComplete()) {
            break;
        }
    }

    if (!requireTrue(client.handshakeComplete(), "client handshake did not complete")) {
        return false;
    }
    // Having sent its Finished in a Handshake packet, the client must have dropped
    // its Initial keys.
    if (!requireTrue(client.initialKeysDiscarded(),
                     "client did not discard Initial keys after sending a Handshake packet")) {
        return false;
    }

    // Replaying the server's Initial flight must now be ignored: no error, no
    // Initial ACK emitted, keys stay discarded.
    if (!requireTrue(!firstServerFlight.isEmpty(), "never captured a server Initial flight")) {
        return false;
    }
    SwVector<SwByteArray> replies;
    if (!requireTrue(client.processIncomingDatagram(firstServerFlight, replies, &error),
                     "client rejected a replayed Initial instead of ignoring it")) {
        std::cerr << error << std::endl;
        return false;
    }
    return requireTrue(replies.empty(),
                       "client emitted an Initial-space response after discarding Initial keys") &&
           requireTrue(client.initialKeysDiscarded(),
                       "client resurrected Initial keys on a replayed Initial packet");
}

} // namespace

int main() {
    if (!testLoopbackHandshake() ||
        !testServerRejectsUnofferedSignatureScheme() ||
        !testClientDiscardsInitialKeys()) {
        return 1;
    }
    std::cout << "QuicHandshakeLoopbackSelfTest passed" << std::endl;
    return 0;
}
