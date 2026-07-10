#pragma once

/**
 * @file src/core/io/http/SwHttpSession.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpSession in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP session interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpSession.
 *
 * Session-level interfaces here describe how one client connection advances through parsing,
 * request handling, response production, and connection shutdown without blocking the IO path.
 *
 * HTTP-facing declarations in this area are designed around non-blocking IO, incremental parsing,
 * bounded buffering, and a clear separation between transport work and higher-level request
 * handling.
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

#include "SwObject.h"
#include "SwAbstractSocket.h"
#include "SwPointer.h"
#include "SwTcpSocket.h"
#include "SwTimer.h"
#include "SwFile.h"
#include "SwDebug.h"
#include "SwDequeue.h"

#include "http/SwHttpTypes.h"
#include "http/SwHttpParser.h"
#include "http/SwHttpRouter.h"
#include "http/SwHttpMultipart.h"

#include <chrono>
#include <atomic>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

static constexpr const char* kSwLogCategory_SwHttpSession = "sw.core.io.swhttpsession";

class SwHttpSession : public SwObject {
    SW_OBJECT(SwHttpSession, SwObject)

public:
    using SwHttpResponseCallback = std::function<void(SwHttpResponse)>;
    using SwHttpRequestHandler = std::function<void(const SwHttpRequest&, const SwHttpResponseCallback&)>;

    /**
     * @brief Constructs a `SwHttpSession` instance.
     * @param socket Socket instance affected by the operation.
     * @param router Value passed to the method.
     * @param limits Limit configuration to apply.
     * @param timeouts Timeout configuration to apply.
     * @param parent Optional parent object that owns this instance.
     * @param timeouts Timeout configuration to apply.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwHttpSession(SwAbstractSocket* socket,
                  SwHttpRouter* router,
                  const SwHttpLimits& limits,
                  const SwHttpTimeouts& timeouts,
                  bool isTls,
                  uint16_t localPort,
                  SwObject* parent = nullptr)
        : SwObject(parent),
          m_socket(socket),
          m_router(router),
          m_limits(limits),
          m_timeouts(timeouts),
          m_isTls(isTls),
          m_localPort(localPort) {
        if (!m_socket || !m_router) {
            deleteLater();
            return;
        }

        m_parser.setLimits(m_limits);
        m_socket->setParent(this);
        connect(m_socket, &SwIODevice::readyRead, this, &SwHttpSession::onReadyRead_);
        connect(m_socket, &SwAbstractSocket::disconnected, this, &SwHttpSession::onDisconnected_);
        connect(m_socket, &SwAbstractSocket::errorOccurred, this, &SwHttpSession::onError_);
        connect(m_socket, &SwAbstractSocket::writeFinished, this, &SwHttpSession::onWriteFinished_);
        connect(m_socket, &SwIODevice::readyWrite, this, &SwHttpSession::onReadyWrite_);

        auto now = std::chrono::steady_clock::now();
        m_lastReadAt = now;
        m_requestStartedAt = now;
        m_waitingResponseAt = now;
        m_writeStartedAt = now;

        m_timeoutWatch = new SwTimer(this);
        m_timeoutWatch->setSingleShot(true);
        connect(m_timeoutWatch, &SwTimer::timeout, this, &SwHttpSession::onTimeoutWatch_);
        scheduleTimeout_();
    }

    /**
     * @brief Destroys the `SwHttpSession` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwHttpSession() override {
        cleanupStreamFile_();
    }

    /**
     * @brief Sets the finished Callback.
     * @param callback Callback invoked by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFinishedCallback(const std::function<void(SwHttpSession*)>& callback) {
        m_onFinished = callback;
    }

    /**
     * @brief Adds the specified cleanup Hook.
     * @param hook Value passed to the method.
     */
    void addCleanupHook(const std::function<void()>& hook) {
        m_cleanupHooks.append(hook);
    }

    /**
     * @brief Sets the request Handler.
     * @param handler Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRequestHandler(const SwHttpRequestHandler& handler) {
        m_requestHandler = handler;
    }

    void setPendingBytesBudgetCallbacks(
        const std::function<bool(std::size_t)>& reserve,
        const std::function<void(std::size_t)>& release) {
        m_reservePendingBytes = reserve;
        m_releasePendingBytes = release;
    }

    /**
     * @brief Closes the session handled by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void closeSession() {
        cleanup_();
    }

    void startBufferedReadProcessing() {
        onReadyRead_();
    }

private:
    enum class SendState {
        Idle,
        SendingBody,
        SendingFile,
        SendingChunked
    };

    SwAbstractSocket* m_socket = nullptr;
    SwHttpRouter* m_router = nullptr;
    SwHttpParser m_parser;
    SwHttpLimits m_limits;
    SwHttpTimeouts m_timeouts;
    SwTimer* m_timeoutWatch = nullptr;

    SwDequeue<SwHttpRequest> m_pendingRequests;
    SwDequeue<std::size_t> m_pendingRequestSizes;
    std::size_t m_pendingRequestBytes = 0;
    std::size_t m_activeRequestReservedBytes = 0;
    bool m_handlingResponse = false;
    bool m_waitingAsyncResponse = false;
    bool m_closeAfterWrite = false;
    bool m_responsePayloadDone = false;
    bool m_cleaned = false;
    bool m_inSocketWrite = false;
    bool m_writeFinishedDeferred = false;
    bool m_deferredWriteScheduled = false;
    bool m_readContinuationScheduled = false;
    bool m_readBackpressured = false;
    bool m_handoverAfterWrite = false;
    bool m_expectContinuePending = false;

    SendState m_sendState = SendState::Idle;
    SwFile* m_streamFile = nullptr;
    std::size_t m_streamBytesRemaining = 0;
    std::size_t m_streamChunkBytes = 64 * 1024;
    SwDequeue<SwByteArray> m_chunkParts;
    bool m_chunkTerminatorSent = false;
    SwByteArray m_bodyPayload;
    std::size_t m_bodyOffset = 0;
    SwByteArray m_deferredSocketWrite;
    bool m_waitingSocketWritable = false;
    SwList<SwString> m_requestTempFiles;
    SwHttpRequest m_activeRequest;
    std::function<void(SwAbstractSocket*)> m_socketHandoverCallback;
    std::function<void(SwAbstractSocket*, SwByteArray)> m_socketHandoverWithDataCallback;
    SwByteArray m_handoverInitialData;

    SwHttpRequestHandler m_requestHandler;
    std::function<bool(std::size_t)> m_reservePendingBytes;
    std::function<void(std::size_t)> m_releasePendingBytes;
    std::function<void(SwHttpSession*)> m_onFinished;
    SwList<std::function<void()>> m_cleanupHooks;
    bool m_isTls = false;
    uint16_t m_localPort = 0;

    std::chrono::steady_clock::time_point m_lastReadAt;
    std::chrono::steady_clock::time_point m_requestStartedAt;
    std::chrono::steady_clock::time_point m_waitingResponseAt;
    std::chrono::steady_clock::time_point m_writeStartedAt;
    std::chrono::steady_clock::time_point m_timeoutDeadline;
    bool m_timeoutArmed = false;

private slots:
    void onReadyRead_() {
        m_readContinuationScheduled = false;
        if (!m_socket || m_cleaned) {
            return;
        }

        if (readBackpressureRequired_()) {
            m_readBackpressured = true;
            return;
        }
        m_readBackpressured = false;

        char readBuffer[kSwTcpDefaultReadChunkSize];
        static const std::size_t kReadBudgetBytes = 256 * 1024;
        static const std::size_t kReadBudgetOperations = 32;
        std::size_t bytesThisTurn = 0;
        std::size_t operationsThisTurn = 0;
        while (bytesThisTurn < kReadBudgetBytes &&
               operationsThisTurn < kReadBudgetOperations &&
               !readBackpressureRequired_()) {
            const std::size_t remainingBudget = kReadBudgetBytes - bytesThisTurn;
            const std::size_t readCapacity = (std::min)(sizeof(readBuffer), remainingBudget);
            const int64_t bytesRead = m_socket->readInto(readBuffer, readCapacity);
            ++operationsThisTurn;
            if (bytesRead <= 0) {
                break;
            }
            bytesThisTurn += static_cast<std::size_t>(bytesRead);

            auto now = std::chrono::steady_clock::now();
            if (!m_parser.hasPartialRequest()) {
                m_requestStartedAt = now;
            }
            m_lastReadAt = now;

            SwList<SwHttpRequest> parsedRequests;
            SwHttpParser::FeedStatus status =
                m_parser.feed(readBuffer, static_cast<std::size_t>(bytesRead), parsedRequests);
            if (status == SwHttpParser::FeedStatus::Error) {
                for (std::size_t i = 0; i < parsedRequests.size(); ++i) {
                    cleanupTempFilesForRequest_(parsedRequests[i]);
                }
                int parseStatus = m_parser.errorStatus();
                if (parseStatus <= 0) {
                    parseStatus = 400;
                }
                sendErrorAndClose_(parseStatus, m_parser.errorMessage());
                return;
            }
            if (m_parser.takeContinueNeeded()) {
                m_expectContinuePending = true;
            }

            SwTcpSocket* tcpSocket = dynamic_cast<SwTcpSocket*>(m_socket);
            const SwString peerAddress = tcpSocket ? tcpSocket->peerAddress() : SwString();
            const uint16_t peerPort = tcpSocket ? tcpSocket->peerPort() : 0;
            for (std::size_t i = 0; i < parsedRequests.size(); ++i) {
                parsedRequests[i].isTls = m_isTls;
                parsedRequests[i].localPort = m_localPort;
                parsedRequests[i].peerAddress = peerAddress;
                parsedRequests[i].peerPort = peerPort;

                const std::size_t requestBytes = requestBufferedBytes_(parsedRequests[i]);
                if (m_limits.maxPendingRequestBytes > 0 &&
                    (requestBytes > m_limits.maxPendingRequestBytes ||
                     m_pendingRequestBytes > m_limits.maxPendingRequestBytes - requestBytes)) {
                    for (std::size_t j = i; j < parsedRequests.size(); ++j) {
                        cleanupTempFilesForRequest_(parsedRequests[j]);
                    }
                    sendErrorAndClose_(429, "Too much pipelined request data");
                    return;
                }
                if (m_reservePendingBytes && !m_reservePendingBytes(requestBytes)) {
                    for (std::size_t j = i; j < parsedRequests.size(); ++j) {
                        cleanupTempFilesForRequest_(parsedRequests[j]);
                    }
                    sendErrorAndClose_(503, "Global request memory budget exhausted");
                    return;
                }
                m_pendingRequests.append(std::move(parsedRequests[i]));
                m_pendingRequestSizes.append(requestBytes);
                m_pendingRequestBytes += requestBytes;
                if (m_limits.maxPipelinedRequests > 0 &&
                    m_pendingRequests.size() > m_limits.maxPipelinedRequests) {
                    for (std::size_t j = i + 1; j < parsedRequests.size(); ++j) {
                        cleanupTempFilesForRequest_(parsedRequests[j]);
                    }
                    sendErrorAndClose_(400, "Too many pipelined requests");
                    return;
                }
            }
        }

        if (!m_handlingResponse && !m_waitingAsyncResponse) {
            processNextRequest_();
        }
        if (!m_handlingResponse && !m_waitingAsyncResponse &&
            m_pendingRequests.isEmpty()) {
            sendPendingContinue_();
        }
        if (readBackpressureRequired_()) {
            m_readBackpressured = true;
        } else if (bytesThisTurn >= kReadBudgetBytes ||
                   operationsThisTurn >= kReadBudgetOperations) {
            scheduleReadContinuation_();
        }
        if (!m_readBackpressured) {
            SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(m_socket);
            if (tcp && !tcp->readNotificationsEnabled()) {
                SwPointer<SwHttpSession> self(this);
                tcp->resumeReadNotifications();
                if (!self) {
                    return;
                }
            }
        }
        scheduleTimeout_();
    }

    void onWriteFinished_() {
        if (!m_socket || m_cleaned) {
            return;
        }
        if (m_inSocketWrite) {
            m_writeFinishedDeferred = true;
            return;
        }

        m_writeStartedAt = std::chrono::steady_clock::now();

        if (m_sendState == SendState::SendingBody) {
            sendNextBodyChunk_();
            return;
        }
        if (m_sendState == SendState::SendingFile) {
            sendNextFileChunk_();
            return;
        }
        if (m_sendState == SendState::SendingChunked) {
            sendNextChunkedPart_();
            return;
        }

        if (m_handlingResponse && m_responsePayloadDone) {
            finalizeResponse_();
        }
    }

    void onReadyWrite_() {
        if (!m_socket || m_cleaned || !m_waitingSocketWritable ||
            m_deferredSocketWrite.isEmpty()) {
            return;
        }

        m_inSocketWrite = true;
        const bool accepted = m_socket->write(m_deferredSocketWrite.constData(),
                                               m_deferredSocketWrite.size());
        m_inSocketWrite = false;
        if (!accepted) {
            SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(m_socket);
            if (!tcp || tcp->lastWriteResult() != SwTcpSocket::WriteResult::WouldBlock) {
                cleanup_();
            }
            return;
        }

        m_waitingSocketWritable = false;
        m_deferredSocketWrite = SwByteArray();
        m_writeStartedAt = std::chrono::steady_clock::now();
        scheduleTimeout_();
        if (m_writeFinishedDeferred) {
            m_writeFinishedDeferred = false;
            scheduleDeferredWriteFinished_();
        }
    }

    void onDeferredWriteFinished_() {
        m_deferredWriteScheduled = false;
        if (!m_socket || m_cleaned) {
            return;
        }
        onWriteFinished_();
    }

    void onDisconnected_() {
        cleanup_();
    }

    void onError_(int) {
        cleanup_();
    }

    void onTimeoutWatch_() {
        if (!m_socket || m_cleaned) {
            return;
        }
        m_timeoutArmed = false;
        auto now = std::chrono::steady_clock::now();

        if (m_handlingResponse) {
            if (m_timeouts.writeTimeoutMs > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_writeStartedAt).count();
                if (elapsed >= m_timeouts.writeTimeoutMs) {
                    swCWarning(kSwLogCategory_SwHttpSession) << "[SwHttpSession] write timeout, closing";
                    cleanup_();
                    return;
                }
            }
            scheduleTimeout_();
            return;
        }

        if (m_waitingAsyncResponse) {
            if (m_timeouts.routeTimeoutMs > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_waitingResponseAt).count();
                if (elapsed >= m_timeouts.routeTimeoutMs) {
                    sendErrorAndClose_(504, "Route async timeout");
                    return;
                }
            }
            scheduleTimeout_();
            return;
        }

        if (m_parser.hasPartialRequest() && m_parser.isAwaitingHeaders() && m_timeouts.headerReadTimeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_requestStartedAt).count();
            if (elapsed >= m_timeouts.headerReadTimeoutMs) {
                sendErrorAndClose_(408, "Request Timeout");
                return;
            }
        }

        if (m_parser.isAwaitingBody() && m_timeouts.bodyReadTimeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_requestStartedAt).count();
            if (elapsed >= m_timeouts.bodyReadTimeoutMs) {
                sendErrorAndClose_(408, "Request Timeout");
                return;
            }
        }

        if (!m_parser.hasPartialRequest() && m_pendingRequests.isEmpty() && m_timeouts.keepAliveIdleTimeoutMs > 0) {
            auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastReadAt).count();
            if (idle >= m_timeouts.keepAliveIdleTimeoutMs) {
                cleanup_();
                return;
            }
        }
        scheduleTimeout_();
    }

private:
    static void addBufferedBytes_(std::size_t& total, std::size_t value) {
        const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
        total = value > maximum - total ? maximum : total + value;
    }

    static std::size_t requestBufferedBytes_(const SwHttpRequest& request) {
        std::size_t bytes = 0;
        addBufferedBytes_(bytes, request.method.size());
        addBufferedBytes_(bytes, request.target.size());
        addBufferedBytes_(bytes, request.path.size());
        addBufferedBytes_(bytes, request.queryString.size());
        addBufferedBytes_(bytes, request.protocol.size());
        addBufferedBytes_(bytes, request.body.size());
        for (SwMap<SwString, SwString>::const_iterator it = request.headers.begin();
             it != request.headers.end(); ++it) {
            addBufferedBytes_(bytes, it.key().size());
            addBufferedBytes_(bytes, it.value().size());
        }
        for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
            addBufferedBytes_(bytes, part.name.size());
            addBufferedBytes_(bytes, part.fileName.size());
            addBufferedBytes_(bytes, part.contentType.size());
            addBufferedBytes_(bytes, part.tempFilePath.size());
            addBufferedBytes_(bytes, part.data.size());
        }
        return bytes;
    }

    bool readBackpressureRequired_() const {
        return m_limits.maxPendingRequestBytes > 0 &&
               m_pendingRequestBytes >= m_limits.maxPendingRequestBytes;
    }

    void scheduleReadContinuation_() {
        if (m_readContinuationScheduled || !m_socket || m_cleaned ||
            readBackpressureRequired_()) {
            return;
        }
        m_readContinuationScheduled = true;
        SwPointer<SwHttpSession> self(this);
        ThreadHandle* affinity = threadHandle();
        if (affinity && affinity->postTaskOnLaneReliable([self]() {
                if (self) {
                    self->onReadyRead_();
                }
            }, SwFiberLane::Control)) {
            return;
        }

        m_readContinuationScheduled = false;
        SwTimer::singleShot(0, this, &SwHttpSession::onReadyRead_);
    }

    void scheduleTimeout_() {
        if (!m_timeoutWatch || !m_socket || m_cleaned) {
            return;
        }

        using Clock = std::chrono::steady_clock;
        Clock::time_point next = Clock::time_point::max();
        auto consider = [&next](const Clock::time_point& base, int timeoutMs) {
            if (timeoutMs <= 0) {
                return;
            }
            const Clock::time_point candidate = base + std::chrono::milliseconds(timeoutMs);
            if (candidate < next) {
                next = candidate;
            }
        };

        if (m_handlingResponse) {
            consider(m_writeStartedAt, m_timeouts.writeTimeoutMs);
        } else if (m_waitingAsyncResponse) {
            consider(m_waitingResponseAt, m_timeouts.routeTimeoutMs);
        } else {
            if (m_parser.hasPartialRequest() && m_parser.isAwaitingHeaders()) {
                consider(m_requestStartedAt, m_timeouts.headerReadTimeoutMs);
            }
            if (m_parser.isAwaitingBody()) {
                consider(m_requestStartedAt, m_timeouts.bodyReadTimeoutMs);
            }
            if (!m_parser.hasPartialRequest() && m_pendingRequests.isEmpty()) {
                consider(m_lastReadAt, m_timeouts.keepAliveIdleTimeoutMs);
            }
        }

        if (next == Clock::time_point::max()) {
            if (m_timeoutWatch->isActive()) {
                m_timeoutWatch->stop();
            }
            m_timeoutArmed = false;
            return;
        }
        if (m_timeoutArmed && next == m_timeoutDeadline && m_timeoutWatch->isActive()) {
            return;
        }

        if (m_timeoutWatch->isActive()) {
            m_timeoutWatch->stop();
        }
        const auto now = Clock::now();
        const auto remainingUs = next > now
                                     ? std::chrono::duration_cast<std::chrono::microseconds>(next - now).count()
                                     : 0;
        long long delayMs = (remainingUs + 999) / 1000;
        if (delayMs < 1) {
            delayMs = 1;
        }
        if (delayMs > (std::numeric_limits<int>::max)()) {
            delayMs = (std::numeric_limits<int>::max)();
        }
        m_timeoutDeadline = next;
        m_timeoutArmed = true;
        m_timeoutWatch->start(static_cast<int>(delayMs));
    }

    void scheduleDeferredWriteFinished_() {
        if (m_deferredWriteScheduled || !m_socket || m_cleaned) {
            return;
        }

        m_deferredWriteScheduled = true;
        SwPointer<SwHttpSession> self(this);
        ThreadHandle* affinity = threadHandle();
        if (affinity && affinity->postTaskOnLaneReliable([self]() {
                if (self) {
                    self->onDeferredWriteFinished_();
                }
            }, SwFiberLane::Control)) {
            return;
        }

        SwTimer::singleShot(0, this, &SwHttpSession::onDeferredWriteFinished_);
    }

    bool writeSocket_(const char* data, std::size_t size) {
        if (!m_socket || (!data && size > 0)) {
            return false;
        }
        if (size == 0) {
            return true;
        }
        if (m_waitingSocketWritable) {
            return false;
        }
        m_inSocketWrite = true;
        const bool accepted = m_socket->write(data, size);
        m_inSocketWrite = false;
        m_writeStartedAt = std::chrono::steady_clock::now();
        scheduleTimeout_();

        if (!accepted) {
            SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(m_socket);
            if (!tcp || tcp->lastWriteResult() != SwTcpSocket::WriteResult::WouldBlock) {
                cleanup_();
                return false;
            }
            m_deferredSocketWrite = SwByteArray(data, size);
            m_waitingSocketWritable = true;
            return true;
        }

        if (m_writeFinishedDeferred) {
            m_writeFinishedDeferred = false;
            scheduleDeferredWriteFinished_();
        }
        return true;
    }

    bool writeSocket_(const SwString& data) {
        return writeSocket_(data.data(), data.size());
    }

    bool writeSocket_(const SwByteArray& data) {
        return writeSocket_(data.constData(), data.size());
    }

    bool sendPendingContinue_() {
        if (!m_expectContinuePending) {
            return true;
        }
        if (!m_socket || m_cleaned || m_handlingResponse || m_waitingAsyncResponse ||
            !m_pendingRequests.isEmpty()) {
            return false;
        }
        static const char kContinue[] = "HTTP/1.1 100 Continue\r\n\r\n";
        if (!writeSocket_(kContinue, sizeof(kContinue) - 1)) {
            return false;
        }
        m_expectContinuePending = false;
        return true;
    }

    static SwString toHex_(std::size_t value) {
        if (value == 0) {
            return "0";
        }
        SwString out;
        while (value > 0) {
            int digit = static_cast<int>(value & 0xF);
            if (digit < 10) {
                out.prepend(static_cast<char>('0' + digit));
            } else {
                out.prepend(static_cast<char>('a' + (digit - 10)));
            }
            value >>= 4;
        }
        return out;
    }

    static bool validHeaderName_(const SwString& name) {
        if (name.isEmpty()) {
            return false;
        }
        for (std::size_t i = 0; i < name.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            const bool alphaNumeric = (c >= '0' && c <= '9') ||
                                      (c >= 'A' && c <= 'Z') ||
                                      (c >= 'a' && c <= 'z');
            if (!alphaNumeric && c != '!' && c != '#' && c != '$' && c != '%' &&
                c != '&' && c != '\'' && c != '*' && c != '+' && c != '-' &&
                c != '.' && c != '^' && c != '_' && c != '`' && c != '|' && c != '~') {
                return false;
            }
        }
        return true;
    }

    bool sendChunkedPart_(const SwByteArray& part) {
        if (!m_socket) {
            return false;
        }
        if (part.isEmpty()) {
            return true;
        }

        const SwString prefix = toHex_(part.size()) + "\r\n";
        SwByteArray payload;
        payload.reserve(prefix.size() + part.size() + 2);
        payload.append(prefix.data(), prefix.size());
        if (!part.isEmpty()) {
            payload.append(part.constData(), part.size());
        }
        payload.append("\r\n", 2);
        return writeSocket_(payload);
    }

    static bool statusForbidsResponseBody_(int status) {
        return (status >= 100 && status < 200) || status == 204 ||
               status == 205 || status == 304;
    }

    static std::size_t responseRepresentationLength_(const SwHttpResponse& response) {
        if (response.hasFile) {
            return response.fileLength;
        }
        if (response.useChunkedTransfer) {
            std::size_t total = 0;
            for (std::size_t i = 0; i < response.chunkedParts.size(); ++i) {
                addBufferedBytes_(total, response.chunkedParts[i].size());
            }
            if (response.chunkedParts.isEmpty()) {
                addBufferedBytes_(total, response.body.size());
            }
            return total;
        }
        return response.body.size();
    }

    bool sendResponseHeaders_(const SwHttpResponse& response,
                              bool suppressBody,
                              bool statusForbidsBody,
                              std::size_t representationLength) {
        SwString raw;
        raw += "HTTP/1.1 ";
        raw += SwString::number(response.status);
        raw += " ";
        raw += response.reason.isEmpty() ? swHttpStatusReason(response.status) : response.reason;
        raw += "\r\n";

        bool hasConnection = false;

        for (auto it = response.headers.begin(); it != response.headers.end(); ++it) {
            SwString key = it.key().trimmed().toLower();
            if (!validHeaderName_(key) || it.value().contains("\r") ||
                it.value().contains("\n")) {
                continue;
            }
            // Wire framing is generated from the payload actually selected below.  Never forward
            // caller-provided CL/TE values: a stale Content-Length or CL+TE combination would make
            // the persistent connection ambiguous to the next request parser.
            if (key == "content-length" || key == "transfer-encoding") {
                continue;
            }
            if (key == "connection") {
                if (!response.switchToRawSocket) {
                    continue;
                }
                hasConnection = true;
            }
            raw += key + ": " + it.value() + "\r\n";
        }

        if (!statusForbidsBody) {
            if (!suppressBody && response.useChunkedTransfer) {
                raw += "transfer-encoding: chunked\r\n";
            } else {
                raw += "content-length: " +
                       SwString::number(static_cast<unsigned long long>(representationLength)) +
                       "\r\n";
            }
        }

        if (!hasConnection) {
            raw += m_closeAfterWrite ? "connection: close\r\n" : "connection: keep-alive\r\n";
        }
        raw += "\r\n";

        return writeSocket_(raw);
    }

    void beginResponse_(const SwHttpRequest& request, SwHttpResponse response) {
        if (!m_socket) {
            cleanup_();
            return;
        }

        if (response.status < 100 || response.status > 599) {
            response.status = 500;
            response.reason = "Internal Server Error";
            response.body = SwByteArray("Invalid response status");
            response.hasFile = false;
            response.useChunkedTransfer = false;
            response.chunkedParts.clear();
            response.closeConnection = true;
        }
        if (response.reason.isEmpty() || response.reason.contains("\r") ||
            response.reason.contains("\n")) {
            response.reason = swHttpStatusReason(response.status);
        }

        m_handlingResponse = true;
        m_waitingAsyncResponse = false;
        m_responsePayloadDone = false;
        m_sendState = SendState::Idle;
        m_chunkTerminatorSent = false;
        m_chunkParts.clear();
        m_bodyPayload = SwByteArray();
        m_bodyOffset = 0;
        cleanupStreamFile_();
        m_handoverAfterWrite = false;
        m_socketHandoverCallback = nullptr;
        m_socketHandoverWithDataCallback = nullptr;
        m_handoverInitialData = SwByteArray();

        m_closeAfterWrite = response.closeConnection || !request.keepAlive;
        if (response.switchToRawSocket &&
            (response.onSwitchToRawSocket || response.onSwitchToRawSocketWithInitialData)) {
            m_handoverAfterWrite = true;
            m_socketHandoverCallback = response.onSwitchToRawSocket;
            m_socketHandoverWithDataCallback = response.onSwitchToRawSocketWithInitialData;
            m_handoverInitialData = m_parser.takeBufferedData();
            m_closeAfterWrite = false;
            if (response.switchToRawSocketWithoutHttpResponse) {
                response.headOnly = true;
                response.hasFile = false;
                response.useChunkedTransfer = false;
                response.chunkedParts.clear();
                response.body.clear();
                m_responsePayloadDone = true;
                finalizeResponse_();
                return;
            }
            response.headOnly = true;
            response.hasFile = false;
            response.useChunkedTransfer = false;
            response.chunkedParts.clear();
            response.body.clear();
        }

        // HEAD responses should not emit body bytes.
        if (request.method.toUpper() == "HEAD") {
            response.headOnly = true;
        }

        const bool statusForbidsBody = statusForbidsResponseBody_(response.status);
        const bool suppressBody = response.headOnly || statusForbidsBody;
        const std::size_t representationLength = responseRepresentationLength_(response);

        if (!sendResponseHeaders_(response,
                                  suppressBody,
                                  statusForbidsBody,
                                  representationLength)) {
            return;
        }

        if (suppressBody) {
            m_responsePayloadDone = true;
            return;
        }

        if (response.hasFile) {
            m_streamFile = new SwFile(response.filePath, this);
            if (!m_streamFile->openBinary(SwFile::Read)) {
                cleanupStreamFile_();
                sendErrorAndClose_(500, "Unable to open file");
                return;
            }
            m_streamFile->seek(static_cast<std::streampos>(response.fileOffset));
            m_streamBytesRemaining = response.fileLength;
            if (response.streamChunkBytes > 0) {
                m_streamChunkBytes = response.streamChunkBytes;
            } else {
                m_streamChunkBytes = (m_limits.maxChunkSize > 0) ? m_limits.maxChunkSize : (64 * 1024);
            }
            if (m_streamChunkBytes < 4096) {
                m_streamChunkBytes = 4096;
            }
            if (m_streamChunkBytes > 64 * 1024) {
                m_streamChunkBytes = 64 * 1024;
            }
            m_sendState = SendState::SendingFile;
            return;
        }

        if (response.useChunkedTransfer) {
            if (!response.chunkedParts.isEmpty()) {
                for (std::size_t i = 0; i < response.chunkedParts.size(); ++i) {
                    if (!response.chunkedParts[i].isEmpty()) {
                        m_chunkParts.append(std::move(response.chunkedParts[i]));
                    }
                }
            } else if (!response.body.isEmpty()) {
                m_chunkParts.append(std::move(response.body));
            }
            m_sendState = SendState::SendingChunked;
            return;
        }

        if (!response.body.isEmpty()) {
            m_bodyPayload = std::move(response.body);
            m_bodyOffset = 0;
            m_sendState = SendState::SendingBody;
            return;
        }
        m_responsePayloadDone = true;
    }

    void sendNextBodyChunk_() {
        if (!m_socket) {
            cleanup_();
            return;
        }
        if (m_bodyOffset >= m_bodyPayload.size()) {
            m_bodyPayload = SwByteArray();
            m_bodyOffset = 0;
            m_sendState = SendState::Idle;
            m_responsePayloadDone = true;
            finalizeResponse_();
            return;
        }

        constexpr std::size_t kBodyWriteChunk = 64 * 1024;
        const std::size_t remaining = m_bodyPayload.size() - m_bodyOffset;
        const std::size_t chunk = (std::min)(remaining, kBodyWriteChunk);
        if (!writeSocket_(m_bodyPayload.constData() + m_bodyOffset, chunk)) {
            return;
        }
        m_bodyOffset += chunk;
    }

    void sendNextFileChunk_() {
        if (!m_socket) {
            cleanup_();
            return;
        }

        if (!m_streamFile) {
            sendErrorAndClose_(500, "Invalid file stream");
            return;
        }

        if (m_streamBytesRemaining == 0) {
            cleanupStreamFile_();
            m_sendState = SendState::Idle;
            m_responsePayloadDone = true;
            finalizeResponse_();
            return;
        }

        std::size_t block = m_streamChunkBytes;
        if (block > m_streamBytesRemaining) {
            block = m_streamBytesRemaining;
        }
        SwString chunk = m_streamFile->readChunk(block);
        if (chunk.isEmpty()) {
            sendErrorAndClose_(500, "File stream read failure");
            return;
        }

        if (!writeSocket_(chunk)) {
            return;
        }

        if (chunk.size() > m_streamBytesRemaining) {
            m_streamBytesRemaining = 0;
        } else {
            m_streamBytesRemaining -= chunk.size();
        }
    }

    void sendNextChunkedPart_() {
        if (!m_socket) {
            cleanup_();
            return;
        }

        while (!m_chunkParts.isEmpty()) {
            SwByteArray part = m_chunkParts.takeFirst();
            if (part.isEmpty()) {
                continue;
            }
            sendChunkedPart_(part);
            return;
        }

        if (!m_chunkTerminatorSent) {
            if (!writeSocket_("0\r\n\r\n")) {
                return;
            }
            m_chunkTerminatorSent = true;
            return;
        }

        m_sendState = SendState::Idle;
        m_responsePayloadDone = true;
        finalizeResponse_();
    }

    void processNextRequest_() {
        if (m_handlingResponse || m_waitingAsyncResponse || m_pendingRequests.isEmpty() || !m_socket) {
            return;
        }

        m_activeRequest = m_pendingRequests.takeFirst();
        if (!m_pendingRequestSizes.isEmpty()) {
            const std::size_t requestBytes = m_pendingRequestSizes.takeFirst();
            m_pendingRequestBytes = requestBytes > m_pendingRequestBytes
                                        ? 0
                                        : m_pendingRequestBytes - requestBytes;
            m_activeRequestReservedBytes = requestBytes;
        }
        if (m_readBackpressured && !readBackpressureRequired_()) {
            m_readBackpressured = false;
            scheduleReadContinuation_();
        }
        m_requestStartedAt = std::chrono::steady_clock::now();
        m_requestTempFiles.clear();

        SwString multipartError;
        if (!swHttpParseMultipartRequest(m_activeRequest, m_limits, multipartError)) {
            sendErrorAndClose_(400, multipartError.isEmpty() ? SwString("Invalid multipart/form-data") : multipartError);
            return;
        }
        collectRequestTempFiles_(m_activeRequest);

        if (m_requestHandler) {
            m_waitingAsyncResponse = true;
            m_waitingResponseAt = std::chrono::steady_clock::now();
            scheduleTimeout_();
            ThreadHandle* affinity = threadHandle();
            SwPointer<SwHttpSession> self(this);
            m_requestHandler(m_activeRequest, [self, affinity](SwHttpResponse response) mutable {
                if (!affinity || ThreadHandle::currentThread() == affinity) {
                    if (!self || self->m_cleaned || !self->m_socket ||
                        !self->m_waitingAsyncResponse || self->m_handlingResponse) {
                        return;
                    }
                    self->m_waitingAsyncResponse = false;
                    self->beginResponse_(self->m_activeRequest, std::move(response));
                    return;
                }

                std::shared_ptr<SwHttpResponse> responseState(
                    new SwHttpResponse(std::move(response)));
                if (!affinity->postTaskOnLaneReliable([self, responseState]() mutable {
                        if (!self || self->m_cleaned || !self->m_socket ||
                            !self->m_waitingAsyncResponse || self->m_handlingResponse) {
                            return;
                        }
                        self->m_waitingAsyncResponse = false;
                        self->beginResponse_(self->m_activeRequest,
                                             std::move(*responseState));
                    }, SwFiberLane::Control)) {
                    // The bounded reliable queue is exhausted. Keep the
                    // session waiting so its exact route deadline returns
                    // a deterministic 504 instead of running cross-thread.
                    return;
                }
            });
            return;
        }

        SwHttpResponse response;
        bool handled = m_router->route(m_activeRequest, response);
        if (!handled) {
            response = swHttpTextResponse(404, "Not Found");
            response.closeConnection = !m_activeRequest.keepAlive;
        }
        beginResponse_(m_activeRequest, std::move(response));
    }

    void finishDetached_() {
        if (m_cleaned) {
            return;
        }
        m_cleaned = true;

        SwPointer<SwHttpSession> self(this);
        for (std::size_t i = 0; i < m_cleanupHooks.size(); ++i) {
            const std::function<void()> hook = m_cleanupHooks[i];
            if (hook) {
                hook();
            }
            if (!self) {
                return;
            }
        }
        m_cleanupHooks.clear();

        cleanupStreamFile_();
        cleanupRequestTempFiles_();
        releaseBufferedState_();
        m_waitingAsyncResponse = false;

        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }
        m_timeoutArmed = false;

        std::function<void(SwHttpSession*)> done = m_onFinished;
        m_onFinished = nullptr;
        if (done) {
            done(this);
            return;
        }
        deleteLater();
    }

    void handoverSocket_() {
        if (!m_socket) {
            finishDetached_();
            return;
        }

        SwAbstractSocket* rawSocket = m_socket;
        m_socket = nullptr;
        rawSocket->disconnectAllSlots();
        rawSocket->setParent(nullptr);

        std::function<void(SwAbstractSocket*)> handover = m_socketHandoverCallback;
        std::function<void(SwAbstractSocket*, SwByteArray)> handoverWithData =
            m_socketHandoverWithDataCallback;
        SwByteArray initialData = std::move(m_handoverInitialData);
        m_socketHandoverCallback = nullptr;
        m_socketHandoverWithDataCallback = nullptr;
        m_handoverAfterWrite = false;

        // Finalize the HTTP session before invoking arbitrary protocol code. The callback may
        // synchronously delete its former session; all data needed below is owned by locals.
        finishDetached_();

        if (handoverWithData) {
            handoverWithData(rawSocket, std::move(initialData));
        } else if (handover) {
            handover(rawSocket);
        } else {
            disposeSocketAfterClose_(rawSocket);
        }

    }

    void finalizeResponse_() {
        if (!m_handlingResponse) {
            return;
        } else {
            m_handlingResponse = false;
            cleanupStreamFile_();
            cleanupRequestTempFiles_();
            m_sendState = SendState::Idle;
            m_chunkParts.clear();
            m_chunkTerminatorSent = false;
            m_bodyPayload = SwByteArray();
            m_bodyOffset = 0;
            m_responsePayloadDone = false;
            m_waitingAsyncResponse = false;
            if (m_activeRequestReservedBytes > 0 && m_releasePendingBytes) {
                m_releasePendingBytes(m_activeRequestReservedBytes);
            }
            m_activeRequestReservedBytes = 0;
            m_activeRequest = SwHttpRequest();

            if (m_handoverAfterWrite) {
                handoverSocket_();
                return;
            }

            if (m_closeAfterWrite) {
                cleanup_();
                return;
            }

            if (!m_pendingRequests.isEmpty()) {
                processNextRequest_();
            } else {
                sendPendingContinue_();
                m_lastReadAt = std::chrono::steady_clock::now();
                scheduleTimeout_();
            }
        }
    }

    void sendErrorAndClose_(int status, const SwString& message) {
        if (!m_socket || m_cleaned) {
            cleanup_();
            return;
        }

        m_waitingAsyncResponse = false;
        m_handoverAfterWrite = false;
        m_socketHandoverCallback = nullptr;
        m_socketHandoverWithDataCallback = nullptr;
        m_handoverInitialData = SwByteArray();
        m_expectContinuePending = false;

        if (m_handlingResponse) {
            cleanup_();
            return;
        }

        SwHttpRequest syntheticRequest;
        syntheticRequest.method = "GET";
        syntheticRequest.keepAlive = false;

        SwHttpResponse response = swHttpTextResponse(status > 0 ? status : 400, message.isEmpty() ? "Bad Request" : message);
        response.closeConnection = true;
        beginResponse_(syntheticRequest, std::move(response));
    }

    void cleanupStreamFile_() {
        if (m_streamFile) {
            m_streamFile->close();
            m_streamFile->deleteLater();
            m_streamFile = nullptr;
        }
        m_streamBytesRemaining = 0;
    }

    static void disposeSocketAfterClose_(SwAbstractSocket* socket) {
        if (!socket) {
            return;
        }
        socket->disconnectAllSlots();
        socket->setParent(nullptr);
        SwPointer<SwAbstractSocket> guard(socket);
        std::shared_ptr<std::atomic<bool>> deletionScheduled(
            new std::atomic<bool>(false));
        SwObject::connect(socket, &SwAbstractSocket::disconnected, socket,
                          [guard, deletionScheduled]() {
            if (guard && !deletionScheduled->exchange(true, std::memory_order_acq_rel)) {
                guard->deleteLater();
            }
        });

        SwTimer* deadline = new SwTimer(6000, socket);
        deadline->setSingleShot(true);
        SwObject::connect(deadline, &SwTimer::timeout, socket,
                          [guard, deletionScheduled]() {
            if (!guard) {
                return;
            }
            if (SwTcpSocket* tcp = dynamic_cast<SwTcpSocket*>(guard.data())) {
                tcp->abort();
            } else {
                guard->close();
            }
            if (guard && !deletionScheduled->exchange(true, std::memory_order_acq_rel)) {
                guard->deleteLater();
            }
        });

        socket->close();
        if (!guard) {
            return;
        }
        if (guard->state() == SwAbstractSocket::UnconnectedState) {
            if (!deletionScheduled->exchange(true, std::memory_order_acq_rel)) {
                guard->deleteLater();
            }
        } else {
            deadline->start();
        }
    }

    void releaseBufferedState_() {
        m_parser.reset(true);
        cleanupTempFilesForRequest_(m_activeRequest);
        for (SwDequeue<SwHttpRequest>::const_iterator it = m_pendingRequests.begin();
             it != m_pendingRequests.end();
             ++it) {
            cleanupTempFilesForRequest_(*it);
        }
        const std::size_t reservedBytes =
            m_pendingRequestBytes > (std::numeric_limits<std::size_t>::max)() -
                                        m_activeRequestReservedBytes
                ? (std::numeric_limits<std::size_t>::max)()
                : m_pendingRequestBytes + m_activeRequestReservedBytes;
        if (reservedBytes > 0 && m_releasePendingBytes) {
            m_releasePendingBytes(reservedBytes);
        }
        m_pendingRequests.clear();
        m_pendingRequestSizes.clear();
        m_pendingRequestBytes = 0;
        m_activeRequestReservedBytes = 0;
        m_activeRequest = SwHttpRequest();
        m_chunkParts.clear();
        m_bodyPayload = SwByteArray();
        m_bodyOffset = 0;
        m_deferredSocketWrite = SwByteArray();
        m_waitingSocketWritable = false;
        m_readBackpressured = false;
        m_readContinuationScheduled = false;
        m_expectContinuePending = false;
    }

    void cleanup_() {
        if (m_cleaned) {
            return;
        }
        m_cleaned = true;

        SwPointer<SwHttpSession> self(this);
        for (std::size_t i = 0; i < m_cleanupHooks.size(); ++i) {
            const std::function<void()> hook = m_cleanupHooks[i];
            if (hook) {
                hook();
            }
            if (!self) {
                return;
            }
        }
        m_cleanupHooks.clear();

        cleanupStreamFile_();
        cleanupRequestTempFiles_();
        releaseBufferedState_();
        m_waitingAsyncResponse = false;
        m_handoverAfterWrite = false;
        m_socketHandoverCallback = nullptr;
        m_socketHandoverWithDataCallback = nullptr;
        m_handoverInitialData = SwByteArray();

        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }
        m_timeoutArmed = false;

        if (m_socket) {
            SwAbstractSocket* socket = m_socket;
            m_socket = nullptr;
            disposeSocketAfterClose_(socket);
        }

        std::function<void(SwHttpSession*)> done = m_onFinished;
        m_onFinished = nullptr;
        if (done) {
            done(this);
            return;
        }
        deleteLater();
    }

    void collectRequestTempFiles_(const SwHttpRequest& request) {
        m_requestTempFiles.clear();
        for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
            if (!part.storedOnDisk || part.tempFilePath.isEmpty()) {
                continue;
            }
            m_requestTempFiles.append(part.tempFilePath);
        }
    }

    static void removeFile_(const SwString& filePath) {
        if (filePath.isEmpty()) {
            return;
        }
#if defined(_WIN32)
        std::wstring widePath = filePath.toStdWString();
        (void)_wremove(widePath.c_str());
#else
        (void)std::remove(filePath.toStdString().c_str());
#endif
    }

    static void cleanupTempFilesForRequest_(const SwHttpRequest& request) {
        for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
            if (part.storedOnDisk && !part.tempFilePath.isEmpty()) {
                removeFile_(part.tempFilePath);
            }
        }
    }

    void cleanupRequestTempFiles_() {
        if (m_requestTempFiles.isEmpty()) {
            return;
        }
        for (std::size_t i = 0; i < m_requestTempFiles.size(); ++i) {
            removeFile_(m_requestTempFiles[i]);
        }
        m_requestTempFiles.clear();
    }
};
