// In-tree validation of the QUIC/HTTP3 foundation modules produced for the
// src/core/io/quic and src/core/io/http3 stacks. Each check exercises a module
// against an official RFC test vector or a hand-computed reference value.

#include "core/io/quic/SwQuicX25519.h"
#include "core/io/quic/SwQuicLossRecovery.h"
#include "core/io/quic/SwQuicCongestionControl.h"
#include "core/io/quic/SwQuicFlowControl.h"
#include "core/io/http3/SwHttp3FrameCodec.h"
#include "core/io/http3/SwHpackHuffman.h"
#include "core/io/http3/SwQpackStaticTable.h"
#include "core/io/http3/SwQpackEncoder.h"
#include "core/io/http3/SwQpackDecoder.h"
#include "core/types/SwPair.h"
#include "core/types/SwVector.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace {

bool requireTrue(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool nearlyEqual(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

SwByteArray hx(const char* hex) {
    return SwByteArray::fromHex(SwByteArray(hex));
}

// ------------------------------------------------------------------ X25519 ---

bool testX25519Rfc7748() {
    SwString error;

    // RFC 7748 section 6.1 Diffie-Hellman.
    const SwByteArray alicePriv = hx("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const SwByteArray bobPriv = hx("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const SwByteArray expectedAlicePub =
        hx("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    const SwByteArray expectedBobPub =
        hx("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const SwByteArray expectedShared =
        hx("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

    SwByteArray alicePub;
    SwByteArray bobPub;
    if (!requireTrue(SwQuicX25519::derivePublicKey(alicePriv, alicePub, &error) &&
                     SwQuicX25519::derivePublicKey(bobPriv, bobPub, &error),
                     "X25519 public key derivation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray sharedAB;
    SwByteArray sharedBA;
    if (!requireTrue(SwQuicX25519::computeSharedSecret(alicePriv, bobPub, sharedAB, &error) &&
                     SwQuicX25519::computeSharedSecret(bobPriv, alicePub, sharedBA, &error),
                     "X25519 shared secret computation failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    return requireTrue(alicePub == expectedAlicePub, "X25519 Alice public key mismatch") &&
           requireTrue(bobPub == expectedBobPub, "X25519 Bob public key mismatch") &&
           requireTrue(sharedAB == expectedShared, "X25519 shared secret (A->B) mismatch") &&
           requireTrue(sharedBA == expectedShared, "X25519 shared secret (B->A) mismatch");
}

// -------------------------------------------------------------- HTTP/3 frames ---

bool testHttp3Frames() {
    SwString error;

    SwHttp3Frame::SettingList settings;
    settings.push_back(std::make_pair(SwHttp3Frame::settingQpackMaxTableCapacity(),
                                      static_cast<std::uint64_t>(4096)));
    settings.push_back(std::make_pair(SwHttp3Frame::settingMaxFieldSectionSize(),
                                      static_cast<std::uint64_t>(16384)));

    SwByteArray stream;
    if (!requireTrue(SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::settings(settings), stream, &error) &&
                     SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::data(SwByteArray("body")), stream, &error) &&
                     SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::unknown(0x21, SwByteArray("grease")), stream, &error) &&
                     SwHttp3FrameCodec::encodeFrame(SwHttp3Frame::goAway(8), stream, &error),
                     "HTTP/3 frame encode failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    std::size_t offset = 0;
    SwHttp3Frame f0;
    SwHttp3Frame f1;
    SwHttp3Frame f2;
    SwHttp3Frame f3;
    if (!requireTrue(SwHttp3FrameCodec::decodeFrame(stream, offset, f0, &error) &&
                     SwHttp3FrameCodec::decodeFrame(stream, offset, f1, &error) &&
                     SwHttp3FrameCodec::decodeFrame(stream, offset, f2, &error) &&
                     SwHttp3FrameCodec::decodeFrame(stream, offset, f3, &error),
                     "HTTP/3 frame decode failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    return requireTrue(offset == stream.size(), "HTTP/3 stream not fully consumed") &&
           requireTrue(f0.type() == SwHttp3Frame::Type::Settings &&
                           f0.settings().size() == 2 &&
                           f0.settings()[0].second == 4096 &&
                           f0.settings()[1].second == 16384,
                       "HTTP/3 SETTINGS round trip mismatch") &&
           requireTrue(f1.type() == SwHttp3Frame::Type::Data &&
                           f1.payload() == SwByteArray("body"),
                       "HTTP/3 DATA round trip mismatch") &&
           requireTrue(f2.type() == SwHttp3Frame::Type::Unknown && f2.rawType() == 0x21,
                       "HTTP/3 GREASE frame should decode as Unknown, not error") &&
           requireTrue(f3.type() == SwHttp3Frame::Type::GoAway && f3.id() == 8,
                       "HTTP/3 GOAWAY round trip mismatch");
}

// ------------------------------------------------------------ QPACK / Huffman ---

bool testHpackHuffman() {
    SwString error;
    const SwByteArray input("www.example.com");
    const SwByteArray expected = hx("f1e3c2e5f23a6ba0ab90f4ff");

    SwByteArray encoded;
    if (!requireTrue(SwHpackHuffman::encode(input, encoded, &error), "Huffman encode failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray decoded;
    if (!requireTrue(SwHpackHuffman::decode(encoded, decoded, &error), "Huffman decode failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    SwByteArray bounded;
    if (!requireTrue(!SwHpackHuffman::decode(encoded, bounded, input.size() - 1, &error),
                     "Huffman decoder ignored its output bound")) {
        return false;
    }

    return requireTrue(encoded == expected, "Huffman encoding does not match RFC 7541 vector") &&
           requireTrue(decoded == input, "Huffman round trip mismatch");
}

bool testQpackRoundTrip() {
    SwString error;

    if (!requireTrue(SwQpackStaticTable::entryCount() == 99, "QPACK static table must hold 99 entries")) {
        return false;
    }

    std::vector<std::pair<SwByteArray, SwByteArray> > headers;
    headers.push_back(std::make_pair(SwByteArray(":method"), SwByteArray("GET")));
    headers.push_back(std::make_pair(SwByteArray(":path"), SwByteArray("/")));
    headers.push_back(std::make_pair(SwByteArray(":scheme"), SwByteArray("https")));
    headers.push_back(std::make_pair(SwByteArray(":authority"), SwByteArray("www.example.com")));
    headers.push_back(std::make_pair(SwByteArray("custom-header"), SwByteArray("custom-value")));

    SwByteArray encoded;
    if (!requireTrue(SwQpackEncoder::encodeFieldSection(headers, encoded, &error),
                     "QPACK encode failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    std::vector<std::pair<SwByteArray, SwByteArray> > decoded;
    if (!requireTrue(SwQpackDecoder::decodeFieldSection(encoded, decoded, &error),
                     "QPACK decode failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    if (!requireTrue(decoded.size() == headers.size(), "QPACK header count mismatch")) {
        return false;
    }
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (!requireTrue(decoded[i].first == headers[i].first &&
                             decoded[i].second == headers[i].second,
                         "QPACK header round trip mismatch")) {
            return false;
        }
    }
    std::vector<std::pair<SwByteArray, SwByteArray> > bounded;
    if (!requireTrue(!SwQpackDecoder::decodeFieldSection(
                         encoded, bounded, 2, 4096, &error),
                     "QPACK decoder ignored the field-count limit") ||
        !requireTrue(!SwQpackDecoder::decodeFieldSection(
                         encoded, bounded, 100, 8, &error),
                     "QPACK decoder ignored the decoded-byte limit")) {
        return false;
    }
    return true;
}

// ------------------------------------------------------------ Loss recovery ---

bool testLossRecoveryRtt() {
    SwString error;
    SwQuicLossRecovery recovery;

    SwQuicLossRecovery::SentPacket p0;
    p0.packetNumber = 0;
    p0.sentTimeMs = 0;
    p0.ackEliciting = true;
    p0.inFlight = true;
    p0.sentBytes = 1200;
    recovery.onPacketSent(p0);

    SwVector<SwPair<std::uint64_t, std::uint64_t> > range0;
    range0.push_back(SwMakePair<std::uint64_t, std::uint64_t>(0, 0));
    SwQuicLossRecovery::AckResult ack0;
    if (!requireTrue(recovery.onAckReceived(0, 0, range0, 100, ack0, &error),
                     "loss recovery first ACK failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    if (!requireTrue(nearlyEqual(recovery.smoothedRttMs(), 100.0, 0.001) &&
                         nearlyEqual(recovery.rttVarMs(), 50.0, 0.001),
                     "loss recovery first RTT sample mismatch")) {
        return false;
    }

    SwQuicLossRecovery::SentPacket p1;
    p1.packetNumber = 1;
    p1.sentTimeMs = 100;
    p1.ackEliciting = true;
    p1.inFlight = true;
    p1.sentBytes = 1200;
    recovery.onPacketSent(p1);

    SwVector<SwPair<std::uint64_t, std::uint64_t> > range1;
    range1.push_back(SwMakePair<std::uint64_t, std::uint64_t>(1, 1));
    SwQuicLossRecovery::AckResult ack1;
    if (!requireTrue(recovery.onAckReceived(1, 0, range1, 220, ack1, &error),
                     "loss recovery second ACK failed")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }

    // second sample latest = 120 -> rttvar = 3/4*50 + 1/4*|100-120| = 42.5,
    // smoothed = 7/8*100 + 1/8*120 = 102.5, PTO(maxAckDelay=25) = 102.5+170+25.
    return requireTrue(nearlyEqual(recovery.rttVarMs(), 42.5, 0.001), "loss recovery rttvar mismatch") &&
           requireTrue(nearlyEqual(recovery.smoothedRttMs(), 102.5, 0.001), "loss recovery smoothed mismatch") &&
           requireTrue(nearlyEqual(recovery.computePtoMsExact(25), 297.5, 0.001), "loss recovery PTO mismatch") &&
           requireTrue(recovery.computePtoMs(25) == 298, "loss recovery PTO rounding mismatch");
}

// --------------------------------------------------------- Congestion control ---

bool testCongestionControl() {
    SwQuicCongestionControl cc;
    const std::uint64_t initial = cc.congestionWindow();
    const std::uint64_t maximumDatagram = cc.maxDatagramSize();
    const std::uint64_t expectedInitial =
        (std::min)(10 * maximumDatagram,
                   (std::max)(2 * maximumDatagram, std::uint64_t(14720)));
    if (!requireTrue(initial == expectedInitial, "congestion initial window mismatch")) {
        return false;
    }

    cc.onPacketSent(1200);
    cc.onPacketSent(1200);
    if (!requireTrue(cc.bytesInFlight() == 2400, "congestion in-flight mismatch")) {
        return false;
    }

    cc.onPacketAcked(1200, 10);  // slow start: cwnd += acked
    if (!requireTrue(cc.congestionWindow() == initial + 1200,
                     "congestion slow-start growth mismatch")) {
        return false;
    }

    const std::uint64_t before = cc.congestionWindow();
    cc.onCongestionEvent(50, 100);  // enters recovery, halves window
    const std::uint64_t halved = cc.congestionWindow();
    if (!requireTrue(halved == before / 2 && cc.ssthresh() == halved,
                     "congestion halving mismatch")) {
        return false;
    }

    // A congestion event for a packet sent before recovery started must be ignored.
    cc.onCongestionEvent(40, 120);
    return requireTrue(cc.congestionWindow() == halved,
                       "congestion should ignore in-recovery duplicate event");
}

// -------------------------------------------------------------- Flow control ---

bool testFlowControl() {
    SwString error;
    SwQuicStreamFlowControl stream(100);

    if (!requireTrue(stream.onDataReceived(0, 60, &error), "flow control legal receive rejected")) {
        std::cerr << error.toStdString() << std::endl;
        return false;
    }
    if (!requireTrue(!stream.onDataReceived(60, 50, &error),
                     "flow control should reject data past the limit")) {
        return false;
    }

    stream.consume(60);
    if (!requireTrue(stream.shouldSendMaxStreamData(),
                     "flow control should request a MAX_STREAM_DATA update")) {
        return false;
    }
    if (!requireTrue(stream.nextMaxStreamData() == 160, "flow control next MAX_STREAM_DATA mismatch")) {
        return false;
    }

    stream.setPeerMaxData(200);
    return requireTrue(stream.sendableBytes(150) == 50, "flow control sendable bytes mismatch") &&
           requireTrue(stream.isBlocked(80, 150), "flow control should report blocked") &&
           requireTrue(!stream.isBlocked(40, 150), "flow control should report unblocked");
}

}  // namespace

int main() {
    if (!testX25519Rfc7748() ||
        !testHttp3Frames() ||
        !testHpackHuffman() ||
        !testQpackRoundTrip() ||
        !testLossRecoveryRtt() ||
        !testCongestionControl() ||
        !testFlowControl()) {
        return 1;
    }

    std::cout << "QuicHttp3FoundationsSelfTest passed" << std::endl;
    return 0;
}
