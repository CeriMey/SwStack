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
#include "SwString.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"

#include <algorithm>
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
    }

    ~SwSslSocket() override {
        close();
    }

    void setPeerHostName(const SwString& host) {
        m_peerHostName = host;
    }

    void setTrustedCaFile(const SwString& path) {
        m_trustedCaFile = path;
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

    SwString read(int64_t maxSize = 0) override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::read(maxSize);
        }
        if (state() != ConnectedState) {
            return "";
        }
        if (m_tlsDecryptedBuffer.isEmpty()) {
            return "";
        }

        const size_t toRead =
            (maxSize > 0 && maxSize < static_cast<int64_t>(m_tlsDecryptedBuffer.size())) ? static_cast<size_t>(maxSize)
                                                                                         : m_tlsDecryptedBuffer.size();
        SwString result = SwString::fromLatin1(m_tlsDecryptedBuffer.data(), static_cast<int>(toRead));
        m_tlsDecryptedBuffer.remove(0, static_cast<int>(toRead));
        closeIfRemoteClosedAndIdle_();
        return result;
    }

    bool write(const SwString& data) override {
        if (m_tlsPhase == TlsPhase::Disabled) {
            return SwTcpSocket::write(data);
        }
        if (!isSocketValid_() || state() != ConnectedState) {
            return false;
        }
        m_writeBuffer.append(data.toStdString());
        scheduleTlsService_();
        return true;
    }

    void close() override {
        const bool preserveAutoStart = m_preserveAutoStartOnClose;
        m_preserveAutoStartOnClose = false;
        if (!preserveAutoStart) {
            m_autoStartEncryptionOnConnect = false;
        }
        m_serviceScheduled = false;
        m_serviceRunning = false;
        m_serviceAgain = false;
        m_socketReadableReady = false;
        m_socketWritableReady = false;
        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        m_tlsPhase = TlsPhase::Disabled;
        m_tlsDecryptedBuffer.clear();
        if (m_sslBackend) {
            m_sslBackend->shutdown();
            m_sslBackend.reset();
        }
        SwTcpSocket::close();
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
        Write
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
        return m_remoteClosed && m_tlsDecryptedBuffer.isEmpty() && m_writeBuffer.isEmpty() &&
               m_activeOperation == TlsOperation::None;
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

        uint32_t events = SwIoDispatcher::Readable | SwIoDispatcher::Error | SwIoDispatcher::Hangup;
        if (m_connecting || m_tlsPhase == TlsPhase::Handshake || !m_writeBuffer.isEmpty() ||
            m_activeOperation == TlsOperation::Write ||
            (m_activeOperation == TlsOperation::Read && m_waitingFor == WaitCondition::Writable) ||
            (m_activeOperation == TlsOperation::Handshake && m_waitingFor == WaitCondition::Writable)) {
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
        case TlsOperation::None:
            break;
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
    SwByteArray m_tlsDecryptedBuffer;

    bool beginClientEncryption_() {
        if (!m_sslBackend) {
            m_sslBackend.reset(new SwBackendSsl());
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

    TlsOperation selectOperation_() const {
        if (m_tlsPhase == TlsPhase::Disabled || m_tlsPhase == TlsPhase::Failed) {
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
                if (m_waitingFor == WaitCondition::None) {
                    return TlsOperation::Read;
                }
                if (m_waitingFor == WaitCondition::Readable && m_socketReadableReady) {
                    return TlsOperation::Read;
                }
                if (m_waitingFor == WaitCondition::Writable && m_socketWritableReady) {
                    return TlsOperation::Read;
                }
                if (!m_writeBuffer.isEmpty()) {
                    return TlsOperation::Write;
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
        if (m_socketReadableReady) {
            return TlsOperation::Read;
        }
        if (!m_writeBuffer.isEmpty()) {
            return TlsOperation::Write;
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
            m_activeOperation = TlsOperation::None;
            m_waitingFor = WaitCondition::None;
            m_tlsPhase = TlsPhase::Encrypted;
            setState(ConnectedState);
            emit connected();
            emit encrypted();
            return true;
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
        if (m_activeOperation == TlsOperation::Read) {
            consumeWaitFlag_();
        } else {
            m_socketReadableReady = false;
        }

        char buffer[4096];
        bool appended = false;
        while (true) {
            int bytes = 0;
            const auto result = m_sslBackend->read(buffer, static_cast<int>(sizeof(buffer)), bytes);
            if (result == SwBackendSsl::IoResult::Ok && bytes > 0) {
                incrementTotalReceivedBytes_(static_cast<size_t>(bytes));
                m_tlsDecryptedBuffer.append(buffer, bytes);
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
                break;
            }
            return failSsl_(-7, "[SwSslSocket] OpenSSL read failed: " + m_sslBackend->lastError());
        }

        if (appended) {
            emit readyRead();
        }
        closeIfRemoteClosedAndIdle_();
        return true;
    }

    bool pumpOpenSslWrite_() {
        if (m_activeOperation == TlsOperation::Write) {
            consumeWaitFlag_();
        }

        while (!m_writeBuffer.isEmpty()) {
            const int toWrite = static_cast<int>(std::min<size_t>(m_writeBuffer.size(), 16 * 1024));
            int written = 0;
            const auto result = m_sslBackend->write(m_writeBuffer.data(), toWrite, written);
            if (result == SwBackendSsl::IoResult::Ok && written > 0) {
                incrementTotalSentBytes_(static_cast<size_t>(written));
                m_writeBuffer.remove(0, written);
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
                closeIfRemoteClosedAndIdle_();
                return true;
            }
            return failSsl_(-5, "[SwSslSocket] OpenSSL write failed: " + m_sslBackend->lastError());
        }

        m_activeOperation = TlsOperation::None;
        m_waitingFor = WaitCondition::None;
        emit writeFinished();
        closeIfRemoteClosedAndIdle_();
        return true;
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
        emit errorOccurred(code);
        close();
        return false;
    }
};
