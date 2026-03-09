#pragma once

/**
 * @file src/core/io/SwTcpSocket.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwTcpSocket in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the TCP socket interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwTcpSocket.
 *
 * Socket-oriented declarations here abstract OS-level descriptors and expose the read, write,
 * connection, and readiness semantics that higher layers build upon.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
 *
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
#include "SwEventLoop.h"
#include "SwBackendSsl.h"
#include "SwDebug.h"
#include "SwByteArray.h"

#include <memory>
static constexpr const char* kSwLogCategory_SwTcpSocket = "sw.core.io.swtcpsocket";


#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "platform/win/SwWindows.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <schannel.h>
#include <security.h>
#include <wincrypt.h>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")


/**
 * @class SwTcpSocket
 * @brief Provides an implementation of a TCP socket using the Winsock2 library.
 *
 * This class extends `SwAbstractSocket` to implement TCP communication functionalities.
 * It supports non-blocking operations, event-based monitoring, and integration with
 * the custom signal/slot system for asynchronous handling of connections, data transfers, and errors.
 *
 * ### Key Features:
 * - Establishes TCP connections with `connectToHost`.
 * - Supports non-blocking reads and writes.
 * - Allows monitoring of socket events like `FD_CONNECT`, `FD_READ`, `FD_WRITE`, and `FD_CLOSE`.
 * - Emits signals for key events such as connection established, data ready to read, errors, and disconnections.
 * - Manages write buffering to handle partial sends in non-blocking mode.
 *
 * @note This implementation is specific to Windows platforms, leveraging Winsock2 for socket operations.
 */
class SwTcpSocket : public SwAbstractSocket {
    SW_OBJECT(SwTcpSocket, SwAbstractSocket)

public:
    /**
     * @brief Constructs a `SwTcpSocket` object and initializes Winsock.
     *
     * This constructor sets up the TCP socket object with default values, initializes the Winsock library,
     * and starts monitoring for socket events.
     *
     * @param parent A pointer to the parent object. Defaults to `nullptr`.
     *
     * @note The socket is initialized to `INVALID_SOCKET`, and no connection is established at construction.
     * @warning Ensure that the parent object properly manages the lifecycle of this socket.
     */
    SwTcpSocket(SwObject* parent = nullptr)
        : SwAbstractSocket(parent),
          m_socket(INVALID_SOCKET),
          m_event(NULL) {
        initializeWinsock();
        startMonitoring();
    }

    /**
     * @brief Destructor for `SwTcpSocket`.
     *
     * Cleans up resources associated with the socket, including stopping event monitoring
     * and closing the underlying socket connection.
     *
     * @note This ensures proper release of Winsock resources and internal states.
     */
    virtual ~SwTcpSocket() {
        stopMonitoring();
        close();
    }

    /**
     * @brief Enables or disables TLS for the next connections.
     *
     * Call this before connectToHost. When enabled, all traffic is encrypted
     * with the system SChannel implementation and server certificates are
     * validated by the OS.
     *
     * @param enabled Whether TLS must be negotiated.
     * @param host Optional server name used for SNI/certificate validation.
     *             Defaults to the host passed to connectToHost.
     */
    void useSsl(bool enabled, const SwString& host = SwString()) {
        m_useTls = enabled;
        m_useOpenSslBackend = enabled;
        if (!enabled) {
            cleanupTls();
            m_tlsMode = TlsState::Disabled;
            m_tlsHost = "";
            m_effectiveTlsHost = "";
        } else {
            if (!host.isEmpty()) {
                m_tlsHost = host;
            }
        }
    }

    /**
     * @brief Returns true if TLS is enabled for the socket.
     */
    bool isUsingSsl() const {
        return m_useTls;
    }

    /**
     * @brief Starts a client-side TLS handshake on an already-connected socket (StartTLS).
     *
     * This is primarily used for HTTP proxy CONNECT tunneling (wss:// through a proxy).
     * The socket must already be in ConnectedState.
     *
     * @param host Optional server name used for SNI/certificate validation.
     * @return true if the TLS handshake completes successfully.
     */
    bool startClientTls(const SwString& host = SwString()) {
        if (m_socket == INVALID_SOCKET || state() != ConnectedState) {
            return false;
        }

        if (!host.isEmpty()) {
            m_tlsHost = host;
        }
        m_effectiveTlsHost = m_tlsHost.isEmpty() ? m_lastHost : m_tlsHost;
        if (m_effectiveTlsHost.isEmpty()) {
            m_effectiveTlsHost = host;
        }

        m_useTls = true;
        m_remoteClosed = false;

        // StartTLS is only implemented for the OpenSSL backend path for now.
        if (!m_useOpenSslBackend) {
            emit errorOccurred(-2146893048);
            return false;
        }

        if (!initOpenSslBackend()) {
            return false;
        }
        if (!performOpenSslBlockingHandshake()) {
            return false;
        }
        return true;
    }

    /**
     * @brief Returns whether the object reports remote Closed.
     * @return `true` when the object reports remote Closed; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isRemoteClosed() const {
        return m_remoteClosed;
    }

    /**
     * @brief Establishes a TCP connection to the specified host and port.
     *
     * This method creates a non-blocking socket, sets up event monitoring, resolves the
     * host's DNS, and initiates the connection. It supports IPv4 and emits appropriate
     * error signals if any step fails.
     *
     * @param host The hostname or IP address to connect to.
     * @param port The port number on the host.
     *
     * @return `true` if the connection is successfully initiated, `false` if an error occurs.
     *
     * @details
     * - The method closes any existing socket before initiating a new connection.
     * - It resolves the hostname using `getaddrinfo`.
     * - The socket is set to non-blocking mode to handle asynchronous connections.
     * - Emits `errorOccurred` for DNS resolution errors or connection failures.
     *
     * @note If the connection cannot be immediately established, the method returns `true`
     *       and waits for the `FD_CONNECT` event to finalize the connection.
     */
    bool connectToHost(const SwString& host, uint16_t port) override {
        close(); // Fermer toute connexion précédente

        m_socket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (m_socket == INVALID_SOCKET) {
            int wsaErr = WSAGetLastError();
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] WSASocket failed: " << wsaErr;
            emit errorOccurred(wsaErr);
            return false;
        }
        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] Socket créé avec succès.";


        // Mettre la socket en non-bloquant
        u_long mode = 1;
        if (ioctlsocket(m_socket, FIONBIO, &mode) == SOCKET_ERROR) {
            int wsaErr = WSAGetLastError();
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] ioctlsocket(FIONBIO) failed: " << wsaErr;
            emit errorOccurred(wsaErr);
            close();
            return false;
        }

        m_event = WSACreateEvent();
        if (m_event == WSA_INVALID_EVENT) {
            int wsaErr = WSAGetLastError();
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] WSACreateEvent failed: " << wsaErr;
            emit errorOccurred(wsaErr);
            close();
            return false;
        }

        if (WSAEventSelect(m_socket, m_event, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
            int wsaErr = WSAGetLastError();
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] WSAEventSelect failed: " << wsaErr;
            emit errorOccurred(wsaErr);
            close();
            return false;
        }

        m_lastHost = host;
        if (m_useTls) {
            m_effectiveTlsHost = m_tlsHost.isEmpty() ? host : m_tlsHost;
        } else {
            m_effectiveTlsHost = "";
        }

        // Résolution du nom d'hôte
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;          // IPv4
        hints.ai_socktype = SOCK_STREAM;    // TCP
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        int rc = getaddrinfo(host.toStdString().c_str(), SwString::number(port).toStdString().c_str(), &hints, &result);
        if (rc != 0 || !result) {
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] getaddrinfo failed for host: " << host.toStdString()
                      << ", port: " << port << " Error: " << rc;
            emit errorOccurred(-1); // Erreur de résolution DNS
            close();
            return false;
        }

        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] Résolution DNS réussie pour " << host.toStdString() << ":" << port;

        // On tente la connexion avec la première adresse retournée
        sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(result->ai_addr);

        setState(ConnectingState);
        int resultConnect = ::connect(m_socket, (sockaddr*)addr_in, (int)result->ai_addrlen);
        freeaddrinfo(result);

        if (resultConnect == SOCKET_ERROR) {
            int err = WSAGetLastError();
            // WSAEWOULDBLOCK et WSAEINPROGRESS sont normaux en mode non-bloquant
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] connect failed: " << err;
                emit errorOccurred(err);
                close();
                return false;
            }
            swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] Connexion en cours (non bloquante), en attente d'événement FD_CONNECT...";
        } else {
            swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] Connexion établie immédiatement.";
        }

        return true;
    }

    /**
     * @brief Waits for the socket to establish a connection within the specified timeout.
     *
     * This method blocks the calling thread until the socket transitions to the `ConnectedState`
     * or the timeout duration expires.
     *
     * @param msecs The maximum time to wait for the connection, in milliseconds. Defaults to 30,000 ms (30 seconds).
     *
     * @return `true` if the socket successfully connects within the specified time, `false` if the timeout expires.
     *
     * @details
     * - Uses a condition function to monitor the socket's state.
     * - Continuously checks for the connection state within the specified timeout.
     *
     * @note This method is blocking and should not be used in the main GUI thread to avoid freezing the application.
     */
    bool waitForConnected(int msecs = 30000) override {
        return waitForCondition([this]() { return state() == ConnectedState; }, msecs);
    }

    /**
     * @brief Closes the TCP socket and cleans up associated resources.
     *
     * This method shuts down the socket, releases the event handle, clears the write buffer,
     * and resets the socket state to `UnconnectedState`. If the socket was in a connected or
     * connecting state, it emits the `disconnected` signal.
     *
     * @details
     * - Ensures graceful shutdown by disabling linger options if not already set.
     * - Calls `closesocket` to close the underlying Winsock socket.
     * - Clears any remaining data in the internal write buffer.
     * - Emits `disconnected` if the socket was previously in `ConnectedState`, `ConnectingState`,
     *   or `ClosingState`.
     *
     * @note This method is safe to call multiple times, as it checks the socket state before attempting cleanup.
     */
    void close() override {
        if (m_socket != INVALID_SOCKET) {
            // Remettre linger par défaut (si pas déjà fait)
            disableLinger();

            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        if (m_event) {
            WSACloseEvent(m_event);
            m_event = NULL;
        }

        m_writeBuffer.clear();
        cleanupTls();
        m_remoteClosed = false;

        if (state() == ConnectedState || state() == ConnectingState || state() == ClosingState) {
            emit disconnected();
        }

        setState(UnconnectedState);
    }

    /**
     * @brief Reads data from the socket up to the specified maximum size.
     *
     * This method attempts to read data from the TCP socket. If successful, it returns the received data
     * as a `SwString`. If the socket is closed or an error occurs, it handles the situation appropriately.
     *
     * @param maxSize The maximum number of bytes to read. Defaults to 0, which allows reading up to 1024 bytes.
     *
     * @return A `SwString` containing the data read from the socket. Returns an empty string if no data is available or an error occurs.
     *
     * @details
     * - If `maxSize` is greater than 0 and less than 1024, it reads up to `maxSize` bytes; otherwise, it reads up to 1024 bytes.
     * - If the connection is closed by the peer (`recv` returns 0), the socket is closed locally.
     * - If an error occurs, it emits the `errorOccurred` signal unless the error is `WSAEWOULDBLOCK` (non-blocking read).
     *
     * @note This method is non-blocking and relies on the socket's state being `ConnectedState`.
     */
    SwString read(int64_t maxSize = 0) override {
        if (m_socket == INVALID_SOCKET || state() != ConnectedState)
            return "";

        if (m_useTls && m_tlsMode == TlsState::Established && m_useOpenSslBackend) {
            if (m_tlsDecryptedBuffer.isEmpty()) {
                pumpOpenSslRead();
            }
            if (m_tlsDecryptedBuffer.isEmpty()) {
                return "";
            }

            size_t toRead = (maxSize > 0 && maxSize < (int64_t)m_tlsDecryptedBuffer.size())
                                ? (size_t)maxSize
                                : m_tlsDecryptedBuffer.size();
            SwString result = SwString::fromLatin1(m_tlsDecryptedBuffer.data(), (int)toRead);
            m_tlsDecryptedBuffer.remove(0, (int)toRead);
            if (m_remoteClosed && m_tlsDecryptedBuffer.isEmpty()) {
                close();
            }
            return result;
        }

        if (m_useTls && m_tlsMode == TlsState::Established) {
            if (m_tlsDecryptedBuffer.isEmpty()) {
                receiveTlsData();
            }
            if (m_tlsDecryptedBuffer.isEmpty()) {
                // If the peer has closed, try one last decrypt pass before giving up.
                if (m_remoteClosed && !m_tlsRecvBuffer.isEmpty()) {
                    decryptPendingTlsRecords();
                }
                if (m_tlsDecryptedBuffer.isEmpty()) {
                    if (m_remoteClosed) {
                        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS remote closed with no decrypted data, recvBuffer="
                                  << m_tlsRecvBuffer.size();
                    }
                    return "";
                }
            }

            size_t toRead = (maxSize > 0 && maxSize < (int64_t)m_tlsDecryptedBuffer.size())
                                ? (size_t)maxSize
                                : m_tlsDecryptedBuffer.size();
            SwString result = SwString::fromLatin1(m_tlsDecryptedBuffer.data(), (int)toRead);
            swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] returning decrypted chunk size=" << toRead;
            m_tlsDecryptedBuffer.remove(0, (int)toRead);
            if (m_remoteClosed && m_tlsDecryptedBuffer.isEmpty()) {
                close();
            }
            return result;
        }

        char buffer[1024];
        int sizeToRead = (maxSize > 0 && maxSize < 1024) ? (int)maxSize : 1024;
        int ret = ::recv(m_socket, buffer, sizeToRead, 0);
        if (ret > 0) {
            return SwString::fromLatin1(buffer, ret);
        } else if (ret == 0) {
            m_remoteClosed = true;
            close();
        } else {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                emit errorOccurred(err);
            }
        }
        return "";
    }

    /**
     * @brief Writes data to the socket.
     *
     * This method queues the provided data into the internal write buffer and attempts
     * to send it to the socket. If the socket cannot immediately send all data, the remaining
     * data is retained in the buffer for subsequent writes.
     *
     * @param data The data to be sent, provided as a `SwString`.
     *
     * @return `true` if the data was successfully queued for sending, `false` if the socket is invalid or not connected.
     *
     * @details
     * - Appends the data to the internal write buffer (`m_writeBuffer`).
     * - Calls `tryFlushWriteBuffer` to attempt immediate sending of the data.
     * - If the socket is non-blocking and cannot send all data at once, the remaining data stays in the buffer.
     *
     * @note Emits `writeFinished` when the buffer is fully flushed to the socket.
     */
    bool write(const SwString& data) override {
        if (m_socket == INVALID_SOCKET || state() != ConnectedState)
            return false;

        m_writeBuffer.append(data.toStdString());

        tryFlushWriteBuffer();

        return true;
    }

    /**
     * @brief Waits for all data in the write buffer to be sent to the socket.
     *
     * This method blocks the calling thread until all data in the internal write buffer is sent,
     * an error occurs, or the timeout expires.
     *
     * @param msecs The maximum time to wait for the data to be written, in milliseconds. Defaults to 30,000 ms (30 seconds).
     *              A negative value indicates no timeout (wait indefinitely).
     *
     * @return `true` if all data was successfully sent within the specified time, `false` if the timeout expired or an error occurred.
     *
     * @details
     * - Continuously monitors the socket's write events using `WSAWaitForMultipleEvents`.
     * - Updates the remaining time and exits if the timeout expires.
     * - Resets the socket event and processes pending socket events via `checkSocketEvents`.
     * - Emits `errorOccurred` if an error occurs during the process.
     *
     * @note This method is blocking and should be used cautiously in the main GUI thread to avoid freezing the application.
     */
    bool waitForBytesWritten(int msecs = 30000) {
        using namespace std::chrono;
        auto start = steady_clock::now();
        int timeout = (msecs < 0) ? -1 : msecs;

        // Tant qu'il reste des données à envoyer
        while (!m_writeBuffer.isEmpty() || !m_tlsEncryptedBuffer.isEmpty()) {
            // Calcul du temps restant
            int remainingTime = -1;
            if (timeout >= 0) {
                auto now = steady_clock::now();
                auto elapsed = duration_cast<milliseconds>(now - start).count();
                if ((int)elapsed >= timeout) {
                    // Délai dépassé
                    return false;
                }
                remainingTime = timeout - (int)elapsed;
            }

            DWORD waitTime = (remainingTime < 0) ? WSA_INFINITE : (DWORD)remainingTime;
            DWORD result = WSAWaitForMultipleEvents(1, &m_event, FALSE, waitTime, FALSE);
            if (result == WSA_WAIT_FAILED) {
                emit errorOccurred(WSAGetLastError());
                return false;
            } else if (result == WSA_WAIT_TIMEOUT) {
                // Délai expiré sans que tout ait été envoyé
                return false;
            }

            // Un événement s’est produit
            WSAResetEvent(m_event);
            if (!checkSocketEvents()) {
                // Une erreur est survenue
                return false;
            }
            tryFlushWriteBuffer();
        }
        // Yield cooperatively so other fibers/tasks can run while we finish flushing
        SwEventLoop::swsleep(1);
        return true;
    }

    /**
     * @brief Signals the end of data transmission and ensures the client receives all sent data.
     *
     * This method shuts down the write operation of the socket, indicating that no more data
     * will be sent. It optionally enables the linger option to ensure the socket waits for
     * acknowledgment (ACK) of all sent data before closing.
     *
     * @param lingerSeconds The duration in seconds to wait for the client to acknowledge the sent data.
     *                      Defaults to 5 seconds.
     *
     * @return `true` if the write operation was successfully shut down, `false` if the socket is invalid,
     *         not connected, or an error occurs.
     *
     * @details
     * - Calls `shutdown` with `SD_SEND` to terminate the write side of the socket.
     * - If `shutdown` fails, emits the `errorOccurred` signal with the corresponding error code.
     * - Enables the linger option with the specified timeout to ensure proper socket closure.
     *
     * @note This method should be called before invoking `close()` to gracefully terminate the connection.
     */
    bool shutdownWrite(int lingerSeconds = 5) {
        if (m_socket == INVALID_SOCKET || state() != ConnectedState)
            return false;

        // Terminer proprement l'envoi
        if (shutdown(m_socket, SD_SEND) == SOCKET_ERROR) {
            emit errorOccurred(WSAGetLastError());
            return false;
        }

        // Activer SO_LINGER pour s'assurer que close() attendra l'ACK des données
        enableLinger(lingerSeconds);
        return true;
    }

    /**
     * @brief Adopts an existing socket and integrates it with the `SwTcpSocket` instance.
     *
     * This method takes ownership of an already created Winsock socket, configures it for non-blocking
     * mode, sets up event monitoring, and updates the internal state to reflect the connection status.
     *
     * @param sock The existing `SOCKET` to be adopted by this instance.
     *
     * @details
     * - Closes any previously managed socket before adopting the new one.
     * - Sets the adopted socket to non-blocking mode using `ioctlsocket`.
     * - Creates an event handle for the socket and configures it to monitor `FD_READ`, `FD_WRITE`, and `FD_CLOSE` events.
     * - Updates the internal state to `ConnectedState` and starts monitoring for events.
     * - Emits the `connected` signal to indicate that the socket is now managed and connected.
     *
     * @note The caller must ensure the validity of the socket being passed.
     */
    void adoptSocket(SOCKET sock) {
        close();
        m_socket = sock;
        if (m_socket != INVALID_SOCKET) {
            u_long mode = 1;
            ioctlsocket(m_socket, FIONBIO, &mode);

            m_event = WSACreateEvent();
            WSAEventSelect(m_socket, m_event, FD_READ | FD_WRITE | FD_CLOSE);

            setState(ConnectedState);
            startMonitoring();
            emit connected();
        }
    }

protected:
    /**
     * @brief Handles periodic checks for socket events.
     *
     * This method overrides the base class implementation to perform additional
     * monitoring of socket events by calling `checkSocketEvents`.
     *
     * @details
     * - Calls the base class `onTimerDescriptor` for standard descriptor management.
     * - Invokes `checkSocketEvents` to process pending network events for the socket.
     *
     * @note This method is part of the event loop and is called periodically to handle
     *       socket-related updates.
     */
    void onTimerDescriptor() override {
        SwIODevice::onTimerDescriptor();
        checkSocketEvents();
    }

private:
    SOCKET m_socket;               ///< The Winsock socket handle used for TCP communication.
    WSAEVENT m_event;              ///< The event handle used for monitoring socket events.
    SwByteArray m_writeBuffer;     ///< Internal buffer to store data for partial writes in non-blocking mode.
    bool m_useTls = false;

    enum class TlsState {
        Disabled,
        Negotiating,
        Established
    };

    TlsState m_tlsMode = TlsState::Disabled;
    SwString m_tlsHost;
    SwString m_effectiveTlsHost;
    CredHandle m_credHandle{};
    CtxtHandle m_ctxtHandle{};
    bool m_credentialsReady = false;
    bool m_contextReady = false;
    SecPkgContext_StreamSizes m_streamSizes{};
    SwByteArray m_tlsRecvBuffer;
    SwByteArray m_tlsDecryptedBuffer;
    SwByteArray m_tlsEncryptedBuffer;
    SwByteArray m_tlsHandshakeBuffer;
    std::unique_ptr<SwBackendSsl> m_sslBackend;
    bool m_useOpenSslBackend = false;
    SwString m_lastHost;
    bool m_remoteClosed = false;

    /**
     * @brief Initializes the Winsock library for network operations.
     *
     * This static method ensures that Winsock is initialized only once during the program's
     * execution. It calls `WSAStartup` to load the Winsock library and prepares it for use.
     *
     * @details
     * - Uses a static boolean flag to prevent multiple initializations.
     * - Logs an error message to `swCError(kSwLogCategory_SwTcpSocket)` if `WSAStartup` fails.
     * - Must be called before any Winsock-dependent functionality is used.
     *
     * @note This method is thread-safe due to the static variable ensuring one-time initialization.
     */
    static void initializeWinsock() {
        static bool initialized = false;
        if (!initialized) {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2,2), &wsaData);
            if (result != 0) {
                swCError(kSwLogCategory_SwTcpSocket) << "WSAStartup failed: " << result;
            } else {
                initialized = true;
            }
        }
    }

    void cleanupTls() {
        if (m_contextReady) {
            DeleteSecurityContext(&m_ctxtHandle);
            m_contextReady = false;
        }
        if (m_credentialsReady) {
            FreeCredentialsHandle(&m_credHandle);
            m_credentialsReady = false;
        }
        m_sslBackend.reset();
        m_tlsRecvBuffer.clear();
        m_tlsDecryptedBuffer.clear();
        m_tlsEncryptedBuffer.clear();
        m_tlsHandshakeBuffer.clear();
        m_effectiveTlsHost = "";
        m_tlsMode = TlsState::Disabled;
    }

    /**
     * @brief Checks and processes pending socket events.
     *
     * This method monitors the socket for network events such as connection completion,
     * data availability, write readiness, or disconnection. It emits appropriate signals
     * and updates the socket's state based on the events detected.
     *
     * @return `true` if the socket is in a valid state after processing events, `false` if an error occurs.
     *
     * @details
     * - Waits for events associated with the socket's event handle (`m_event`).
     * - Resets the event after processing to prepare for future monitoring.
     * - Handles the following events:
     *   - `FD_CONNECT`: Emits `connected` if successful, or `errorOccurred` if an error occurs.
     *   - `FD_READ`: Emits `readyRead` if data is available, or `errorOccurred` if an error occurs.
     *   - `FD_WRITE`: Flushes the write buffer or emits `errorOccurred` if an error occurs.
     *   - `FD_CLOSE`: Transitions the socket to `ClosingState` and closes it.
     * - If `WSAEnumNetworkEvents` fails, emits the `errorOccurred` signal with the error code.
     *
     * @note This method should be called periodically to handle pending socket events.
     */
    bool checkSocketEvents() {
        if (m_event == NULL || m_socket == INVALID_SOCKET) {
            return true;
        }

        DWORD res = WSAWaitForMultipleEvents(1, &m_event, FALSE, 0, FALSE);
        if (res == WSA_WAIT_TIMEOUT) {
            return true;
        }

        WSAResetEvent(m_event);

        WSANETWORKEVENTS networkEvents;
        if (WSAEnumNetworkEvents(m_socket, m_event, &networkEvents) == SOCKET_ERROR) {
            emit errorOccurred(WSAGetLastError());
            return false;
        }

        if (networkEvents.lNetworkEvents & FD_CONNECT) {
            if (networkEvents.iErrorCode[FD_CONNECT_BIT] == 0) {
                if (m_useTls) {
                    if (m_useOpenSslBackend) {
                        if (!initOpenSslBackend()) {
                            close();
                            return false;
                        }
                        if (!performOpenSslBlockingHandshake()) {
                            close();
                            return false;
                        }
                    } else if (!beginTlsHandshake()) {
                        close();
                        return false;
                    }
                } else {
                    setState(ConnectedState);
                    emit connected();
                }
            } else {
                emit errorOccurred(networkEvents.iErrorCode[FD_CONNECT_BIT]);
                close();
            }
        }

        if (networkEvents.lNetworkEvents & FD_READ) {
            if (networkEvents.iErrorCode[FD_READ_BIT] == 0) {
                if (m_useTls) {
                    if (m_useOpenSslBackend) {
                        if (m_tlsMode == TlsState::Negotiating) {
                            if (!driveOpenSslHandshake()) {
                                return false;
                            }
                        }
                        if (m_tlsMode == TlsState::Established) {
                            if (!pumpOpenSslRead()) {
                                return false;
                            }
                            if (!m_tlsDecryptedBuffer.isEmpty()) {
                                emit readyRead();
                            }
                        }
                    } else {
                        if (m_tlsMode == TlsState::Negotiating) {
                            if (!processTlsHandshakeData()) {
                                return false;
                            }
                        }
                        if (m_tlsMode == TlsState::Established) {
                            if (!receiveTlsData()) {
                                return false;
                            }
                            if (!m_tlsDecryptedBuffer.isEmpty()) {
                                emit readyRead();
                            }
                        }
                    }
                } else {
                    emit readyRead();
                }
            } else {
                emit errorOccurred(networkEvents.iErrorCode[FD_READ_BIT]);
            }
        }

        if (networkEvents.lNetworkEvents & FD_WRITE) {
            if (networkEvents.iErrorCode[FD_WRITE_BIT] != 0) {
                emit errorOccurred(networkEvents.iErrorCode[FD_WRITE_BIT]);
            } else {
                if (m_useTls && m_tlsMode == TlsState::Negotiating) {
                    if (m_useOpenSslBackend) {
                        if (!driveOpenSslHandshake()) {
                            return false;
                        }
                    } else {
                        if (!flushTlsHandshakeBuffer()) {
                            return false;
                        }
                    }
                }
                tryFlushWriteBuffer();
            }
        }

        if (networkEvents.lNetworkEvents & FD_CLOSE) {
            m_remoteClosed = true;
            emit readyRead();
        }

        // If the peer has closed and there is nothing left to decrypt or send, close cleanly now.
        if (m_remoteClosed && m_tlsRecvBuffer.isEmpty() && m_tlsDecryptedBuffer.isEmpty() &&
            m_tlsEncryptedBuffer.isEmpty() && m_writeBuffer.isEmpty() && state() != UnconnectedState) {
            close();
        }

        return true;
    }

    // Flushes any pending TLS handshake tokens (ClientHello/KeyExchange/etc.).
    // Handshake chunks are small; if the socket would block we keep the data
    // queued and retry on the next FD_WRITE notification.
    bool flushTlsHandshakeBuffer() {
        if (m_socket == INVALID_SOCKET) {
            return false;
        }
        while (!m_tlsHandshakeBuffer.isEmpty()) {
            int sent = ::send(m_socket, m_tlsHandshakeBuffer.data(), (int)m_tlsHandshakeBuffer.size(), 0);
            if (sent == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return true;
                }
                emit errorOccurred(err);
                return false;
            }
            m_tlsHandshakeBuffer.remove(0, (int)sent);
        }
        return true;
    }

    bool initOpenSslBackend() {
        if (!m_sslBackend) {
            m_sslBackend.reset(new SwBackendSsl());
        }
        if (!m_sslBackend->init(m_effectiveTlsHost.toStdString(), static_cast<intptr_t>(m_socket))) {
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] OpenSSL init failed: " << m_sslBackend->lastError();
            emit errorOccurred(-2146893048);
            return false;
        }
        m_tlsMode = TlsState::Negotiating;
        return true;
    }

    bool driveOpenSslHandshake() {
        if (!m_sslBackend) {
            return false;
        }
        auto res = m_sslBackend->handshake();
        if (res == SwBackendSsl::IoResult::Ok) {
            m_tlsMode = TlsState::Established;
            setState(ConnectedState);
            emit connected();
            return true;
        }
        if (res == SwBackendSsl::IoResult::WantRead || res == SwBackendSsl::IoResult::WantWrite) {
            return true;
        }
        swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] OpenSSL handshake error: " << m_sslBackend->lastError();
        emit errorOccurred(-2146893048);
        return false;
    }

    // Helper to complete the OpenSSL handshake in blocking mode using select() so we don't
    // have to juggle WANT_READ/WANT_WRITE in the event loop during negotiation.
    bool performOpenSslBlockingHandshake() {
        if (!m_sslBackend) {
            return false;
        }

        // Temporarily switch to blocking
        u_long blocking = 0;
        ioctlsocket(m_socket, FIONBIO, &blocking);

        bool ok = false;
        for (int i = 0; i < 40; ++i) { // up to ~20s with 500ms waits
            auto res = m_sslBackend->handshake();
            if (res == SwBackendSsl::IoResult::Ok) {
                ok = true;
                break;
            }
            if (res == SwBackendSsl::IoResult::Closed || res == SwBackendSsl::IoResult::Error) {
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] OpenSSL handshake error: " << m_sslBackend->lastError();
                break;
            }

            fd_set rfds, wfds;
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            if (res == SwBackendSsl::IoResult::WantRead) {
                FD_SET(m_socket, &rfds);
            }
            if (res == SwBackendSsl::IoResult::WantWrite) {
                FD_SET(m_socket, &wfds);
            }
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 500 * 1000; // 500ms
            int sel = select(0, &rfds, &wfds, nullptr, &tv);
            if (sel < 0) {
                break;
            }
        }

        // Restore non-blocking
        u_long nonBlocking = 1;
        ioctlsocket(m_socket, FIONBIO, &nonBlocking);

        if (ok) {
            m_tlsMode = TlsState::Established;
            setState(ConnectedState);
            emit connected();
            return true;
        }

        emit errorOccurred(-2146893048);
        return false;
    }

    bool pumpOpenSslRead() {
        if (!m_sslBackend) {
            return false;
        }
        char buffer[4096];
        while (true) {
            int bytes = 0;
            auto res = m_sslBackend->read(buffer, sizeof(buffer), bytes);
            if (res == SwBackendSsl::IoResult::Ok && bytes > 0) {
                m_tlsDecryptedBuffer.append(buffer, bytes);
                continue;
            }
            if (res == SwBackendSsl::IoResult::WantRead || res == SwBackendSsl::IoResult::WantWrite) {
                return true;
            }
            if (res == SwBackendSsl::IoResult::Closed) {
                m_remoteClosed = true;
                return true;
            }
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] OpenSSL read error: " << m_sslBackend->lastError();
            emit errorOccurred(-7);
            return false;
        }
    }

    /**
     * @brief Attempts to flush the internal write buffer to the socket.
     *
     * This method sends data from the internal write buffer (`m_writeBuffer`) to the socket.
     * If the data cannot be fully sent, the remaining data stays in the buffer for future attempts.
     *
     * @details
     * - If the socket is invalid, not connected, or the buffer is empty, the method exits immediately.
     * - Sends data using `send` and removes the transmitted portion from the buffer.
     * - Emits the `writeFinished` signal when the buffer is fully flushed.
     * - If `send` fails due to a non-recoverable error, emits the `errorOccurred` signal.
     * - If the error is `WSAEWOULDBLOCK`, it waits for the `FD_WRITE` event to retry the operation.
     *
     * @note This method does not block; it immediately returns after attempting to send data.
     */
    void tryFlushWriteBuffer() {
        if (m_socket == INVALID_SOCKET || state() != ConnectedState)
            return;

        if (m_useTls && m_tlsMode == TlsState::Established && m_useOpenSslBackend) {
            if (!m_sslBackend) {
                return;
            }
            while (!m_writeBuffer.isEmpty()) {
                int written = 0;
                auto res = m_sslBackend->write(m_writeBuffer.data(), (int)m_writeBuffer.size(), written);
                if (res == SwBackendSsl::IoResult::Ok && written > 0) {
                    m_writeBuffer.remove(0, (int)written);
                    continue;
                }
                if (res == SwBackendSsl::IoResult::WantRead || res == SwBackendSsl::IoResult::WantWrite) {
                    break;
                }
                if (res == SwBackendSsl::IoResult::Closed) {
                    m_remoteClosed = true;
                    close();
                    return;
                }
                emit errorOccurred(-5);
                close();
                return;
            }
            if (m_writeBuffer.isEmpty()) {
                emit writeFinished();
            }
            return;
        }

        if (m_useTls && m_tlsMode == TlsState::Established) {
            if (!sendPendingEncrypted()) {
                return;
            }

            if (m_tlsEncryptedBuffer.isEmpty() && !m_writeBuffer.isEmpty()) {
                size_t chunk = std::min<size_t>(m_streamSizes.cbMaximumMessage, m_writeBuffer.size());
                if (!enqueueEncryptedChunk(chunk)) {
                    return;
                }
                sendPendingEncrypted();
            }

            if (m_writeBuffer.isEmpty() && m_tlsEncryptedBuffer.isEmpty()) {
                emit writeFinished();
            }
            return;
        }

        if (m_writeBuffer.isEmpty()) {
            return;
        }

        const char* dataPtr = m_writeBuffer.data();
        int dataSize = (int)m_writeBuffer.size();

        int ret = ::send(m_socket, dataPtr, dataSize, 0);
        if (ret > 0) {
            m_writeBuffer.remove(0, (int)ret);
            if (m_writeBuffer.isEmpty()) {
                emit writeFinished();
            }
        } else if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                emit errorOccurred(err);
            }
            // On attend FD_WRITE pour réessayer
        }
    }


    /**
     * @brief Waits for a specified condition to be met within a given timeout.
     *
     * This method repeatedly checks a user-defined condition and blocks the calling thread
     * until the condition is satisfied or the timeout expires.
     *
     * @tparam Condition A callable that returns a boolean indicating whether the condition is met.
     *
     * @param condition The condition to be evaluated in each iteration.
     * @param msecs The maximum time to wait for the condition, in milliseconds.
     *              A negative value indicates no timeout (wait indefinitely).
     *
     * @return `true` if the condition is satisfied within the timeout, `false` if the timeout expires.
     *
     * @details
     * - Continuously evaluates the condition in a loop with a 10 ms delay between checks.
     * - Uses `std::chrono` to measure elapsed time for timeout calculations.
     * - Calls `checkSocketEvents` periodically to process pending network events during the wait.
     *
     * @note This method is blocking and should be used cautiously to avoid freezing the application.
     */
    template<typename Condition>
    bool waitForCondition(Condition condition, int msecs) {
        using namespace std::chrono;
        auto start = steady_clock::now();
        int timeout = (msecs < 0) ? -1 : msecs;
        SwCoreApplication* app = SwCoreApplication::instance(false);

        while (!condition()) {
            if (timeout >= 0) {
                auto now = steady_clock::now();
                auto elapsed = duration_cast<milliseconds>(now - start).count();
                if (elapsed >= timeout) {
                    return false;
                }
            }

            if (app) {
                SwCoreApplication::release();
            } else {
                Sleep(10);
            }
            checkSocketEvents();
        }
        return true;
    }

    /**
     * @brief Enables the linger option on the socket to control its closure behavior.
     *
     * This method sets the linger option, causing the socket to block during closure until
     * all pending data is sent or the specified timeout expires.
     *
     * @param seconds The linger timeout in seconds. If set to `0`, the socket closes immediately.
     *
     * @details
     * - Configures the socket using the `setsockopt` function with the `SO_LINGER` option.
     * - Setting the linger option is particularly useful for ensuring that the peer receives all
     *   transmitted data before the socket is closed.
     *
     * @note This method should be called only when the socket is in a valid state.
     */
    void enableLinger(int seconds) {
        linger lng;
        lng.l_onoff = 1;
        lng.l_linger = seconds;
        setsockopt(m_socket, SOL_SOCKET, SO_LINGER, (const char*)&lng, sizeof(lng));
    }

    /**
     * @brief Disables the linger option on the socket.
     *
     * This method configures the socket to close immediately without waiting for pending data
     * to be transmitted or acknowledged by the peer.
     *
     * @details
     * - Sets the `SO_LINGER` option to off using `setsockopt`.
     * - Ensures the socket closes without delay, regardless of unacknowledged data.
     *
     * @note This method should be called when immediate socket closure is required.
     */
    void disableLinger() {
        linger lng;
        lng.l_onoff = 0;
        lng.l_linger = 0;
        setsockopt(m_socket, SOL_SOCKET, SO_LINGER, (const char*)&lng, sizeof(lng));
    }

    bool ensureCredentialHandle() {
        if (m_credentialsReady) {
            return true;
        }

        SCHANNEL_CRED credData = {};
        credData.dwVersion = SCHANNEL_CRED_VERSION;
        // Restrict to modern protocols; Windows will pick the best available (TLS 1.2/1.3).
        DWORD protocols = SP_PROT_TLS1_2_CLIENT;
#ifdef SP_PROT_TLS1_3_CLIENT
        protocols |= SP_PROT_TLS1_3_CLIENT;
#endif
        credData.grbitEnabledProtocols = protocols;
        credData.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO;

        SECURITY_STATUS status = AcquireCredentialsHandleW(
            nullptr,
            const_cast<wchar_t*>(UNISP_NAME_W),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &credData,
            nullptr,
            nullptr,
            &m_credHandle,
            nullptr);

        if (status != SEC_E_OK) {
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] AcquireCredentialsHandle failed: " << status;
            return false;
        }

        m_credentialsReady = true;
        return true;
    }

    bool beginTlsHandshake() {
        if (!m_useTls) {
            return false;
        }
        if (!ensureCredentialHandle()) {
            return false;
        }

        m_tlsMode = TlsState::Negotiating;
        m_tlsRecvBuffer.clear();
        m_tlsDecryptedBuffer.clear();
        m_tlsEncryptedBuffer.clear();
        m_contextReady = false;

        return continueTlsHandshake(nullptr, 0);
    }

    // Perform a blocking TLS handshake using SChannel. This avoids the tricky event-driven
    // token juggling that was causing servers (Mapbox/ArcGIS) to reject our ClientHello.
    bool performBlockingTlsHandshake() {
        if (!m_useTls) {
            return false;
        }
        if (!ensureCredentialHandle()) {
            return false;
        }

        // Switch to blocking for the handshake phase.
        u_long blocking = 0;
        ioctlsocket(m_socket, FIONBIO, &blocking);

        m_tlsMode = TlsState::Negotiating;
        m_tlsRecvBuffer.clear();
        m_tlsDecryptedBuffer.clear();
        m_tlsEncryptedBuffer.clear();
        m_tlsHandshakeBuffer.clear();
        m_contextReady = false;

        DWORD contextReq = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT |
                           ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM;
        DWORD contextAttr = 0;
        TimeStamp expiry{};

        std::wstring targetNameW;
        const wchar_t* targetPtr = nullptr;
        if (!m_effectiveTlsHost.isEmpty()) {
            targetNameW = m_effectiveTlsHost.toStdWString();
            targetPtr = targetNameW.c_str();
        }

        SwByteArray inData;
        auto recvMore = [&](int timeoutMs) -> bool {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(m_socket, &readfds);
            timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            int sel = select(0, &readfds, nullptr, nullptr, &tv);
            if (sel <= 0) {
                return false;
            }
            char buf[4096];
            int ret = ::recv(m_socket, buf, sizeof(buf), 0);
            if (ret <= 0) {
                return false;
            }
            inData.append(buf, static_cast<size_t>(ret));
            return true;
        };

        // Loop until SEC_E_OK or a fatal error.
        while (true) {
            SecBuffer inBuffers[2];
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBuffers[0].pvBuffer = inData.isEmpty() ? nullptr : inData.data();
            inBuffers[0].cbBuffer = (ULONG)inData.size();
            inBuffers[1].BufferType = SECBUFFER_EMPTY;
            inBuffers[1].pvBuffer = nullptr;
            inBuffers[1].cbBuffer = 0;

            SecBufferDesc inDesc{SECBUFFER_VERSION, 2, inBuffers};

            SecBuffer outBuffer{};
            outBuffer.BufferType = SECBUFFER_TOKEN;
            outBuffer.pvBuffer = nullptr;
            outBuffer.cbBuffer = 0;
            SecBufferDesc outDesc{SECBUFFER_VERSION, 1, &outBuffer};

            SECURITY_STATUS status = InitializeSecurityContextW(
                &m_credHandle,
                m_contextReady ? &m_ctxtHandle : nullptr,
                const_cast<wchar_t*>(targetPtr),
                contextReq,
                0,
                SECURITY_NATIVE_DREP,
                inData.isEmpty() ? nullptr : &inDesc,
                0,
                &m_ctxtHandle,
                &outDesc,
                &contextAttr,
                &expiry);

            if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK) {
                m_contextReady = true;
                if (outBuffer.pvBuffer && outBuffer.cbBuffer > 0) {
                    int sent = ::send(m_socket, static_cast<const char*>(outBuffer.pvBuffer),
                                      (int)outBuffer.cbBuffer, 0);
                    FreeContextBuffer(outBuffer.pvBuffer);
                    if (sent == SOCKET_ERROR) {
                        m_tlsMode = TlsState::Disabled;
                        goto handshake_fail;
                    }
                }
            }

            if (status == SEC_E_OK) {
                SECURITY_STATUS sizesStatus = QueryContextAttributes(&m_ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &m_streamSizes);
                if (sizesStatus != SEC_E_OK) {
                    m_tlsMode = TlsState::Disabled;
                    goto handshake_fail;
                }
                m_tlsMode = TlsState::Established;
                m_tlsRecvBuffer = inData; // any leftover bytes
                m_tlsDecryptedBuffer.clear();
                setState(ConnectedState);
                emit connected();
                break;
            }

            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                if (!recvMore(5000)) {
                    m_tlsMode = TlsState::Disabled;
                    goto handshake_fail;
                }
            } else if (status == SEC_I_CONTINUE_NEEDED) {
                size_t extra = 0;
                void* extraPtr = nullptr;
                for (auto& buf : inBuffers) {
                    if (buf.BufferType == SECBUFFER_EXTRA && buf.cbBuffer > 0) {
                        extra = buf.cbBuffer;
                        extraPtr = buf.pvBuffer;
                        break;
                    }
                }
                if (extra > 0 && extraPtr) {
                    SwByteArray rest;
                    rest.resize(extra);
                    memcpy(rest.data(), extraPtr, extra);
                    inData = std::move(rest);
                } else {
                    inData.clear();
                }
                if (inData.isEmpty()) {
                    if (!recvMore(5000)) {
                        m_tlsMode = TlsState::Disabled;
                        goto handshake_fail;
                    }
                }
            } else {
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS blocking handshake failed status=" << status;
                m_tlsMode = TlsState::Disabled;
                goto handshake_fail;
            }
        }

        // Restore non-blocking
        {
            u_long nonBlocking = 1;
            ioctlsocket(m_socket, FIONBIO, &nonBlocking);
        }
        return true;

    handshake_fail:
        {
            u_long nonBlocking = 1;
            ioctlsocket(m_socket, FIONBIO, &nonBlocking);
        }
        emit errorOccurred(-2146893048); // propagate as TLS failure
        return false;
    }

    bool continueTlsHandshake(const char* inputData, size_t inputSize) {
        if (inputData && inputSize > 0) {
            m_tlsRecvBuffer.append(inputData, inputSize);
        }

        size_t inputBefore = m_tlsRecvBuffer.size();

        DWORD contextReq = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT |
                           ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM;
        DWORD contextAttr = 0;
        TimeStamp expiry{};

        SecBuffer inBuffers[2];
        inBuffers[0].BufferType = SECBUFFER_TOKEN;
        inBuffers[0].pvBuffer = m_tlsRecvBuffer.isEmpty() ? nullptr : m_tlsRecvBuffer.data();
        inBuffers[0].cbBuffer = (ULONG)m_tlsRecvBuffer.size();
        inBuffers[1].BufferType = SECBUFFER_EMPTY;
        inBuffers[1].pvBuffer = nullptr;
        inBuffers[1].cbBuffer = 0;

        SecBufferDesc inDesc;
        inDesc.ulVersion = SECBUFFER_VERSION;
        inDesc.cBuffers = 2;
        inDesc.pBuffers = inBuffers;

        SecBuffer outBuffer{};
        outBuffer.BufferType = SECBUFFER_TOKEN;
        outBuffer.pvBuffer = nullptr;
        outBuffer.cbBuffer = 0;
        SecBufferDesc outDesc{SECBUFFER_VERSION, 1, &outBuffer};

        std::wstring targetNameW;
        const wchar_t* targetPtr = nullptr;
        if (!m_effectiveTlsHost.isEmpty()) {
            targetNameW = m_effectiveTlsHost.toStdWString();
            targetPtr = targetNameW.c_str();
        }

        SECURITY_STATUS status = InitializeSecurityContextW(
            &m_credHandle,
            m_contextReady ? &m_ctxtHandle : nullptr,
            const_cast<wchar_t*>(targetPtr),
            contextReq,
            0,
            SECURITY_NATIVE_DREP,
            m_tlsRecvBuffer.isEmpty() ? nullptr : &inDesc,
            0,
            &m_ctxtHandle,
            &outDesc,
            &contextAttr,
            &expiry);

        size_t extra = 0;
        void* extraPtr = nullptr;
        for (auto& buf : inBuffers) {
            if (buf.BufferType == SECBUFFER_EXTRA && buf.cbBuffer > 0) {
                extra = buf.cbBuffer;
                extraPtr = buf.pvBuffer;
                break;
            }
        }

        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS ISC status=" << status
                  << " in=" << inputBefore
                  << " extra=" << extra
                  << " host=" << m_effectiveTlsHost.toStdString();

        if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK) {
            m_contextReady = true;
            if (outBuffer.pvBuffer && outBuffer.cbBuffer > 0) {
                m_tlsHandshakeBuffer.append(static_cast<const char*>(outBuffer.pvBuffer), outBuffer.cbBuffer);
            }
            if (outBuffer.pvBuffer) {
                FreeContextBuffer(outBuffer.pvBuffer);
            }
            if (!flushTlsHandshakeBuffer()) {
                return false;
            }
        }

        // Prepare input buffer for next call
        if (extra > 0 && extraPtr) {
            SwByteArray rest;
            rest.resize(extra);
            memcpy(rest.data(), extraPtr, extra);
            m_tlsRecvBuffer = std::move(rest);
        } else if (status == SEC_E_INCOMPLETE_MESSAGE) {
            // keep existing data; server has not sent a full record yet
        } else {
            m_tlsRecvBuffer.clear();
        }

        if (status == SEC_E_OK) {
            SECURITY_STATUS sizesStatus = QueryContextAttributes(&m_ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &m_streamSizes);
            if (sizesStatus != SEC_E_OK) {
                emit errorOccurred((int)sizesStatus);
                return false;
            }
            m_tlsMode = TlsState::Established;
            m_tlsDecryptedBuffer.clear();
            m_tlsHandshakeBuffer.clear();
            setState(ConnectedState);
            emit connected();
            if (!m_tlsRecvBuffer.isEmpty()) {
                decryptPendingTlsRecords();
            }
            return true;
        }

        if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE) {
            if (extra > 0 && extraPtr) {
                SwByteArray rest;
                rest.resize(extra);
                memcpy(rest.data(), extraPtr, extra);
                m_tlsRecvBuffer = std::move(rest);
            } else if (status == SEC_E_INCOMPLETE_MESSAGE) {
                // keep partial data
            } else {
                m_tlsRecvBuffer.clear();
            }
            return true;
        }

        if (!m_tlsRecvBuffer.isEmpty()) {
            size_t previewLen = std::min<size_t>(m_tlsRecvBuffer.size(), 64);
            std::string previewHex;
            previewHex.reserve(previewLen * 2);
            const char* hex = "0123456789ABCDEF";
            for (size_t i = 0; i < previewLen; ++i) {
                unsigned char c = static_cast<unsigned char>(m_tlsRecvBuffer[i]);
                previewHex.push_back(hex[c >> 4]);
                previewHex.push_back(hex[c & 0x0F]);
            }
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS handshake failed status=" << status
                      << " recvSize=" << m_tlsRecvBuffer.size()
                      << " preview=" << previewHex;
            std::ofstream tlsDump("tls_fail.bin", std::ios::binary | std::ios::trunc);
            if (tlsDump) {
                tlsDump.write(m_tlsRecvBuffer.data(), static_cast<std::streamsize>(m_tlsRecvBuffer.size()));
            }
        } else {
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS handshake failed status=" << status << " with no data";
        }
        emit errorOccurred((int)status);
        return false;
    }

    bool processTlsHandshakeData() {
        if (m_tlsMode != TlsState::Negotiating) {
            return true;
        }

        auto processBuffered = [this]() -> bool {
            if (m_tlsRecvBuffer.isEmpty()) {
                return true;
            }
            return continueTlsHandshake(nullptr, 0);
        };

        if (!processBuffered()) {
            return false;
        }
        if (m_tlsMode != TlsState::Negotiating) {
            return true;
        }

        char buffer[4096];
        int ret = ::recv(m_socket, buffer, sizeof(buffer), 0);
        if (ret == 0) {
            m_remoteClosed = true;
            return true;
        }
        if (ret < 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return true;
            }
            emit errorOccurred(err);
            return false;
        }

        if (!continueTlsHandshake(buffer, (size_t)ret)) {
            return false;
        }

        // Some handshakes complete fully with a single packet that contains extra data.
        while (m_tlsMode == TlsState::Negotiating && !m_tlsRecvBuffer.isEmpty()) {
            size_t before = m_tlsRecvBuffer.size();
            if (!continueTlsHandshake(nullptr, 0)) {
                return false;
            }
            if (m_tlsRecvBuffer.size() == before) {
                // No progress; wait for more bytes from the wire.
                break;
            }
        }
        return true;
    }

    bool decryptPendingTlsRecords() {
        while (!m_tlsRecvBuffer.isEmpty()) {
            SecBuffer buffers[4];
            buffers[0].BufferType = SECBUFFER_DATA;
            buffers[0].pvBuffer = m_tlsRecvBuffer.data();
            buffers[0].cbBuffer = (ULONG)m_tlsRecvBuffer.size();
            buffers[1].BufferType = SECBUFFER_EMPTY;
            buffers[2].BufferType = SECBUFFER_EMPTY;
            buffers[3].BufferType = SECBUFFER_EMPTY;

            SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};

            SECURITY_STATUS status = DecryptMessage(&m_ctxtHandle, &desc, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS decrypt incomplete, buffer=" << m_tlsRecvBuffer.size();
                return true;
            }
            bool contextExpired = (status == SEC_I_CONTEXT_EXPIRED);
            if (contextExpired) {
                swCWarning(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS context expired during decrypt";
                m_remoteClosed = true;
            } else if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS decrypt error " << status;
                emit errorOccurred((int)status);
                return false;
            }

            size_t extra = 0;
            void* extraPtr = nullptr;

            for (int i = 0; i < 4; ++i) {
                if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0) {
                    m_tlsDecryptedBuffer.append(static_cast<char*>(buffers[i].pvBuffer), buffers[i].cbBuffer);
                } else if (buffers[i].BufferType == SECBUFFER_EXTRA) {
                    extra = buffers[i].cbBuffer;
                    extraPtr = buffers[i].pvBuffer;
                }
            }

            if (extra > 0 && extraPtr) {
                SwByteArray rest;
                rest.resize(extra);
                memcpy(rest.data(), extraPtr, extra);
                m_tlsRecvBuffer = std::move(rest);
            } else {
                m_tlsRecvBuffer.clear();
            }

            if (status == SEC_I_RENEGOTIATE) {
                m_tlsMode = TlsState::Negotiating;
                return beginTlsHandshake();
            }

            if (contextExpired) {
                // Remote sent a close_notify; stop decrypting further but keep any leftover data.
                break;
            }
        }
        return true;
    }

    bool receiveTlsData() {
        // Dechiffrer tout ce qui est déjà en attente
        if (!m_tlsRecvBuffer.isEmpty()) {
            if (!decryptPendingTlsRecords()) {
                return false;
            }
            if (!m_tlsDecryptedBuffer.isEmpty()) {
                swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] decrypted buffer after pending=" << m_tlsDecryptedBuffer.size();
                return true;
            }
        }

        bool readSomething = false;
        while (true) {
            char buffer[4096];
            int ret = ::recv(m_socket, buffer, sizeof(buffer), 0);
            if (ret == 0) {
                m_remoteClosed = true;
                break;
            }
            if (ret < 0) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    if (!readSomething) {
                        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS recv would block";
                    }
                    break;
                }
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS recv error " << err;
                emit errorOccurred(err);
                return false;
            }

            readSomething = true;
            m_tlsRecvBuffer.append(buffer, static_cast<size_t>(ret));
            swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS recv read " << ret << " bytes";
        }

        if (!m_tlsRecvBuffer.isEmpty()) {
            return decryptPendingTlsRecords();
        }
        return true;
    }

    bool sendPendingEncrypted() {
        while (!m_tlsEncryptedBuffer.isEmpty()) {
            int sent = ::send(m_socket, m_tlsEncryptedBuffer.data(), (int)m_tlsEncryptedBuffer.size(), 0);
            if (sent == SOCKET_ERROR) {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) {
                    return false;
                }
                emit errorOccurred(err);
                return false;
            }
            m_tlsEncryptedBuffer.remove(0, (int)sent);
        }
        return true;
    }

    bool enqueueEncryptedChunk(size_t plainSize) {
        if (plainSize == 0)
            return true;

        size_t totalSize = m_streamSizes.cbHeader + plainSize + m_streamSizes.cbTrailer;
        SwByteArray out;
        out.resize(totalSize);

        memcpy(out.data() + m_streamSizes.cbHeader, m_writeBuffer.data(), plainSize);

        SecBuffer buffers[4];
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer = out.data();
        buffers[0].cbBuffer = m_streamSizes.cbHeader;
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer = out.data() + m_streamSizes.cbHeader;
        buffers[1].cbBuffer = (ULONG)plainSize;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer = out.data() + m_streamSizes.cbHeader + plainSize;
        buffers[2].cbBuffer = m_streamSizes.cbTrailer;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].pvBuffer = nullptr;
        buffers[3].cbBuffer = 0;

        SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
        SECURITY_STATUS status = EncryptMessage(&m_ctxtHandle, 0, &desc, 0);
        if (status != SEC_E_OK) {
            emit errorOccurred((int)status);
            return false;
        }

        m_tlsEncryptedBuffer.append(out.data(), buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer);
        m_writeBuffer.remove(0, (int)plainSize);
        return true;
    }

};

#else // !_WIN32

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

class SwTcpSocket : public SwAbstractSocket
{
    SW_OBJECT(SwTcpSocket, SwAbstractSocket)

public:
    /**
     * @brief Constructs a `SwTcpSocket` instance.
     * @param parent Optional parent object that owns this instance.
     * @param false Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwTcpSocket(SwObject *parent = nullptr)
        : SwAbstractSocket(parent),
          m_socket(-1),
          m_connecting(false)
    {
        startMonitoring();
    }

    /**
     * @brief Destroys the `SwTcpSocket` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwTcpSocket() override
    {
        stopMonitoring();
        close();
    }

    /**
     * @brief Performs the `useSsl` operation.
     * @param enabled Value passed to the method.
     * @param host Value passed to the method.
     */
    void useSsl(bool enabled, const SwString& host = SwString()) {
        m_useTls = enabled;
        if (!host.isEmpty()) {
            m_tlsHost = host;
        }
    }

    // StartTLS for already-connected sockets (useful after an HTTP proxy CONNECT tunnel).
    /**
     * @brief Starts the client Tls managed by the object.
     * @param host Value passed to the method.
     * @return `true` on success; otherwise `false`.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    bool startClientTls(const SwString& host = SwString()) {
        if (m_socket < 0 || state() != ConnectedState) {
            return false;
        }

        m_useTls = true;
        if (!host.isEmpty()) {
            m_tlsHost = host;
        }
        m_effectiveTlsHost = m_tlsHost.isEmpty() ? m_lastHost : m_tlsHost;
        if (m_effectiveTlsHost.isEmpty()) {
            m_effectiveTlsHost = host;
        }

        if (!initOpenSslBackend() || !performOpenSslBlockingHandshake()) {
            return false;
        }
        return true;
    }

    /**
     * @brief Returns whether the object reports remote Closed.
     * @return `true` when the object reports remote Closed; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isRemoteClosed() const {
        return m_remoteClosed;
    }

    /**
     * @brief Performs the `connectToHost` operation.
     * @param host Value passed to the method.
     * @param port Local port used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool connectToHost(const SwString &host, uint16_t port) override
    {
        close();
        m_lastHost = host;

        struct addrinfo hints = {};
        hints.ai_family = AF_INET; // prefer IPv4 like Windows path to avoid v6 stalls on unavailable stacks
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo *result = nullptr;
        if (getaddrinfo(host.toStdString().c_str(), SwString::number(port).toStdString().c_str(), &hints, &result) != 0 || result == nullptr)
        {
            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] getaddrinfo failed for " << host.toStdString() << ":" << port << " err=" << errno;
            emit errorOccurred(errno);
            return false;
        }

        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] DNS resolved for " << host.toStdString() << ":" << port;
        int fd = -1;
        for (auto *ptr = result; ptr != nullptr; ptr = ptr->ai_next)
        {
            fd = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (fd < 0)
            {
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] socket() failed err=" << errno;
                continue;
            }

            setNonBlocking(fd);

            if (::connect(fd, ptr->ai_addr, ptr->ai_addrlen) == 0)
            {
                swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] connect established immediately";
                break;
            }

            if (errno == EINPROGRESS)
            {
                swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] connect in progress (non-blocking)";
                m_connecting = true;
                break;
            }

            swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] connect failed err=" << errno;
            ::close(fd);
            fd = -1;
        }

        freeaddrinfo(result);

        if (fd < 0)
        {
            emit errorOccurred(errno);
            return false;
        }

        m_socket = fd;
        m_effectiveTlsHost = m_tlsHost.isEmpty() ? host : m_tlsHost;
        if (!m_connecting)
        {
            if (m_useTls) {
                if (!initOpenSslBackend()) {
                    close();
                    return false;
                }
                if (!performOpenSslBlockingHandshake()) {
                    close();
                    return false;
                }
                else {
                    swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS established (immediate connect)";
                }
            } else {
                setState(ConnectedState);
                emit connected();
            }
        }
        else
        {
            setState(ConnectingState);
        }

        return true;
    }

    /**
     * @brief Performs the `waitForConnected` operation.
     * @param msecs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool waitForConnected(int msecs = 30000) override
    {
        return waitForCondition([this]() { return state() == ConnectedState; }, msecs);
    }

    /**
     * @brief Performs the `waitForBytesWritten` operation.
     * @param msecs Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool waitForBytesWritten(int msecs = 30000) override
    {
        return waitForCondition([this]() { return m_writeBuffer.isEmpty(); }, msecs);
    }

    /**
     * @brief Performs the `shutdownWrite` operation.
     * @param lingerSeconds Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool shutdownWrite(int lingerSeconds = 5)
    {
        if (m_socket < 0 || state() != ConnectedState)
        {
            return false;
        }

        ::shutdown(m_socket, SHUT_WR);
        linger opt;
        opt.l_onoff = lingerSeconds > 0 ? 1 : 0;
        opt.l_linger = lingerSeconds;
        ::setsockopt(m_socket, SOL_SOCKET, SO_LINGER, &opt, sizeof(opt));
        return true;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close() override
    {
        if (m_closing)
            return;
        m_closing = true;

        if (m_socket >= 0)
        {
            ::close(m_socket);
            m_socket = -1;
        }

        m_writeBuffer.clear();
        m_connecting = false;
        m_remoteClosed = false;
        m_sslBackend.reset();
        m_tlsDecryptedBuffer.clear();

        bool wasConnected = (state() != UnconnectedState);
        setState(UnconnectedState);
        if (wasConnected) {
            emit disconnected();
        }
        m_closing = false;
    }

    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param maxSize Value passed to the method.
     * @return The resulting read.
     */
    SwString read(int64_t maxSize = 0) override
    {
        if (m_socket < 0)
        {
            return SwString();
        }

        if (m_useTls && m_tlsMode == TlsState::Established && m_sslBackend)
        {
            if (m_tlsDecryptedBuffer.isEmpty())
            {
                pumpOpenSslRead();
            }
            if (m_tlsDecryptedBuffer.isEmpty())
            {
                return SwString();
            }
            size_t toRead = (maxSize > 0 && maxSize < (int64_t)m_tlsDecryptedBuffer.size())
                                ? (size_t)maxSize
                                : m_tlsDecryptedBuffer.size();
            SwString result;
            result.resize((int)toRead);
            memcpy(result.data(), m_tlsDecryptedBuffer.data(), toRead);
            m_tlsDecryptedBuffer.remove(0, (int)toRead);
            if (m_remoteClosed && m_tlsDecryptedBuffer.isEmpty()) {
                close();
            }
            return result;
        }

        size_t bufferSize = (maxSize > 0 && maxSize < 4096) ? static_cast<size_t>(maxSize) : 4096;
        SwByteArray buffer;
        buffer.resize(bufferSize);
        ssize_t received = ::recv(m_socket, buffer.data(), buffer.size(), 0);

        if (received > 0)
        {
            SwString result;
            result.resize(static_cast<int>(received));
            std::memcpy(result.data(), buffer.data(), static_cast<size_t>(received));
            return result;
        }
        else if (received == 0)
        {
            close();
        }
        else if (errno != EWOULDBLOCK && errno != EAGAIN)
        {
            emit errorOccurred(errno);
            close();
        }

        return SwString();
    }

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool write(const SwString &data) override
    {
        swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] queue write size=" << data.size() << " tls=" << (m_useTls ? 1 : 0)
                  << " state=" << static_cast<int>(state());
        m_writeBuffer.append(data.toStdString());
        tryFlushWriteBuffer();
        return true;
    }

    /**
     * @brief Performs the `adoptSocket` operation.
     * @param fd Value passed to the method.
     */
    void adoptSocket(int fd)
    {
        close();
        m_socket = fd;
        setNonBlocking(m_socket);
        m_connecting = false;
        setState(ConnectedState);
        emit connected();
    }

protected:
    /**
     * @brief Performs the `onTimerDescriptor` operation.
     */
    void onTimerDescriptor() override
    {
        SwIODevice::onTimerDescriptor();
        pollEvents();
    }

private:
    void setNonBlocking(int fd)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1)
        {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    void pollEvents()
    {
        if (m_socket < 0)
        {
            return;
        }

        short events = POLLIN | POLLERR | POLLHUP;
        if (m_connecting || !m_writeBuffer.isEmpty())
        {
            events |= POLLOUT;
        }

        struct pollfd pfd { m_socket, events, 0 };
        int ret = ::poll(&pfd, 1, 0);
        if (ret <= 0)
        {
            return;
        }

        if (m_connecting && (pfd.revents & POLLOUT))
        {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)
            {
                m_connecting = false;
                if (m_useTls)
                {
                    if (!initOpenSslBackend() || !performOpenSslBlockingHandshake())
                    {
                        close();
                        return;
                    }
                    swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS established after poll";
                }
                else
                {
                    setState(ConnectedState);
                    emit connected();
                }
            }
            else
            {
                swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] connect failed after poll err=" << err;
                emit errorOccurred(err);
                close();
                return;
            }
        }

        if (pfd.revents & POLLIN)
        {
            if (m_useTls && m_tlsMode == TlsState::Established)
            {
                if (!pumpOpenSslRead())
                {
                    return;
                }
                if (!m_tlsDecryptedBuffer.isEmpty())
                {
                    emit readyRead();
                }
            }
            else
            {
                emit readyRead();
            }
        }

        if (!m_writeBuffer.isEmpty() && (pfd.revents & POLLOUT))
        {
            tryFlushWriteBuffer();
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &err, &len);
            emit errorOccurred(err);
            close();
        }
    }

    void tryFlushWriteBuffer()
    {
        if (m_socket < 0 || m_writeBuffer.isEmpty() || state() != ConnectedState)
        {
            return;
        }

        if (m_useTls && m_tlsMode == TlsState::Established && m_sslBackend)
        {
            while (!m_writeBuffer.isEmpty())
            {
                int written = 0;
                auto res = m_sslBackend->write(m_writeBuffer.data(), (int)m_writeBuffer.size(), written);
            if (res == SwBackendSsl::IoResult::Ok && written > 0)
            {
                 swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS wrote " << written << " bytes";
                m_writeBuffer.remove(0, (int)written);
                continue;
            }
            if (res == SwBackendSsl::IoResult::WantRead || res == SwBackendSsl::IoResult::WantWrite)
            {
                    break;
                }
                if (res == SwBackendSsl::IoResult::Closed)
                {
                    m_remoteClosed = true;
                    close();
                    return;
                }
                 swCError(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] OpenSSL write error: " << (m_sslBackend ? m_sslBackend->lastError() : "unknown");
                emit errorOccurred(errno);
                close();
                return;
            }
            if (m_writeBuffer.isEmpty())
            {
                emit writeFinished();
            }
            return;
        }

        ssize_t written = ::send(m_socket, m_writeBuffer.data(), m_writeBuffer.size(), 0);
        if (written > 0)
        {
            m_writeBuffer.remove(0, (int)written);
            if (m_writeBuffer.isEmpty())
            {
                emit writeFinished();
            }
        }
        else if (written < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
        {
            emit errorOccurred(errno);
            close();
        }
    }

    template <typename Condition>
    bool waitForCondition(Condition condition, int msecs)
    {
        using namespace std::chrono;
        auto start = steady_clock::now();
        int timeout = (msecs < 0) ? -1 : msecs;
        SwCoreApplication* app = SwCoreApplication::instance(false);

        while (!condition())
        {
            if (timeout >= 0)
            {
                auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
                if (elapsed >= timeout)
                {
                    return false;
                }
            }

            if (app)
            {
                SwCoreApplication::release();
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            pollEvents();
        }
        return true;
    }

    int m_socket;
    bool m_connecting;
    SwByteArray m_writeBuffer;

    bool m_useTls = false;
    enum class TlsState { Disabled, Negotiating, Established };
    TlsState m_tlsMode = TlsState::Disabled;
    SwString m_tlsHost;
    SwString m_effectiveTlsHost;
    SwString m_lastHost;
    std::unique_ptr<SwBackendSsl> m_sslBackend;
    SwByteArray m_tlsDecryptedBuffer;
    bool m_remoteClosed = false;
    bool m_closing = false;

    bool initOpenSslBackend()
    {
        if (!m_sslBackend)
        {
            m_sslBackend.reset(new SwBackendSsl());
        }
        if (!m_sslBackend->init(m_effectiveTlsHost.toStdString(), (intptr_t)m_socket))
        {
            emit errorOccurred(errno);
            return false;
        }
        m_tlsMode = TlsState::Negotiating;
        return true;
    }

    bool performOpenSslBlockingHandshake()
    {
        if (!m_sslBackend)
            return false;

        bool ok = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto res = m_sslBackend->handshake();
            if (res == SwBackendSsl::IoResult::Ok)
            {
                ok = true;
                break;
            }
            if (res == SwBackendSsl::IoResult::Closed || res == SwBackendSsl::IoResult::Error)
            {
                break;
            }
            // Wait for readiness
            fd_set rfds, wfds;
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            if (res == SwBackendSsl::IoResult::WantRead)
                FD_SET(m_socket, &rfds);
            if (res == SwBackendSsl::IoResult::WantWrite)
                FD_SET(m_socket, &wfds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 200 * 1000; // 200ms
            int sel = select(m_socket + 1, &rfds, &wfds, nullptr, &tv);
            if (sel < 0)
            {
                break;
            }
        }

        if (ok)
        {
            m_tlsMode = TlsState::Established;
            setState(ConnectedState);
            emit connected();
            return true;
        }
        emit errorOccurred(errno);
        return false;
    }

    bool pumpOpenSslRead()
    {
        if (!m_sslBackend)
            return false;
        char buffer[4096];
        while (true)
        {
            int bytes = 0;
            auto res = m_sslBackend->read(buffer, sizeof(buffer), bytes);
            if (res == SwBackendSsl::IoResult::Ok && bytes > 0)
            {
                swCDebug(kSwLogCategory_SwTcpSocket) << "[SwTcpSocket] TLS recv " << bytes << " bytes";
                m_tlsDecryptedBuffer.append(buffer, bytes);
                continue;
            }
            if (res == SwBackendSsl::IoResult::WantRead || res == SwBackendSsl::IoResult::WantWrite)
            {
                return true;
            }
            if (res == SwBackendSsl::IoResult::Closed)
            {
                m_remoteClosed = true;
                return true;
            }
            emit errorOccurred(errno);
            return false;
        }
    }
};

#endif // _WIN32
