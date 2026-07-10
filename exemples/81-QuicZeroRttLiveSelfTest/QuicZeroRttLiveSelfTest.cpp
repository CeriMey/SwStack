// Live 0-RTT through the real handshake drivers.
//
// Connection 1: a full handshake; the server issues a NewSessionTicket and the
// client stores it. Connection 2: the client resumes with setResumption(ticket,
// earlyData) -- start() emits an Initial coalesced with a 0-RTT packet carrying
// the early application data. The server (holding the ticket store) accepts the
// PSK, decrypts the 0-RTT data DURING the handshake, echoes early_data, and
// both sides complete with matching 1-RTT keys.

#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicHandshakeServer.h"
#include "core/io/quic/SwQuicServerCredential.h"
#include "core/io/quic/SwQuicSessionTicket.h"

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

bool driveToCompletion(SwQuicHandshakeClient& client, SwQuicHandshakeServer& server,
                       const SwByteArray& firstClientDatagram, SwString* error) {
    SwVector<SwByteArray> clientToServer;
    clientToServer.push_back(firstClientDatagram);

    for (int round = 0; round < 8; ++round) {
        SwVector<SwByteArray> serverToClient;
        for (std::size_t i = 0; i < clientToServer.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!server.processIncomingDatagram(clientToServer[i], replies, error)) {
                return false;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                serverToClient.push_back(replies[j]);
            }
        }
        clientToServer.clear();
        for (std::size_t i = 0; i < serverToClient.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!client.processIncomingDatagram(serverToClient[i], replies, error)) {
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
    return client.handshakeComplete() && server.handshakeComplete();
}

bool testLiveZeroRtt() {
    SwString error;

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("resume.test"),
                                                             credential, &error),
                     "credential generation failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    SwQuicTicketStore store;
    std::uint64_t monotonicNowMs = 1000;
    store.setMonotonicNowProvider([&monotonicNowMs]() {
        return monotonicNowMs;
    });

    // ---- Connection 1: full handshake + ticket issuance -----------------
    SwQuicSessionTicket validTicket;
    SwQuicSessionTicket expiringTicket;
    {
        SwQuicHandshakeClient client;
        client.setVerifyPeer(true);
        client.setVerifyCertificateChain(false);
        SwQuicHandshakeServer server;
        server.setCredential(credential);

        SwByteArray initial;
        if (!requireTrue(client.start(SwString("resume.test"), initial, &error),
                         "connection 1 start failed")) {
            std::cerr << error << std::endl;
            return false;
        }
        if (!requireTrue(driveToCompletion(client, server, initial, &error),
                         "connection 1 handshake failed")) {
            std::cerr << "c=" << client.errorString()
                      << " s=" << server.errorString() << std::endl;
            return false;
        }

        const auto issueTicket = [&](SwQuicSessionTicket& outTicket) -> bool {
            SwByteArray ticketBytes;
            SwByteArray ticketNonce;
            if (!SwQuicRandom::fill(ticketBytes, 24, &error) ||
                !SwQuicRandom::fill(ticketNonce, 8, &error)) {
                return false;
            }
            SwByteArray nstBody;
            return server.issueNewSessionTicket(
                       store, ticketBytes, ticketNonce,
                       2, 0x11223344, 0xffffffffu, nstBody, &error) &&
                   client.processNewSessionTicket(
                       nstBody, SwByteArray(), outTicket, &error);
        };
        if (!requireTrue(issueTicket(validTicket),
                         "valid-window ticket issuance failed") ||
            !requireTrue(issueTicket(expiringTicket),
                         "expiry-window ticket issuance failed") ||
            !requireTrue(validTicket.allowsEarlyData() &&
                             expiringTicket.allowsEarlyData(),
                         "issued tickets do not allow early data") ||
            !requireTrue(store.size() == 2,
                         "server did not retain both fresh tickets")) {
            std::cerr << error << std::endl;
            return false;
        }
    }

    // ---- Connection 2: resumption just before expiry --------------------
    // Tickets were issued at t=1000ms with a two-second lifetime. The first
    // remains valid at the last millisecond before its exclusive deadline.
    monotonicNowMs = 2999;
    const SwByteArray earlyData("early GET /api/state HTTP/3");

    SwQuicHandshakeClient client2;
    client2.setVerifyPeer(true);
    client2.setVerifyCertificateChain(false);
    client2.setResumption(validTicket, earlyData);

    SwQuicHandshakeServer server2;
    server2.setCredential(credential);
    server2.setTicketStore(&store);

    SwByteArray resumptionInitial;
    if (!requireTrue(client2.start(SwString("resume.test"), resumptionInitial, &error),
                     "resumption start failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(client2.hasEarlyKeys(), "client did not derive early keys")) {
        return false;
    }

    if (!requireTrue(driveToCompletion(client2, server2, resumptionInitial, &error),
                     "resumption handshake failed")) {
        std::cerr << "c=" << client2.errorString()
                  << " s=" << server2.errorString() << std::endl;
        return false;
    }

    // Exporters remain symmetric after a PSK resumption and never expose the
    // underlying exporter_master_secret itself.
    SwByteArray clientExporter;
    SwByteArray serverExporter;
    const SwString exporterLabel("EXPORTER-swstack-resumption");
    const SwByteArray exporterContext("0-rtt-session");
    if (!requireTrue(client2.exportKeyingMaterial(exporterLabel, exporterContext, 32,
                                                  clientExporter, &error),
                     "resumed client exporter derivation failed") ||
        !requireTrue(server2.exportKeyingMaterial(exporterLabel, exporterContext, 32,
                                                  serverExporter, &error),
                     "resumed server exporter derivation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    // The server must have accepted the PSK, decrypted the 0-RTT early data
    // during the handshake, and echoed early_data; both sides agree on keys.
    if (!requireTrue(server2.isResuming(),
                     "server rejected a ticket before its expiry") ||
        !requireTrue(server2.acceptedEarlyData(),
                     "server did not accept pre-expiry early data") ||
        !requireTrue(server2.receivedEarlyData() == earlyData,
                     "server did not recover the 0-RTT early data") ||
        !requireTrue(client2.earlyDataAccepted(),
                     "client was not told early data was accepted") ||
        !requireTrue(clientExporter.size() == 32 && clientExporter == serverExporter,
                     "resumed client/server exporters differ") ||
        !requireTrue(client2.clientApplicationKeys().key ==
                         server2.clientApplicationKeys().key &&
                         client2.serverApplicationKeys().key ==
                         server2.serverApplicationKeys().key,
                     "resumed 1-RTT keys differ between client and server")) {
        return false;
    }

    // ---- Connection 3: same policy exactly at/after expiry --------------
    // At the exclusive deadline the second ticket must be removed and its PSK
    // ignored. TLS still completes via a full certificate handshake, but 0-RTT
    // is rejected and the server never exposes the early request to the app.
    monotonicNowMs = 3000;
    SwQuicHandshakeClient client3;
    client3.setVerifyPeer(true);
    client3.setVerifyCertificateChain(false);
    client3.setResumption(expiringTicket, earlyData);

    SwQuicHandshakeServer server3;
    server3.setCredential(credential);
    server3.setTicketStore(&store);

    SwByteArray expiredInitial;
    if (!requireTrue(client3.start(SwString("resume.test"), expiredInitial, &error),
                     "expired-ticket fallback start failed") ||
        !requireTrue(driveToCompletion(client3, server3, expiredInitial, &error),
                     "expired-ticket full-handshake fallback failed")) {
        std::cerr << "c=" << client3.errorString()
                  << " s=" << server3.errorString() << std::endl;
        return false;
    }

    return requireTrue(!server3.isResuming(),
                       "server resumed with an expired ticket") &&
           requireTrue(!server3.acceptedEarlyData() &&
                           server3.receivedEarlyData().isEmpty(),
                       "server accepted early data from an expired ticket") &&
           requireTrue(!client3.earlyDataAccepted(),
                       "client was told expired-ticket early data was accepted") &&
           requireTrue(!store.lookup(expiringTicket.ticket).found &&
                           store.size() == 0,
                       "expired ticket was not purged from the store");
}

} // namespace

int main() {
    if (!testLiveZeroRtt()) {
        return 1;
    }
    std::cout << "QuicZeroRttLiveSelfTest passed" << std::endl;
    return 0;
}
