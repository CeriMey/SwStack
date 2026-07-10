// Deterministic loopback validation of the full QUIC v1 + TLS 1.3 handshake
// driver (SwQuicHandshakeClient). A minimal in-process "server" answers the
// client's Initial with a real ServerHello (its own ephemeral X25519 key) and a
// Handshake flight (EncryptedExtensions, Certificate, CertificateVerify,
// Finished). The client must complete the handshake, authenticate the server
// Finished, and derive 1-RTT keys identical to the server's -- proving the whole
// key schedule, ECDHE exchange, transcript hashing and packet protection are
// wired together correctly.

#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicClientHelloBuilder.h"
#include "core/io/quic/SwQuicConnectionId.h"
#include "core/io/quic/SwQuicFrame.h"
#include "core/io/quic/SwQuicFrameCodec.h"
#include "core/io/quic/SwQuicInitialSecrets.h"
#include "core/io/quic/SwQuicPacketKeys.h"
#include "core/io/quic/SwQuicPacketProtector.h"
#include "core/io/quic/SwQuicRandom.h"
#include "core/io/quic/SwQuicVarIntCodec.h"
#include "core/io/quic/SwQuicX25519.h"
#include "core/io/quic/SwTls13KeySchedule.h"
#include "core/io/quic/SwTls13Messages.h"

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

void appendU16(SwByteArray& out, std::uint16_t value) {
    out.append(static_cast<char>((value >> 8) & 0xffU));
    out.append(static_cast<char>(value & 0xffU));
}

SwByteArray rawMessage(std::uint8_t type, const SwByteArray& body) {
    SwByteArray out;
    out.append(static_cast<char>(type));
    out.append(static_cast<char>((body.size() >> 16) & 0xffU));
    out.append(static_cast<char>((body.size() >> 8) & 0xffU));
    out.append(static_cast<char>(body.size() & 0xffU));
    out.append(body);
    return out;
}

// Parse the cleartext DCID/SCID from a QUIC long-header packet.
bool extractLongHeaderCids(const SwByteArray& packet,
                           SwByteArray& dcid,
                           SwByteArray& scid) {
    std::size_t pos = 1 + 4;  // first byte + version
    if (pos + 1 > packet.size()) return false;
    const std::size_t dcidLen = static_cast<std::uint8_t>(packet.constData()[pos]);
    ++pos;
    if (pos + dcidLen + 1 > packet.size()) return false;
    dcid = packet.mid(static_cast<int>(pos), static_cast<int>(dcidLen));
    pos += dcidLen;
    const std::size_t scidLen = static_cast<std::uint8_t>(packet.constData()[pos]);
    ++pos;
    if (pos + scidLen > packet.size()) return false;
    scid = packet.mid(static_cast<int>(pos), static_cast<int>(scidLen));
    return true;
}

SwByteArray buildServerHelloBody(const SwByteArray& serverPublic32) {
    SwByteArray body;
    appendU16(body, 0x0303);                 // legacy_version
    body.append(SwByteArray(32, 'S'));       // random (not the HRR magic)
    body.append(static_cast<char>(0));       // legacy_session_id echo (empty)
    appendU16(body, 0x1301);                 // cipher_suite TLS_AES_128_GCM_SHA256
    body.append(static_cast<char>(0));       // legacy_compression_method

    SwByteArray extensions;
    // supported_versions -> TLS 1.3
    appendU16(extensions, 0x002b);
    appendU16(extensions, 0x0002);
    appendU16(extensions, 0x0304);
    // key_share -> x25519
    appendU16(extensions, 0x0033);
    appendU16(extensions, static_cast<std::uint16_t>(4 + serverPublic32.size()));
    appendU16(extensions, 0x001d);
    appendU16(extensions, static_cast<std::uint16_t>(serverPublic32.size()));
    extensions.append(serverPublic32);

    appendU16(body, static_cast<std::uint16_t>(extensions.size()));
    body.append(extensions);
    return body;
}

// Builds a protected long-header packet (Initial when hasToken, else Handshake).
bool buildLongHeaderPacket(const SwQuicInitialKeys& keys,
                           bool initial,
                           const SwByteArray& dcid,
                           const SwByteArray& scid,
                           std::uint64_t packetNumber,
                           const SwByteArray& cryptoStream,
                           SwByteArray& outPacket,
                           SwString* error) {
    SwVector<SwQuicFrame> frames;
    frames.push_back(SwQuicFrame::crypto(0, cryptoStream));
    SwByteArray plaintext;
    if (!SwQuicFrameCodec::encodeFrames(frames, plaintext, error)) {
        return false;
    }

    const std::uint8_t pnLen = 2;
    SwByteArray header;
    const std::uint8_t typeBits = initial ? 0xc0U : 0xe0U;
    header.append(static_cast<char>(typeBits | (pnLen - 1U)));
    header.append(static_cast<char>(0));
    header.append(static_cast<char>(0));
    header.append(static_cast<char>(0));
    header.append(static_cast<char>(1));  // version 1
    header.append(static_cast<char>(dcid.size()));
    header.append(dcid);
    header.append(static_cast<char>(scid.size()));
    header.append(scid);
    if (initial) {
        if (!SwQuicVarIntCodec::encode(0, header, error)) {  // token length
            return false;
        }
    }
    const std::uint64_t lengthField = plaintext.size() + SwQuicPacketProtector::kTagLength + pnLen;
    if (!SwQuicVarIntCodec::encode(lengthField, header, error)) {
        return false;
    }
    header.append(static_cast<char>((packetNumber >> 8) & 0xffU));
    header.append(static_cast<char>(packetNumber & 0xffU));

    return SwQuicPacketProtector::protectLongHeader(keys, packetNumber, pnLen, header,
                                                    plaintext, outPacket, error);
}

bool keysEqual(const SwQuicInitialKeys& a, const SwQuicInitialKeys& b) {
    return a.key == b.key && a.iv == b.iv && a.headerProtectionKey == b.headerProtectionKey;
}

bool testFullLoopbackHandshake() {
    SwString error;
    SwQuicHandshakeClient client;
    // The loopback "server" uses a synthetic certificate and signature: skip
    // the PKI checks, this test proves the key schedule and packet protection.
    client.setVerifyPeer(false);

    // 1) Client starts: builds the Initial datagram.
    SwByteArray clientInitial;
    if (!requireTrue(client.start(SwString("example.test"), clientInitial, &error),
                     "client.start failed")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(clientInitial.size() >= 1200, "client Initial below 1200 bytes")) {
        return false;
    }

    // 2) Server side: recover the connection IDs and the client ClientHello.
    SwByteArray originalDcid;
    SwByteArray clientScid;
    if (!requireTrue(extractLongHeaderCids(clientInitial, originalDcid, clientScid),
                     "could not parse client Initial header")) {
        return false;
    }

    SwQuicConnectionId dcidId;
    if (!SwQuicConnectionId::fromBytes(originalDcid, dcidId, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    SwQuicInitialKeys clientInitialKeys;
    SwQuicInitialKeys serverInitialKeys;
    if (!requireTrue(SwQuicInitialSecrets::deriveV1(dcidId, clientInitialKeys, serverInitialKeys, &error),
                     "server initial key derivation failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    SwQuicPacketHeader chHeader;
    SwByteArray chPayload;
    if (!requireTrue(SwQuicPacketProtector::unprotectInitial(clientInitialKeys, clientInitial,
                                                            chHeader, chPayload, nullptr, &error),
                     "server failed to unprotect client Initial")) {
        std::cerr << error << std::endl;
        return false;
    }
    SwVector<SwQuicFrame> chFrames;
    if (!SwQuicFrameCodec::decodeFrames(chPayload, chFrames, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    SwByteArray clientHelloMessage;
    for (std::size_t i = 0; i < chFrames.size(); ++i) {
        if (chFrames[i].type() == SwQuicFrame::Type::Crypto) {
            clientHelloMessage = chFrames[i].data();
            break;
        }
    }
    if (!requireTrue(!clientHelloMessage.isEmpty(), "client Initial carried no ClientHello")) {
        return false;
    }

    const SwByteArray clientPublic = client.clientEphemeralPublicKey();

    // 3) Server ephemeral key + ECDHE.
    SwByteArray serverPriv;
    SwByteArray serverPub;
    if (!SwQuicRandom::fill(serverPriv, 32, &error) ||
        !SwQuicX25519::derivePublicKey(serverPriv, serverPub, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    SwByteArray ecdhe;
    if (!SwQuicX25519::computeSharedSecret(serverPriv, clientPublic, ecdhe, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    // 4) ServerHello + transcript(CH..SH) + handshake key schedule.
    const SwByteArray serverHelloBody = buildServerHelloBody(serverPub);
    const SwByteArray serverHelloMessage = rawMessage(0x02, serverHelloBody);

    SwByteArray transcriptChSh;
    transcriptChSh.append(clientHelloMessage);
    transcriptChSh.append(serverHelloMessage);
    const SwByteArray thChSh = SwTls13KeySchedule::transcriptHash(transcriptChSh);

    SwByteArray earlySecret;
    SwByteArray handshakeSecret;
    SwByteArray serverHsTrafficSecret;
    SwByteArray clientHsTrafficSecret;
    if (!SwTls13KeySchedule::earlySecret(earlySecret, &error) ||
        !SwTls13KeySchedule::handshakeSecret(earlySecret, ecdhe, handshakeSecret, &error) ||
        !SwTls13KeySchedule::serverHandshakeTrafficSecret(handshakeSecret, thChSh, serverHsTrafficSecret, &error) ||
        !SwTls13KeySchedule::clientHandshakeTrafficSecret(handshakeSecret, thChSh, clientHsTrafficSecret, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    SwQuicInitialKeys serverHsKeys;
    SwQuicInitialKeys clientHsKeys;
    if (!SwQuicPacketKeys::deriveAes128(serverHsTrafficSecret, serverHsKeys, &error) ||
        !SwQuicPacketKeys::deriveAes128(clientHsTrafficSecret, clientHsKeys, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    // Server connection IDs: it addresses the client by the client's SCID and
    // picks its own SCID.
    const SwByteArray serverScid("srvSCID0");

    // 5) Server Initial packet carrying the ServerHello.
    SwByteArray serverInitialDatagram;
    if (!requireTrue(buildLongHeaderPacket(serverInitialKeys, true, clientScid, serverScid, 0,
                                           serverHelloMessage, serverInitialDatagram, &error),
                     "server Initial build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    // 6) Handshake flight: EE, Certificate, CertificateVerify, Finished.
    // EncryptedExtensions carries the negotiated ALPN "h3" (the client now
    // enforces ALPN per RFC 9001 8.1).
    SwByteArray alpnExtBody;
    appendU16(alpnExtBody, 0x0003);                    // ProtocolNameList length
    alpnExtBody.append(static_cast<char>(0x02));       // protocol name length
    alpnExtBody.append("h3", 2);
    SwByteArray eeExtensions;
    appendU16(eeExtensions, 0x0010);                   // ALPN extension type
    appendU16(eeExtensions, static_cast<std::uint16_t>(alpnExtBody.size()));
    eeExtensions.append(alpnExtBody);
    // quic_transport_parameters (0x0039) with the RFC 9000 7.3 connection-ID
    // bindings the client now validates.
    SwQuicTransportParameters serverParams;
    serverParams.initialMaxData = 1048576;
    serverParams.initialMaxStreamDataBidiLocal = 262144;
    serverParams.initialMaxStreamDataBidiRemote = 262144;
    serverParams.initialMaxStreamsBidi = 100;
    serverParams.hasOriginalDestinationConnectionId = true;
    serverParams.originalDestinationConnectionId = originalDcid;
    serverParams.hasInitialSourceConnectionId = true;
    serverParams.initialSourceConnectionId = serverScid;
    SwByteArray tpBody;
    serverParams.encode(tpBody, nullptr);
    appendU16(eeExtensions, 0x0039);
    appendU16(eeExtensions, static_cast<std::uint16_t>(tpBody.size()));
    eeExtensions.append(tpBody);
    SwByteArray eeBody;
    appendU16(eeBody, static_cast<std::uint16_t>(eeExtensions.size()));
    eeBody.append(eeExtensions);
    const SwByteArray encryptedExtensions = rawMessage(0x08, eeBody);

    SwByteArray certBody;
    certBody.append(static_cast<char>(0));            // certificate_request_context (empty)
    SwByteArray certList;
    const SwByteArray fakeLeaf("FAKE-DER-CERT");
    certList.append(static_cast<char>((fakeLeaf.size() >> 16) & 0xffU));
    certList.append(static_cast<char>((fakeLeaf.size() >> 8) & 0xffU));
    certList.append(static_cast<char>(fakeLeaf.size() & 0xffU));
    certList.append(fakeLeaf);
    appendU16(certList, 0);                            // per-cert extensions (empty)
    certBody.append(static_cast<char>((certList.size() >> 16) & 0xffU));
    certBody.append(static_cast<char>((certList.size() >> 8) & 0xffU));
    certBody.append(static_cast<char>(certList.size() & 0xffU));
    certBody.append(certList);
    const SwByteArray certificate = rawMessage(0x0b, certBody);

    SwByteArray cvBody;
    appendU16(cvBody, 0x0804);                         // signature scheme rsa_pss_rsae_sha256
    appendU16(cvBody, 8);                              // signature length
    cvBody.append(SwByteArray("SIGNSIGN"));
    const SwByteArray certificateVerify = rawMessage(0x0f, cvBody);

    SwByteArray transcriptToCv;
    transcriptToCv.append(clientHelloMessage);
    transcriptToCv.append(serverHelloMessage);
    transcriptToCv.append(encryptedExtensions);
    transcriptToCv.append(certificate);
    transcriptToCv.append(certificateVerify);
    const SwByteArray thToCv = SwTls13KeySchedule::transcriptHash(transcriptToCv);

    SwByteArray serverFinishedKey;
    SwByteArray serverVerifyData;
    if (!SwTls13KeySchedule::finishedKey(serverHsTrafficSecret, serverFinishedKey, &error) ||
        !SwTls13KeySchedule::verifyData(serverFinishedKey, thToCv, serverVerifyData, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    const SwByteArray serverFinished = rawMessage(0x14, serverVerifyData);

    SwByteArray handshakeCryptoStream;
    handshakeCryptoStream.append(encryptedExtensions);
    handshakeCryptoStream.append(certificate);
    handshakeCryptoStream.append(certificateVerify);
    handshakeCryptoStream.append(serverFinished);

    SwByteArray serverHandshakeDatagram;
    if (!requireTrue(buildLongHeaderPacket(serverHsKeys, false, clientScid, serverScid, 0,
                                           handshakeCryptoStream, serverHandshakeDatagram, &error),
                     "server Handshake build failed")) {
        std::cerr << error << std::endl;
        return false;
    }

    // 7) Client processes the server Initial then the server Handshake.
    SwVector<SwByteArray> out1;
    if (!requireTrue(client.processIncomingDatagram(serverInitialDatagram, out1, &error),
                     "client failed on server Initial")) {
        std::cerr << error << std::endl;
        return false;
    }
    if (!requireTrue(client.state() == SwQuicHandshakeClient::State::WaitServerHandshake,
                     "client should be waiting for the Handshake flight")) {
        return false;
    }
    if (!requireTrue(keysEqual(client.serverHandshakeKeys(), serverHsKeys),
                     "client/server Handshake keys diverged")) {
        return false;
    }

    SwVector<SwByteArray> out2;
    if (!requireTrue(client.processIncomingDatagram(serverHandshakeDatagram, out2, &error),
                     "client failed on server Handshake")) {
        std::cerr << error << std::endl;
        return false;
    }

    if (!requireTrue(client.handshakeComplete(), "handshake did not complete")) {
        return false;
    }
    if (!requireTrue(client.serverCertificateDer() == fakeLeaf,
                     "leaf certificate was not extracted")) {
        return false;
    }

    // 8) The client must have produced a Handshake datagram carrying its Finished.
    if (!requireTrue(out2.size() == 1, "client did not emit exactly one Handshake datagram")) {
        return false;
    }
    SwQuicPacketHeader clientFinHeader;
    SwByteArray clientFinPayload;
    if (!requireTrue(SwQuicPacketProtector::unprotectHandshake(clientHsKeys, out2[0], clientFinHeader,
                                                              clientFinPayload, nullptr, &error),
                     "server failed to unprotect client Handshake")) {
        std::cerr << error << std::endl;
        return false;
    }
    SwVector<SwQuicFrame> clientFinFrames;
    if (!SwQuicFrameCodec::decodeFrames(clientFinPayload, clientFinFrames, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    SwByteArray clientFinishedMessage;
    for (std::size_t i = 0; i < clientFinFrames.size(); ++i) {
        if (clientFinFrames[i].type() == SwQuicFrame::Type::Crypto) {
            clientFinishedMessage = clientFinFrames[i].data();
        }
    }
    // Verify the client's Finished verify_data against the transcript CH..serverFinished.
    SwByteArray transcriptToServerFinished = transcriptToCv;
    transcriptToServerFinished.append(serverFinished);
    const SwByteArray thToServerFinished = SwTls13KeySchedule::transcriptHash(transcriptToServerFinished);
    SwByteArray clientFinishedKey;
    SwByteArray expectedClientVerifyData;
    if (!SwTls13KeySchedule::finishedKey(clientHsTrafficSecret, clientFinishedKey, &error) ||
        !SwTls13KeySchedule::verifyData(clientFinishedKey, thToServerFinished, expectedClientVerifyData, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    const SwByteArray expectedClientFinished = rawMessage(0x14, expectedClientVerifyData);
    if (!requireTrue(clientFinishedMessage == expectedClientFinished,
                     "client Finished verify_data is incorrect")) {
        return false;
    }

    // 9) The 1-RTT application keys the client derived must equal the server's.
    SwByteArray masterSecret;
    SwByteArray serverAppSecret;
    SwByteArray clientAppSecret;
    if (!SwTls13KeySchedule::masterSecret(handshakeSecret, masterSecret, &error) ||
        !SwTls13KeySchedule::serverApplicationTrafficSecret(masterSecret, thToServerFinished, serverAppSecret, &error) ||
        !SwTls13KeySchedule::clientApplicationTrafficSecret(masterSecret, thToServerFinished, clientAppSecret, &error)) {
        std::cerr << error << std::endl;
        return false;
    }
    SwQuicInitialKeys serverAppKeys;
    SwQuicInitialKeys clientAppKeys;
    if (!SwQuicPacketKeys::deriveAes128(serverAppSecret, serverAppKeys, &error) ||
        !SwQuicPacketKeys::deriveAes128(clientAppSecret, clientAppKeys, &error)) {
        std::cerr << error << std::endl;
        return false;
    }

    return requireTrue(keysEqual(client.serverApplicationKeys(), serverAppKeys),
                       "client/server 1-RTT server keys diverged") &&
           requireTrue(keysEqual(client.clientApplicationKeys(), clientAppKeys),
                       "client/server 1-RTT client keys diverged");
}

}  // namespace

int main() {
    if (!testFullLoopbackHandshake()) {
        return 1;
    }
    std::cout << "QuicHandshakeSelfTest passed" << std::endl;
    return 0;
}
