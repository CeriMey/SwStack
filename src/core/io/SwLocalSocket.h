#pragma once

/**
 * @file src/core/io/SwLocalSocket.h
 * @ingroup core_io
 * @brief Local IPC stream socket: Windows named pipe / Unix domain socket behind one portable API.
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
#include "SwByteRingBuffer.h"
#include "SwCoreApplication.h"
#include "SwDebug.h"
#include "SwEventLoop.h"
#include "SwTimer.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

static constexpr const char* kSwLogCategory_SwLocalSocket = "sw.core.io.swlocalsocket";

static constexpr std::size_t kSwLocalDefaultReadChunkSize = 16 * 1024;
static constexpr std::size_t kSwLocalDefaultWriteChunkSize = 64 * 1024;
static constexpr std::size_t kSwLocalDefaultWriteHighWatermark = 8u * 1024u * 1024u;
static constexpr std::size_t kSwLocalDefaultWriteLowWatermark = 4u * 1024u * 1024u;
static constexpr int kSwLocalDefaultConnectTimeoutMs = 30000;
static constexpr int kSwLocalConnectRetryIntervalMs = 50;

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
using SwNativeLocalHandle = HANDLE;
static const SwNativeLocalHandle kSwInvalidLocalHandle = INVALID_HANDLE_VALUE;
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
using SwNativeLocalHandle = int;
static const SwNativeLocalHandle kSwInvalidLocalHandle = -1;
#endif

/**
 * @class SwLocalSocket
 * @brief Stream socket for same-machine IPC, portable over named pipes (Windows) and
 *        AF_UNIX stream sockets (POSIX).
 *
 * The API mirrors the stream-socket idiom of SwTcpSocket (states, connected/disconnected/
 * errorOccurred/writeFinished signals, buffered writes with watermarks) with two local-only
 * additions: connectToServer(name) resolves a short server name to the platform endpoint
 * (`\\.\pipe\<name>` / `/tmp/<name>`, absolute paths honored as-is), and peerCredentials()
 * exposes the identity of the process on the other end of the connection (pid on Windows,
 * pid/uid/gid on Linux via SO_PEERCRED) — the load-bearing primitive for local authorization.
 *
 * Reads are buffered internally (SwSerial idiom): readyRead() means bytes are already in the
 * buffer, and buffered bytes stay readable after a disconnect. Error codes carried by
 * errorOccurred(int) are native (GetLastError() / errno).
 */
class SwLocalSocket : public SwAbstractSocket {
    SW_OBJECT(SwLocalSocket, SwAbstractSocket)

public:
    /** Result of a buffered write attempt (mirror of the SwTcpSocket contract). */
    enum class WriteResult {
        Accepted,
        WouldBlock,
        TooLarge,
        NotConnected,
        WriteClosed,
        Error
    };

    /** Identity of the peer process. Fields are -1 when unknown on the platform. */
    struct PeerCredentials {
        std::int64_t pid = -1;
        std::int64_t uid = -1;
        std::int64_t gid = -1;
    };

    explicit SwLocalSocket(SwObject* parent = nullptr)
        : SwAbstractSocket(parent)
    {
        m_connectRetryTimer = new SwTimer(this);
        m_connectRetryTimer->setSingleShot(true);
        connect(m_connectRetryTimer, &SwTimer::timeout, this, [this]() {
            onConnectRetry_();
        });

        m_connectDeadlineTimer = new SwTimer(this);
        m_connectDeadlineTimer->setSingleShot(true);
        connect(m_connectDeadlineTimer, &SwTimer::timeout, this, [this]() {
            onConnectDeadline_();
        });
    }

    ~SwLocalSocket() override {
        m_lifetimeGuard->alive = false;
        closeNow_(false);
    }

    /**
     * @brief Connects to a local server by name.
     *
     * A plain name maps to `\\.\pipe\<name>` on Windows and `/tmp/<name>` on POSIX; a name
     * starting with `\\` (Windows) or `/` (POSIX) is used verbatim as the endpoint path.
     * Connection progress is reported through connected()/errorOccurred(int); a busy server
     * (ERROR_PIPE_BUSY / EAGAIN) is retried until the connect timeout elapses.
     */
    bool connectToServer(const SwString& name) {
        std::shared_ptr<LifetimeGuard_> guard = m_lifetimeGuard;
        closeNow_(true);
        if (!guard->alive) {
            return false;
        }

        m_serverName = name;
        m_fullServerName = fullNameForServerName(name);
        if (m_fullServerName.isEmpty()) {
            swCError(kSwLogCategory_SwLocalSocket) << "[SwLocalSocket] invalid server name";
            return false;
        }

        setState(ConnectingState);
        if (m_connectTimeoutMs >= 0) {
            m_connectDeadlineTimer->start(m_connectTimeoutMs);
        }
        return attemptConnect_();
    }

    /**
     * @brief SwAbstractSocket contract adapter: the host is the local server name, the port is
     *        meaningless for a local endpoint and is ignored.
     */
    bool connectToHost(const SwString& host, uint16_t port) override {
        (void)port;
        return connectToServer(host);
    }

    /** Qt-parity alias for close(). */
    void disconnectFromServer() {
        close();
    }

    /** Closes immediately, discarding any buffered outbound bytes. */
    void abort() {
        closeNow_(true);
    }

    /** Cancels an in-progress connection attempt without emitting errorOccurred. */
    void cancelConnect() {
        if (state() == ConnectingState) {
            closeNow_(false);
        }
    }

    SwString serverName() const { return m_serverName; }
    SwString fullServerName() const { return m_fullServerName; }

    void setConnectTimeout(int msecs) { m_connectTimeoutMs = msecs; }

    void setWriteBufferWatermarks(std::size_t highWatermark, std::size_t lowWatermark) {
        if (highWatermark == 0 || lowWatermark > highWatermark) {
            return;
        }
        m_writeHighWatermark = highWatermark;
        m_writeLowWatermark = lowWatermark;
    }

    /**
     * @brief Returns the identity of the peer process, when the platform can attest it.
     *
     * POSIX fills pid/uid/gid from SO_PEERCRED; Windows fills pid from
     * GetNamedPipeClientProcessId (server side) or GetNamedPipeServerProcessId (client side).
     */
    PeerCredentials peerCredentials() const {
        PeerCredentials credentials;
#if defined(_WIN32)
        if (m_handle != kSwInvalidLocalHandle) {
            ULONG pid = 0;
            const BOOL ok = m_serverSide ? GetNamedPipeClientProcessId(m_handle, &pid)
                                         : GetNamedPipeServerProcessId(m_handle, &pid);
            if (ok) {
                credentials.pid = static_cast<std::int64_t>(pid);
            }
        }
#else
#if defined(SO_PEERCRED)
        if (m_fd != kSwInvalidLocalHandle) {
            struct ucred peer;
            std::memset(&peer, 0, sizeof(peer));
            socklen_t length = sizeof(peer);
            if (::getsockopt(m_fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) == 0) {
                credentials.pid = static_cast<std::int64_t>(peer.pid);
                credentials.uid = static_cast<std::int64_t>(peer.uid);
                credentials.gid = static_cast<std::int64_t>(peer.gid);
            }
        }
#endif
#endif
        return credentials;
    }

    /**
     * @brief Adopts an already-connected native endpoint (used by SwLocalServer for accepted
     *        connections). The socket takes ownership of the handle.
     */
    bool adoptHandle(SwNativeLocalHandle handle, bool serverSide = false, bool emitConnectedSignal = true) {
        closeNow_(false);
        if (handle == kSwInvalidLocalHandle) {
            return false;
        }

        m_serverSide = serverSide;
#if defined(_WIN32)
        m_handle = handle;
        if (!setupWindowsEvents_() || !registerDispatcher_()) {
            closeNow_(false);
            return false;
        }
        setState(ConnectedState);
        if (!armRead_()) {
            return false;
        }
#else
        m_fd = handle;
        setNonBlockingAndCloseOnExec_(m_fd);
        if (!registerDispatcher_()) {
            closeNow_(false);
            return false;
        }
        setState(ConnectedState);
        updateDispatcherInterest_();
#endif
        if (emitConnectedSignal) {
            emit connected();
        }
        return true;
    }

    /**
     * @brief Requests a close. Buffered outbound bytes are flushed first (ClosingState), then
     *        the transport is torn down and disconnected() is emitted.
     */
    void close() override {
        if (state() == UnconnectedState) {
            return;
        }
        if (state() == ConnectedState && hasPendingWrites()) {
            m_closeRequested = true;
            setState(ClosingState);
            flushWrites_();
            return;
        }
        closeNow_(true);
    }

    SwByteArray read(int64_t maxSize = 0) override {
        std::lock_guard<std::mutex> lock(m_readMutex);
        const std::size_t available = m_readBuffer.size();
        if (available == 0) {
            return SwByteArray();
        }
        std::size_t toRead = available;
        if (maxSize > 0 && static_cast<std::size_t>(maxSize) < toRead) {
            toRead = static_cast<std::size_t>(maxSize);
        }
        return m_readBuffer.read(toRead);
    }

    int64_t readInto(char* data, int64_t maxSize) override {
        if (!data || maxSize <= 0) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_readMutex);
        return static_cast<int64_t>(m_readBuffer.readInto(data, static_cast<std::size_t>(maxSize)));
    }

    /** Number of bytes already received and readable without blocking. */
    std::size_t bytesAvailable() const {
        std::lock_guard<std::mutex> lock(m_readMutex);
        return m_readBuffer.size();
    }

    bool write(const SwString& data) override {
        return tryWrite(data.data(), data.size()) == WriteResult::Accepted;
    }

    bool write(const SwByteArray& data) override {
        return tryWrite(data.constData(), data.size()) == WriteResult::Accepted;
    }

    bool write(const char* data, std::size_t size) override {
        if (size == 0) {
            return true;
        }
        if (!data) {
            return false;
        }
        return tryWrite(data, size) == WriteResult::Accepted;
    }

    /**
     * @brief Queues bytes for sending with explicit backpressure reporting.
     *
     * Accepted bytes are owned by the internal ring buffer and flushed as the endpoint drains;
     * writeFinished() fires when the buffer empties, readyWrite() fires when a backpressured
     * buffer falls back under the low watermark.
     */
    WriteResult tryWrite(const char* data, std::size_t size) {
        if (size == 0) {
            return WriteResult::Accepted;
        }
        if (!data) {
            return WriteResult::Error;
        }
        if (state() != ConnectedState) {
            return WriteResult::NotConnected;
        }
        if (m_closeRequested) {
            return WriteResult::WriteClosed;
        }
        if (size > m_writeHighWatermark) {
            return WriteResult::TooLarge;
        }
        if (m_writeBuffer.size() + size > m_writeHighWatermark) {
            m_writeBackpressured = true;
            return WriteResult::WouldBlock;
        }

        m_writeBuffer.append(data, size);
        flushWrites_();
        return WriteResult::Accepted;
    }

    std::size_t bytesToWrite() const {
#if defined(_WIN32)
        return m_writeBuffer.size() + (m_writeStage.size() - m_writeStageOffset);
#else
        return m_writeBuffer.size();
#endif
    }

    bool isRemoteClosed() const override { return m_remoteClosed; }

    bool hasPendingWrites() const override {
#if defined(_WIN32)
        return !m_writeBuffer.isEmpty() || m_writePending;
#else
        return !m_writeBuffer.isEmpty();
#endif
    }

    bool waitForConnected(int msecs = 30000) override {
        if (state() == ConnectedState) {
            return true;
        }
        if (state() == UnconnectedState) {
            return false;
        }
        bool success = false;
        SwEventLoop loop;
        SwTimer timeoutTimer;
        connect(this, &SwLocalSocket::connected, &loop, [&loop, &success]() {
            success = true;
            loop.quit();
        });
        connect(this, &SwLocalSocket::disconnected, &loop, [&loop]() { loop.quit(); });
        connect(this, &SwLocalSocket::errorOccurred, &loop, [&loop](int) { loop.quit(); });
        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&loop]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }
        loop.exec();
        return success;
    }

    bool waitForBytesWritten(int msecs = 30000) override {
        if (!hasPendingWrites()) {
            return true;
        }
        bool success = false;
        SwEventLoop loop;
        SwTimer timeoutTimer;
        connect(this, &SwLocalSocket::writeFinished, &loop, [&loop, &success]() {
            success = true;
            loop.quit();
        });
        connect(this, &SwLocalSocket::disconnected, &loop, [&loop]() { loop.quit(); });
        connect(this, &SwLocalSocket::errorOccurred, &loop, [&loop](int) { loop.quit(); });
        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&loop]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }
        loop.exec();
        return success;
    }

    /** Blocks the caller's event loop until readyRead() fires or the timeout elapses. */
    bool waitForReadyRead(int msecs = 30000) {
        if (bytesAvailable() > 0) {
            return true;
        }
        bool success = false;
        SwEventLoop loop;
        SwTimer timeoutTimer;
        connect(this, &SwLocalSocket::readyRead, &loop, [&loop, &success]() {
            success = true;
            loop.quit();
        });
        connect(this, &SwLocalSocket::disconnected, &loop, [&loop]() { loop.quit(); });
        connect(this, &SwLocalSocket::errorOccurred, &loop, [&loop](int) { loop.quit(); });
        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&loop]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }
        loop.exec();
        return success;
    }

    bool waitForDisconnected(int msecs = 30000) {
        if (state() == UnconnectedState) {
            return true;
        }
        bool success = false;
        SwEventLoop loop;
        SwTimer timeoutTimer;
        connect(this, &SwLocalSocket::disconnected, &loop, [&loop, &success]() {
            success = true;
            loop.quit();
        });
        if (msecs >= 0) {
            timeoutTimer.setSingleShot(true);
            connect(&timeoutTimer, &SwTimer::timeout, &loop, [&loop]() { loop.quit(); });
            timeoutTimer.start(msecs);
        }
        loop.exec();
        return success;
    }

    /** Maps a short server name to the platform endpoint path. */
    static SwString fullNameForServerName(const SwString& name) {
        const std::string plain = name.toStdString();
        if (plain.empty()) {
            return SwString();
        }
#if defined(_WIN32)
        if (plain.size() >= 2 && plain[0] == '\\' && plain[1] == '\\') {
            return name;
        }
        return SwString(std::string("\\\\.\\pipe\\") + plain);
#else
        if (plain[0] == '/') {
            return name;
        }
        return SwString(std::string("/tmp/") + plain);
#endif
    }

private:
    struct LifetimeGuard_ {
        std::atomic<bool> alive{true};
    };

    bool attemptConnect_() {
#if defined(_WIN32)
        const std::wstring widePath = m_fullServerName.toStdWString();
        HANDLE handle = CreateFileW(widePath.c_str(),
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_OVERLAPPED,
                                    nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            m_handle = handle;
            m_serverSide = false;
            if (!setupWindowsEvents_() || !registerDispatcher_()) {
                failAndClose_(static_cast<int>(GetLastError()));
                return false;
            }
            m_connectRetryTimer->stop();
            m_connectDeadlineTimer->stop();
            setState(ConnectedState);
            if (!armRead_()) {
                return false;
            }
            emit connected();
            return true;
        }

        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            m_connectRetryTimer->start(kSwLocalConnectRetryIntervalMs);
            return true;
        }
        failConnect_(static_cast<int>(error));
        return false;
#else
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            failConnect_(errno);
            return false;
        }
        setNonBlockingAndCloseOnExec_(fd);

        struct sockaddr_un address;
        std::memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        const std::string path = m_fullServerName.toStdString();
        if (path.size() >= sizeof(address.sun_path)) {
            ::close(fd);
            failConnect_(ENAMETOOLONG);
            return false;
        }
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

        const int result = ::connect(fd, reinterpret_cast<struct sockaddr*>(&address),
                                     static_cast<socklen_t>(sizeof(address)));
        if (result == 0) {
            m_fd = fd;
            m_serverSide = false;
            if (!registerDispatcher_()) {
                failConnect_(EINVAL);
                return false;
            }
            m_connectRetryTimer->stop();
            m_connectDeadlineTimer->stop();
            setState(ConnectedState);
            updateDispatcherInterest_();
            emit connected();
            return true;
        }

        const int error = errno;
        if (error == EINPROGRESS) {
            m_fd = fd;
            m_serverSide = false;
            m_connecting = true;
            if (!registerDispatcher_()) {
                failConnect_(EINVAL);
                return false;
            }
            updateDispatcherInterest_();
            return true;
        }
        ::close(fd);
        if (error == EAGAIN || error == ECONNREFUSED || error == ENOENT) {
            // Busy backlog or server not (yet) there: retry until the connect deadline —
            // the local mirror of the ERROR_PIPE_BUSY loop on Windows. ECONNREFUSED/ENOENT
            // retries make connect-then-listen races survivable for supervised daemons.
            m_connectRetryTimer->start(kSwLocalConnectRetryIntervalMs);
            return true;
        }
        failConnect_(error);
        return false;
#endif
    }

    void onConnectRetry_() {
        if (state() != ConnectingState) {
            return;
        }
        attemptConnect_();
    }

    void onConnectDeadline_() {
        if (state() != ConnectingState) {
            return;
        }
#if defined(_WIN32)
        failConnect_(static_cast<int>(ERROR_SEM_TIMEOUT));
#else
        failConnect_(ETIMEDOUT);
#endif
    }

    void failConnect_(int errorCode) {
        closeNow_(false);
        emit errorOccurred(errorCode);
    }

    void failAndClose_(int errorCode) {
        const bool wasConnected = (state() == ConnectedState || state() == ClosingState);
        std::shared_ptr<LifetimeGuard_> guard = m_lifetimeGuard;
        closeNow_(false);
        if (!guard->alive) {
            return;
        }
        emit errorOccurred(errorCode);
        if (wasConnected && guard->alive && SwObject::isLive(this)) {
            emit disconnected();
        }
    }

    void handleRemoteClosed_() {
        m_remoteClosed = true;
        closeNow_(true);
    }

    // Tears the transport down. Buffered *received* bytes stay readable on purpose; the
    // disconnected() emission is the last access to the object (a direct slot may destroy it).
    void closeNow_(bool notifyDisconnected) {
        const bool wasUp = (state() == ConnectedState || state() == ClosingState);
        ++m_transportGeneration;
        m_connectRetryTimer->stop();
        m_connectDeadlineTimer->stop();
        unregisterDispatcher_();
#if defined(_WIN32)
        if (m_handle != kSwInvalidLocalHandle) {
            CancelIoEx(m_handle, nullptr);
        }
        if (m_readEvent) {
            CloseHandle(m_readEvent);
            m_readEvent = nullptr;
        }
        if (m_writeEvent) {
            CloseHandle(m_writeEvent);
            m_writeEvent = nullptr;
        }
        if (m_handle != kSwInvalidLocalHandle) {
            CloseHandle(m_handle);
            m_handle = kSwInvalidLocalHandle;
        }
        m_readPending = false;
        m_writePending = false;
        m_writeStage.clear();
        m_writeStageOffset = 0;
#else
        if (m_fd != kSwInvalidLocalHandle) {
            ::close(m_fd);
            m_fd = kSwInvalidLocalHandle;
        }
#endif
        m_writeBuffer.clear();
        m_connecting = false;
        m_closeRequested = false;
        m_writeBackpressured = false;
        setState(UnconnectedState);
        if (notifyDisconnected && wasUp) {
            emit disconnected();
        }
    }

    bool registerDispatcher_() {
        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return false;
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }
        const SwIoDispatcher::ReliableAffinityPoster poster =
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
            };

        std::weak_ptr<LifetimeGuard_> weakGuard = m_lifetimeGuard;
#if defined(_WIN32)
        if (!m_readEvent || !m_writeEvent) {
            return false;
        }
        m_readToken = app->ioDispatcher().watchHandleReliable(
            m_readEvent,
            poster,
            [this, weakGuard]() {
                std::shared_ptr<LifetimeGuard_> guard = weakGuard.lock();
                if (!guard || !guard->alive) {
                    return;
                }
                onReadEvent_();
            });
        m_writeToken = app->ioDispatcher().watchHandleReliable(
            m_writeEvent,
            poster,
            [this, weakGuard]() {
                std::shared_ptr<LifetimeGuard_> guard = weakGuard.lock();
                if (!guard || !guard->alive) {
                    return;
                }
                onWriteEvent_();
            });
        return m_readToken != 0 && m_writeToken != 0;
#else
        m_fdToken = app->ioDispatcher().watchFdReliable(
            m_fd,
            SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup,
            poster,
            [this, weakGuard](uint32_t events) {
                std::shared_ptr<LifetimeGuard_> guard = weakGuard.lock();
                if (!guard || !guard->alive) {
                    return;
                }
                onFdEvents_(events);
            });
        m_currentFdMask = SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup;
        return m_fdToken != 0;
#endif
    }

    void unregisterDispatcher_() {
        SwCoreApplication* app = SwCoreApplication::instance(false);
#if defined(_WIN32)
        if (m_readToken) {
            if (app) {
                app->ioDispatcher().remove(m_readToken);
            }
            m_readToken = 0;
        }
        if (m_writeToken) {
            if (app) {
                app->ioDispatcher().remove(m_writeToken);
            }
            m_writeToken = 0;
        }
#else
        if (m_fdToken) {
            if (app) {
                app->ioDispatcher().remove(m_fdToken);
            }
            m_fdToken = 0;
            m_currentFdMask = 0;
        }
#endif
    }

    void flushWrites_() {
#if defined(_WIN32)
        kickWrite_();
#else
        flushPosixWrites_();
#endif
    }

    void notifyDrainProgress_() {
        if (m_writeBackpressured && m_writeBuffer.size() <= m_writeLowWatermark) {
            m_writeBackpressured = false;
            emit readyWrite();
        }
    }

    void notifyDrainedIfIdle_() {
        if (!hasPendingWrites()) {
            std::shared_ptr<LifetimeGuard_> guard = m_lifetimeGuard;
            const std::uint64_t generation = m_transportGeneration;
            emit writeFinished();
            if (!guard->alive || generation != m_transportGeneration) {
                return;
            }
            if (m_closeRequested) {
                closeNow_(true);
            }
        }
    }

#if defined(_WIN32)
    bool setupWindowsEvents_() {
        m_readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        m_writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return m_readEvent != nullptr && m_writeEvent != nullptr;
    }

    bool armRead_() {
        if (m_handle == kSwInvalidLocalHandle || !m_readEvent || m_readPending) {
            return true;
        }
        ResetEvent(m_readEvent);
        std::memset(&m_readOverlapped, 0, sizeof(m_readOverlapped));
        m_readOverlapped.hEvent = m_readEvent;
        const BOOL ok = ReadFile(m_handle, m_readChunk, static_cast<DWORD>(sizeof(m_readChunk)),
                                 nullptr, &m_readOverlapped);
        if (ok) {
            m_readPending = true;
            return true;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            m_readPending = true;
            return true;
        }
        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
            handleRemoteClosed_();
            return false;
        }
        failAndClose_(static_cast<int>(error));
        return false;
    }

    void onReadEvent_() {
        if (!m_readPending || m_handle == kSwInvalidLocalHandle) {
            return;
        }
        DWORD transferred = 0;
        if (!GetOverlappedResult(m_handle, &m_readOverlapped, &transferred, FALSE)) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_INCOMPLETE) {
                return;
            }
            m_readPending = false;
            if (error == ERROR_OPERATION_ABORTED) {
                return;
            }
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                handleRemoteClosed_();
                return;
            }
            failAndClose_(static_cast<int>(error));
            return;
        }

        m_readPending = false;
        ResetEvent(m_readEvent);
        if (transferred > 0) {
            {
                std::lock_guard<std::mutex> lock(m_readMutex);
                m_readBuffer.append(m_readChunk, static_cast<std::size_t>(transferred));
            }
            std::shared_ptr<LifetimeGuard_> guard = m_lifetimeGuard;
            const std::uint64_t generation = m_transportGeneration;
            emit readyRead();
            if (!guard->alive || generation != m_transportGeneration) {
                return;
            }
        }
        armRead_();
    }

    void kickWrite_() {
        if (m_writePending || m_handle == kSwInvalidLocalHandle || !m_writeEvent) {
            return;
        }
        if (m_writeStage.size() == m_writeStageOffset) {
            if (m_writeBuffer.isEmpty()) {
                notifyDrainedIfIdle_();
                return;
            }
            std::size_t chunk = m_writeBuffer.contiguousSize();
            if (chunk > kSwLocalDefaultWriteChunkSize) {
                chunk = kSwLocalDefaultWriteChunkSize;
            }
            m_writeStage.assign(m_writeBuffer.contiguousData(), m_writeBuffer.contiguousData() + chunk);
            m_writeStageOffset = 0;
            m_writeBuffer.consume(chunk);
            notifyDrainProgress_();
        }

        ResetEvent(m_writeEvent);
        std::memset(&m_writeOverlapped, 0, sizeof(m_writeOverlapped));
        m_writeOverlapped.hEvent = m_writeEvent;
        const DWORD toWrite = static_cast<DWORD>(m_writeStage.size() - m_writeStageOffset);
        const BOOL ok = WriteFile(m_handle, m_writeStage.data() + m_writeStageOffset, toWrite,
                                  nullptr, &m_writeOverlapped);
        if (ok || GetLastError() == ERROR_IO_PENDING) {
            m_writePending = true;
            return;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA) {
            handleRemoteClosed_();
            return;
        }
        failAndClose_(static_cast<int>(error));
    }

    void onWriteEvent_() {
        if (!m_writePending || m_handle == kSwInvalidLocalHandle) {
            return;
        }
        DWORD transferred = 0;
        if (!GetOverlappedResult(m_handle, &m_writeOverlapped, &transferred, FALSE)) {
            const DWORD error = GetLastError();
            if (error == ERROR_IO_INCOMPLETE) {
                return;
            }
            m_writePending = false;
            if (error == ERROR_OPERATION_ABORTED) {
                return;
            }
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_NO_DATA) {
                handleRemoteClosed_();
                return;
            }
            failAndClose_(static_cast<int>(error));
            return;
        }

        m_writePending = false;
        ResetEvent(m_writeEvent);
        m_writeStageOffset += static_cast<std::size_t>(transferred);
        if (m_writeStageOffset >= m_writeStage.size()) {
            m_writeStage.clear();
            m_writeStageOffset = 0;
        }
        kickWrite_();
    }
#else
    void onFdEvents_(uint32_t events) {
        if (m_fd == kSwInvalidLocalHandle) {
            return;
        }
        if (m_connecting) {
            if (events & (SwIoDispatcher::Writable | SwIoDispatcher::Error | SwIoDispatcher::Hangup)) {
                finishPosixConnect_();
            }
            return;
        }
        if (events & SwIoDispatcher::Readable) {
            drainPosixReads_();
            if (m_fd == kSwInvalidLocalHandle) {
                return;
            }
        }
        if (events & SwIoDispatcher::Writable) {
            flushPosixWrites_();
            if (m_fd == kSwInvalidLocalHandle) {
                return;
            }
        }
        if (events & (SwIoDispatcher::Error | SwIoDispatcher::Hangup)) {
            // Hangup with no pending readable bytes: confirm EOF through the read path so
            // buffered bytes are delivered before disconnected().
            drainPosixReads_();
        }
    }

    void finishPosixConnect_() {
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0) {
            socketError = errno;
        }
        m_connecting = false;
        if (socketError != 0) {
            failConnect_(socketError);
            return;
        }
        m_connectRetryTimer->stop();
        m_connectDeadlineTimer->stop();
        setState(ConnectedState);
        updateDispatcherInterest_();
        emit connected();
    }

    void drainPosixReads_() {
        bool sawEof = false;
        bool appended = false;
        char chunk[kSwLocalDefaultReadChunkSize];
        while (m_fd != kSwInvalidLocalHandle) {
            const ssize_t received = ::recv(m_fd, chunk, sizeof(chunk), 0);
            if (received > 0) {
                std::lock_guard<std::mutex> lock(m_readMutex);
                m_readBuffer.append(chunk, static_cast<std::size_t>(received));
                appended = true;
                continue;
            }
            if (received == 0) {
                sawEof = true;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            failAndClose_(errno);
            return;
        }

        if (appended) {
            std::shared_ptr<LifetimeGuard_> guard = m_lifetimeGuard;
            const std::uint64_t generation = m_transportGeneration;
            emit readyRead();
            if (!guard->alive || generation != m_transportGeneration) {
                return;
            }
        }
        if (sawEof) {
            handleRemoteClosed_();
        }
    }

    void flushPosixWrites_() {
        while (m_fd != kSwInvalidLocalHandle && !m_writeBuffer.isEmpty()) {
#if defined(MSG_NOSIGNAL)
            const ssize_t sent = ::send(m_fd, m_writeBuffer.contiguousData(),
                                        m_writeBuffer.contiguousSize(), MSG_NOSIGNAL);
#else
            const ssize_t sent = ::send(m_fd, m_writeBuffer.contiguousData(),
                                        m_writeBuffer.contiguousSize(), 0);
#endif
            if (sent > 0) {
                m_writeBuffer.consume(static_cast<std::size_t>(sent));
                notifyDrainProgress_();
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                handleRemoteClosed_();
                return;
            }
            failAndClose_(errno);
            return;
        }
        updateDispatcherInterest_();
        notifyDrainedIfIdle_();
    }

    void updateDispatcherInterest_() {
        if (!m_fdToken) {
            return;
        }
        uint32_t mask = SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup;
        if (m_connecting || !m_writeBuffer.isEmpty()) {
            mask |= SwIoDispatcher::Writable;
        }
        if (mask == m_currentFdMask) {
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            if (app->ioDispatcher().updateFd(m_fdToken, mask)) {
                m_currentFdMask = mask;
            }
        }
    }

    static void setNonBlockingAndCloseOnExec_(int fd) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
        const int fdFlags = ::fcntl(fd, F_GETFD, 0);
        if (fdFlags >= 0) {
            ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC);
        }
    }
#endif

    SwString m_serverName;
    SwString m_fullServerName;
    int m_connectTimeoutMs = kSwLocalDefaultConnectTimeoutMs;
    bool m_serverSide = false;
    bool m_connecting = false;
    bool m_closeRequested = false;
    bool m_remoteClosed = false;
    bool m_writeBackpressured = false;
    std::size_t m_writeHighWatermark = kSwLocalDefaultWriteHighWatermark;
    std::size_t m_writeLowWatermark = kSwLocalDefaultWriteLowWatermark;
    std::uint64_t m_transportGeneration = 0;
    std::shared_ptr<LifetimeGuard_> m_lifetimeGuard = std::make_shared<LifetimeGuard_>();

    mutable std::mutex m_readMutex;
    SwByteRingBuffer m_readBuffer;
    SwByteRingBuffer m_writeBuffer;

    SwTimer* m_connectRetryTimer = nullptr;
    SwTimer* m_connectDeadlineTimer = nullptr;

#if defined(_WIN32)
    SwNativeLocalHandle m_handle = kSwInvalidLocalHandle;
    HANDLE m_readEvent = nullptr;
    HANDLE m_writeEvent = nullptr;
    OVERLAPPED m_readOverlapped{};
    OVERLAPPED m_writeOverlapped{};
    bool m_readPending = false;
    bool m_writePending = false;
    char m_readChunk[kSwLocalDefaultReadChunkSize];
    std::vector<char> m_writeStage;
    std::size_t m_writeStageOffset = 0;
    SwIoDispatcher::Token m_readToken = 0;
    SwIoDispatcher::Token m_writeToken = 0;
#else
    SwNativeLocalHandle m_fd = kSwInvalidLocalHandle;
    SwIoDispatcher::Token m_fdToken = 0;
    uint32_t m_currentFdMask = 0;
#endif

    friend class SwLocalServer;
};
