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

    // ---- Connection 1: full handshake + ticket issuance -----------------
    SwQuicSessionTicket ticket;
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

        SwByteArray ticketBytes;
        SwByteArray ticketNonce;
        if (!SwQuicRandom::fill(ticketBytes, 24, &error) ||
            !SwQuicRandom::fill(ticketNonce, 8, &error)) {
            std::cerr << error << std::endl;
            return false;
        }
        SwByteArray nstBody;
        if (!requireTrue(server.issueNewSessionTicket(store, ticketBytes, ticketNonce,
                                                      7200, 0x11223344, 0xffffffffu, nstBody, &error),
                         "ticket issuance failed") ||
            !requireTrue(client.processNewSessionTicket(nstBody, SwByteArray(), ticket, &error),
                         "client NST processing failed")) {
            std::cerr << error << std::endl;
            return false;
        }
        if (!requireTrue(ticket.allowsEarlyData(), "ticket does not allow early data")) {
            return false;
        }
    }

    // ---- Connection 2: resumption + 0-RTT early data --------------------
    const SwByteArray earlyData("early GET /api/state HTTP/3");

    SwQuicHandshakeClient client2;
    client2.setVerifyPeer(true);
    client2.setVerifyCertificateChain(false);
    client2.setResumption(ticket, earlyData);

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
    return requireTrue(server2.isResuming(), "server did not resume the session") &&
           requireTrue(server2.acceptedEarlyData(), "server did not accept early data") &&
           requireTrue(server2.receivedEarlyData() == earlyData,
                       "server did not recover the 0-RTT early data") &&
           requireTrue(client2.earlyDataAccepted(),
                       "client was not told early data was accepted") &&
           requireTrue(clientExporter.size() == 32 && clientExporter == serverExporter,
                       "resumed client/server exporters differ") &&
           requireTrue(client2.clientApplicationKeys().key == server2.clientApplicationKeys().key &&
                           client2.serverApplicationKeys().key == server2.serverApplicationKeys().key,
                       "resumed 1-RTT keys differ between client and server");
}

} // namespace

int main() {
    if (!testLiveZeroRtt()) {
        return 1;
    }
    std::cout << "QuicZeroRttLiveSelfTest passed" << std::endl;
    return 0;
}
