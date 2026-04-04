#pragma once

/**
 * @file src/core/io/http/SwHttpParser.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpParser in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP parser interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpParser.
 *
 * This file is especially important for fragmented HTTP input because the public contract has to
 * make parser progress, limits, and parse outcomes explicit across packet boundaries.
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

#include "http/SwHttpTypes.h"
#include "http/SwHttpMultipart.h"

class SwHttpParser {
public:
    enum class FeedStatus {
        Ok,
        NeedMoreData,
        Error
    };

    /**
     * @brief Constructs a `SwHttpParser` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHttpParser() = default;

    /**
     * @brief Sets the limits.
     * @param limits Limit configuration to apply.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLimits(const SwHttpLimits& limits) {
        m_limits = limits;
    }

    /**
     * @brief Resets the object to a baseline state.
     */
    void reset() {
        m_buffer.clear();
        resetCurrentRequest();
        m_state = State::RequestLine;
        m_errorStatus = 0;
        m_errorMessage.clear();
        m_headerBytes = 0;
        m_headerCount = 0;
        m_contentLengthRemaining = 0;
        m_chunkBytesRemaining = 0;
        m_chunkedBody = false;
        m_bodyBytesReceived = 0;
        m_multipartStreamingActive = false;
    }

    /**
     * @brief Performs the `feed` operation.
     * @param data Value passed to the method.
     * @param outRequests Output value filled by the method.
     * @return The requested feed.
     */
    FeedStatus feed(const SwByteArray& data, SwList<SwHttpRequest>& outRequests) {
        if (!data.isEmpty()) {
            m_buffer.append(data);
        }

        bool progressed = false;
        while (true) {
            if (m_state == State::Error) {
                return FeedStatus::Error;
            }

            bool loopProgress = false;
            switch (m_state) {
            case State::RequestLine:
                loopProgress = parseRequestLine_();
                break;
            case State::Headers:
                loopProgress = parseHeaders_();
                break;
            case State::BodyFixed:
                loopProgress = parseFixedBody_();
                break;
            case State::ChunkSize:
                loopProgress = parseChunkSize_();
                break;
            case State::ChunkData:
                loopProgress = parseChunkData_();
                break;
            case State::ChunkDataCRLF:
                loopProgress = parseChunkDataCrlf_();
                break;
            case State::ChunkTrailers:
                loopProgress = parseChunkTrailers_();
                break;
            case State::Complete:
                finalizeRequest_(outRequests);
                loopProgress = true;
                break;
            case State::Error:
                return FeedStatus::Error;
            }

            if (!loopProgress) {
                break;
            }
            progressed = true;
        }

        if (m_state == State::Error) {
            return FeedStatus::Error;
        }
        if (progressed) {
            return FeedStatus::Ok;
        }
        return FeedStatus::NeedMoreData;
    }

    /**
     * @brief Returns whether the object reports error.
     * @return `true` when the object reports error; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasError() const {
        return m_state == State::Error;
    }

    /**
     * @brief Returns the current error Status.
     * @return The current error Status.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int errorStatus() const {
        return m_errorStatus;
    }

    /**
     * @brief Returns the current error Message.
     * @return The current error Message.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString errorMessage() const {
        return m_errorMessage;
    }

    /**
     * @brief Returns whether the object reports awaiting Headers.
     * @return `true` when the object reports awaiting Headers; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isAwaitingHeaders() const {
        return m_state == State::RequestLine || m_state == State::Headers;
    }

    /**
     * @brief Returns whether the object reports awaiting Body.
     * @return `true` when the object reports awaiting Body; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isAwaitingBody() const {
        return m_state == State::BodyFixed || m_state == State::ChunkSize ||
               m_state == State::ChunkData || m_state == State::ChunkDataCRLF ||
               m_state == State::ChunkTrailers;
    }

    /**
     * @brief Returns whether the object reports partial Request.
     * @return `true` when the object reports partial Request; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool hasPartialRequest() const {
        return m_state != State::RequestLine || !m_buffer.isEmpty();
    }

private:
    enum class State {
        RequestLine,
        Headers,
        BodyFixed,
        ChunkSize,
        ChunkData,
        ChunkDataCRLF,
        ChunkTrailers,
        Complete,
        Error
    };

    SwHttpLimits m_limits;
    SwByteArray m_buffer;
    State m_state = State::RequestLine;

    SwHttpRequest m_currentRequest;
    std::size_t m_headerBytes = 0;
    std::size_t m_headerCount = 0;
    std::size_t m_contentLengthRemaining = 0;
    std::size_t m_chunkBytesRemaining = 0;
    bool m_chunkedBody = false;
    std::size_t m_bodyBytesReceived = 0;
    bool m_multipartStreamingActive = false;
    SwHttpMultipartStreamParser m_multipartStreamParser;

    int m_errorStatus = 0;
    SwString m_errorMessage;

    void setError_(int status, const SwString& message) {
        if (m_multipartStreamingActive) {
            m_multipartStreamParser.cleanupTemporaryFiles();
            m_multipartStreamingActive = false;
        }
        m_state = State::Error;
        m_errorStatus = status;
        m_errorMessage = message;
    }

    void resetCurrentRequest() {
        m_currentRequest = SwHttpRequest();
        m_headerBytes = 0;
        m_headerCount = 0;
        m_contentLengthRemaining = 0;
        m_chunkBytesRemaining = 0;
        m_chunkedBody = false;
        m_bodyBytesReceived = 0;
        m_multipartStreamingActive = false;
        m_multipartStreamParser.reset();
    }

    bool popLine_(SwString& outLine) {
        int eol = m_buffer.indexOf("\r\n");
        if (eol < 0) {
            return false;
        }
        SwByteArray line = m_buffer.left(eol);
        m_buffer.remove(0, eol + 2);
        outLine = SwString(line.toStdString());
        return true;
    }

    bool parseRequestLine_() {
        SwString line;
        if (!popLine_(line)) {
            if (m_buffer.size() > m_limits.maxRequestLineBytes) {
                setError_(414, "Request line too long");
            }
            return false;
        }

        if (line.isEmpty()) {
            // Ignore leading empty lines.
            return true;
        }

        if (line.size() > m_limits.maxRequestLineBytes) {
            setError_(414, "Request line too long");
            return false;
        }

        SwList<SwString> parts = line.split(' ');
        if (parts.size() < 3) {
            setError_(400, "Malformed request line");
            return false;
        }

        m_currentRequest.method = parts[0].trimmed().toUpper();
        m_currentRequest.target = parts[1].trimmed();
        m_currentRequest.protocol = parts[2].trimmed().toUpper();
        if (m_currentRequest.method.isEmpty() || m_currentRequest.target.isEmpty() || m_currentRequest.protocol.isEmpty()) {
            setError_(400, "Malformed request line");
            return false;
        }

        if (!parseTarget_(m_currentRequest.target)) {
            setError_(400, "Malformed target");
            return false;
        }

        m_currentRequest.keepAlive = (m_currentRequest.protocol == "HTTP/1.1");
        m_state = State::Headers;
        return true;
    }

    bool parseTarget_(const SwString& target) {
        SwString effectiveTarget = target.trimmed();
        const SwString lowerTarget = effectiveTarget.toLower();
        if (lowerTarget.startsWith("http://") || lowerTarget.startsWith("https://")) {
            const int authorityPos = effectiveTarget.indexOf("://");
            const int pathPos = (authorityPos >= 0) ? effectiveTarget.indexOf("/", authorityPos + 3) : -1;
            effectiveTarget = (pathPos >= 0) ? effectiveTarget.mid(pathPos) : SwString("/");
        }

        int queryPos = effectiveTarget.indexOf("?");
        SwString rawPath = (queryPos >= 0) ? effectiveTarget.left(queryPos) : effectiveTarget;
        SwString query = (queryPos >= 0) ? effectiveTarget.mid(queryPos + 1) : SwString();
        if (rawPath.isEmpty()) {
            rawPath = "/";
        }

        SwString decodedPath;
        if (!swHttpPercentDecode(rawPath, decodedPath, false)) {
            return false;
        }

        m_currentRequest.path = swHttpNormalizePath(decodedPath);
        m_currentRequest.queryString = query;
        swHttpParseQueryString(query, m_currentRequest.queryParams);
        return true;
    }

    bool parseHeaders_() {
        while (true) {
            SwString line;
            if (!popLine_(line)) {
                return false;
            }

            m_headerBytes += line.size() + 2;
            if (m_headerBytes > m_limits.maxHeaderBytes) {
                setError_(431, "Headers too large");
                return false;
            }

            if (line.isEmpty()) {
                return finalizeHeaders_();
            }

            int colon = line.indexOf(":");
            if (colon <= 0) {
                setError_(400, "Malformed header");
                return false;
            }
            if (++m_headerCount > m_limits.maxHeaderCount) {
                setError_(431, "Too many headers");
                return false;
            }

            SwString key = line.left(colon).trimmed().toLower();
            SwString value = line.mid(colon + 1).trimmed();
            m_currentRequest.headers[key] = value;
        }
    }

    bool finalizeHeaders_() {
        if (m_currentRequest.headers.contains("connection")) {
            SwString conn = m_currentRequest.headers["connection"].toLower();
            if (swHttpHeaderContainsToken(conn, "close")) {
                m_currentRequest.keepAlive = false;
            } else if (swHttpHeaderContainsToken(conn, "keep-alive")) {
                m_currentRequest.keepAlive = true;
            }
        }

        bool chunked = false;
        if (m_currentRequest.headers.contains("transfer-encoding")) {
            SwString te = m_currentRequest.headers["transfer-encoding"].toLower();
            chunked = te.contains("chunked");
        }

        bool hasLength = false;
        std::size_t contentLength = 0;
        if (m_currentRequest.headers.contains("content-length")) {
            bool ok = false;
            long long parsed = m_currentRequest.headers["content-length"].toLongLong(&ok);
            if (!ok || parsed < 0) {
                setError_(400, "Invalid Content-Length");
                return false;
            }
            hasLength = true;
            contentLength = static_cast<std::size_t>(parsed);
        }

        if (chunked && hasLength) {
            setError_(400, "Conflicting body framing");
            return false;
        }

        if (chunked) {
            m_currentRequest.isChunkedBody = true;
            m_chunkedBody = true;
            if (!prepareMultipartStreaming_()) {
                return false;
            }
            m_state = State::ChunkSize;
            return true;
        }

        if (hasLength && contentLength > 0) {
            if (contentLength > m_limits.maxBodyBytes) {
                setError_(413, "Body too large");
                return false;
            }
            if (!prepareMultipartStreaming_()) {
                return false;
            }
            m_contentLengthRemaining = contentLength;
            m_state = State::BodyFixed;
            return true;
        }

        m_state = State::Complete;
        return true;
    }

    bool parseFixedBody_() {
        if (m_contentLengthRemaining == 0) {
            m_state = State::Complete;
            return true;
        }
        if (m_buffer.isEmpty()) {
            return false;
        }

        std::size_t take = m_contentLengthRemaining;
        if (take > m_buffer.size()) {
            take = m_buffer.size();
        }
        if (take > 0) {
            m_bodyBytesReceived += take;
            if (m_bodyBytesReceived > m_limits.maxBodyBytes) {
                setError_(413, "Body too large");
                return false;
            }

            if (m_multipartStreamingActive) {
                SwString streamError;
                if (!m_multipartStreamParser.feed(m_buffer.left(static_cast<int>(take)), streamError)) {
                    setError_(400, streamError.isEmpty() ? SwString("Malformed multipart body") : streamError);
                    return false;
                }
            } else {
                m_currentRequest.body.append(m_buffer.constData(), take);
            }
            m_buffer.remove(0, static_cast<int>(take));
            m_contentLengthRemaining -= take;
        }

        if (m_contentLengthRemaining == 0) {
            if (!finalizeMultipartStreaming_()) {
                return false;
            }
            m_state = State::Complete;
            return true;
        }
        return false;
    }

    bool parseChunkSize_() {
        SwString line;
        if (!popLine_(line)) {
            return false;
        }
        std::size_t chunkSize = 0;
        if (!swHttpParseHexSize(line, chunkSize)) {
            setError_(400, "Invalid chunk size");
            return false;
        }
        if (chunkSize > m_limits.maxChunkSize) {
            setError_(413, "Chunk too large");
            return false;
        }
        if (m_bodyBytesReceived + chunkSize > m_limits.maxBodyBytes) {
            setError_(413, "Body too large");
            return false;
        }
        m_chunkBytesRemaining = chunkSize;
        if (chunkSize == 0) {
            m_state = State::ChunkTrailers;
            return true;
        }
        m_state = State::ChunkData;
        return true;
    }

    bool parseChunkData_() {
        if (m_chunkBytesRemaining == 0) {
            m_state = State::ChunkDataCRLF;
            return true;
        }
        if (m_buffer.isEmpty()) {
            return false;
        }

        std::size_t take = m_chunkBytesRemaining;
        if (take > m_buffer.size()) {
            take = m_buffer.size();
        }
        m_bodyBytesReceived += take;
        if (m_bodyBytesReceived > m_limits.maxBodyBytes) {
            setError_(413, "Body too large");
            return false;
        }

        if (m_multipartStreamingActive) {
            SwString streamError;
            if (!m_multipartStreamParser.feed(m_buffer.left(static_cast<int>(take)), streamError)) {
                setError_(400, streamError.isEmpty() ? SwString("Malformed multipart body") : streamError);
                return false;
            }
        } else {
            m_currentRequest.body.append(m_buffer.constData(), take);
        }
        m_buffer.remove(0, static_cast<int>(take));
        m_chunkBytesRemaining -= take;

        if (m_chunkBytesRemaining == 0) {
            m_state = State::ChunkDataCRLF;
            return true;
        }
        return false;
    }

    bool parseChunkDataCrlf_() {
        if (m_buffer.size() < 2) {
            return false;
        }
        if (m_buffer[0] != '\r' || m_buffer[1] != '\n') {
            setError_(400, "Malformed chunk delimiter");
            return false;
        }
        m_buffer.remove(0, 2);
        m_state = State::ChunkSize;
        return true;
    }

    bool parseChunkTrailers_() {
        while (true) {
            SwString line;
            if (!popLine_(line)) {
                return false;
            }
            // Trailers ignored for now, but bounded by global header size policy.
            m_headerBytes += line.size() + 2;
            if (m_headerBytes > m_limits.maxHeaderBytes) {
                setError_(431, "Trailers too large");
                return false;
            }
            if (line.isEmpty()) {
                if (!finalizeMultipartStreaming_()) {
                    return false;
                }
                m_state = State::Complete;
                return true;
            }
        }
    }

    bool prepareMultipartStreaming_() {
        m_multipartStreamingActive = false;
        m_multipartStreamParser.reset();

        if (!m_limits.enableMultipartFileStreaming) {
            return true;
        }
        if (!m_currentRequest.headers.contains("content-type")) {
            return true;
        }

        SwString contentType = m_currentRequest.headers["content-type"];
        if (!contentType.toLower().startsWith("multipart/form-data")) {
            return true;
        }

        SwString boundary;
        if (!swHttpExtractMultipartBoundary(contentType, boundary)) {
            setError_(400, "Missing multipart boundary");
            return false;
        }

        SwString streamError;
        if (!m_multipartStreamParser.begin(boundary, m_limits, m_limits.multipartTempDirectory, streamError)) {
            setError_(500, streamError.isEmpty() ? SwString("Unable to initialize multipart streaming") : streamError);
            return false;
        }

        m_multipartStreamingActive = true;
        m_currentRequest.isMultipartFormData = true;
        return true;
    }

    bool finalizeMultipartStreaming_() {
        if (!m_multipartStreamingActive) {
            return true;
        }

        SwString streamError;
        if (!m_multipartStreamParser.finish(m_currentRequest.multipartParts, m_currentRequest.formFields, streamError)) {
            setError_(400, streamError.isEmpty() ? SwString("Malformed multipart body") : streamError);
            return false;
        }

        m_currentRequest.isMultipartFormData = true;
        m_multipartStreamingActive = false;
        return true;
    }

    void finalizeRequest_(SwList<SwHttpRequest>& outRequests) {
        outRequests.append(m_currentRequest);
        if (outRequests.size() > m_limits.maxPipelinedRequests) {
            setError_(400, "Too many pipelined requests");
            return;
        }
        resetCurrentRequest();
        m_state = State::RequestLine;
    }
};
