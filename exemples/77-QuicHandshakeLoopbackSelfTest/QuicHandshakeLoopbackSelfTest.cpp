// Full QUIC v1 + TLS 1.3 handshake between the two real drivers, no synthetic
// scaffolding: SwQuicHandshakeClient <-> SwQuicHandshakeServer over an in-memory
// datagram loop. The server presents a freshly generated self-signed ECDSA
// P-256 credential and signs a real CertificateVerify; the client verifies that
// signature (chain policy is disabled because the cert is self-signed) and the
// server Finished, then both sides must derive identical 1-RTT keys and the
// server must verify the client Finished.

#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicHandshakeServer.h"
#include "core/io/quic/SwQuicCertificateVerifier.h"
#include "core/io/quic/SwQuicServerCredential.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <cstdio>
#include <cstdint>
#include <iostream>
#include <string>
#include "core/types/SwVector.h"

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool driveHandshake(SwQuicHandshakeClient& client,
                    SwQuicHandshakeServer& server,
                    const SwString& serverName,
                    SwString* error) {
    SwByteArray initial;
    if (!client.start(serverName, initial, error)) return false;
    SwVector<SwByteArray> clientToServer;
    clientToServer.push_back(initial);
    for (int round = 0; round < 16; ++round) {
        SwVector<SwByteArray> serverToClient;
        for (std::size_t i = 0; i < clientToServer.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!server.processIncomingDatagram(clientToServer[i], replies, error)) return false;
            for (std::size_t j = 0; j < replies.size(); ++j) {
                serverToClient.push_back(replies[j]);
            }
        }
        clientToServer.clear();
        for (std::size_t i = 0; i < serverToClient.size(); ++i) {
            SwVector<SwByteArray> replies;
            if (!client.processIncomingDatagram(serverToClient[i], replies, error)) return false;
            for (std::size_t j = 0; j < replies.size(); ++j) {
                clientToServer.push_back(replies[j]);
            }
        }
        if (client.handshakeComplete() && server.handshakeComplete()) return true;
    }
    return false;
}

struct PemFixture {
    std::string certificatePath;
    std::string privateKeyPath;

    ~PemFixture() {
        if (!certificatePath.empty()) std::remove(certificatePath.c_str());
        if (!privateKeyPath.empty()) std::remove(privateKeyPath.c_str());
    }
};

bool setOpenSslError(SwString* error, const char* message) {
    if (error) {
        *error = SwString(message);
        const unsigned long code = ERR_get_error();
        if (code != 0) {
            char detail[256] = {};
            ERR_error_string_n(code, detail, sizeof(detail));
            *error += SwString(": ") + SwString(detail);
        }
    }
    ERR_clear_error();
    return false;
}

bool generatePemFixture(const char* label,
                        int keyType,
                        bool restrictRsaPssToSha384,
                        PemFixture& fixture,
                        SwString* error) {
    fixture.certificatePath = std::string("swquic_") + label + "_certificate.pem";
    fixture.privateKeyPath = std::string("swquic_") + label + "_private_key.pem";

    EVP_PKEY_CTX* keyContext = EVP_PKEY_CTX_new_id(keyType, nullptr);
    EVP_PKEY* key = nullptr;
    bool ok = keyContext && EVP_PKEY_keygen_init(keyContext) == 1;
    if (ok && keyType == EVP_PKEY_RSA_PSS) {
        ok = EVP_PKEY_CTX_set_rsa_keygen_bits(keyContext, 2048) > 0;
        if (ok && restrictRsaPssToSha384) {
            ok = EVP_PKEY_CTX_set_rsa_pss_keygen_md(keyContext, EVP_sha384()) > 0 &&
                 EVP_PKEY_CTX_set_rsa_pss_keygen_mgf1_md(keyContext, EVP_sha384()) > 0 &&
                 EVP_PKEY_CTX_set_rsa_pss_keygen_saltlen(keyContext, 48) > 0;
        }
    }
    if (ok) ok = EVP_PKEY_keygen(keyContext, &key) == 1 && key;
    if (keyContext) EVP_PKEY_CTX_free(keyContext);
    if (!ok) {
        if (key) EVP_PKEY_free(key);
        return setOpenSslError(error, "Unable to generate the test private key");
    }

    X509* certificate = X509_new();
    ok = certificate &&
         X509_set_version(certificate, 2) == 1 &&
         ASN1_INTEGER_set(X509_get_serialNumber(certificate),
                          keyType == EVP_PKEY_ED25519 ? 7001 : 7002) == 1 &&
         X509_gmtime_adj(X509_get_notBefore(certificate), -60) &&
         X509_gmtime_adj(X509_get_notAfter(certificate), 3600) &&
         X509_set_pubkey(certificate, key) == 1;
    X509_NAME* subject = certificate ? X509_get_subject_name(certificate) : nullptr;
    if (ok) {
        ok = subject &&
             X509_NAME_add_entry_by_txt(
                 subject, "CN", MBSTRING_ASC,
                 reinterpret_cast<const unsigned char*>("loopback.test"),
                 -1, -1, 0) == 1 &&
             X509_set_issuer_name(certificate, subject) == 1;
    }
    const EVP_MD* certificateDigest =
        keyType == EVP_PKEY_ED25519 ? nullptr : EVP_sha384();
    if (ok) ok = X509_sign(certificate, key, certificateDigest) > 0;
    if (!ok) {
        if (certificate) X509_free(certificate);
        EVP_PKEY_free(key);
        return setOpenSslError(error, "Unable to create the test certificate");
    }

    BIO* certificateBio = BIO_new_file(fixture.certificatePath.c_str(), "wb");
    ok = certificateBio && PEM_write_bio_X509(certificateBio, certificate) == 1;
    if (certificateBio) BIO_free(certificateBio);
    BIO* keyBio = ok ? BIO_new_file(fixture.privateKeyPath.c_str(), "wb") : nullptr;
    if (ok) {
        ok = keyBio &&
             PEM_write_bio_PrivateKey(keyBio, key, nullptr, nullptr, 0,
                                      nullptr, nullptr) == 1;
    }
    if (keyBio) BIO_free(keyBio);
    X509_free(certificate);
    EVP_PKEY_free(key);
    if (!ok) {
        return setOpenSslError(error, "Unable to write the test PEM credential");
    }
    if (error) error->clear();
    return true;
}

bool testPemCredential(const char* label,
                       int keyType,
                       bool restrictRsaPssToSha384,
                       std::uint16_t expectedSignatureScheme) {
    SwString error;
    PemFixture fixture;
    if (!requireTrue(generatePemFixture(label, keyType, restrictRsaPssToSha384,
                                        fixture, &error),
                     "PEM fixture generation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicPemCredential::load(
                         SwString(fixture.certificatePath),
                         SwString(fixture.privateKeyPath), credential, &error),
                     "PEM credential loading failed") ||
        !requireTrue(credential.signatureScheme == expectedSignatureScheme,
                     "PEM credential selected the wrong TLS signature scheme")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicHandshakeClient client;
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false);
    SwQuicHandshakeServer server;
    server.setCredential(credential);
    if (!requireTrue(driveHandshake(client, server, SwString("loopback.test"), &error),
                     "PEM credential handshake failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(client.handshakeComplete() && server.handshakeComplete(),
                     "PEM credential handshake did not authenticate both endpoints")) {
        return false;
    }

    // Exercise the same credential in the client-authentication direction so
    // CertificateRequest negotiation and server-side CertificateVerify
    // validation cover the newly supported schemes too.
    SwQuicServerCredential mutualTlsServerCredential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(
                         SwString("mtls-loopback.test"),
                         mutualTlsServerCredential, &error),
                     "mTLS server credential generation failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    SwQuicHandshakeClient mutualTlsClient;
    mutualTlsClient.setVerifyPeer(true);
    mutualTlsClient.setVerifyCertificateChain(false);
    mutualTlsClient.setCredential(credential);
    SwQuicHandshakeServer mutualTlsServer;
    mutualTlsServer.setCredential(mutualTlsServerCredential);
    mutualTlsServer.setRequireClientAuthentication(true);
    mutualTlsServer.setClientSubjectPublicKeyInfoVerifier(
        [](const SwByteArray& spki) { return !spki.isEmpty(); });
    if (!requireTrue(driveHandshake(mutualTlsClient, mutualTlsServer,
                                    SwString("mtls-loopback.test"), &error),
                     "PEM client credential mTLS handshake failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    return requireTrue(mutualTlsClient.handshakeComplete() &&
                           mutualTlsServer.handshakeComplete(),
                       "PEM client credential did not complete mutual TLS");
}

bool testModernPemCredentials() {
    return testPemCredential("ed25519", EVP_PKEY_ED25519, false, 0x0807) &&
           testPemCredential("rsa_pss_sha384", EVP_PKEY_RSA_PSS, true, 0x080a);
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

bool testMutualAuthenticationAndCustomAlpn() {
    SwString error;
    SwQuicServerCredential serverCredential;
    SwQuicServerCredential clientCredential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(
                         SwString("mtls-server.test"), serverCredential, &error),
                     "mTLS server credential generation failed") ||
        !requireTrue(SwQuicEcdsaCredential::createSelfSigned(
                         SwString("mtls-client.test"), clientCredential, &error),
                     "mTLS client credential generation failed")) {
        return false;
    }

    SwQuicHandshakeClient client;
    client.setVerifyPeer(true);
    client.setVerifyCertificateChain(false);
    client.setCredential(clientCredential);
    const SwByteArray alpn("swstack-test");
    if (!requireTrue(client.setApplicationProtocol(alpn),
                     "client custom ALPN setup failed")) return false;

    SwByteArray policySpki;
    SwQuicHandshakeServer server;
    server.setCredential(serverCredential);
    server.setRequireClientAuthentication(true);
    server.setClientSubjectPublicKeyInfoVerifier(
        [&](const SwByteArray& spki) -> bool {
            policySpki = spki;
            return !spki.isEmpty();
        });
    if (!requireTrue(server.setApplicationProtocol(alpn),
                     "server custom ALPN setup failed") ||
        !requireTrue(client.authenticatedServerSubjectPublicKeyInfo().isEmpty() &&
                         server.authenticatedClientSubjectPublicKeyInfo().isEmpty(),
                     "authenticated identity leaked before Finished")) {
        return false;
    }

    if (!requireTrue(driveHandshake(client, server, SwString("mtls-server.test"), &error),
                     "mutually authenticated custom-ALPN handshake failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(client.negotiatedAlpn() == alpn && server.negotiatedAlpn() == alpn,
                       "custom ALPN was not negotiated end-to-end") &&
           requireTrue(!client.authenticatedServerSubjectPublicKeyInfo().isEmpty(),
                       "client did not publish authenticated server identity") &&
           requireTrue(!policySpki.isEmpty() &&
                           server.authenticatedClientSubjectPublicKeyInfo() == policySpki,
                       "server did not publish the policy-approved client identity");
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
    // Force a scheme outside the client's advertised set so the server cannot
    // authenticate with a scheme the client accepts.
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
    if (!testModernPemCredentials() ||
        !testLoopbackHandshake() ||
        !testMutualAuthenticationAndCustomAlpn() ||
        !testServerRejectsUnofferedSignatureScheme() ||
        !testClientDiscardsInitialKeys()) {
        return 1;
    }
    std::cout << "QuicHandshakeLoopbackSelfTest passed" << std::endl;
    return 0;
}
