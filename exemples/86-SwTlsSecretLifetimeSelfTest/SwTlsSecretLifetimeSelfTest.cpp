#include "core/types/SwByteArray.h"
#include "core/types/SwVector.h"
#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicHandshakeServer.h"
#include "core/io/quic/SwQuicServerCredential.h"
#include "core/io/quic/SwQuicSessionTicket.h"

#include <cstdio>

namespace {

int failures = 0;

void require(bool condition, const char* message) {
    if (condition) {
        return;
    }
    ++failures;
    ::printf("FAIL: %s\n", message);
}

bool driveStartedHandshake(SwQuicHandshakeClient& client,
                           SwQuicHandshakeServer& server,
                           const SwByteArray& initial,
                           SwString* error) {
    SwVector<SwByteArray> clientToServer;
    clientToServer.append(initial);
    for (int round = 0; round < 8; ++round) {
        SwVector<SwByteArray> serverToClient;
        for (size_t i = 0; i < clientToServer.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!server.processIncomingDatagram(clientToServer[i], replies, error)) {
                return false;
            }
            for (size_t j = 0; j < replies.size(); ++j) {
                serverToClient.append(replies[j]);
            }
        }

        clientToServer.clear();
        for (size_t i = 0; i < serverToClient.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!client.processIncomingDatagram(serverToClient[i], replies, error)) {
                return false;
            }
            for (size_t j = 0; j < replies.size(); ++j) {
                clientToServer.append(replies[j]);
            }
        }

        if (client.handshakeComplete() && server.handshakeComplete()) {
            return true;
        }
    }
    return false;
}

bool driveHandshake(SwQuicHandshakeClient& client,
                    SwQuicHandshakeServer& server,
                    SwString* error) {
    SwByteArray initial;
    if (!client.start(SwString("secure-clear.test"), initial, error)) {
        return false;
    }
    return driveStartedHandshake(client, server, initial, error);
}

void testSecureZeroAndClear() {
    unsigned char raw[64];
    for (size_t i = 0; i < sizeof(raw); ++i) {
        raw[i] = static_cast<unsigned char>(i + 1);
    }
    SwByteArray::secureZero(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); ++i) {
        require(raw[i] == 0, "secureZero must overwrite every raw byte");
    }

    SwByteArray secret("a-secret-that-must-not-remain");
    secret.resize(8); // leaves a larger live backing vector in this implementation
    secret.secureClear();
    require(secret.isEmpty(), "secureClear must leave the byte array empty");
    require(!secret.isNull(), "secureClear must preserve clear()-style non-null emptiness");
    require(secret.constData() && secret.constData()[0] == '\0',
            "secureClear must preserve the null terminator invariant");

    secret.append("reused");
    require(secret == SwByteArray("reused"),
            "a securely cleared byte array must remain reusable");
}

void testHandshakeSecretLifetime() {
    SwQuicServerCredential credential;
    SwString error;
    require(SwQuicEcdsaCredential::createSelfSigned(
                SwString("secure-clear.test"), credential, &error),
            "test credential creation");
    if (!credential.isValid()) {
        return;
    }

    const SwByteArray certificateDer = credential.certificateChain[0];
    SwByteArray spkiDer;
    require(SwQuicCertificateVerifier::extractSubjectPublicKeyInfo(
                certificateDer, spkiDer, &error) &&
                !spkiDer.isEmpty() && spkiDer != certificateDer,
            "strict cross-platform X.509 to canonical SPKI extraction");
    SwByteArray malformedCertificate = certificateDer;
    malformedCertificate.append(static_cast<char>(0));
    SwByteArray rejectedSpki("sentinel");
    require(!SwQuicCertificateVerifier::extractSubjectPublicKeyInfo(
                malformedCertificate, rejectedSpki, &error) && rejectedSpki.isEmpty(),
            "SPKI extraction must reject trailing certificate bytes");

    SwQuicHandshakeClient client;
    client.setVerifyPeer(false);
    SwQuicHandshakeServer server;
    server.setCredential(credential);

    const bool handshakeDriven = driveHandshake(client, server, &error);
    require(handshakeDriven, "full TLS 1.3 handshake");
    if (!handshakeDriven) {
        ::printf("handshake drive error: %s\n", error.c_str());
    }
    if (!client.handshakeComplete() || !server.handshakeComplete()) {
        ::printf("handshake error: %s\n", error.c_str());
        return;
    }

    require(client.transientSecretsDiscardedForTest(),
            "client ephemeral and handshake traffic secrets must be discarded");
    require(server.transientSecretsDiscardedForTest(),
            "server Initial, Handshake and temporary schedule secrets must be discarded");

    require(!client.clientApplicationKeys().secret.isEmpty(),
            "client application traffic secret must remain available");
    require(!server.serverApplicationKeys().secret.isEmpty(),
            "server application traffic secret must remain available");

    const SwString label("EXPORTER-sw-secret-lifetime-test");
    const SwByteArray context(size_t(0), '\0');
    SwByteArray clientExporter;
    SwByteArray serverExporter;
    require(client.exportKeyingMaterial(label, context, 32, clientExporter, &error),
            "client exporter after transient-secret cleanup");
    require(server.exportKeyingMaterial(label, context, 32, serverExporter, &error),
            "server exporter after transient-secret cleanup");
    require(clientExporter.size() == 32 && clientExporter == serverExporter,
            "exporter master secret must survive and agree across peers");

    clientExporter.secureClear();
    serverExporter.secureClear();
    require(clientExporter.isEmpty() && serverExporter.isEmpty(),
            "derived exporter values must themselves be securely clearable");

    // Exercise the client cleanup with genuinely populated resumption and
    // 0-RTT state. The first connection issues a ticket; the second derives
    // early keys and then must discard both them and its private PSK copy.
    SwQuicTicketStore ticketStore;
    SwQuicSessionTicket ticket;
    SwByteArray ticketBody;
    const SwByteArray ticketId("secret-lifetime-ticket");
    const SwByteArray ticketNonce("nonce-01");
    const bool issued = server.issueNewSessionTicket(ticketStore, ticketId, ticketNonce,
                                                     7200, 0x11223344, 0xffffffffu,
                                                     ticketBody, &error);
    require(issued, "issue resumption ticket for secret-lifetime test");
    const bool stored = issued &&
        client.processNewSessionTicket(ticketBody, SwByteArray(), ticket, &error);
    require(stored && ticket.allowsEarlyData(),
            "client must derive a usable resumption PSK");
    if (!stored || !ticket.allowsEarlyData()) {
        return;
    }

    SwQuicHandshakeClient resumedClient;
    resumedClient.setVerifyPeer(false);
    resumedClient.setResumption(ticket, SwByteArray("early secret-lifetime probe"));
    SwQuicHandshakeServer resumedServer;
    resumedServer.setCredential(credential);
    resumedServer.setTicketStore(&ticketStore);

    SwByteArray resumedInitial;
    const bool resumedStarted = resumedClient.start(SwString("secure-clear.test"),
                                                    resumedInitial, &error);
    require(resumedStarted && resumedClient.hasEarlyKeys(),
            "resumed client must populate 0-RTT key material before completion");
    if (!resumedStarted) {
        return;
    }
    const bool resumedDriven = driveStartedHandshake(resumedClient, resumedServer,
                                                     resumedInitial, &error);
    require(resumedDriven, "resumed TLS 1.3 handshake");
    if (!resumedDriven) {
        ::printf("resumed handshake error: %s\n", error.c_str());
        return;
    }
    require(resumedClient.earlyDataAccepted(),
            "server must accept the test's 0-RTT data");
    require(resumedClient.transientSecretsDiscardedForTest(),
            "client must discard early keys and its copied resumption PSK");
    require(resumedServer.transientSecretsDiscardedForTest(),
            "server must discard temporary resumption schedule secrets");
}

} // namespace

int main() {
    testSecureZeroAndClear();
    testHandshakeSecretLifetime();
    if (failures == 0) {
        ::printf("SwTlsSecretLifetimeSelfTest OK\n");
    }
    return failures == 0 ? 0 : 1;
}
