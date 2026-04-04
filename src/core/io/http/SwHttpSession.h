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
#include "SwTimer.h"
#include "SwFile.h"
#include "SwDebug.h"

#include "http/SwHttpTypes.h"
#include "http/SwHttpParser.h"
#include "http/SwHttpRouter.h"
#include "http/SwHttpMultipart.h"

#include <chrono>
#include <cstdio>
#include <functional>

static constexpr const char* kSwLogCategory_SwHttpSession = "sw.core.io.swhttpsession";

class SwHttpSession : public SwObject {
    SW_OBJECT(SwHttpSession, SwObject)

public:
    using SwHttpResponseCallback = std::function<void(const SwHttpResponse&)>;
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

        m_timeoutWatch = new SwTimer(200, this);
        connect(m_timeoutWatch, &SwTimer::timeout, this, &SwHttpSession::onTimeoutWatch_);
        m_timeoutWatch->start();

        auto now = std::chrono::steady_clock::now();
        m_lastReadAt = now;
        m_requestStartedAt = now;
        m_waitingResponseAt = now;
        m_writeStartedAt = now;
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
        SendingFile,
        SendingChunked
    };

    SwAbstractSocket* m_socket = nullptr;
    SwHttpRouter* m_router = nullptr;
    SwHttpParser m_parser;
    SwHttpLimits m_limits;
    SwHttpTimeouts m_timeouts;
    SwTimer* m_timeoutWatch = nullptr;

    SwList<SwHttpRequest> m_pendingRequests;
    bool m_handlingResponse = false;
    bool m_waitingAsyncResponse = false;
    bool m_closeAfterWrite = false;
    bool m_responsePayloadDone = false;
    bool m_cleaned = false;
    bool m_inSocketWrite = false;
    bool m_writeFinishedDeferred = false;
    bool m_deferredWriteScheduled = false;
    bool m_handoverAfterWrite = false;

    SendState m_sendState = SendState::Idle;
    SwFile* m_streamFile = nullptr;
    std::size_t m_streamBytesRemaining = 0;
    std::size_t m_streamChunkBytes = 64 * 1024;
    SwList<SwByteArray> m_chunkParts;
    std::size_t m_chunkPartIndex = 0;
    bool m_chunkTerminatorSent = false;
    SwList<SwString> m_requestTempFiles;
    SwHttpRequest m_activeRequest;
    std::function<void(SwAbstractSocket*)> m_socketHandoverCallback;

    SwHttpRequestHandler m_requestHandler;
    std::function<void(SwHttpSession*)> m_onFinished;
    SwList<std::function<void()>> m_cleanupHooks;
    bool m_isTls = false;
    uint16_t m_localPort = 0;

    std::chrono::steady_clock::time_point m_lastReadAt;
    std::chrono::steady_clock::time_point m_requestStartedAt;
    std::chrono::steady_clock::time_point m_waitingResponseAt;
    std::chrono::steady_clock::time_point m_writeStartedAt;

private slots:
    void onReadyRead_() {
        if (!m_socket || m_cleaned) {
            return;
        }

        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }

            auto now = std::chrono::steady_clock::now();
            if (!m_parser.hasPartialRequest()) {
                m_requestStartedAt = now;
            }
            m_lastReadAt = now;

            SwByteArray bytes(chunk.data(), chunk.size());
            SwList<SwHttpRequest> parsedRequests;
            SwHttpParser::FeedStatus status = m_parser.feed(bytes, parsedRequests);
            if (status == SwHttpParser::FeedStatus::Error) {
                int parseStatus = m_parser.errorStatus();
                if (parseStatus <= 0) {
                    parseStatus = 400;
                }
                sendErrorAndClose_(parseStatus, m_parser.errorMessage());
                return;
            }

            for (std::size_t i = 0; i < parsedRequests.size(); ++i) {
                parsedRequests[i].isTls = m_isTls;
                parsedRequests[i].localPort = m_localPort;
                m_pendingRequests.append(parsedRequests[i]);
                if (m_pendingRequests.size() > m_limits.maxPipelinedRequests) {
                    sendErrorAndClose_(400, "Too many pipelined requests");
                    return;
                }
            }
        }

        if (!m_handlingResponse && !m_waitingAsyncResponse) {
            processNextRequest_();
        }
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
        auto now = std::chrono::steady_clock::now();

        if (m_handlingResponse) {
            if (m_timeouts.writeTimeoutMs > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_writeStartedAt).count();
                if (elapsed > m_timeouts.writeTimeoutMs) {
                    swCWarning(kSwLogCategory_SwHttpSession) << "[SwHttpSession] write timeout, closing";
                    cleanup_();
                }
            }
            return;
        }

        if (m_waitingAsyncResponse) {
            if (m_timeouts.bodyReadTimeoutMs > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_waitingResponseAt).count();
                if (elapsed > m_timeouts.bodyReadTimeoutMs) {
                    sendErrorAndClose_(504, "Route async timeout");
                }
            }
            return;
        }

        if (m_parser.isAwaitingHeaders() && m_timeouts.headerReadTimeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_requestStartedAt).count();
            if (elapsed > m_timeouts.headerReadTimeoutMs) {
                sendErrorAndClose_(408, "Request Timeout");
                return;
            }
        }

        if (m_parser.isAwaitingBody() && m_timeouts.bodyReadTimeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_requestStartedAt).count();
            if (elapsed > m_timeouts.bodyReadTimeoutMs) {
                sendErrorAndClose_(408, "Request Timeout");
                return;
            }
        }

        if (!m_parser.hasPartialRequest() && m_pendingRequests.isEmpty() && m_timeouts.keepAliveIdleTimeoutMs > 0) {
            auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastReadAt).count();
            if (idle > m_timeouts.keepAliveIdleTimeoutMs) {
                cleanup_();
            }
        }
    }

private:
    void scheduleDeferredWriteFinished_() {
        if (m_deferredWriteScheduled || !m_socket || m_cleaned) {
            return;
        }

        m_deferredWriteScheduled = true;
        SwPointer<SwHttpSession> self(this);
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->postEventOnLane([self]() {
                if (self) {
                    self->onDeferredWriteFinished_();
                }
            }, SwFiberLane::Normal);
            return;
        }

        SwTimer::singleShot(0, this, &SwHttpSession::onDeferredWriteFinished_);
    }

    void writeSocket_(const SwString& data) {
        if (!m_socket || data.isEmpty()) {
            return;
        }
        m_inSocketWrite = true;
        m_socket->write(data);
        m_inSocketWrite = false;
        m_writeStartedAt = std::chrono::steady_clock::now();

        if (m_writeFinishedDeferred) {
            m_writeFinishedDeferred = false;
            scheduleDeferredWriteFinished_();
        }
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

    void sendChunkedPart_(const SwByteArray& part) {
        if (!m_socket) {
            return;
        }

        SwString payload = toHex_(part.size()) + "\r\n";
        if (!part.isEmpty()) {
            payload.append(SwString(part.toStdString()));
        }
        payload.append("\r\n");
        writeSocket_(payload);
    }

    void sendResponseHeaders_(const SwHttpResponse& response) {
        SwString raw;
        raw += "HTTP/1.1 ";
        raw += SwString::number(response.status);
        raw += " ";
        raw += response.reason.isEmpty() ? swHttpStatusReason(response.status) : response.reason;
        raw += "\r\n";

        bool hasContentLength = false;
        bool hasTransferEncoding = false;
        bool hasConnection = false;

        for (auto it = response.headers.begin(); it != response.headers.end(); ++it) {
            SwString key = it.key().trimmed().toLower();
            if (key.isEmpty()) {
                continue;
            }
            if (key == "content-length") hasContentLength = true;
            if (key == "transfer-encoding") hasTransferEncoding = true;
            if (key == "connection") hasConnection = true;
            raw += key + ": " + it.value() + "\r\n";
        }

        if (response.useChunkedTransfer) {
            if (!hasTransferEncoding) {
                raw += "transfer-encoding: chunked\r\n";
            }
        } else if (!hasContentLength) {
            if (response.hasFile) {
                raw += "content-length: " + SwString::number(static_cast<long long>(response.fileLength)) + "\r\n";
            } else {
                raw += "content-length: " + SwString::number(static_cast<long long>(response.body.size())) + "\r\n";
            }
        }

        if (!hasConnection) {
            raw += m_closeAfterWrite ? "connection: close\r\n" : "connection: keep-alive\r\n";
        }
        raw += "\r\n";

        writeSocket_(raw);
    }

    void beginResponse_(const SwHttpRequest& request, const SwHttpResponse& sourceResponse) {
        if (!m_socket) {
            cleanup_();
            return;
        }

        SwHttpResponse response = sourceResponse;
        if (response.reason.isEmpty()) {
            response.reason = swHttpStatusReason(response.status);
        }

        m_handlingResponse = true;
        m_waitingAsyncResponse = false;
        m_responsePayloadDone = false;
        m_sendState = SendState::Idle;
        m_chunkPartIndex = 0;
        m_chunkTerminatorSent = false;
        m_chunkParts.clear();
        cleanupStreamFile_();
        m_handoverAfterWrite = false;
        m_socketHandoverCallback = nullptr;

        m_closeAfterWrite = response.closeConnection || !request.keepAlive;
        if (response.switchToRawSocket && response.onSwitchToRawSocket) {
            m_handoverAfterWrite = true;
            m_socketHandoverCallback = response.onSwitchToRawSocket;
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

        sendResponseHeaders_(response);

        if (response.headOnly) {
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
            m_sendState = SendState::SendingFile;
            return;
        }

        if (response.useChunkedTransfer) {
            if (!response.chunkedParts.isEmpty()) {
                m_chunkParts = response.chunkedParts;
            } else if (!response.body.isEmpty()) {
                m_chunkParts.append(response.body);
            }
            m_sendState = SendState::SendingChunked;
            return;
        }

        if (!response.body.isEmpty()) {
            writeSocket_(SwString(response.body.toStdString()));
        }
        m_responsePayloadDone = true;
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

        writeSocket_(chunk);

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

        if (m_chunkPartIndex < m_chunkParts.size()) {
            sendChunkedPart_(m_chunkParts[m_chunkPartIndex]);
            ++m_chunkPartIndex;
            return;
        }

        if (!m_chunkTerminatorSent) {
            writeSocket_("0\r\n\r\n");
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

        m_activeRequest = m_pendingRequests.first();
        m_pendingRequests.removeFirst();
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
            ThreadHandle* affinity = threadHandle();
            m_requestHandler(m_activeRequest, [this, affinity](const SwHttpResponse& response) {
                auto deliver = [this, response]() {
                    if (m_cleaned || !m_socket || !m_waitingAsyncResponse || m_handlingResponse) {
                        return;
                    }
                    m_waitingAsyncResponse = false;
                    beginResponse_(m_activeRequest, response);
                };

                if (affinity && ThreadHandle::currentThread() != affinity) {
                    affinity->postTaskOnLane(deliver, SwFiberLane::Control);
                } else {
                    deliver();
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
        beginResponse_(m_activeRequest, response);
    }

    void finishDetached_() {
        if (m_cleaned) {
            return;
        }
        m_cleaned = true;

        for (std::size_t i = 0; i < m_cleanupHooks.size(); ++i) {
            m_cleanupHooks[i]();
        }
        m_cleanupHooks.clear();

        cleanupStreamFile_();
        cleanupRequestTempFiles_();
        m_waitingAsyncResponse = false;

        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }

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
        m_socketHandoverCallback = nullptr;
        m_handoverAfterWrite = false;

        if (handover) {
            handover(rawSocket);
        } else {
            rawSocket->close();
            rawSocket->deleteLater();
        }

        finishDetached_();
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
            m_chunkPartIndex = 0;
            m_chunkTerminatorSent = false;
            m_responsePayloadDone = false;
            m_waitingAsyncResponse = false;

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

        if (m_handlingResponse) {
            cleanup_();
            return;
        }

        SwHttpRequest syntheticRequest;
        syntheticRequest.method = "GET";
        syntheticRequest.keepAlive = false;

        SwHttpResponse response = swHttpTextResponse(status > 0 ? status : 400, message.isEmpty() ? "Bad Request" : message);
        response.closeConnection = true;
        beginResponse_(syntheticRequest, response);
    }

    void cleanupStreamFile_() {
        if (m_streamFile) {
            m_streamFile->close();
            m_streamFile->deleteLater();
            m_streamFile = nullptr;
        }
        m_streamBytesRemaining = 0;
    }

    void cleanup_() {
        if (m_cleaned) {
            return;
        }
        m_cleaned = true;

        for (std::size_t i = 0; i < m_cleanupHooks.size(); ++i) {
            m_cleanupHooks[i]();
        }
        m_cleanupHooks.clear();

        cleanupStreamFile_();
        cleanupRequestTempFiles_();
        m_waitingAsyncResponse = false;
        m_handoverAfterWrite = false;
        m_socketHandoverCallback = nullptr;

        if (m_timeoutWatch) {
            m_timeoutWatch->stop();
        }

        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
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
