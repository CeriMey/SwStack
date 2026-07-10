#include "core/io/SwUdpSocket.h"
#include "core/runtime/SwCoreApplication.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[SwUdpSocketSelfTest] FAIL " << message << "\n";
    }
    return condition;
}

bool bindLoopback(SwUdpSocket& socket) {
    socket.setReadNotificationsEnabled(false);
    return socket.bind("127.0.0.1", 0, SwUdpSocket::DontShareAddress);
}

bool resolveLoopback(SwUdpSocket& sender,
                     uint16_t port,
                     SwUdpSocket::ResolvedAddress& target) {
    sender.setReadNotificationsEnabled(false);
    return sender.resolveHostAddress("127.0.0.1", port, target);
}

bool waitForPending(SwUdpSocket& socket, std::size_t count, int timeoutMs = 1000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (socket.pendingDatagramCount() < count &&
           std::chrono::steady_clock::now() < deadline) {
        socket.pollPendingDatagrams(20);
    }
    return socket.pendingDatagramCount() >= count;
}

bool runEmptyDatagramCheck() {
    SwUdpSocket receiver;
    SwUdpSocket sender;
    receiver.setMaxDatagramSize(256U);
    receiver.setMaxPendingDatagrams(8U);
    receiver.setMaxPendingBytes(2048U);
    bool ok = expect(bindLoopback(receiver), "empty receiver bind");
#if defined(_WIN32)
    ok = expect(receiver.udpConnectionResetSuppressionEnabled(),
                "unconnected Windows UDP reset suppression enabled") && ok;
#endif
    SwUdpSocket::ResolvedAddress target;
    ok = expect(resolveLoopback(sender, receiver.localPort(), target),
                "empty target resolve") && ok;
    const char unused = 0;
    ok = expect(sender.writeDatagram(&unused, 0, target) == 0,
                "zero-length datagram send") && ok;
    ok = expect(waitForPending(receiver, 1U), "zero-length datagram received") && ok;
    bool visited = false;
    ok = expect(receiver.consumePendingDatagram(
                    [&visited](const SwUdpSocket::DatagramView& view) {
                        visited = view.size == 0U && view.originalSize == 0U &&
                                  !view.truncated && view.data == nullptr;
                    }),
                "zero-length datagram borrowed") && ok;
    ok = expect(visited, "zero-length metadata preserved") && ok;
    ok = expect(receiver.suppressedConnectionResetErrors() == 0U,
                "loopback path has no suppressed reset errors") && ok;
    receiver.close();
    sender.close();
    return ok;
}

bool runConnectionResetPolicyCheck() {
#if defined(_WIN32)
    SwUdpSocket socket;
    socket.setReadNotificationsEnabled(false);
    bool ok = expect(socket.connectToHost("127.0.0.1", 9U),
                     "connected UDP policy socket created");
    ok = expect(!socket.udpConnectionResetSuppressionEnabled(),
                "connected UDP retains reset error reporting") && ok;
    socket.disconnectFromHost();
    ok = expect(socket.udpConnectionResetSuppressionEnabled(),
                "disconnect restores server-style reset suppression") && ok;
    socket.close();
    return ok;
#else
    SwUdpSocket socket;
    return expect(!socket.udpConnectionResetSuppressionEnabled(),
                  "Windows reset policy disabled on non-Windows");
#endif
}

bool runTruncationCheck() {
    SwUdpSocket receiver;
    SwUdpSocket sender;
    receiver.setMaxDatagramSize(32U);
    receiver.setMaxPendingDatagrams(4U);
    receiver.setMaxPendingBytes(128U);
    bool ok = expect(bindLoopback(receiver), "truncation receiver bind");
    SwUdpSocket::ResolvedAddress target;
    ok = expect(resolveLoopback(sender, receiver.localPort(), target),
                "truncation target resolve") && ok;
    SwByteArray payload(128, 'T');
    ok = expect(sender.writeDatagram(payload.constData(),
                                     static_cast<int64_t>(payload.size()),
                                     target) == static_cast<int64_t>(payload.size()),
                "oversized datagram send") && ok;
    ok = expect(waitForPending(receiver, 1U), "oversized datagram received") && ok;
    bool truncated = false;
    std::size_t originalSize = 0U;
    SwByteArray received = receiver.receiveDatagram(nullptr,
                                                     nullptr,
                                                     &truncated,
                                                     &originalSize);
    ok = expect(received.size() == 32U, "truncated payload bounded") && ok;
    ok = expect(truncated && originalSize > received.size(),
                "truncation metadata reported") && ok;
    ok = expect(receiver.truncatedDatagrams() == 1U,
                "truncation counter incremented once") && ok;
    receiver.close();
    sender.close();
    return ok;
}

bool runSaturationCheck() {
    SwUdpSocket receiver;
    SwUdpSocket sender;
    receiver.setMaxDatagramSize(64U);
    receiver.setMaxPendingDatagrams(2U);
    receiver.setMaxPendingBytes(128U);
    bool ok = expect(bindLoopback(receiver), "saturation receiver bind");
    SwUdpSocket::ResolvedAddress target;
    ok = expect(resolveLoopback(sender, receiver.localPort(), target),
                "saturation target resolve") && ok;
    for (uint8_t index = 0; index < 5U; ++index) {
        char payload[16] = {};
        payload[0] = static_cast<char>(index);
        ok = expect(sender.writeDatagram(payload, sizeof(payload), target) ==
                        static_cast<int64_t>(sizeof(payload)),
                    "saturation datagram send") && ok;
    }
    ok = expect(waitForPending(receiver, 2U), "saturation queue filled") && ok;
    // One readiness drain consumes all five kernel datagrams but retains only
    // the newest two under the configured slot/byte budget.
    receiver.pollPendingDatagrams(20);
    ok = expect(receiver.pendingDatagramCount() == 2U,
                "saturation queue remains bounded") && ok;
    ok = expect(receiver.droppedDatagrams() == 3U,
                "saturation drop counter aggregated") && ok;
    SwByteArray first = receiver.receiveDatagram();
    SwByteArray second = receiver.receiveDatagram();
    ok = expect(first.size() == 16U &&
                    static_cast<uint8_t>(first[0]) == 3U &&
                    second.size() == 16U &&
                    static_cast<uint8_t>(second[0]) == 4U,
                "saturation keeps newest datagrams") && ok;
    receiver.close();
    sender.close();
    return ok;
}

bool runFairnessCheck() {
    SwUdpSocket receiver;
    SwUdpSocket sender;
    receiver.setMaxDatagramSize(64U);
    receiver.setMaxPendingDatagrams(16U);
    receiver.setMaxPendingBytes(1024U);
    receiver.setMaxReadBatchDatagrams(2U);
    bool ok = expect(bindLoopback(receiver), "fairness receiver bind");
    SwUdpSocket::ResolvedAddress target;
    ok = expect(resolveLoopback(sender, receiver.localPort(), target),
                "fairness target resolve") && ok;
    const char payload[8] = {};
    for (int i = 0; i < 5; ++i) {
        ok = expect(sender.writeDatagram(payload, sizeof(payload), target) ==
                        static_cast<int64_t>(sizeof(payload)),
                    "fairness datagram send") && ok;
    }
    receiver.pollPendingDatagrams(1000);
    ok = expect(receiver.totalReceivedDatagrams() == 2U,
                "first readiness drain honors batch budget") && ok;
    receiver.receiveDatagram();
    receiver.receiveDatagram();
    receiver.pollPendingDatagrams(1000);
    ok = expect(receiver.totalReceivedDatagrams() == 4U,
                "second readiness drain remains fair") && ok;
    receiver.receiveDatagram();
    receiver.receiveDatagram();
    receiver.pollPendingDatagrams(1000);
    ok = expect(receiver.totalReceivedDatagrams() == 5U,
                "final readiness drain consumes remainder") && ok;
    receiver.close();
    sender.close();
    return ok;
}

bool runMemoryLifecycleCheck() {
    SwUdpSocket receiver;
    SwUdpSocket sender;
    receiver.setMaxDatagramSize(65536U);
    receiver.setMaxPendingDatagrams(2048U);
    receiver.setMaxPendingBytes(4U * 1024U * 1024U);
    bool ok = expect(receiver.effectiveMaxPendingDatagrams() == 64U,
                     "byte budget limits effective slots");
    ok = expect(receiver.allocatedReceiveBufferBytes() == 0U,
                "receive payload buffers start lazy") && ok;
    ok = expect(bindLoopback(receiver), "memory receiver bind") && ok;
    ok = expect(receiver.allocatedReceiveBufferBytes() == 0U,
                "bind does not allocate payload buffers") && ok;
    SwUdpSocket::ResolvedAddress target;
    ok = expect(resolveLoopback(sender, receiver.localPort(), target),
                "memory target resolve") && ok;
    const char payload[64] = {};
    ok = expect(sender.writeDatagram(payload, sizeof(payload), target) ==
                    static_cast<int64_t>(sizeof(payload)),
                "memory datagram send") && ok;
    ok = expect(waitForPending(receiver, 1U), "memory datagram received") && ok;
    ok = expect(receiver.allocatedReceiveBufferBytes() >= 65536U,
                "receive slot allocated on demand") && ok;
    SwUdpSocket::OwnedDatagram owned;
    ok = expect(receiver.takePendingDatagram(owned) && owned.size == sizeof(payload),
                "receive slot ownership moves without a payload copy") && ok;
    const std::size_t socketBytesAfterMove = receiver.allocatedReceiveBufferBytes();
    ok = expect(socketBytesAfterMove == 0U,
                "moved slot allocation leaves the socket") && ok;
    receiver.close();
    ok = expect(receiver.allocatedReceiveBufferBytes() == 0U,
                "close releases receive payload buffers") && ok;
    receiver.close();
    ok = expect(receiver.allocatedReceiveBufferBytes() == 0U,
                "close is idempotent") && ok;
    sender.close();
    return ok;
}

bool runReadyReadDestructionCheck() {
    SwCoreApplication app;
    std::unique_ptr<SwUdpSocket> receiver(new SwUdpSocket());
    SwUdpSocket sender;
    receiver->setMaxDatagramSize(64U);
    receiver->setMaxPendingDatagrams(4U);
    receiver->setMaxPendingBytes(256U);
    bool notified = false;
    SwObject callbackContext;
    SwObject::connect(receiver.get(), &SwUdpSocket::readyRead,
                      &callbackContext, [&receiver, &notified]() {
        notified = true;
        receiver.reset();
    });

    bool ok = expect(receiver->bind("127.0.0.1", 0,
                                    SwUdpSocket::DontShareAddress),
                     "readyRead destruction receiver bind");
    SwUdpSocket::ResolvedAddress target;
    ok = expect(resolveLoopback(sender, receiver->localPort(), target),
                "readyRead destruction target resolve") && ok;
    const char payload[8] = {};
    ok = expect(sender.writeDatagram(payload, sizeof(payload), target) ==
                    static_cast<int64_t>(sizeof(payload)),
                "readyRead destruction datagram send") && ok;

    // With current-thread affinity, this manual poll emits readyRead
    // synchronously without processing the runloop. The slot deliberately
    // destroys the receiver: no native-I/O mutex guard may still reference
    // the socket when the signal is delivered.
    SwUdpSocket* const rawReceiver = receiver.get();
    const bool received = rawReceiver->pollPendingDatagrams(1000);
    ok = expect(received, "readyRead destruction poll reports receive") && ok;
    ok = expect(notified, "readyRead destruction callback invoked") && ok;
    ok = expect(receiver == nullptr,
                "readyRead destruction releases socket inside callback") && ok;
    sender.close();
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok = runEmptyDatagramCheck() && ok;
    ok = runConnectionResetPolicyCheck() && ok;
    ok = runTruncationCheck() && ok;
    ok = runSaturationCheck() && ok;
    ok = runFairnessCheck() && ok;
    ok = runMemoryLifecycleCheck() && ok;
    ok = runReadyReadDestructionCheck() && ok;
    if (!ok) {
        return EXIT_FAILURE;
    }
    std::cout << "[SwUdpSocketSelfTest] PASS\n";
    return EXIT_SUCCESS;
}
