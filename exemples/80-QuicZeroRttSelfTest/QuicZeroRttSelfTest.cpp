// TLS 1.3 session resumption + 0-RTT early data (RFC 8446 4.6.1, RFC 9001 4.6).
//
// Connection 1: a full handshake between the real client/server drivers; the
// server issues a NewSessionTicket, the client turns it into a stored ticket.
// Both sides independently derive the SAME resumption PSK.
//
// Connection 2 (resumption): the client builds a resumption ClientHello with a
// real PSK binder and derives its early (0-RTT) packet keys; the server parses
// the pre_shared_key extension, verifies the binder, and derives the SAME early
// keys. Finally a 0-RTT packet carrying early application data is protected by
// the client and deprotected by the server -- proving end to end that early
// data can be sent and read before the resumption handshake completes.

#include "core/io/quic/SwQuicClientHelloBuilder.h"
#include "core/io/quic/SwQuicFrame.h"
#include "core/io/quic/SwQuicFrameCodec.h"
#include "core/io/quic/SwQuicHandshakeClient.h"
#include "core/io/quic/SwQuicHandshakeServer.h"
#include "core/io/quic/SwQuicPacketProtector.h"
#include "core/io/quic/SwQuicRandom.h"
#include "core/io/quic/SwQuicServerCredential.h"
#include "core/io/quic/SwQuicSessionTicket.h"
#include "core/io/quic/SwQuicX25519.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

// Drive the two handshake drivers to completion in memory.
bool runHandshake(SwQuicHandshakeClient& client, SwQuicHandshakeServer& server,
                  SwString* error) {
    SwByteArray clientInitial;
    if (!client.start(SwString("resume.test"), clientInitial, error)) {
        return false;
    }

    std::vector<SwByteArray> clientToServer;
    clientToServer.push_back(clientInitial);

    for (int round = 0; round < 8; ++round) {
        std::vector<SwByteArray> serverToClient;
        for (std::size_t i = 0; i < clientToServer.size(); ++i) {
            std::vector<SwByteArray> replies;
            if (!server.processIncomingDatagram(clientToServer[i], replies, error)) {
                return false;
            }
            for (std::size_t j = 0; j < replies.size(); ++j) {
                serverToClient.push_back(replies[j]);
            }
        }
        clientToServer.clear();
        for (std::size_t i = 0; i < serverToClient.size(); ++i) {
            std::vector<SwByteArray> replies;
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

bool testZeroRtt() {
    SwString error;

    // ---- Connection 1: full handshake + ticket issuance -----------------
    SwQuicServerCredential credential;
    if (!requireTrue(SwQuicEcdsaCredential::createSelfSigned(SwString("resume.test"),
                                                             credential, &error),
                     "credential generation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicHandshakeClient client1;
    client1.setVerifyPeer(true);
    client1.setVerifyCertificateChain(false);
    SwQuicHandshakeServer server1;
    server1.setCredential(credential);

    if (!requireTrue(runHandshake(client1, server1, &error), "connection 1 handshake failed")) {
        std::cerr << "c=" << client1.errorString().toStdString()
                  << " s=" << server1.errorString().toStdString() << std::endl;
        return false;
    }

    // Both sides must have derived the same resumption_master_secret.
    if (!requireTrue(client1.resumptionMasterSecret() == server1.resumptionMasterSecret(),
                     "resumption_master_secret differs between client and server")) {
        return false;
    }

    // Server issues a NewSessionTicket accepting up to 16 KiB of 0-RTT data.
    SwQuicTicketStore store;
    SwByteArray ticketBytes;
    SwByteArray ticketNonce;
    if (!SwQuicRandom::fill(ticketBytes, 24, &error) ||
        !SwQuicRandom::fill(ticketNonce, 8, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    SwByteArray nstBody;
    if (!requireTrue(server1.issueNewSessionTicket(store, ticketBytes, ticketNonce,
                                                   7200, 0x01020304, 0xffffffffu, nstBody, &error),
                     "server ticket issuance failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Client turns the NewSessionTicket into stored resumption state.
    SwQuicSessionTicket ticket;
    if (!requireTrue(client1.processNewSessionTicket(nstBody, SwByteArray(), ticket, &error),
                     "client NST processing failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    const SwQuicTicketStore::Entry stored = store.lookup(ticketBytes);
    if (!requireTrue(ticket.valid && ticket.allowsEarlyData(),
                     "ticket is not valid for early data") ||
        !requireTrue(stored.found, "server did not store the ticket") ||
        !requireTrue(ticket.resumptionPsk == stored.resumptionPsk,
                     "client and server derived different resumption PSKs") ||
        !requireTrue(ticket.maxEarlyDataSize == 0xffffffffu,
                     "max_early_data_size must be 0xffffffff (RFC 9001 4.6.1)")) {
        return false;
    }

    // ---- Connection 2: resumption ClientHello + 0-RTT keys ---------------
    SwByteArray priv;
    SwByteArray pub;
    SwByteArray random;
    if (!SwQuicRandom::fill(priv, 32, &error) ||
        !SwQuicX25519::derivePublicKey(priv, pub, &error) ||
        !SwQuicRandom::fill(random, 32, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("rtt-scid"), scid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray resumptionClientHello;
    SwQuicInitialKeys clientEarlyKeys;
    if (!requireTrue(SwQuicClientHelloBuilder::buildForHttp3Resumption(
                         SwString("resume.test"), scid, pub, random,
                         ticket.ticket, ticket.ticketAgeAdd, ticket.resumptionPsk,
                         resumptionClientHello, clientEarlyKeys, &error),
                     "resumption ClientHello build failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Server parses the ClientHello, looks up the ticket, verifies the binder.
    SwTls13Messages::ClientHello parsed;
    // parseClientHello takes the body (after the 4-byte handshake header).
    const SwByteArray chBody = resumptionClientHello.mid(
        4, static_cast<int>(resumptionClientHello.size() - 4));
    if (!requireTrue(SwTls13Messages::parseClientHello(chBody, parsed, &error),
                     "server ClientHello parse failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(parsed.hasPreSharedKey, "server did not see a pre_shared_key") ||
        !requireTrue(parsed.offersEarlyData, "server did not see early_data") ||
        !requireTrue(parsed.pskIdentity == ticket.ticket, "PSK identity mismatch")) {
        return false;
    }

    const SwQuicTicketStore::Entry entry = store.lookup(parsed.pskIdentity);
    if (!requireTrue(entry.found, "server could not find the offered ticket")) {
        return false;
    }

    SwByteArray expectedBinder;
    if (!requireTrue(SwQuicClientHelloBuilder::computeExpectedBinder(
                         resumptionClientHello, entry.resumptionPsk, expectedBinder, &error),
                     "server binder computation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(expectedBinder == parsed.pskBinder,
                     "PSK binder verification failed")) {
        return false;
    }

    // Server derives the early keys from the PSK and the received ClientHello.
    SwQuicInitialKeys serverEarlyKeys;
    if (!requireTrue(SwQuicClientHelloBuilder::deriveServerEarlyKeys(
                         resumptionClientHello, entry.resumptionPsk, serverEarlyKeys, &error),
                     "server early key derivation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(clientEarlyKeys.key == serverEarlyKeys.key &&
                         clientEarlyKeys.iv == serverEarlyKeys.iv &&
                         clientEarlyKeys.headerProtectionKey == serverEarlyKeys.headerProtectionKey,
                     "client and server early (0-RTT) keys differ")) {
        return false;
    }

    // ---- 0-RTT packet round trip: early application data -----------------
    SwQuicConnectionId serverDcid;
    SwQuicConnectionId clientScid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("srv-dcid"), serverDcid, &error) ||
        !SwQuicConnectionId::fromBytes(SwByteArray("cli-scid"), clientScid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // Build a 0-RTT long header (type bits 0x10 -> first byte 0xd0 | pnLen-1).
    std::vector<SwQuicFrame> earlyFrames;
    earlyFrames.push_back(SwQuicFrame::stream(0, 0, SwByteArray("early GET /resource"), true));
    SwByteArray earlyPayload;
    if (!requireTrue(SwQuicFrameCodec::encodeFrames(earlyFrames, earlyPayload, &error),
                     "early frame encode failed")) {
        return false;
    }
    while (earlyPayload.size() < 4) {
        earlyPayload.append(static_cast<char>(0));
    }

    const std::uint8_t pnLen = 4;
    const std::uint64_t packetNumber = 0;
    SwByteArray header;
    header.append(static_cast<char>(0xd0U | (pnLen - 1U)));
    header.append(static_cast<char>(0));
    header.append(static_cast<char>(0));
    header.append(static_cast<char>(0));
    header.append(static_cast<char>(1)); // version 1
    header.append(static_cast<char>(serverDcid.size()));
    header.append(serverDcid.bytes());
    header.append(static_cast<char>(clientScid.size()));
    header.append(clientScid.bytes());
    {
        const std::uint64_t lengthField =
            static_cast<std::uint64_t>(earlyPayload.size() + pnLen +
                                       SwQuicPacketProtector::kTagLength);
        if (!SwQuicVarIntCodec::encode(lengthField, header, &error)) {
            std::cerr << error.toStdString() << std::endl;
            return false;
        }
    }
    for (std::uint8_t i = 0; i < pnLen; ++i) {
        const std::uint8_t shift = static_cast<std::uint8_t>((pnLen - 1 - i) * 8);
        header.append(static_cast<char>((packetNumber >> shift) & 0xffU));
    }

    SwByteArray zeroRttPacket;
    if (!requireTrue(SwQuicPacketProtector::protectLongHeader(clientEarlyKeys, packetNumber,
                                                             pnLen, header, earlyPayload,
                                                             zeroRttPacket, &error),
                     "0-RTT packet protection failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwQuicPacketHeader outHeader;
    SwByteArray outPayload;
    if (!requireTrue(SwQuicPacketProtector::unprotectZeroRtt(serverEarlyKeys, zeroRttPacket,
                                                            outHeader, outPayload, nullptr, &error),
                     "0-RTT packet deprotection failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    std::vector<SwQuicFrame> decodedFrames;
    if (!requireTrue(SwQuicFrameCodec::decodeFrames(outPayload, decodedFrames, &error),
                     "0-RTT frame decode failed") ||
        !requireTrue(!decodedFrames.empty(), "no frames in 0-RTT packet")) {
        return false;
    }

    bool sawEarlyData = false;
    for (std::size_t i = 0; i < decodedFrames.size(); ++i) {
        if (decodedFrames[i].type() == SwQuicFrame::Type::Stream &&
            decodedFrames[i].data() == SwByteArray("early GET /resource")) {
            sawEarlyData = true;
        }
    }
    return requireTrue(outHeader.longPacketType() ==
                           SwQuicPacketHeader::LongPacketType::ZeroRtt,
                       "deprotected packet is not 0-RTT") &&
           requireTrue(sawEarlyData, "server did not recover the 0-RTT early data");
}

// RFC 8446 4.2.11.2: the PSK binder is computed over the ClientHello truncated by
// the WHOLE PskBinderEntry list, not a fixed 35 bytes. parseClientHello must
// report that tail length and the server must feed it to computeExpectedBinder,
// otherwise a ClientHello offering more than one binder would be mis-truncated.
bool testResumptionBinderTruncationLength() {
    SwString error;

    // Produce a real single-binder resumption ClientHello.
    SwByteArray priv, pub, random;
    if (!SwQuicRandom::fill(priv, 32, &error) ||
        !SwQuicX25519::derivePublicKey(priv, pub, &error) ||
        !SwQuicRandom::fill(random, 32, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    SwQuicConnectionId scid;
    if (!SwQuicConnectionId::fromBytes(SwByteArray("bindscid"), scid, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    SwByteArray ticket, psk;
    if (!SwQuicRandom::fill(ticket, 24, &error) || !SwQuicRandom::fill(psk, 32, &error)) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray ch;
    SwQuicInitialKeys earlyKeys;
    if (!requireTrue(SwQuicClientHelloBuilder::buildForHttp3Resumption(
                         SwString("resume.test"), scid, pub, random, ticket,
                         0x11223344, psk, ch, earlyKeys, &error),
                     "resumption ClientHello build failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwTls13Messages::ClientHello parsed;
    const SwByteArray chBody = ch.mid(4, static_cast<int>(ch.size() - 4));
    if (!requireTrue(SwTls13Messages::parseClientHello(chBody, parsed, &error),
                     "ClientHello parse failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // A single 32-byte binder: 2 (list len) + 1 (entry len) + 32 = 35.
    if (!requireTrue(parsed.pskBindersTotalLength == 35,
                     "parsed binders tail length wrong for a single binder")) {
        return false;
    }

    // The server's real path: strip exactly the parsed tail. This must reproduce
    // the embedded binder.
    SwByteArray withParsed;
    if (!requireTrue(SwQuicClientHelloBuilder::computeExpectedBinder(
                         ch, psk, withParsed, &error, parsed.pskBindersTotalLength),
                     "binder recompute with parsed length failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(withParsed == parsed.pskBinder,
                     "binder recomputed with parsed length does not match")) {
        return false;
    }

    // The truncation length is load-bearing: stripping a two-binder tail (68) from
    // this one-binder message yields a different, wrong binder. This is exactly the
    // mis-truncation the fix avoids when a peer offers multiple PSKs.
    SwByteArray withWrongLength;
    if (!requireTrue(SwQuicClientHelloBuilder::computeExpectedBinder(
                         ch, psk, withWrongLength, &error, 68),
                     "binder recompute with alternate length failed")) {
        return false;
    }
    return requireTrue(withWrongLength != parsed.pskBinder,
                       "truncation length is not actually affecting the binder");
}

} // namespace

int main() {
    if (!testZeroRtt() ||
        !testResumptionBinderTruncationLength()) {
        return 1;
    }
    std::cout << "QuicZeroRttSelfTest passed" << std::endl;
    return 0;
}
