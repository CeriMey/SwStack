#pragma once

/**
 * @file src/core/io/SwTcpSocket.h
 * @ingroup core_io
 * @brief TCP-only non-blocking socket used as the transport base for higher layers.
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include "SwAbstractSocket.h"
#include "SwByteArray.h"
#include "SwByteRingBuffer.h"
#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwEventLoop.h"
#include "SwHostResolver.h"
#include "SwSocketTrafficTelemetry.h"
#include "SwTimer.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

static constexpr const char* kSwLogCategory_SwTcpSocket = "sw.core.io.swtcpsocket";
static constexpr int kSwTcpDefaultReadChunkSize = 16 * 1024;
static constexpr std::size_t kSwTcpDefaultWriteHighWatermark = 64U * 1024U * 1024U;
static constexpr std::size_t kSwTcpDefaultWriteLowWatermark = 32U * 1024U * 1024U;
static constexpr std::size_t kSwTcpDefaultWriteFlushBudget = 256U * 1024U;

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/win/SwWindows.h"

#pragma comment(lib, "ws2_32.lib")

using SwNativeSocketHandle = SOCKET;
static constexpr SwNativeSocketHandle kSwInvalidSocketHandle = INVALID_SOCKET;

#else

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using SwNativeSocketHandle = int;
static constexpr SwNativeSocketHandle kSwInvalidSocketHandle = -1;

#endif

class SwTcpSocket : public SwAbstractSocket {
    SW_OBJECT(SwTcpSocket, SwAbstractSocket)

public:
    enum class WriteResult {
        Accepted,
        WouldBlock,
        TooLarge,
        NotConnected,
        WriteClosed,
        Error
    };

    explicit SwTcpSocket(SwObject* parent = nullptr)
        : SwAbstractSocket(parent) {
        socketTrafficState_ = swSocketTrafficRegisterSocket(this, SwSocketTrafficTransportKind::Tcp);
#if defined(_WIN32)
        initializeWinsock_();
#endif
        m_happyEyeballsTimer = new SwTimer(this);
        m_happyEyeballsTimer->setSingleShot(true);
        connect(m_happyEyeballsTimer, &SwTimer::timeout, this, [this]() {
            if (state() == ConnectingState) {
                startNextConnectAttempt_();
            }
        });

        m_connectDeadlineTimer = new SwTimer(this);
        m_connectDeadlineTimer->setSingleShot(true);
        connect(m_connectDeadlineTimer, &SwTimer::timeout, this, [this]() {
            if (state() == HostLookupState || state() == ConnectingState) {
#if defined(_WIN32)
                failAndClose_(WSAETIMEDOUT);
#else
                failAndClose_(ETIMEDOUT);
#endif
            }
        });
    }

    ~SwTcpSocket() override {
        if (m_lifetimeGuard) {
            m_lifetimeGuard->alive.store(false, std::memory_order_release);
        }
        closeNow_(false);
        swSocketTrafficUnregisterSocket(this);
        socketTrafficState_.reset();
    }

    bool connectToHost(const SwString& host, uint16_t port) override {
        const std::shared_ptr<SocketLifetimeGuard> operationGuard = m_lifetimeGuard;
        const std::uint64_t expectedClosedGeneration = m_transportGeneration + 1;
        closeNow_(true);
        if (!operationGuard->alive.load(std::memory_order_acquire) ||
            m_transportGeneration != expectedClosedGeneration) {
            return false;
        }
        m_lastHost = host;          // keep the ORIGINAL host (TLS SNI fallback, logging)
        m_remoteClosed = false;
        m_remoteEofConfirmed = false;
        m_autoCloseOnRemoteShutdown = true;
        resetCloseState_();
        m_connectPort = port;
        m_lastConnectError = 0;
        m_nextConnectCandidate = 0;
        m_connectCandidates.clear();
        setState(HostLookupState);

        const std::uint64_t generation = m_transportGeneration;
        if (m_connectTimeoutMs > 0) {
            m_connectDeadlineTimer->start(m_connectTimeoutMs);
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        const std::weak_ptr<SocketLifetimeGuard> lifetime = operationGuard;
        SwHostResolver::instance().resolveAllAsync(
            host,
            [this, generation, affinity, lifetime](const SwHostResolver::AddressList& addresses) {
                const std::shared_ptr<SocketLifetimeGuard> guard = lifetime.lock();
                if (!guard || !guard->alive.load(std::memory_order_acquire)) {
                    return;
                }
                std::function<void()> deliver = [this, generation, addresses, guard]() {
                    if (!guard->alive.load(std::memory_order_acquire) ||
                        m_transportGeneration != generation ||
                        state() != HostLookupState) {
                        return;
                    }
                    handleResolvedAddresses_(addresses);
                };
                if (!affinity || ThreadHandle::currentThread() == affinity) {
                    deliver();
                    return;
                }
                if (ThreadHandle::isLive(affinity)) {
                    (void)affinity->postTaskOnLaneReliable(std::move(deliver), SwFiberLane::Control);
                }
            });

        if (!operationGuard->alive.load(std::memory_order_acquire)) {
            return false;
        }
        return state() == HostLookupState || state() == ConnectingState ||
               state() == ConnectedState;
    }

    bool waitForConnected(int msecs = 30000) override {
        if (state() == ConnectedState) {
            return true;
        }

        SwEventLoop loop;
        SwTimer timeoutTimer;
        bool success = false;

        auto complete = [&]() {
            success = (state() == ConnectedState);
            loop.quit();
        };

        connect(this, &SwTcpSocket::connected, &loop, complete);
        connect(this, &SwTcpSocket::disconnected, &loop, [&]() { loop.quit(); });
        connect(this, &SwTcpSocket::errorOccurred, &loop, [&](int) { loop.quit(); });

        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }

        loop.exec();
        return success || state() == ConnectedState;
    }

    void close() override {
        if (!isSocketValid_()) {
            closeNow_(true);
            return;
        }

        if ((state() == ConnectedState || state() == ClosingState) && !m_writeBuffer.isEmpty()) {
            const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
            m_closeRequested = true;
            m_writeShutdownRequested = true;
            setState(ClosingState);
            tryFlushWriteBuffer_();
            if (guard->alive.load(std::memory_order_acquire)) {
                updateDispatcherInterest_();
            }
            return;
        }

        closeNow_(true);
    }

    /** Immediately discards pending writes and closes the native socket. */
    void abort() {
        closeNow_(true);
    }

    int64_t readInto(char* data, int64_t maxSize) override {
        if (!data || maxSize <= 0 || !isSocketValid_() || state() != ConnectedState) {
            return 0;
        }
        ++m_readCallGeneration;

#if defined(_WIN32)
        const int toRead = static_cast<int>(
            std::min<int64_t>(maxSize, static_cast<int64_t>(std::numeric_limits<int>::max())));
        const int ret = ::recv(m_socket, data, toRead, 0);
        if (ret > 0) {
            incrementTotalReceivedBytes_(static_cast<size_t>(ret));
            return ret;
        }
        if (ret == 0) {
            markRemoteClosed_(true);
            scheduleRemoteCloseCheck_();
            return 0;
        }

        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return 0;
        }
        failAndClose_(err);
        return -1;
#else
        ssize_t ret = -1;
        do {
            ret = ::recv(m_socket, data, static_cast<size_t>(maxSize), 0);
        } while (ret < 0 && errno == EINTR);
        if (ret > 0) {
            incrementTotalReceivedBytes_(static_cast<size_t>(ret));
            return static_cast<int64_t>(ret);
        }
        if (ret == 0) {
            markRemoteClosed_(true);
            scheduleRemoteCloseCheck_();
            return 0;
        }
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0;
        }
        failAndClose_(errno);
        return -1;
#endif
    }

    SwByteArray read(int64_t maxSize = 0) override {
        if (!isSocketValid_() || state() != ConnectedState) {
            return SwByteArray();
        }

        const int64_t requested = maxSize > 0 ? maxSize : kSwTcpDefaultReadChunkSize;
        const int64_t capped = std::min<int64_t>(
            requested, static_cast<int64_t>(std::numeric_limits<int>::max()));
        SwByteArray result;
        result.resize(static_cast<size_t>(capped));
        const int64_t ret = readInto(result.data(), capped);
        if (ret <= 0) {
            return SwByteArray();
        }

        result.resize(static_cast<size_t>(ret));
        return result;
    }

    bool write(const SwString& data) override {
        return write(data.data(), data.size());
    }

    bool write(const SwByteArray& data) override {
        return write(data.constData(), data.size());
    }

    bool write(const char* data, std::size_t size) override {
        return tryWrite(data, size) == WriteResult::Accepted;
    }

    WriteResult tryWrite(const char* data, std::size_t size) {
        if (!isSocketValid_() || state() != ConnectedState) {
            m_lastWriteResult = WriteResult::NotConnected;
            return m_lastWriteResult;
        }
        if (m_writeShutdownRequested || m_writeShutdownPerformed || m_closeRequested) {
            m_lastWriteResult = WriteResult::WriteClosed;
            return m_lastWriteResult;
        }
        if (size == 0) {
            m_lastWriteResult = WriteResult::Accepted;
            return m_lastWriteResult;
        }
        if (!data) {
            m_lastWriteResult = WriteResult::Error;
            return m_lastWriteResult;
        }
        if (size > m_writeHighWatermark) {
            m_lastWriteResult = WriteResult::TooLarge;
            return m_lastWriteResult;
        }
        if (m_writeBuffer.size() > m_writeHighWatermark - size) {
            m_writeBackpressured = true;
            m_lastWriteResult = WriteResult::WouldBlock;
            return m_lastWriteResult;
        }

        m_writeBuffer.append(data, size);
        if (m_writeBuffer.size() >= m_writeHighWatermark) {
            m_writeBackpressured = true;
        }
        m_lastWriteResult = WriteResult::Accepted;
        onWriteQueued_();
        return m_lastWriteResult;
    }

    bool waitForBytesWritten(int msecs = 30000) override {
        if (m_writeBuffer.isEmpty()) {
            return true;
        }

        SwEventLoop loop;
        SwTimer timeoutTimer;
        bool success = false;

        auto tryComplete = [&]() {
            if (m_writeBuffer.isEmpty()) {
                success = true;
                loop.quit();
            }
        };

        connect(this, &SwTcpSocket::writeFinished, &loop, tryComplete);
        connect(this, &SwTcpSocket::disconnected, &loop, [&]() { loop.quit(); });
        connect(this, &SwTcpSocket::errorOccurred, &loop, [&](int) { loop.quit(); });

        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }

        onWriteQueued_();
        tryComplete();
        if (!success) {
            loop.exec();
        }
        return success;
    }

    unsigned long long totalReceivedBytes() const {
        return totalReceivedBytes_.load(std::memory_order_relaxed);
    }

    unsigned long long totalSentBytes() const {
        return totalSentBytes_.load(std::memory_order_relaxed);
    }

    std::size_t bytesToWrite() const {
        return m_writeBuffer.size();
    }

    WriteResult lastWriteResult() const {
        return m_lastWriteResult;
    }

    void setWriteBufferWatermarks(std::size_t highWatermark, std::size_t lowWatermark) {
        if (highWatermark == 0) {
            return;
        }
        m_writeHighWatermark = highWatermark;
        m_writeLowWatermark = (std::min)(lowWatermark, highWatermark);
        if (m_writeBuffer.size() >= m_writeHighWatermark) {
            m_writeBackpressured = true;
        }
    }

    void setWriteHighWatermark(std::size_t bytes) {
        setWriteBufferWatermarks(bytes, (std::min)(m_writeLowWatermark, bytes));
    }

    void setWriteLowWatermark(std::size_t bytes) {
        setWriteBufferWatermarks(m_writeHighWatermark, bytes);
    }

    std::size_t writeHighWatermark() const {
        return m_writeHighWatermark;
    }

    std::size_t writeLowWatermark() const {
        return m_writeLowWatermark;
    }

    void setWriteFlushBudget(std::size_t bytes) {
        if (bytes > 0) {
            m_writeFlushBudget = bytes;
        }
    }

    std::size_t writeFlushBudget() const {
        return m_writeFlushBudget;
    }

    void resumeReadNotifications() {
        if (!m_remoteEofConfirmed) {
            m_readNotificationsEnabled = true;
            updateDispatcherInterest_();
            if (isSocketValid_()) {
                emit readyRead();
            }
        }
    }

    bool readNotificationsEnabled() const {
        return m_readNotificationsEnabled;
    }

    void setAutoCloseOnRemoteShutdown(bool enabled) {
        m_autoCloseOnRemoteShutdown = enabled;
    }

    bool autoCloseOnRemoteShutdown() const {
        return m_autoCloseOnRemoteShutdown;
    }

    /** Cancels a DNS lookup or every in-flight Happy Eyeballs connection attempt. */
    void cancelConnect() {
        if (state() == HostLookupState || state() == ConnectingState) {
            closeNow_(true);
        }
    }

    void setConnectTimeout(int milliseconds) {
        m_connectTimeoutMs = milliseconds;
    }

    int connectTimeout() const {
        return m_connectTimeoutMs;
    }

    void setHappyEyeballsDelay(int milliseconds) {
        if (milliseconds >= 0) {
            m_happyEyeballsDelayMs = milliseconds;
        }
    }

    int happyEyeballsDelay() const {
        return m_happyEyeballsDelayMs;
    }

    void setTcpNoDelay(bool enabled) {
        m_tcpNoDelay = enabled;
        applyTcpOptionsToAllHandles_();
    }

    bool tcpNoDelay() const {
        return m_tcpNoDelay;
    }

    void setKeepAlive(bool enabled) {
        m_keepAlive = enabled;
        applyTcpOptionsToAllHandles_();
    }

    bool keepAlive() const {
        return m_keepAlive;
    }

    /**
     * Maximum time, in milliseconds, that transmitted data may remain unacknowledged.
     * A value of zero restores the platform default. Unsupported platforms retain the
     * requested value and simply skip the native TCP_USER_TIMEOUT option.
     */
    void setTcpUserTimeout(int milliseconds) {
        if (milliseconds < 0) {
            return;
        }
        m_tcpUserTimeoutMs = milliseconds;
        applyTcpOptionsToAllHandles_();
    }

    int tcpUserTimeout() const {
        return m_tcpUserTimeoutMs;
    }

    void setReceiveBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_receiveBufferSize = bytes;
        if (isSocketValid_()) {
            applyReceiveBufferSize_();
        }
    }

    void setSendBufferSize(int bytes) {
        if (bytes <= 0) {
            return;
        }
        m_sendBufferSize = bytes;
        if (isSocketValid_()) {
            applySendBufferSize_();
        }
    }

    int requestedReceiveBufferSize() const {
        return m_receiveBufferSize;
    }

    int requestedSendBufferSize() const {
        return m_sendBufferSize;
    }

    int actualReceiveBufferSize() const {
        return socketIntOption_(SO_RCVBUF);
    }

    int actualSendBufferSize() const {
        return socketIntOption_(SO_SNDBUF);
    }

    SwString localAddress() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(false, address)) {
            return SwString();
        }
        return socketAddressToString_(address);
    }

    uint16_t localPort() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(false, address)) {
            return 0;
        }
        return socketAddressPort_(address);
    }

    SwString peerAddress() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(true, address)) {
            return SwString();
        }
        return socketAddressToString_(address);
    }

    uint16_t peerPort() const {
        sockaddr_storage address {};
        if (!querySocketAddress_(true, address)) {
            return 0;
        }
        return socketAddressPort_(address);
    }

    virtual bool shutdownWrite(int lingerSeconds = 5) {
        (void)lingerSeconds;
        if (!isSocketValid_() || (state() != ConnectedState && state() != ClosingState)) {
            return false;
        }
        if (m_writeShutdownPerformed || m_writeShutdownRequested) {
            return true;
        }
        m_writeShutdownRequested = true;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        if (!m_writeBuffer.isEmpty()) {
            tryFlushWriteBuffer_();
            if (!guard->alive.load(std::memory_order_acquire)) {
                return false;
            }
            updateDispatcherInterest_();
            return true;
        }
        const bool shutdownOk = performShutdownWrite_();
        if (shutdownOk && guard->alive.load(std::memory_order_acquire)) {
            closeIfRemoteClosedAndIdle_();
        }
        return shutdownOk;
    }

    void adoptSocket(SwNativeSocketHandle sock, bool emitConnectedSignal = true) {
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        const std::uint64_t expectedClosedGeneration = m_transportGeneration + 1;
        closeNow_(true);
        if (!guard->alive.load(std::memory_order_acquire) ||
            m_transportGeneration != expectedClosedGeneration) {
            return;
        }
        m_socket = sock;
        if (!isSocketValid_()) {
            return;
        }
        ++m_transportGeneration;
        resetCloseState_();
        m_remoteClosed = false;
        m_remoteEofConfirmed = false;
        // Accepted sockets keep their write half alive after peer EOF so servers can finish a
        // response. Outbound sockets default to auto-close for close-delimited client protocols.
        m_autoCloseOnRemoteShutdown = false;
        applySocketBufferSizes_();
        applyTcpOptionsToHandle_(m_socket);

#if defined(_WIN32)
        u_long mode = 1;
        if (::ioctlsocket(m_socket, FIONBIO, &mode) == SOCKET_ERROR) {
            failAndClose_(WSAGetLastError());
            return;
        }

        m_event = ::WSACreateEvent();
        if (m_event == WSA_INVALID_EVENT) {
            failAndClose_(WSAGetLastError());
            return;
        }

        if (::WSAEventSelect(m_socket, m_event, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
            failAndClose_(WSAGetLastError());
            return;
        }
#else
        if (!setNonBlockingAndCloseOnExec_(m_socket)) {
            failAndClose_(errno);
            return;
        }
        m_connecting = false;
#endif

        if (!registerDispatcher_()) {
            closeNow_(false);
            return;
        }
        updateDispatcherInterest_();

        setState(ConnectedState);
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        if (emitConnectedSignal) {
            emit connected();
        }
    }

    bool isRemoteClosed() const override {
        return m_remoteClosed;
    }

    bool hasPendingWrites() const override {
        return !m_writeBuffer.isEmpty();
    }

protected:
    struct SocketLifetimeGuard {
        std::atomic<bool> alive{true};
    };

    struct ConnectAttempt {
        SwNativeSocketHandle socket = kSwInvalidSocketHandle;
        size_t dispatchToken = 0;
#if defined(_WIN32)
        WSAEVENT event = NULL;
#endif
    };

    SwNativeSocketHandle m_socket = kSwInvalidSocketHandle;
    SwByteRingBuffer m_writeBuffer;
    SwString m_lastHost;
    bool m_remoteClosed = false;
    bool m_remoteEofConfirmed = false;
    size_t m_dispatchToken = 0;
    std::atomic<unsigned long long> totalReceivedBytes_{0};
    std::atomic<unsigned long long> totalSentBytes_{0};
    SwSocketTrafficStateHandle socketTrafficState_;
    int m_receiveBufferSize = 0;
    int m_sendBufferSize = 0;
    std::size_t m_writeHighWatermark = kSwTcpDefaultWriteHighWatermark;
    std::size_t m_writeLowWatermark = kSwTcpDefaultWriteLowWatermark;
    std::size_t m_writeFlushBudget = kSwTcpDefaultWriteFlushBudget;
    WriteResult m_lastWriteResult = WriteResult::Accepted;
    bool m_writeBackpressured = false;
    bool m_writeShutdownRequested = false;
    bool m_writeShutdownPerformed = false;
    bool m_closeRequested = false;
    bool m_autoCloseOnRemoteShutdown = true;
    bool m_writeContinuationScheduled = false;
    bool m_remoteCloseCheckScheduled = false;
    bool m_readNotificationsEnabled = true;
    std::uint64_t m_readCallGeneration = 0;
    std::uint64_t m_transportGeneration = 0;
    SwHostResolver::AddressList m_connectCandidates;
    std::vector<std::shared_ptr<ConnectAttempt>> m_connectAttempts;
    std::size_t m_nextConnectCandidate = 0;
    uint16_t m_connectPort = 0;
    int m_lastConnectError = 0;
    int m_connectTimeoutMs = 30000;
    int m_happyEyeballsDelayMs = 250;
    bool m_tcpNoDelay = false;
    bool m_keepAlive = false;
    int m_tcpUserTimeoutMs = 0;
    std::shared_ptr<SocketLifetimeGuard> m_lifetimeGuard =
        std::make_shared<SocketLifetimeGuard>();
    SwTimer* m_happyEyeballsTimer = nullptr;
    SwTimer* m_connectDeadlineTimer = nullptr;

#if defined(_WIN32)
    WSAEVENT m_event = NULL;
#else
    bool m_connecting = false;
#endif

    bool isSocketValid_() const {
#if defined(_WIN32)
        return m_socket != INVALID_SOCKET;
#else
        return m_socket >= 0;
#endif
    }

    intptr_t nativeSocket_() const {
        return static_cast<intptr_t>(m_socket);
    }

    int socketIntOption_(int option) const {
        if (!isSocketValid_()) {
            return 0;
        }
        return socketIntOptionForHandle_(m_socket, option);
    }

    void applyReceiveBufferSize_() {
        applySocketBufferSizeToHandle_(m_socket, SO_RCVBUF, m_receiveBufferSize, "receive");
    }

    void applySendBufferSize_() {
        applySocketBufferSizeToHandle_(m_socket, SO_SNDBUF, m_sendBufferSize, "send");
    }

    void applySocketBufferSizes_() {
        applyReceiveBufferSize_();
        applySendBufferSize_();
    }

    virtual bool handleTransportConnectedEvent_() {
        setState(ConnectedState);
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        emit connected();
        return true;
    }

    virtual bool handleTransportReadableEvent_() {
        if (!m_readNotificationsEnabled || m_remoteEofConfirmed) {
            return true;
        }
        const std::uint64_t readGeneration = m_readCallGeneration;
        const std::uint64_t transportGeneration = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        emit readyRead();
        if (!guard->alive.load(std::memory_order_acquire) ||
            m_transportGeneration != transportGeneration) {
            return false;
        }
        // A listener that does not attempt a read must not keep a level-triggered fd hot.
        // It can explicitly resume once it is ready to consume data.
        if (m_readCallGeneration == readGeneration) {
            m_readNotificationsEnabled = false;
        }
        return true;
    }

    virtual bool handleTransportWritableEvent_() {
        const std::uint64_t transportGeneration = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        tryFlushWriteBuffer_();
        if (!guard->alive.load(std::memory_order_acquire) ||
            m_transportGeneration != transportGeneration) {
            return false;
        }
        updateDispatcherInterest_();
        return true;
    }

    virtual void handleTransportCloseEvent_() {
        if (m_remoteClosed) {
            return;
        }
        markRemoteClosed_(false);
        emit readyRead();
    }

    virtual bool shouldCloseAfterTransportClose_() const {
        return m_remoteEofConfirmed && m_writeBuffer.isEmpty() &&
               (m_autoCloseOnRemoteShutdown || m_writeShutdownPerformed || m_closeRequested);
    }

    virtual void onWriteQueued_() {
        tryFlushWriteBuffer_();
        updateDispatcherInterest_();
    }

    virtual void tryFlushWriteBuffer_() {
        if (!isSocketValid_() ||
            (state() != ConnectedState && state() != ClosingState) ||
            m_writeBuffer.isEmpty()) {
            return;
        }
        const std::uint64_t transportGeneration = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;

        bool madeProgress = false;
        std::size_t budget = m_writeFlushBudget;
        while (!m_writeBuffer.isEmpty()) {
            if (budget == 0) {
#if defined(_WIN32)
                if (scheduleWriteContinuation_()) {
                    break;
                }
                // The runtime is saturated and could not retain the continuation. Preserve
                // correctness by falling back to the historical drain-until-WOULDBLOCK path.
                budget = (std::numeric_limits<std::size_t>::max)();
#else
                break;
#endif
            }
            const char* data = m_writeBuffer.contiguousData();
            const std::size_t available = m_writeBuffer.contiguousSize();
            if (!data || available == 0) {
                break;
            }

#if defined(_WIN32)
            const int toSend = static_cast<int>(
                std::min<std::size_t>((std::min)(available, budget),
                                      static_cast<std::size_t>(std::numeric_limits<int>::max())));
            int sent = SOCKET_ERROR;
            do {
                sent = ::send(m_socket, data, toSend, 0);
            } while (sent == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
            if (sent > 0) {
                madeProgress = true;
                incrementTotalSentBytes_(static_cast<size_t>(sent));
                m_writeBuffer.consume(static_cast<std::size_t>(sent));
                budget -= static_cast<std::size_t>(sent);
                continue;
            }

            if (sent == SOCKET_ERROR) {
                const int err = WSAGetLastError();
                if (err != WSAEWOULDBLOCK) {
                    failAndClose_(err);
                    return;
                }
            }
            break;
#else
            const std::size_t toSend = (std::min)(available, budget);
            int sendFlags = 0;
#if defined(MSG_NOSIGNAL)
            sendFlags |= MSG_NOSIGNAL;
#endif
            ssize_t sent = -1;
            do {
                sent = ::send(m_socket, data, toSend, sendFlags);
            } while (sent < 0 && errno == EINTR);
            if (sent > 0) {
                madeProgress = true;
                incrementTotalSentBytes_(static_cast<size_t>(sent));
                m_writeBuffer.consume(static_cast<std::size_t>(sent));
                budget -= static_cast<std::size_t>(sent);
                continue;
            }

            if (sent < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                failAndClose_(errno);
                return;
            }
            break;
#endif
        }

        if (!notifyWriteBufferLowWatermark_() ||
            m_transportGeneration != transportGeneration) {
            return;
        }

        if (madeProgress && m_writeBuffer.isEmpty()) {
            emit writeFinished();
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != transportGeneration) {
                return;
            }
            if (m_writeShutdownRequested && !m_writeShutdownPerformed && !performShutdownWrite_()) {
                return;
            }
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != transportGeneration) {
                return;
            }
            if (m_closeRequested) {
                closeNow_(true);
                return;
            }
            closeIfRemoteClosedAndIdle_();
        }
    }

#if !defined(_WIN32)
    virtual SwIoDispatcher::EventMask desiredDispatcherEvents_() const {
        uint32_t events = SwIoDispatcher::Error;
        if (!m_remoteEofConfirmed && m_readNotificationsEnabled) {
            events |= SwIoDispatcher::Readable | SwIoDispatcher::Hangup;
        }
        if (m_connecting || !m_writeBuffer.isEmpty()) {
            events |= SwIoDispatcher::Writable;
        }
        return events;
    }
#endif

    void updateDispatcherInterest_() {
#if !defined(_WIN32)
        if (!m_dispatchToken) {
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->ioDispatcher().updateFd(m_dispatchToken, desiredDispatcherEvents_());
        }
#endif
    }

    void closeIfRemoteClosedAndIdle_() {
        if (m_remoteClosed && shouldCloseAfterTransportClose_() && state() != UnconnectedState) {
            close();
        }
    }

    void resetCloseState_() {
        m_writeShutdownRequested = false;
        m_writeShutdownPerformed = false;
        m_closeRequested = false;
        m_writeBackpressured = false;
        m_writeContinuationScheduled = false;
        m_remoteCloseCheckScheduled = false;
        m_readNotificationsEnabled = true;
        m_lastWriteResult = WriteResult::Accepted;
    }

    bool notifyWriteBufferLowWatermark_() {
        if (!m_writeBackpressured || m_writeBuffer.size() > m_writeLowWatermark) {
            return true;
        }
        m_writeBackpressured = false;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        emit readyWrite();
        return guard->alive.load(std::memory_order_acquire);
    }

    bool scheduleWriteContinuation_() {
        if (m_writeContinuationScheduled) {
            return true;
        }
        ThreadHandle* affinity = threadHandle();
        if (!affinity || !ThreadHandle::isLive(affinity)) {
            return false;
        }

        m_writeContinuationScheduled = true;
        const std::uint64_t generation = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        std::function<void()> continuation = [this, generation, guard]() {
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return;
            }
            m_writeContinuationScheduled = false;
            tryFlushWriteBuffer_();
            if (guard->alive.load(std::memory_order_acquire) &&
                m_transportGeneration == generation) {
                updateDispatcherInterest_();
            }
        };
        std::function<void()> controlFallback = continuation;
        if (affinity->postTaskOnLane(std::move(continuation), SwFiberLane::Input)) {
            return true;
        }
        if (affinity->postTaskOnLane(std::move(controlFallback), SwFiberLane::Control)) {
            return true;
        }
        m_writeContinuationScheduled = false;
        return false;
    }

    void scheduleRemoteCloseCheck_() {
        if (m_remoteCloseCheckScheduled || state() == UnconnectedState) {
            return;
        }
        m_remoteCloseCheckScheduled = true;
        const std::uint64_t generation = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        std::function<void()> continuation = [this, generation, guard]() {
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return;
            }
            m_remoteCloseCheckScheduled = false;
            closeIfRemoteClosedAndIdle_();
        };

        ThreadHandle* affinity = threadHandle();
        if (affinity && ThreadHandle::isLive(affinity) &&
            affinity->postTaskOnLaneReliable(continuation, SwFiberLane::Control)) {
            return;
        }
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (app && app->postEventOnLaneReliable(std::move(continuation),
                                                SwFiberLane::Control)) {
            return;
        }
        // A dispatcher close/hangup notification will retry the check without spinning.
        m_remoteCloseCheckScheduled = false;
    }

    void markRemoteClosed_(bool eofConfirmed) {
        m_remoteClosed = true;
        if (eofConfirmed) {
            m_remoteEofConfirmed = true;
            m_readNotificationsEnabled = false;
        }
    }

    bool performShutdownWrite_() {
        if (m_writeShutdownPerformed) {
            return true;
        }
        if (!isSocketValid_() || !m_writeBuffer.isEmpty()) {
            return false;
        }

#if defined(_WIN32)
        if (::shutdown(m_socket, SD_SEND) == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err != WSAENOTCONN && err != WSAESHUTDOWN) {
                failAndClose_(err);
                return false;
            }
        }
#else
        if (::shutdown(m_socket, SHUT_WR) != 0) {
            const int err = errno;
            if (err != ENOTCONN && err != EPIPE) {
                failAndClose_(err);
                return false;
            }
        }
#endif
        m_writeShutdownPerformed = true;
        return true;
    }

    void closeNow_(bool notifyDisconnected) {
        const SocketState oldState = state();
        const bool shouldNotify = notifyDisconnected &&
                                  (oldState == HostLookupState ||
                                   oldState == ConnectedState ||
                                   oldState == ConnectingState ||
                                   oldState == ClosingState);

        // Invalidate callbacks from the old native transport before releasing its handle.
        ++m_transportGeneration;
        cleanupConnectAttempts_();
        unregisterDispatcher_();
        swSocketTrafficSetOpenState(socketTrafficState_, false);

#if defined(_WIN32)
        if (m_socket != INVALID_SOCKET) {
            ::closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        if (m_event) {
            ::WSACloseEvent(m_event);
            m_event = NULL;
        }
#else
        if (m_socket >= 0) {
            ::close(m_socket);
            m_socket = -1;
        }
        m_connecting = false;
#endif

        m_writeBuffer.clear();
        resetCloseState_();
        setState(UnconnectedState);

        // State and resources are final before the signal, and this must remain the last access
        // to the object: a direct slot is allowed to destroy or reconnect it.
        if (shouldNotify) {
            emit disconnected();
        }
    }

    void failAndClose_(int errorCode) {
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        const SocketState oldState = state();
        const bool shouldNotifyDisconnected = oldState == HostLookupState ||
                                              oldState == ConnectedState ||
                                              oldState == ConnectingState ||
                                              oldState == ClosingState;
        closeNow_(false);
        const std::uint64_t closedGeneration = m_transportGeneration;
        emit errorOccurred(errorCode);
        if (!guard->alive.load(std::memory_order_acquire) ||
            m_transportGeneration != closedGeneration) {
            return;
        }
        if (shouldNotifyDisconnected) {
            emit disconnected();
        }
    }

    void incrementTotalReceivedBytes_(size_t bytes) {
        if (bytes == 0) {
            return;
        }
        totalReceivedBytes_.fetch_add(static_cast<unsigned long long>(bytes), std::memory_order_relaxed);
        swSocketTrafficAddReceivedBytes(socketTrafficState_, static_cast<unsigned long long>(bytes));
    }

    void incrementTotalSentBytes_(size_t bytes) {
        if (bytes == 0) {
            return;
        }
        totalSentBytes_.fetch_add(static_cast<unsigned long long>(bytes), std::memory_order_relaxed);
        swSocketTrafficAddSentBytes(socketTrafficState_, static_cast<unsigned long long>(bytes));
    }

    void refreshTrafficMonitorEndpoints_() {
        swSocketTrafficUpdateEndpoints(socketTrafficState_, localAddress(), localPort(), peerAddress(), peerPort());
    }

private:
    static SwHostResolver::AddressList alternateAddressFamilies_(
        const SwHostResolver::AddressList& addresses) {
        SwHostResolver::AddressList ordered;
        if (addresses.empty()) {
            return ordered;
        }
        ordered.reserve(addresses.size());
        const int firstFamily = addresses.front().family;
        const int secondFamily = firstFamily == AF_INET6 ? AF_INET : AF_INET6;
        std::size_t firstIndex = 0;
        std::size_t secondIndex = 0;
        for (;;) {
            bool appended = false;
            while (firstIndex < addresses.size()) {
                const SwHostResolver::ResolvedAddress& candidate = addresses[firstIndex++];
                if (candidate.family == firstFamily) {
                    ordered.push_back(candidate);
                    appended = true;
                    break;
                }
            }
            while (secondIndex < addresses.size()) {
                const SwHostResolver::ResolvedAddress& candidate = addresses[secondIndex++];
                if (candidate.family == secondFamily) {
                    ordered.push_back(candidate);
                    appended = true;
                    break;
                }
            }
            if (!appended) {
                break;
            }
        }
        return ordered;
    }

    void handleResolvedAddresses_(const SwHostResolver::AddressList& addresses) {
        if (addresses.empty()) {
#if defined(_WIN32)
            failAndClose_(WSAHOST_NOT_FOUND);
#else
            failAndClose_(EHOSTUNREACH);
#endif
            return;
        }
        m_connectCandidates = alternateAddressFamilies_(addresses);
        m_nextConnectCandidate = 0;
        setState(ConnectingState);
        startNextConnectAttempt_();
    }

    static void setAddressPort_(sockaddr_storage& storage, uint16_t port) {
        if (storage.ss_family == AF_INET) {
            reinterpret_cast<sockaddr_in*>(&storage)->sin_port = htons(port);
        } else if (storage.ss_family == AF_INET6) {
            reinterpret_cast<sockaddr_in6*>(&storage)->sin6_port = htons(port);
        }
    }

    void startNextConnectAttempt_() {
        if (state() != ConnectingState) {
            return;
        }

        while (m_nextConnectCandidate < m_connectCandidates.size()) {
            const SwHostResolver::ResolvedAddress candidate =
                m_connectCandidates[m_nextConnectCandidate++];
            std::shared_ptr<ConnectAttempt> attempt(new ConnectAttempt());

#if defined(_WIN32)
            attempt->socket = ::WSASocketW(candidate.family,
                                           SOCK_STREAM,
                                           IPPROTO_TCP,
                                           nullptr,
                                           0,
                                           WSA_FLAG_OVERLAPPED);
            if (!isNativeSocketValid_(attempt->socket)) {
                m_lastConnectError = WSAGetLastError();
                continue;
            }
            u_long mode = 1;
            if (::ioctlsocket(attempt->socket, FIONBIO, &mode) == SOCKET_ERROR) {
                m_lastConnectError = WSAGetLastError();
                closeConnectAttemptNative_(attempt);
                continue;
            }
            attempt->event = ::WSACreateEvent();
            if (attempt->event == WSA_INVALID_EVENT) {
                attempt->event = NULL;
                m_lastConnectError = WSAGetLastError();
                closeConnectAttemptNative_(attempt);
                continue;
            }
            if (::WSAEventSelect(attempt->socket,
                                 attempt->event,
                                 FD_CONNECT | FD_CLOSE) == SOCKET_ERROR) {
                m_lastConnectError = WSAGetLastError();
                closeConnectAttemptNative_(attempt);
                continue;
            }
#else
            int socketType = SOCK_STREAM;
#if defined(SOCK_NONBLOCK)
            socketType |= SOCK_NONBLOCK;
#endif
#if defined(SOCK_CLOEXEC)
            socketType |= SOCK_CLOEXEC;
#endif
            attempt->socket = ::socket(candidate.family, socketType, IPPROTO_TCP);
            if (!isNativeSocketValid_(attempt->socket)) {
                m_lastConnectError = errno;
                continue;
            }
            if (!setNonBlockingAndCloseOnExec_(attempt->socket)) {
                m_lastConnectError = errno;
                closeConnectAttemptNative_(attempt);
                continue;
            }
#endif

            applySocketBufferSizeToHandle_(attempt->socket, SO_RCVBUF, m_receiveBufferSize, "receive");
            applySocketBufferSizeToHandle_(attempt->socket, SO_SNDBUF, m_sendBufferSize, "send");
            applyTcpOptionsToHandle_(attempt->socket);

            sockaddr_storage target = candidate.storage;
            setAddressPort_(target, m_connectPort);
            const int connectResult = ::connect(
                attempt->socket,
                reinterpret_cast<const sockaddr*>(&target),
#if defined(_WIN32)
                static_cast<int>(candidate.length)
#else
                candidate.length
#endif
            );
            if (connectResult == 0) {
                m_connectAttempts.push_back(attempt);
                promoteConnectAttempt_(attempt);
                return;
            }

#if defined(_WIN32)
            const int connectError = WSAGetLastError();
            const bool pending = connectError == WSAEWOULDBLOCK ||
                                 connectError == WSAEINPROGRESS ||
                                 connectError == WSAEALREADY;
#else
            const int connectError = errno;
            const bool pending = connectError == EINPROGRESS ||
                                 connectError == EWOULDBLOCK ||
                                 connectError == EALREADY;
#endif
            if (!pending) {
                m_lastConnectError = connectError;
                closeConnectAttemptNative_(attempt);
                continue;
            }

            m_connectAttempts.push_back(attempt);
            if (!registerConnectAttemptDispatcher_(attempt)) {
#if defined(_WIN32)
                m_lastConnectError = WSAGetLastError();
#else
                m_lastConnectError = errno != 0 ? errno : EIO;
#endif
                removeConnectAttempt_(attempt);
                continue;
            }
            if (m_nextConnectCandidate < m_connectCandidates.size() &&
                m_happyEyeballsTimer) {
                m_happyEyeballsTimer->start(m_happyEyeballsDelayMs);
            }
            return;
        }

        if (m_connectAttempts.empty()) {
#if defined(_WIN32)
            failAndClose_(m_lastConnectError != 0 ? m_lastConnectError : WSAECONNREFUSED);
#else
            failAndClose_(m_lastConnectError != 0 ? m_lastConnectError : ECONNREFUSED);
#endif
        }
    }

    bool registerConnectAttemptDispatcher_(const std::shared_ptr<ConnectAttempt>& attempt) {
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app || !attempt || !isNativeSocketValid_(attempt->socket)) {
            return false;
        }
        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        const std::uint64_t generation = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        const std::weak_ptr<SocketLifetimeGuard> lifetime = m_lifetimeGuard;

#if defined(_WIN32)
        attempt->dispatchToken = app->ioDispatcher().watchHandleReliable(
            attempt->event,
            [affinity](std::function<void()> task) mutable -> bool {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    std::function<void()> controlFallback = task;
                    if (affinity->postTaskOnLane(std::move(task), SwFiberLane::Input)) {
                        return true;
                    }
                    return affinity->postTaskOnLane(std::move(controlFallback), SwFiberLane::Control);
                }
                task();
                return true;
            },
            [this, attempt, generation, lifetime]() {
                const std::shared_ptr<SocketLifetimeGuard> guard = lifetime.lock();
                if (!guard || !guard->alive.load(std::memory_order_acquire) ||
                    m_transportGeneration != generation ||
                    !attempt || !isNativeSocketValid_(attempt->socket) || !attempt->event) {
                    return;
                }
                WSANETWORKEVENTS networkEvents {};
                if (::WSAEnumNetworkEvents(attempt->socket,
                                           attempt->event,
                                           &networkEvents) == SOCKET_ERROR) {
                    finishConnectAttempt_(attempt, WSAGetLastError());
                    return;
                }
                if (networkEvents.lNetworkEvents & FD_CONNECT) {
                    finishConnectAttempt_(attempt,
                                          networkEvents.iErrorCode[FD_CONNECT_BIT]);
                    return;
                }
                if (networkEvents.lNetworkEvents & FD_CLOSE) {
                    const int closeError = networkEvents.iErrorCode[FD_CLOSE_BIT];
                    finishConnectAttempt_(attempt,
                                          closeError != 0 ? closeError : WSAECONNREFUSED);
                }
            });
#else
        attempt->dispatchToken = app->ioDispatcher().watchFdReliable(
            attempt->socket,
            SwIoDispatcher::Writable | SwIoDispatcher::Error | SwIoDispatcher::Hangup,
            [affinity](std::function<void()> task) mutable -> bool {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    std::function<void()> controlFallback = task;
                    if (affinity->postTaskOnLane(std::move(task), SwFiberLane::Input)) {
                        return true;
                    }
                    return affinity->postTaskOnLane(std::move(controlFallback), SwFiberLane::Control);
                }
                task();
                return true;
            },
            [this, attempt, generation, lifetime](uint32_t) {
                const std::shared_ptr<SocketLifetimeGuard> guard = lifetime.lock();
                if (!guard || !guard->alive.load(std::memory_order_acquire) ||
                    m_transportGeneration != generation ||
                    !attempt || !isNativeSocketValid_(attempt->socket)) {
                    return;
                }
                int connectError = 0;
                socklen_t length = sizeof(connectError);
                if (::getsockopt(attempt->socket,
                                 SOL_SOCKET,
                                 SO_ERROR,
                                 &connectError,
                                 &length) != 0) {
                    connectError = errno;
                }
                finishConnectAttempt_(attempt, connectError);
            });
#endif
        return attempt->dispatchToken != 0;
    }

    void finishConnectAttempt_(const std::shared_ptr<ConnectAttempt>& attempt,
                               int connectError) {
        if (!attempt || !isNativeSocketValid_(attempt->socket) ||
            state() != ConnectingState) {
            return;
        }
        if (connectError == 0) {
            promoteConnectAttempt_(attempt);
            return;
        }

        m_lastConnectError = connectError;
        removeConnectAttempt_(attempt);
        if (m_happyEyeballsTimer && m_happyEyeballsTimer->isActive()) {
            m_happyEyeballsTimer->stop();
        }
        startNextConnectAttempt_();
    }

    void promoteConnectAttempt_(const std::shared_ptr<ConnectAttempt>& winner) {
        if (!winner || !isNativeSocketValid_(winner->socket) ||
            state() != ConnectingState) {
            return;
        }

        const SwNativeSocketHandle connectedSocket = winner->socket;
        winner->socket = kSwInvalidSocketHandle;
#if defined(_WIN32)
        const WSAEVENT connectedEvent = winner->event;
        winner->event = NULL;
#endif
        cleanupConnectAttempts_();

        m_socket = connectedSocket;
#if defined(_WIN32)
        m_event = connectedEvent;
        if (!m_event ||
            ::WSAEventSelect(m_socket, m_event, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
            failAndClose_(WSAGetLastError());
            return;
        }
#else
        m_connecting = false;
#endif
        if (!registerDispatcher_()) {
#if defined(_WIN32)
            failAndClose_(WSAGetLastError());
#else
            failAndClose_(errno != 0 ? errno : EIO);
#endif
            return;
        }
        updateDispatcherInterest_();

        const std::uint64_t generation = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;
        if (!handleTransportConnectedEvent_()) {
            if (guard->alive.load(std::memory_order_acquire) &&
                m_transportGeneration == generation) {
                closeNow_(true);
            }
        }
    }

    void removeConnectAttempt_(const std::shared_ptr<ConnectAttempt>& attempt) {
        if (!attempt) {
            return;
        }
        if (attempt->dispatchToken != 0) {
            if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
                app->ioDispatcher().remove(attempt->dispatchToken);
            }
            attempt->dispatchToken = 0;
        }
        closeConnectAttemptNative_(attempt);
        m_connectAttempts.erase(
            std::remove(m_connectAttempts.begin(), m_connectAttempts.end(), attempt),
            m_connectAttempts.end());
    }

    static void closeConnectAttemptNative_(const std::shared_ptr<ConnectAttempt>& attempt) {
        if (!attempt) {
            return;
        }
#if defined(_WIN32)
        if (isNativeSocketValid_(attempt->socket)) {
            ::closesocket(attempt->socket);
            attempt->socket = kSwInvalidSocketHandle;
        }
        if (attempt->event) {
            ::WSACloseEvent(attempt->event);
            attempt->event = NULL;
        }
#else
        if (isNativeSocketValid_(attempt->socket)) {
            ::close(attempt->socket);
            attempt->socket = kSwInvalidSocketHandle;
        }
#endif
    }

    void cleanupConnectAttempts_() {
        if (m_happyEyeballsTimer && m_happyEyeballsTimer->isActive()) {
            m_happyEyeballsTimer->stop();
        }
        if (m_connectDeadlineTimer && m_connectDeadlineTimer->isActive()) {
            m_connectDeadlineTimer->stop();
        }
        for (std::size_t i = 0; i < m_connectAttempts.size(); ++i) {
            const std::shared_ptr<ConnectAttempt>& attempt = m_connectAttempts[i];
            if (attempt && attempt->dispatchToken != 0) {
                if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
                    app->ioDispatcher().remove(attempt->dispatchToken);
                }
                attempt->dispatchToken = 0;
            }
            closeConnectAttemptNative_(attempt);
        }
        m_connectAttempts.clear();
        m_connectCandidates.clear();
        m_nextConnectCandidate = 0;
        m_connectPort = 0;
    }

    static bool setNativeIntOption_(SwNativeSocketHandle socket,
                                    int level,
                                    int option,
                                    int value) {
#if defined(_WIN32)
        return ::setsockopt(socket,
                            level,
                            option,
                            reinterpret_cast<const char*>(&value),
                            sizeof(value)) == 0;
#else
        return ::setsockopt(socket, level, option, &value, sizeof(value)) == 0;
#endif
    }

    void applyTcpOptionsToHandle_(SwNativeSocketHandle socket) const {
        if (!isNativeSocketValid_(socket)) {
            return;
        }
        if (!setNativeIntOption_(socket,
                                 IPPROTO_TCP,
                                 TCP_NODELAY,
                                 m_tcpNoDelay ? 1 : 0)) {
            swCWarning(kSwLogCategory_SwTcpSocket)
                << "[SwTcpSocket] Failed to apply TCP_NODELAY error=" << lastSocketError_();
        }
        if (!setNativeIntOption_(socket,
                                 SOL_SOCKET,
                                 SO_KEEPALIVE,
                                 m_keepAlive ? 1 : 0)) {
            swCWarning(kSwLogCategory_SwTcpSocket)
                << "[SwTcpSocket] Failed to apply SO_KEEPALIVE error=" << lastSocketError_();
        }
#if defined(TCP_USER_TIMEOUT)
        if (!setNativeIntOption_(socket,
                                 IPPROTO_TCP,
                                 TCP_USER_TIMEOUT,
                                 m_tcpUserTimeoutMs)) {
            swCWarning(kSwLogCategory_SwTcpSocket)
                << "[SwTcpSocket] Failed to apply TCP_USER_TIMEOUT error=" << lastSocketError_();
        }
#endif
    }

    void applyTcpOptionsToAllHandles_() {
        applyTcpOptionsToHandle_(m_socket);
        for (std::size_t i = 0; i < m_connectAttempts.size(); ++i) {
            if (m_connectAttempts[i]) {
                applyTcpOptionsToHandle_(m_connectAttempts[i]->socket);
            }
        }
    }

    static bool isNativeSocketValid_(SwNativeSocketHandle socket) {
#if defined(_WIN32)
        return socket != INVALID_SOCKET;
#else
        return socket >= 0;
#endif
    }

    static int socketIntOptionForHandle_(SwNativeSocketHandle socket, int option) {
        if (!isNativeSocketValid_(socket)) {
            return 0;
        }
        int value = 0;
#if defined(_WIN32)
        int length = sizeof(value);
        if (::getsockopt(socket,
                         SOL_SOCKET,
                         option,
                         reinterpret_cast<char*>(&value),
                         &length) != 0) {
            return 0;
        }
#else
        socklen_t length = sizeof(value);
        if (::getsockopt(socket, SOL_SOCKET, option, &value, &length) != 0) {
            return 0;
        }
#endif
        return value;
    }

    static int lastSocketError_() {
#if defined(_WIN32)
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    static void applySocketBufferSizeToHandle_(SwNativeSocketHandle socket,
                                               int option,
                                               int bytes,
                                               const char* label) {
        if (bytes <= 0 || !isNativeSocketValid_(socket)) {
            return;
        }
        int desired = bytes;
        int rc = 0;
#if defined(_WIN32)
        rc = ::setsockopt(socket,
                          SOL_SOCKET,
                          option,
                          reinterpret_cast<const char*>(&desired),
                          sizeof(desired));
#else
        rc = ::setsockopt(socket, SOL_SOCKET, option, &desired, sizeof(desired));
#endif
        if (rc != 0) {
            swCWarning(kSwLogCategory_SwTcpSocket)
                << "[SwTcpSocket] Failed to apply " << label
                << " buffer size=" << bytes
                << " error=" << lastSocketError_();
            return;
        }

        const int actual = socketIntOptionForHandle_(socket, option);
        if (actual > 0 && actual < bytes) {
            swCWarning(kSwLogCategory_SwTcpSocket)
                << "[SwTcpSocket] " << label
                << " buffer requested=" << bytes
                << " actual=" << actual;
        }
    }

    SwString socketAddressToString_(const sockaddr_storage& address) const {
        char buffer[INET6_ADDRSTRLEN] = {0};
        if (address.ss_family == AF_INET) {
            const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
#if defined(_WIN32)
            InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), buffer, sizeof(buffer));
#else
            inet_ntop(AF_INET, const_cast<in_addr*>(&ipv4->sin_addr), buffer, sizeof(buffer));
#endif
            return SwString(buffer);
        }
        if (address.ss_family == AF_INET6) {
            const sockaddr_in6* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
            if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
                in_addr mapped{};
                std::memcpy(&mapped, ipv6->sin6_addr.s6_addr + 12, sizeof(mapped));
#if defined(_WIN32)
                InetNtopA(AF_INET, &mapped, buffer, sizeof(buffer));
#else
                inet_ntop(AF_INET, &mapped, buffer, sizeof(buffer));
#endif
                return SwString(buffer);
            }
#if defined(_WIN32)
            InetNtopA(AF_INET6, const_cast<IN6_ADDR*>(&ipv6->sin6_addr), buffer, sizeof(buffer));
#else
            inet_ntop(AF_INET6, const_cast<in6_addr*>(&ipv6->sin6_addr), buffer, sizeof(buffer));
#endif
            return SwString(buffer);
        }
        return SwString();
    }

    uint16_t socketAddressPort_(const sockaddr_storage& address) const {
        if (address.ss_family == AF_INET) {
            return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
        }
        if (address.ss_family == AF_INET6) {
            return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
        }
        return 0;
    }

    bool querySocketAddress_(bool peer, sockaddr_storage& address) const {
        if (!isSocketValid_()) {
            return false;
        }
        std::memset(&address, 0, sizeof(address));
#if defined(_WIN32)
        int length = sizeof(address);
        const int rc = peer ? ::getpeername(m_socket, reinterpret_cast<sockaddr*>(&address), &length)
                            : ::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length);
#else
        socklen_t length = sizeof(address);
        const int rc = peer ? ::getpeername(m_socket, reinterpret_cast<sockaddr*>(&address), &length)
                            : ::getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &length);
#endif
        return rc == 0;
    }

    bool registerDispatcher_() {
        unregisterDispatcher_();

#if defined(_WIN32)
        if (!m_event) {
            return false;
        }
#else
        if (!isSocketValid_()) {
            return false;
        }
#endif

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return false;
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        const std::weak_ptr<SocketLifetimeGuard> lifetime = m_lifetimeGuard;

#if defined(_WIN32)
        m_dispatchToken = app->ioDispatcher().watchHandleReliable(
            m_event,
            [affinity](std::function<void()> task) mutable -> bool {
                if (affinity && ThreadHandle::isLive(affinity) && ThreadHandle::currentThread() != affinity) {
                    std::function<void()> controlFallback = task;
                    if (affinity->postTaskOnLane(std::move(task), SwFiberLane::Input)) {
                        return true;
                    }
                    return affinity->postTaskOnLane(std::move(controlFallback), SwFiberLane::Control);
                }
                task();
                return true;
            },
            [this, lifetime]() {
                const std::shared_ptr<SocketLifetimeGuard> guard = lifetime.lock();
                if (!guard || !guard->alive.load(std::memory_order_acquire)) {
                    return;
                }
                checkSocketEvents_();
            });
#else
        m_dispatchToken = app->ioDispatcher().watchFdReliable(
            m_socket,
            desiredDispatcherEvents_(),
            [affinity](std::function<void()> task) mutable -> bool {
                if (affinity && ThreadHandle::isLive(affinity) && ThreadHandle::currentThread() != affinity) {
                    std::function<void()> controlFallback = task;
                    if (affinity->postTaskOnLane(std::move(task), SwFiberLane::Input)) {
                        return true;
                    }
                    return affinity->postTaskOnLane(std::move(controlFallback), SwFiberLane::Control);
                }
                task();
                return true;
            },
            [this, lifetime](uint32_t events) {
                const std::shared_ptr<SocketLifetimeGuard> guard = lifetime.lock();
                if (!guard || !guard->alive.load(std::memory_order_acquire)) {
                    return;
                }
                pollEvents_(events);
                if (guard->alive.load(std::memory_order_acquire)) {
                    updateDispatcherInterest_();
                }
            });
#endif
        return m_dispatchToken != 0;
    }

    void unregisterDispatcher_() {
        if (!m_dispatchToken) {
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->ioDispatcher().remove(m_dispatchToken);
        }
        m_dispatchToken = 0;
    }

#if defined(_WIN32)
    bool checkSocketEvents_() {
        if (!isSocketValid_() || m_event == NULL) {
            return true;
        }
        const std::uint64_t generation = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;

        WSANETWORKEVENTS networkEvents {};
        if (::WSAEnumNetworkEvents(m_socket, m_event, &networkEvents) == SOCKET_ERROR) {
            failAndClose_(WSAGetLastError());
            return false;
        }

        if (networkEvents.lNetworkEvents & FD_CONNECT) {
            if (networkEvents.iErrorCode[FD_CONNECT_BIT] != 0) {
                failAndClose_(networkEvents.iErrorCode[FD_CONNECT_BIT]);
                return false;
            }
            if (!handleTransportConnectedEvent_()) {
                if (guard->alive.load(std::memory_order_acquire) &&
                    m_transportGeneration == generation) {
                    closeNow_(true);
                }
                return false;
            }
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return false;
            }
        }

        if (networkEvents.lNetworkEvents & FD_READ) {
            if (networkEvents.iErrorCode[FD_READ_BIT] != 0) {
                failAndClose_(networkEvents.iErrorCode[FD_READ_BIT]);
                return false;
            }
            if (!handleTransportReadableEvent_()) {
                if (guard->alive.load(std::memory_order_acquire) &&
                    m_transportGeneration == generation) {
                    closeNow_(true);
                }
                return false;
            }
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return false;
            }
        }

        if (networkEvents.lNetworkEvents & FD_WRITE) {
            if (networkEvents.iErrorCode[FD_WRITE_BIT] != 0) {
                failAndClose_(networkEvents.iErrorCode[FD_WRITE_BIT]);
                return false;
            }
            if (!handleTransportWritableEvent_()) {
                if (guard->alive.load(std::memory_order_acquire) &&
                    m_transportGeneration == generation) {
                    closeNow_(true);
                }
                return false;
            }
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return false;
            }
        }

        if (networkEvents.lNetworkEvents & FD_CLOSE) {
            handleTransportCloseEvent_();
            if (guard->alive.load(std::memory_order_acquire) &&
                m_transportGeneration == generation) {
                closeIfRemoteClosedAndIdle_();
            }
        }

        return true;
    }

    void enableLinger_(int seconds) {
        linger lng {};
        lng.l_onoff = 1;
        lng.l_linger = static_cast<u_short>(seconds);
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lng), sizeof(lng));
    }

    void disableLinger_() {
        if (!isSocketValid_()) {
            return;
        }
        linger lng {};
        lng.l_onoff = 0;
        lng.l_linger = 0;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lng), sizeof(lng));
    }

    static void initializeWinsock_() {
        static std::once_flag once;
        std::call_once(once, []() {
            WSADATA wsaData {};
            (void)::WSAStartup(MAKEWORD(2, 2), &wsaData);
        });
    }
#else
    void pollEvents_(uint32_t events) {
        if (!isSocketValid_()) {
            return;
        }
        const std::uint64_t generation = m_transportGeneration;
        const std::shared_ptr<SocketLifetimeGuard> guard = m_lifetimeGuard;

        const bool readable = (events & SwIoDispatcher::Readable) != 0;
        const bool writable = (events & SwIoDispatcher::Writable) != 0;
        const bool failed = (events & (SwIoDispatcher::Error | SwIoDispatcher::Hangup)) != 0;

        if (m_connecting && (writable || failed)) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                m_connecting = false;
                if (!handleTransportConnectedEvent_()) {
                    if (guard->alive.load(std::memory_order_acquire) &&
                        m_transportGeneration == generation) {
                        closeNow_(true);
                    }
                    return;
                }
                if (!guard->alive.load(std::memory_order_acquire) ||
                    m_transportGeneration != generation) {
                    return;
                }
            } else {
                failAndClose_(err != 0 ? err : errno);
                return;
            }
        }

        if (readable) {
            if (!handleTransportReadableEvent_()) {
                if (guard->alive.load(std::memory_order_acquire) &&
                    m_transportGeneration == generation) {
                    closeNow_(true);
                }
                return;
            }
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return;
            }
        }

        if (writable) {
            if (!handleTransportWritableEvent_()) {
                if (guard->alive.load(std::memory_order_acquire) &&
                    m_transportGeneration == generation) {
                    closeNow_(true);
                }
                return;
            }
            if (!guard->alive.load(std::memory_order_acquire) ||
                m_transportGeneration != generation) {
                return;
            }
        }

        if (failed && !m_connecting) {
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0) {
                failAndClose_(err);
                return;
            }
            handleTransportCloseEvent_();
            if (guard->alive.load(std::memory_order_acquire) &&
                m_transportGeneration == generation) {
                closeIfRemoteClosedAndIdle_();
            }
        }
    }

    bool setNonBlockingAndCloseOnExec_(int fd) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
            return false;
        }
        const int descriptorFlags = ::fcntl(fd, F_GETFD, 0);
        if (descriptorFlags < 0 || ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) != 0) {
            return false;
        }
        return true;
    }

    void enableLinger_(int seconds) {
        linger lng {};
        lng.l_onoff = 1;
        lng.l_linger = seconds;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
    }

    void disableLinger_() {
        if (!isSocketValid_()) {
            return;
        }
        linger lng {};
        lng.l_onoff = 0;
        lng.l_linger = 0;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
    }
#endif
};
