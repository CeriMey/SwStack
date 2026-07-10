#pragma once

/**
 * @file src/core/io/SwSslSocket.h
 * @ingroup core_io
 * @brief TLS socket layered on top of SwTcpSocket with event-driven OpenSSL progression.
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

#include "SwBackendSsl.h"
#include "SwByteArray.h"
#include "SwList.h"
#include "SwPointer.h"
#include "SwString.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"

#include <algorithm>
#include <limits>
#include <memory>

static constexpr const char* kSwLogCategory_SwSslSocket = "sw.core.io.swsslsocket";

class SwSslServer;
using SwSslErrorList = SwList<SwString>;

class SwSslSocket : public SwTcpSocket {
    SW_OBJECT(SwSslSocket, SwTcpSocket)

public:
    explicit SwSslSocket(SwObject* parent = nullptr)
        : SwTcpSocket(parent) {
        swSocketTrafficSetTransportKind(socketTrafficState_, SwSocketTrafficTransportKind::Tls);
        m_tlsCloseTimer = new SwTimer(this);
        m_tlsCloseTimer->setSingleShot(true);
        connect(m_tlsCloseTimer, &SwTimer::timeout, this, [this]() {
            if (!m_tlsShutdownRequested) {
                return;
            }
            swCWarning(kSwLogCategory_SwSslSocket)
                << "[SwSslSocket] TLS close-notify deadline expired; aborting transport";
            abortTlsTransport_();
        });
    }

    ~SwSslSocket() override {
        // Destructors cannot wait for dispatcher readiness.  Drop the SSL object here and let
        // SwTcpSocket's destructor release the native transport without emitting callbacks.
        invalidateTlsCloseDeadline_();
        if (m_sslBackend) {
            m_sslBackend->abort();
            m_sslBackend.reset();
        }
        m_tlsPhase = TlsPhase::Disabled;
    }

    void setPeerHostName(const SwString& host) {
        m_peerHostName = host;
    }

    void setTrustedCaFile(const SwString& path) {
        m_trustedCaFile = path;
    }

    /**
     * Configure the exact ALPN identifiers offered by a client connection.
     * This must be called before encryption starts.  Identifiers are opaque
     * byte strings and no prefix/substring matching is performed.
     */
    bool setApplicationProtocols(const SwList<SwByteArray>& protocols) {
        if (m_tlsPhase != TlsPhase::Disabled || isSocketValid_()) {
            return false;
        }
        for (std::size_t i = 0; i < protocols.size(); ++i) {
            if (protocols[i].isEmpty() || protocols[i].size() > 255) {
                return false;
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (protocols[j] == protocols[i]) {
                    return false;
                }
            }
        }
        m_applicationProtocols = protocols;
        return true;
    }

    bool setApplicationProtocol(const SwByteArray& protocol) {
        SwList<SwByteArray> protocols;
        protocols.append(protocol);
        return setApplicationProtocols(protocols);
    }

    /** Exact ALPN selected by the completed handshake, or empty. */
    SwByteArray negotiatedApplicationProtocol() const {
        if (!m_sslBackend || !isEncrypted()) {
            return SwByteArray();
        }
        const std::string protocol = m_sslBackend->negotiatedApplicationProtocol();
        return SwByteArray(protocol.data(), protocol.size());
    }

    bool isTls13() const {
        return m_sslBackend && isEncrypted() && m_sslBackend->isTls13();
    }

    /**
     * RFC 8446 exporter with no context.  Empty means unavailable/failure;
     * callers must fail closed and never derive substitute keying material.
     */
    SwByteArray exportKeyingMaterial(const SwString& label, std::size_t length) const {
        if (!m_sslBackend || !isEncrypted() || label.isEmpty()) {
            return SwByteArray();
        }
        std::vector<unsigned char> output;
        if (!m_sslBackend->exportKeyingMaterial(label.toStdString(), nullptr, 0,
                                                false, length, output)) {
            return SwByteArray();
        }
        return SwByteArray(reinterpret_cast<const char*>(output.data()), output.size());
    }

    bool connectToHostEncrypted(const SwString& host, uint16_t port) {
        if (m_peerHostName.isEmpty()) {
            setPeerHostName(host);
        }
        m_preserveAutoStartOnClose = true;
        m_autoStartEncryptionOnConnect = true;
        return SwTcpSocket::connectToHost(host, port);
    }

    bool startClientEncryption() {
        if (!isSocketValid_() || (state() != ConnectedState && state() != ConnectingState)) {
            return false;
        }
        return beginClientEncryption_();
    }

    bool startServerEncryption(void* sharedSslCtx) {
        return startServerEncryption_(sharedSslCtx);
    }

    bool isEncrypted() const {
        return m_tlsPhase == TlsPhase::Encrypted;
    }

    int64_t readInto(char* data, int64_t maxSize) override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::readInto(data, maxSize);
        }
        if (!data || maxSize <= 0 ||
            (state() != ConnectedState && state() != ClosingState) ||
            m_tlsDecryptedBuffer.isEmpty()) {
            return 0;
        }

        const size_t toRead =
            (maxSize < static_cast<int64_t>(m_tlsDecryptedBuffer.size())) ? static_cast<size_t>(maxSize)
                                                                          : m_tlsDecryptedBuffer.size();
        const size_t bytes = m_tlsDecryptedBuffer.readInto(data, toRead);
        afterTlsReadDrained_();
        return static_cast<int64_t>(bytes);
    }

    SwByteArray read(int64_t maxSize = 0) override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::read(maxSize);
        }
        if (state() != ConnectedState && state() != ClosingState) {
            return SwByteArray();
        }
        if (m_tlsDecryptedBuffer.isEmpty()) {
            return SwByteArray();
        }

        const size_t toRead =
            (maxSize > 0 && maxSize < static_cast<int64_t>(m_tlsDecryptedBuffer.size())) ? static_cast<size_t>(maxSize)
                                                                                          : m_tlsDecryptedBuffer.size();
        SwByteArray result = m_tlsDecryptedBuffer.read(toRead);
        afterTlsReadDrained_();
        return result;
    }

    bool write(const SwString& data) override {
        return write(data.data(), data.size());
    }

    bool write(const SwByteArray& data) override {
        return write(data.constData(), data.size());
    }

    bool write(const char* data, std::size_t size) override {
        // SwTcpSocket owns the bounded admission policy. Its virtual onWriteQueued_ hook routes
        // accepted plaintext into the TLS service rather than the raw TCP flush path.
        return SwTcpSocket::write(data, size);
    }

    bool shutdownWrite(int lingerSeconds = 5) override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::shutdownWrite(lingerSeconds);
        }
        if (!isSocketValid_() ||
            (state() != ConnectedState && state() != ClosingState) ||
            m_tlsPhase != TlsPhase::Encrypted) {
            return false;
        }
        const int maxSeconds = (std::numeric_limits<int>::max)() / 1000;
        const int timeoutMs = lingerSeconds <= 0
                                  ? 0
                                  : (std::min)(lingerSeconds, maxSeconds) * 1000;
        requestTlsShutdown_(timeoutMs);
        return true;
    }

    void close() override {
        const bool preserveAutoStart = m_preserveAutoStartOnClose;
        m_preserveAutoStartOnClose = false;
        if (!preserveAutoStart) {
            m_autoStartEncryptionOnConnect = false;
        }

        if (m_tlsPhase == TlsPhase::Disabled) {
            SwTcpSocket::close();
            return;
        }
        if (!isSocketValid_() || m_tlsPhase != TlsPhase::Encrypted) {
            abortTlsTransport_();
            return;
        }
        requestTlsShutdown_(m_tlsCloseTimeoutMs);
    }

    void setTlsCloseTimeout(int timeoutMs) {
        m_tlsCloseTimeoutMs = (std::max)(0, timeoutMs);
    }

    int tlsCloseTimeout() const {
        return m_tlsCloseTimeoutMs;
    }

    void setTlsReadBufferWatermarks(std::size_t highWatermark,
                                    std::size_t resumeWatermark) {
        if (highWatermark == 0) {
            return;
        }
        m_tlsReadHighWatermark = highWatermark;
        m_tlsReadResumeWatermark = (std::min)(resumeWatermark, highWatermark);
    }

    std::size_t tlsBufferedReadBytes() const {
        return m_tlsDecryptedBuffer.size();
    }

signals:
    DECLARE_SIGNAL_VOID(encrypted)
    DECLARE_SIGNAL(sslErrors, SwSslErrorList)

protected:
    enum class TlsPhase {
        Disabled,
        Handshake,
        Encrypted,
        Failed
    };

    enum class TlsOperation {
        None,
        Handshake,
        Read,
        Write,
        Shutdown
    };

    enum class WaitCondition {
        None,
        Readable,
        Writable
    };

    bool handleTransportConnectedEvent_() override {
        if (!m_autoStartEncryptionOnConnect) {
            return SwTcpSocket::handleTransportConnectedEvent_();
        }
        m_autoStartEncryptionOnConnect = false;
        return beginClientEncryption_();
    }

    bool handleTransportReadableEvent_() override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::handleTransportReadableEvent_();
        }
        if (!m_readNotificationsEnabled &&
            m_tlsPhase == TlsPhase::Encrypted &&
            !m_tlsShutdownRequested) {
            return true;
        }
        m_socketReadableReady = true;
        scheduleTlsService_();
        return true;
    }

    bool handleTransportWritableEvent_() override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::handleTransportWritableEvent_();
        }
        m_socketWritableReady = true;
        scheduleTlsService_();
        return true;
    }

    void handleTransportCloseEvent_() override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            SwTcpSocket::handleTransportCloseEvent_();
            return;
        }
        m_remoteClosed = true;
        m_socketReadableReady = true;
        scheduleTlsService_();
    }

    bool shouldCloseAfterTransportClose_() const override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::shouldCloseAfterTransportClose_();
        }
        // The TLS service must answer a peer close-notify before the TCP transport is released.
        return false;
    }

    void onWriteQueued_() override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            SwTcpSocket::onWriteQueued_();
            return;
        }
        scheduleTlsService_();
    }

#if !defined(_WIN32)
    SwIoDispatcher::EventMask desiredDispatcherEvents_() const override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::desiredDispatcherEvents_();
        }

        uint32_t events = SwIoDispatcher::Error;
        if (m_readNotificationsEnabled || m_tlsPhase == TlsPhase::Handshake ||
            m_tlsShutdownRequested) {
            events |= SwIoDispatcher::Readable | SwIoDispatcher::Hangup;
        }
        bool wantsWritable = m_connecting;
        if (m_activeOperation != TlsOperation::None) {
            wantsWritable = wantsWritable ||
                            m_waitingFor == WaitCondition::Writable ||
                            (m_waitingFor == WaitCondition::None &&
                             (m_activeOperation == TlsOperation::Handshake ||
                              m_activeOperation == TlsOperation::Write ||
                              m_activeOperation == TlsOperation::Shutdown ||
                              !m_writeBuffer.isEmpty()));
        } else {
            wantsWritable = wantsWritable ||
                            m_tlsPhase == TlsPhase::Handshake ||
                            !m_writeBuffer.isEmpty() ||
                            m_tlsShutdownRequested;
        }
        if (wantsWritable) {
            events |= SwIoDispatcher::Writable;
        }
        return events;
    }
#endif

private slots:
    void serviceTls_() {
        m_serviceScheduled = false;
        if (m_serviceRunning) {
            m_serviceAgain = true;
            return;
        }
        if (!m_sslBackend || !isSocketValid_()) {
            return;
        }

        m_serviceRunning = true;
        m_serviceAgain = false;

        const TlsOperation operation = selectOperation_();
        bool ok = true;
        switch (operation) {
        case TlsOperation::Handshake:
            ok = stepHandshake_();
            break;
        case TlsOperation::Read:
            ok = pumpOpenSslRead_();
            break;
        case TlsOperation::Write:
            ok = pumpOpenSslWrite_();
            break;
        case TlsOperation::Shutdown:
            ok = stepShutdown_();
            break;
        case TlsOperation::None:
            break;
        }

        if (!SwObject::isLive(this)) {
            return;
        }
        m_serviceRunning = false;
        updateDispatcherInterest_();

        if (!ok || !isSocketValid_()) {
            return;
        }
        if (shouldReschedule_(operation)) {
            scheduleTlsService_();
        }
    }

private:
    friend class SwSslServer;

    SwString m_peerHostName;
    SwString m_trustedCaFile;
    SwList<SwByteArray> m_applicationProtocols;
    std::unique_ptr<SwBackendSsl> m_sslBackend;
    TlsPhase m_tlsPhase = TlsPhase::Disabled;
    TlsOperation m_activeOperation = TlsOperation::None;
    WaitCondition m_waitingFor = WaitCondition::None;
    bool m_autoStartEncryptionOnConnect = false;
    bool m_preserveAutoStartOnClose = false;
    bool m_socketReadableReady = false;
    bool m_socketWritableReady = false;
    bool m_serviceScheduled = false;
    bool m_serviceRunning = false;
    bool m_serviceAgain = false;
    bool m_tlsShutdownRequested = false;
    bool m_peerCloseNotifyReceived = false;
    int m_tlsCloseTimeoutMs = 5000;
    std::size_t m_tlsReadHighWatermark = 4 * 1024 * 1024;
    std::size_t m_tlsReadResumeWatermark = 2 * 1024 * 1024;
    std::size_t m_tlsReadBudget = 256 * 1024;
    SwTimer* m_tlsCloseTimer = nullptr;
    SwByteRingBuffer m_tlsDecryptedBuffer;

    bool beginClientEncryption_() {
        if (!m_sslBackend) {
            m_sslBackend.reset(new SwBackendSsl());
        }

        std::vector<std::string> applicationProtocols;
        applicationProtocols.reserve(m_applicationProtocols.size());
        for (std::size_t i = 0; i < m_applicationProtocols.size(); ++i) {
            applicationProtocols.emplace_back(m_applicationProtocols[i].constData(),
                                              m_applicationProtocols[i].size());
        }
        if (!m_sslBackend->setApplicationProtocols(applicationProtocols)) {
            return failSsl_(-2146893048,
                            "[SwSslSocket] TLS ALPN configuration failed: " +
                                m_sslBackend->lastError());
        }

        SwString effectiveHost = m_peerHostName;
        if (effectiveHost.isEmpty()) {
            effectiveHost = m_lastHost;
        }
        if (effectiveHost.isEmpty()) {
            return failSsl_(-2146893048, "[SwSslSocket] TLS peer hostname missing");
        }

        if (!m_sslBackend->init(effectiveHost.toStdString(),
                                nativeSocket_(),
                                m_trustedCaFile.toStdString())) {
            return failSsl_(-2146893048, "[SwSslSocket] OpenSSL client init failed: " + m_sslBackend->lastError());
        }
        setState(ConnectingState);
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        m_tlsPhase = TlsPhase::Handshake;
        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        m_socketReadableReady = true;
        m_socketWritableReady = true;
        m_tlsShutdownRequested = false;
        m_peerCloseNotifyReceived = false;
        m_readNotificationsEnabled = true;
        invalidateTlsCloseDeadline_();
        m_tlsDecryptedBuffer.clear();
        scheduleTlsService_();
        return true;
    }

    bool startServerEncryption_(void* sharedSslCtx) {
        if (!isSocketValid_() || state() != ConnectedState || !sharedSslCtx) {
            return false;
        }
        if (!m_sslBackend) {
            m_sslBackend.reset(new SwBackendSsl());
        }
        if (!m_sslBackend->initServer(sharedSslCtx, nativeSocket_())) {
            return failSsl_(-2146893048, "[SwSslSocket] OpenSSL server init failed: " + m_sslBackend->lastError());
        }

        setState(ConnectingState);
        swSocketTrafficSetOpenState(socketTrafficState_, true);
        refreshTrafficMonitorEndpoints_();
        m_tlsPhase = TlsPhase::Handshake;
        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        m_socketReadableReady = true;
        m_socketWritableReady = true;
        m_tlsShutdownRequested = false;
        m_peerCloseNotifyReceived = false;
        m_readNotificationsEnabled = true;
        invalidateTlsCloseDeadline_();
        m_tlsDecryptedBuffer.clear();
        scheduleTlsService_();
        return true;
    }

    void scheduleTlsService_() {
        if (m_serviceScheduled || !SwObject::isLive(this)) {
            return;
        }
        m_serviceScheduled = true;
        SwTimer::singleShot(0, this, &SwSslSocket::serviceTls_);
        updateDispatcherInterest_();
    }

    void afterTlsReadDrained_() {
        if (m_tlsDecryptedBuffer.size() > m_tlsReadResumeWatermark) {
            return;
        }

        if (m_peerCloseNotifyReceived && m_tlsDecryptedBuffer.isEmpty()) {
            requestTlsShutdown_(m_tlsCloseTimeoutMs);
            return;
        }

        if (!m_readNotificationsEnabled &&
            m_tlsPhase == TlsPhase::Encrypted &&
            !m_tlsShutdownRequested && isSocketValid_()) {
            // The high watermark deliberately removed the socket's readable
            // interest.  Once the application has drained enough plaintext,
            // retry SSL_read once: OpenSSL may already hold another complete
            // record even when the kernel does not emit a fresh edge.
            m_readNotificationsEnabled = true;
            m_socketReadableReady = true;
            scheduleTlsService_();
        }
    }

    TlsOperation selectOperation_() const {
        if (m_tlsPhase == TlsPhase::Disabled || m_tlsPhase == TlsPhase::Failed) {
            return TlsOperation::None;
        }

        if (m_tlsShutdownRequested && m_tlsPhase == TlsPhase::Encrypted) {
            // Preserve an in-flight SSL_write until all admitted plaintext is encrypted.  An
            // idle SSL_read may be abandoned: no new application data is delivered once local
            // shutdown starts, and SSL_shutdown owns the protocol progression from here.
            if (!m_writeBuffer.isEmpty()) {
                if (m_activeOperation != TlsOperation::Write ||
                    m_waitingFor == WaitCondition::None) {
                    return TlsOperation::Write;
                }
                if (m_waitingFor == WaitCondition::Readable && m_socketReadableReady) {
                    return TlsOperation::Write;
                }
                if (m_waitingFor == WaitCondition::Writable && m_socketWritableReady) {
                    return TlsOperation::Write;
                }
                return TlsOperation::None;
            }

            if (m_activeOperation != TlsOperation::Shutdown ||
                m_waitingFor == WaitCondition::None) {
                return TlsOperation::Shutdown;
            }
            if (m_waitingFor == WaitCondition::Readable && m_socketReadableReady) {
                return TlsOperation::Shutdown;
            }
            if (m_waitingFor == WaitCondition::Writable && m_socketWritableReady) {
                return TlsOperation::Shutdown;
            }
            return TlsOperation::None;
        }

        if (m_activeOperation != TlsOperation::None) {
            if (m_activeOperation == TlsOperation::Handshake) {
                switch (m_waitingFor) {
                case WaitCondition::None:
                    return m_activeOperation;
                case WaitCondition::Readable:
                    return m_socketReadableReady ? m_activeOperation : TlsOperation::None;
                case WaitCondition::Writable:
                    return m_socketWritableReady ? m_activeOperation : TlsOperation::None;
                }
            }

            if (m_activeOperation == TlsOperation::Read) {
                if (!m_writeBuffer.isEmpty()) {
                    return TlsOperation::Write;
                }
                if (m_waitingFor == WaitCondition::None) {
                    return TlsOperation::Read;
                }
                if (m_waitingFor == WaitCondition::Readable && m_socketReadableReady) {
                    return TlsOperation::Read;
                }
                if (m_waitingFor == WaitCondition::Writable && m_socketWritableReady) {
                    return TlsOperation::Read;
                }
                return TlsOperation::None;
            }

            if (m_activeOperation == TlsOperation::Write) {
                if (m_waitingFor == WaitCondition::None) {
                    return TlsOperation::Write;
                }
                if (m_waitingFor == WaitCondition::Readable && m_socketReadableReady) {
                    return TlsOperation::Write;
                }
                if (m_waitingFor == WaitCondition::Writable && m_socketWritableReady) {
                    return TlsOperation::Write;
                }
                if (m_socketReadableReady) {
                    return TlsOperation::Read;
                }
                return TlsOperation::None;
            }

            switch (m_waitingFor) {
            case WaitCondition::None:
                return m_activeOperation;
            case WaitCondition::Readable:
                return m_socketReadableReady ? m_activeOperation : TlsOperation::None;
            case WaitCondition::Writable:
                return m_socketWritableReady ? m_activeOperation : TlsOperation::None;
            }
        }

        if (m_tlsPhase == TlsPhase::Handshake) {
            return TlsOperation::Handshake;
        }
        if (!m_writeBuffer.isEmpty()) {
            return TlsOperation::Write;
        }
        if (m_socketReadableReady) {
            return TlsOperation::Read;
        }
        if (m_remoteClosed) {
            return TlsOperation::Read;
        }
        return TlsOperation::None;
    }

    bool stepHandshake_() {
        consumeWaitFlag_();

        const auto result = m_sslBackend->handshake();
        if (result == SwBackendSsl::IoResult::Ok) {
            const std::uint64_t transportGeneration = m_transportGeneration;
            SwPointer<SwSslSocket> self(this);
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
            m_tlsPhase = TlsPhase::Encrypted;
            setState(ConnectedState);
            emit connected();
            if (!self || m_transportGeneration != transportGeneration ||
                m_tlsPhase != TlsPhase::Encrypted || state() != ConnectedState) {
                return false;
            }
            emit encrypted();
            return self && m_transportGeneration == transportGeneration &&
                   m_tlsPhase == TlsPhase::Encrypted;
        }
        if (result == SwBackendSsl::IoResult::WantRead) {
            m_activeOperation = TlsOperation::Handshake;
            m_waitingFor = WaitCondition::Readable;
            return true;
        }
        if (result == SwBackendSsl::IoResult::WantWrite) {
            m_activeOperation = TlsOperation::Handshake;
            m_waitingFor = WaitCondition::Writable;
            return true;
        }
        return failSsl_(-2146893048, "[SwSslSocket] OpenSSL handshake failed: " + m_sslBackend->lastError());
    }

    bool pumpOpenSslRead_() {
        if (!m_readNotificationsEnabled && !m_tlsShutdownRequested) {
            return true;
        }
        if (m_activeOperation == TlsOperation::Read) {
            consumeWaitFlag_();
        } else {
            m_socketReadableReady = false;
        }

        char buffer[16 * 1024];
        bool appended = false;
        std::size_t budget = m_tlsReadBudget;
        std::size_t syscalls = 0;
        while (budget > 0 && syscalls < 64 &&
               m_tlsDecryptedBuffer.size() < m_tlsReadHighWatermark) {
            const std::size_t capacity = m_tlsReadHighWatermark - m_tlsDecryptedBuffer.size();
            const int toRead = static_cast<int>((std::min)(
                (std::min)(sizeof(buffer), budget), capacity));
            if (toRead <= 0) {
                break;
            }
            int bytes = 0;
            ++syscalls;
            const auto result = m_sslBackend->read(buffer, toRead, bytes);
            if (result == SwBackendSsl::IoResult::Ok && bytes > 0) {
                incrementTotalReceivedBytes_(static_cast<size_t>(bytes));
                m_tlsDecryptedBuffer.append(buffer, bytes);
                budget -= static_cast<std::size_t>(bytes);
                appended = true;
                continue;
            }
            if (result == SwBackendSsl::IoResult::WantRead) {
                m_activeOperation = TlsOperation::Read;
                m_waitingFor = WaitCondition::Readable;
                break;
            }
            if (result == SwBackendSsl::IoResult::WantWrite) {
                m_activeOperation = TlsOperation::Read;
                m_waitingFor = WaitCondition::Writable;
                break;
            }
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
            if (result == SwBackendSsl::IoResult::Closed) {
                m_remoteClosed = true;
                m_peerCloseNotifyReceived = true;
                break;
            }
            return failSsl_(-7, "[SwSslSocket] OpenSSL read failed: " + m_sslBackend->lastError());
        }

        if (m_tlsDecryptedBuffer.size() >= m_tlsReadHighWatermark) {
            m_readNotificationsEnabled = false;
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
        } else if (budget == 0 || syscalls >= 64) {
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
            m_serviceAgain = true;
        }

        if (appended) {
            const std::uint64_t transportGeneration = m_transportGeneration;
            SwPointer<SwSslSocket> self(this);
            emit readyRead();
            if (!self || m_transportGeneration != transportGeneration ||
                m_tlsPhase == TlsPhase::Disabled || m_tlsPhase == TlsPhase::Failed) {
                return false;
            }
        }
        if (m_peerCloseNotifyReceived) {
            m_readNotificationsEnabled = false;
            if (m_tlsDecryptedBuffer.isEmpty()) {
                requestTlsShutdown_(m_tlsCloseTimeoutMs);
            }
        }
        return true;
    }

    bool pumpOpenSslWrite_() {
        if (m_activeOperation == TlsOperation::Write) {
            consumeWaitFlag_();
        }

        const std::uint64_t transportGeneration = m_transportGeneration;
        std::size_t budget = m_writeFlushBudget;
        while (!m_writeBuffer.isEmpty() && budget > 0) {
            const char* data = m_writeBuffer.contiguousData();
            const size_t available = m_writeBuffer.contiguousSize();
            if (!data || available == 0) {
                break;
            }

            const int toWrite = static_cast<int>((std::min)(
                (std::min)(available, static_cast<size_t>(16 * 1024)), budget));
            int written = 0;
            const auto result = m_sslBackend->write(data, toWrite, written);
            if (result == SwBackendSsl::IoResult::Ok && written > 0) {
                incrementTotalSentBytes_(static_cast<size_t>(written));
                m_writeBuffer.consume(static_cast<size_t>(written));
                budget -= static_cast<size_t>(written);
                continue;
            }
            if (result == SwBackendSsl::IoResult::WantRead) {
                m_activeOperation = TlsOperation::Write;
                m_waitingFor = WaitCondition::Readable;
                return true;
            }
            if (result == SwBackendSsl::IoResult::WantWrite) {
                m_activeOperation = TlsOperation::Write;
                m_waitingFor = WaitCondition::Writable;
                return true;
            }
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
            if (result == SwBackendSsl::IoResult::Closed) {
                m_remoteClosed = true;
                m_peerCloseNotifyReceived = true;
                return failSsl_(-5,
                                "[SwSslSocket] TLS peer closed while plaintext was pending");
            }
            return failSsl_(-5, "[SwSslSocket] OpenSSL write failed: " + m_sslBackend->lastError());
        }

        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        if (!notifyWriteBufferLowWatermark_() ||
            m_transportGeneration != transportGeneration) {
            return false;
        }
        if (!m_writeBuffer.isEmpty()) {
            // Yield after the same fairness budget as plain TCP, then schedule another service
            // turn without waiting for a new network edge if OpenSSL kept making progress.
            m_serviceAgain = true;
            return true;
        }
        emit writeFinished();
        if (!SwObject::isLive(this) || m_transportGeneration != transportGeneration) {
            return false;
        }
        if (m_tlsShutdownRequested) {
            m_serviceAgain = true;
            return true;
        }
        closeIfRemoteClosedAndIdle_();
        return true;
    }

    bool stepShutdown_() {
        if (!m_tlsShutdownRequested || !m_sslBackend) {
            return true;
        }
        if (!m_writeBuffer.isEmpty()) {
            m_serviceAgain = true;
            return true;
        }

        if (m_activeOperation == TlsOperation::Shutdown) {
            consumeWaitFlag_();
        } else {
            // Stop carrying readiness from a previously idle SSL_read into SSL_shutdown.
            m_socketReadableReady = false;
            m_socketWritableReady = false;
        }

        bool complete = false;
        const SwBackendSsl::IoResult result = m_sslBackend->shutdownStep(complete);
        if (result == SwBackendSsl::IoResult::Ok && complete) {
            completeTlsShutdown_();
            return false;
        }
        if (result == SwBackendSsl::IoResult::WantRead) {
            m_activeOperation = TlsOperation::Shutdown;
            m_waitingFor = WaitCondition::Readable;
            return true;
        }
        if (result == SwBackendSsl::IoResult::WantWrite) {
            m_activeOperation = TlsOperation::Shutdown;
            m_waitingFor = WaitCondition::Writable;
            return true;
        }
        if (result == SwBackendSsl::IoResult::Closed) {
            completeTlsShutdown_();
            return false;
        }
        return failSsl_(-5, "[SwSslSocket] OpenSSL shutdown failed: " + m_sslBackend->lastError());
    }

    void requestTlsShutdown_(int timeoutMs) {
        if (m_tlsShutdownRequested || m_tlsPhase != TlsPhase::Encrypted || !isSocketValid_()) {
            return;
        }

        m_tlsShutdownRequested = true;
        m_writeShutdownRequested = true;
        m_closeRequested = true;
        setState(ClosingState);

        // No application read is kept outstanding once shutdown owns the TLS state machine.
        // SSL_write, on the other hand, must be retried with the admitted plaintext intact.
        if (m_activeOperation == TlsOperation::Read) {
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
        }

        scheduleTlsService_();
        if (m_tlsCloseTimer) {
            m_tlsCloseTimer->start((std::max)(0, timeoutMs));
        }
    }

    void invalidateTlsCloseDeadline_() {
        if (m_tlsCloseTimer) {
            m_tlsCloseTimer->stop();
        }
    }

    void resetTlsServiceState_() {
        invalidateTlsCloseDeadline_();
        m_serviceScheduled = false;
        m_serviceAgain = false;
        m_socketReadableReady = false;
        m_socketWritableReady = false;
        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        m_tlsShutdownRequested = false;
        m_peerCloseNotifyReceived = false;
        m_readNotificationsEnabled = true;
        m_tlsDecryptedBuffer.release();
    }

    void completeTlsShutdown_() {
        resetTlsServiceState_();
        m_tlsPhase = TlsPhase::Disabled;
        if (m_sslBackend) {
            m_sslBackend->abort();
            m_sslBackend.reset();
        }
        // All plaintext was encrypted and the TLS alert exchange is complete.  The base close
        // can now release TCP without ever exposing queued plaintext on the raw transport.
        SwTcpSocket::close();
    }

    void abortTlsTransport_() {
        resetTlsServiceState_();
        m_tlsPhase = TlsPhase::Disabled;
        if (m_sslBackend) {
            m_sslBackend->abort();
            m_sslBackend.reset();
        }
        SwTcpSocket::abort();
    }

    void consumeWaitFlag_() {
        switch (m_waitingFor) {
        case WaitCondition::Readable:
            m_socketReadableReady = false;
            break;
        case WaitCondition::Writable:
            m_socketWritableReady = false;
            break;
        case WaitCondition::None:
            break;
        }
    }

    bool shouldReschedule_(TlsOperation performed) const {
        if (m_serviceAgain) {
            return true;
        }
        if (m_activeOperation != TlsOperation::None) {
            if (m_activeOperation == TlsOperation::Read && !m_writeBuffer.isEmpty()) {
                return true;
            }
            if (m_activeOperation == TlsOperation::Write && m_socketReadableReady) {
                return true;
            }
            if (m_waitingFor == WaitCondition::Readable && m_socketReadableReady) {
                return true;
            }
            if (m_waitingFor == WaitCondition::Writable && m_socketWritableReady) {
                return true;
            }
            return false;
        }
        if (m_tlsPhase == TlsPhase::Handshake) {
            return true;
        }
        if (m_tlsPhase == TlsPhase::Encrypted) {
            if (m_tlsShutdownRequested && m_activeOperation == TlsOperation::None) {
                return true;
            }
            if (performed != TlsOperation::Read && m_socketReadableReady) {
                return true;
            }
            if (performed != TlsOperation::Write && !m_writeBuffer.isEmpty()) {
                return true;
            }
        }
        return false;
    }

    bool failSsl_(int code, const std::string& message) {
        m_tlsPhase = TlsPhase::Failed;
        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        swCError(kSwLogCategory_SwSslSocket) << message;

        SwSslErrorList errors;
        errors.append(SwString(message.c_str()));
        emit sslErrors(errors);
        if (!SwObject::isLive(this)) {
            return false;
        }
        emit errorOccurred(code);
        if (!SwObject::isLive(this)) {
            return false;
        }
        abortTlsTransport_();
        return false;
    }
};
