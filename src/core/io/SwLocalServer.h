#pragma once

/**
 * @file src/core/io/SwLocalServer.h
 * @ingroup core_io
 * @brief Local IPC stream server: accepts SwLocalSocket connections over a named pipe
 *        (Windows) or a Unix domain socket (POSIX).
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

#include "SwDebug.h"
#include "SwDequeue.h"
#include "SwLocalSocket.h"

#if defined(_WIN32)
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")
#endif

static constexpr const char* kSwLogCategory_SwLocalServer = "sw.core.io.swlocalserver";

static constexpr int kSwLocalServerDefaultAcceptBudget = 64;
static constexpr int kSwLocalServerDefaultMaxPendingConnections = 1024;
static constexpr std::size_t kSwLocalServerPipeBufferBytes = 64 * 1024;

/**
 * @class SwLocalServer
 * @brief Accepts local IPC stream connections and hands them out as SwLocalSocket instances.
 *
 * The server mirrors the SwTcpServer contract: listen(name), newConnection() and a pending
 * queue drained by nextPendingConnection(). The listen name follows the SwLocalSocket mapping
 * (`\\.\pipe\<name>` / `/tmp/<name>`, absolute paths verbatim). Access control is declarative:
 * setSocketOptions() maps to chmod bits on the POSIX socket file and to a matching DACL on the
 * Windows pipe; setSecurityDescriptorSddl() overrides the Windows DACL with an explicit SDDL
 * string for servers that need a precise ACL.
 */
class SwLocalServer : public SwObject {
    SW_OBJECT(SwLocalServer, SwObject)

public:
    /** Who may connect (POSIX: chmod bits on the socket file; Windows: pipe DACL). */
    enum SocketOption : uint32_t {
        NoOptions = 0x0,
        UserAccessOption = 0x1,
        GroupAccessOption = 0x2,
        OtherAccessOption = 0x4,
        WorldAccessOption = 0x7
    };

    explicit SwLocalServer(SwObject* parent = nullptr)
        : SwObject(parent)
    {
    }

    ~SwLocalServer() override {
        close();
    }

    /**
     * @brief Starts listening on the given local server name.
     *
     * Fails (with a log) when the endpoint already exists and is owned by someone else —
     * call removeServer() first to reclaim a stale POSIX socket file.
     */
    bool listen(const SwString& name) {
        if (isListening()) {
            if (name == m_serverName) {
                return true;
            }
            close();
        }

        m_serverName = name;
        m_fullServerName = SwLocalSocket::fullNameForServerName(name);
        if (m_fullServerName.isEmpty()) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] invalid server name";
            return false;
        }

#if defined(_WIN32)
        m_connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_connectEvent) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] CreateEventW failed: "
                                                   << GetLastError();
            close();
            return false;
        }
        if (!createInstanceAndArm_(true)) {
            close();
            return false;
        }
        if (!registerDispatcher_()) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] dispatcher registration failed";
            close();
            return false;
        }
#else
        m_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_listenFd < 0) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] socket failed: "
                                                   << std::strerror(errno);
            return false;
        }
        setNonBlockingAndCloseOnExec_(m_listenFd);

        struct sockaddr_un address;
        std::memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        const std::string path = m_fullServerName.toStdString();
        if (path.size() >= sizeof(address.sun_path)) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] socket path too long";
            close();
            return false;
        }
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

        if (::bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&address),
                   static_cast<socklen_t>(sizeof(address))) != 0) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] bind failed on " << path
                                                   << ": " << std::strerror(errno);
            close();
            return false;
        }
        m_boundPath = path;

        if (m_socketOptions != NoOptions) {
            ::chmod(path.c_str(), accessModeForOptions_(m_socketOptions));
        }

        if (::listen(m_listenFd, SOMAXCONN) != 0) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] listen failed: "
                                                   << std::strerror(errno);
            close();
            return false;
        }
        if (!registerDispatcher_()) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] dispatcher registration failed";
            close();
            return false;
        }
#endif
        m_listening = true;
        return true;
    }

    /** Stops listening and deletes every connection still waiting in the pending queue. */
    void close() {
        m_listening = false;
        unregisterDispatcher_();
#if defined(_WIN32)
        if (m_pipeInstance != INVALID_HANDLE_VALUE) {
            CancelIoEx(m_pipeInstance, nullptr);
            CloseHandle(m_pipeInstance);
            m_pipeInstance = INVALID_HANDLE_VALUE;
        }
        if (m_connectEvent) {
            CloseHandle(m_connectEvent);
            m_connectEvent = nullptr;
        }
        m_connectPending = false;
        m_connectSatisfied = false;
        freeSecurityDescriptor_();
#else
        if (m_listenFd >= 0) {
            ::close(m_listenFd);
            m_listenFd = -1;
        }
        if (!m_boundPath.empty()) {
            ::unlink(m_boundPath.c_str());
            m_boundPath.clear();
        }
#endif
        while (!m_pendingConnections.isEmpty()) {
            SwLocalSocket* socket = m_pendingConnections.takeFirst();
            delete socket;
        }
    }

    bool isListening() const { return m_listening; }
    SwString serverName() const { return m_serverName; }
    SwString fullServerName() const { return m_fullServerName; }

    /** Access policy applied at the next listen(); has no effect on a live endpoint. */
    void setSocketOptions(uint32_t options) { m_socketOptions = options; }
    uint32_t socketOptions() const { return m_socketOptions; }

#if defined(_WIN32)
    /**
     * @brief Explicit SDDL DACL for the pipe (overrides setSocketOptions), applied at the next
     *        listen(). Example: "D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)".
     */
    void setSecurityDescriptorSddl(const SwString& sddl) { m_securityDescriptorSddl = sddl; }
#endif

    void setMaxPendingConnections(int maxPendingConnections) {
        if (maxPendingConnections > 0) {
            m_maxPendingConnections = maxPendingConnections;
        }
    }

    void setAcceptBudget(int acceptBudget) {
        if (acceptBudget > 0) {
            m_acceptBudget = acceptBudget;
        }
    }

    /** Takes ownership of the next accepted connection, or nullptr when the queue is empty. */
    virtual SwLocalSocket* nextPendingConnection() {
        if (m_pendingConnections.isEmpty()) {
            return nullptr;
        }
        return m_pendingConnections.takeFirst();
    }

    /**
     * @brief Reclaims a stale endpoint left behind by a crashed server (POSIX socket file
     *        unlink; nothing to reclaim on Windows, pipes vanish with their last handle).
     */
    static bool removeServer(const SwString& name) {
#if defined(_WIN32)
        (void)name;
        return true;
#else
        const std::string path = SwLocalSocket::fullNameForServerName(name).toStdString();
        if (path.empty()) {
            return false;
        }
        if (::unlink(path.c_str()) == 0) {
            return true;
        }
        return errno == ENOENT;
#endif
    }

signals:
    DECLARE_SIGNAL_VOID(newConnection)        ///< Emitted after a connection is queued.

protected:
    SwDequeue<SwLocalSocket*> m_pendingConnections;

    /** Hook: sub-classes may hand out SwLocalSocket derivatives. */
    virtual SwLocalSocket* createPendingSocket_() {
        return new SwLocalSocket();
    }

    /** Hook: whether adoptHandle() should emit connected() on the accepted socket. */
    virtual bool shouldEmitConnectedOnAdopt_(SwLocalSocket* socket) {
        (void)socket;
        return true;
    }

    /** Hook: last step for an accepted socket; the default queues it and signals. */
    virtual bool finalizeAcceptedSocket_(SwLocalSocket* socket) {
        queuePendingConnection_(socket);
        return true;
    }

    void queuePendingConnection_(SwLocalSocket* socket) {
        if (!socket) {
            return;
        }
        if (m_pendingConnections.size() >= m_maxPendingConnections) {
            swCWarning(kSwLogCategory_SwLocalServer)
                << "[SwLocalServer] pending connection cap reached, dropping connection";
            socket->abort();
            socket->deleteLater();
            return;
        }
        m_pendingConnections.append(socket);
        emit newConnection();
    }

private:
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

#if defined(_WIN32)
        m_dispatchToken = app->ioDispatcher().watchHandleReliable(
            m_connectEvent,
            poster,
            [this]() {
                if (!SwObject::isLive(this) || !isListening()) {
                    return;
                }
                onConnectEvent_();
            });
#else
        m_dispatchToken = app->ioDispatcher().watchFdReliable(
            m_listenFd,
            SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup,
            poster,
            [this](uint32_t events) {
                if (!SwObject::isLive(this) || !isListening()) {
                    return;
                }
                onListenEvents_(events);
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

    void handleAcceptedHandle_(SwNativeLocalHandle handle) {
        SwLocalSocket* client = createPendingSocket_();
        if (!client) {
#if defined(_WIN32)
            CloseHandle(handle);
#else
            ::close(handle);
#endif
            return;
        }
        if (!client->adoptHandle(handle, true, shouldEmitConnectedOnAdopt_(client)) ||
            client->state() != SwAbstractSocket::ConnectedState) {
            delete client;
            return;
        }
        finalizeAcceptedSocket_(client);
    }

#if defined(_WIN32)
    bool createInstanceAndArm_(bool firstInstance) {
        DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (firstInstance) {
            openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        }
        const DWORD pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                               PIPE_REJECT_REMOTE_CLIENTS;

        SECURITY_ATTRIBUTES* securityAttributes = nullptr;
        SECURITY_ATTRIBUTES attributes;
        if (firstInstance) {
            freeSecurityDescriptor_();
            buildSecurityDescriptor_();
        }
        if (m_securityDescriptor) {
            std::memset(&attributes, 0, sizeof(attributes));
            attributes.nLength = sizeof(attributes);
            attributes.lpSecurityDescriptor = m_securityDescriptor;
            attributes.bInheritHandle = FALSE;
            securityAttributes = &attributes;
        }

        const std::wstring widePath = m_fullServerName.toStdWString();
        m_pipeInstance = CreateNamedPipeW(widePath.c_str(),
                                          openMode,
                                          pipeMode,
                                          PIPE_UNLIMITED_INSTANCES,
                                          static_cast<DWORD>(kSwLocalServerPipeBufferBytes),
                                          static_cast<DWORD>(kSwLocalServerPipeBufferBytes),
                                          0,
                                          securityAttributes);
        if (m_pipeInstance == INVALID_HANDLE_VALUE) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] CreateNamedPipeW failed: "
                                                   << GetLastError();
            return false;
        }

        ResetEvent(m_connectEvent);
        std::memset(&m_connectOverlapped, 0, sizeof(m_connectOverlapped));
        m_connectOverlapped.hEvent = m_connectEvent;
        m_connectSatisfied = false;
        if (ConnectNamedPipe(m_pipeInstance, &m_connectOverlapped)) {
            // Overlapped ConnectNamedPipe is documented to return FALSE; treat TRUE as connected.
            m_connectSatisfied = true;
            SetEvent(m_connectEvent);
            m_connectPending = true;
            return true;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            m_connectPending = true;
            return true;
        }
        if (error == ERROR_PIPE_CONNECTED) {
            m_connectSatisfied = true;
            SetEvent(m_connectEvent);
            m_connectPending = true;
            return true;
        }
        swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] ConnectNamedPipe failed: "
                                               << error;
        CloseHandle(m_pipeInstance);
        m_pipeInstance = INVALID_HANDLE_VALUE;
        return false;
    }

    void onConnectEvent_() {
        if (!m_connectPending || m_pipeInstance == INVALID_HANDLE_VALUE) {
            return;
        }

        if (!m_connectSatisfied) {
            DWORD transferred = 0;
            if (!GetOverlappedResult(m_pipeInstance, &m_connectOverlapped, &transferred, FALSE)) {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_INCOMPLETE) {
                    return;
                }
                m_connectPending = false;
                if (error == ERROR_OPERATION_ABORTED) {
                    return;
                }
                // Client vanished between connect and accept: recycle the instance.
                swCWarning(kSwLogCategory_SwLocalServer)
                    << "[SwLocalServer] pipe accept failed: " << error;
                CloseHandle(m_pipeInstance);
                m_pipeInstance = INVALID_HANDLE_VALUE;
                ResetEvent(m_connectEvent);
                createInstanceAndArm_(false);
                return;
            }
        }

        m_connectPending = false;
        m_connectSatisfied = false;
        ResetEvent(m_connectEvent);

        const HANDLE accepted = m_pipeInstance;
        m_pipeInstance = INVALID_HANDLE_VALUE;

        // Re-arm a fresh instance before surfacing the accepted one so no connector is lost.
        if (!createInstanceAndArm_(false)) {
            swCError(kSwLogCategory_SwLocalServer)
                << "[SwLocalServer] could not re-arm pipe instance, server stops accepting";
        }
        handleAcceptedHandle_(accepted);
    }

    void buildSecurityDescriptor_() {
        SwString sddl = m_securityDescriptorSddl;
        if (sddl.isEmpty() && m_socketOptions != NoOptions) {
            // The pipe namespace has no owner/group/other split: any non-empty access option
            // beyond the creator maps to "everyone may read/write", mirroring chmod 0666.
            if ((m_socketOptions & (GroupAccessOption | OtherAccessOption)) != 0) {
                sddl = "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;WD)";
            }
        }
        if (sddl.isEmpty()) {
            return;
        }
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.toStdWString().c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
            swCWarning(kSwLogCategory_SwLocalServer)
                << "[SwLocalServer] invalid SDDL, falling back to default pipe ACL";
            return;
        }
        m_securityDescriptor = descriptor;
    }

    void freeSecurityDescriptor_() {
        if (m_securityDescriptor) {
            LocalFree(m_securityDescriptor);
            m_securityDescriptor = nullptr;
        }
    }
#else
    void onListenEvents_(uint32_t events) {
        if (events & (SwIoDispatcher::Error | SwIoDispatcher::Hangup)) {
            swCError(kSwLogCategory_SwLocalServer) << "[SwLocalServer] listen socket error";
            close();
            return;
        }
        if (!(events & SwIoDispatcher::Readable)) {
            return;
        }
        for (int accepted = 0; accepted < m_acceptBudget; ++accepted) {
#if defined(__linux__)
            int handle = ::accept4(m_listenFd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
            int handle = ::accept(m_listenFd, nullptr, nullptr);
#endif
            if (handle < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    swCWarning(kSwLogCategory_SwLocalServer) << "[SwLocalServer] accept failed: "
                                                             << std::strerror(errno);
                }
                break;
            }
#if !defined(__linux__)
            setNonBlockingAndCloseOnExec_(handle);
#endif
            handleAcceptedHandle_(handle);
            if (!SwObject::isLive(this) || !isListening()) {
                return;
            }
        }
    }

    static mode_t accessModeForOptions_(uint32_t options) {
        mode_t mode = 0;
        if (options & UserAccessOption) {
            mode |= S_IRUSR | S_IWUSR;
        }
        if (options & GroupAccessOption) {
            mode |= S_IRGRP | S_IWGRP;
        }
        if (options & OtherAccessOption) {
            mode |= S_IROTH | S_IWOTH;
        }
        return mode;
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
    bool m_listening = false;
    uint32_t m_socketOptions = NoOptions;
    int m_maxPendingConnections = kSwLocalServerDefaultMaxPendingConnections;
    int m_acceptBudget = kSwLocalServerDefaultAcceptBudget;
    SwIoDispatcher::Token m_dispatchToken = 0;

#if defined(_WIN32)
    HANDLE m_pipeInstance = INVALID_HANDLE_VALUE;
    HANDLE m_connectEvent = nullptr;
    OVERLAPPED m_connectOverlapped{};
    bool m_connectPending = false;
    bool m_connectSatisfied = false;
    SwString m_securityDescriptorSddl;
    PSECURITY_DESCRIPTOR m_securityDescriptor = nullptr;
#else
    int m_listenFd = -1;
    std::string m_boundPath;
#endif
};
