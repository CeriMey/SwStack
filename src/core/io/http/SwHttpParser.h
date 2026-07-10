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
#include "SwByteRingBuffer.h"

#include <limits>
#include <utility>

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
    void reset(bool releaseStorage = false) {
        if (releaseStorage) {
            m_buffer.release();
        } else {
            m_buffer.clear();
        }
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
        m_continueNeeded = false;
        m_multipartStreamingActive = false;
    }

    /**
     * @brief Performs the `feed` operation.
     * @param data Value passed to the method.
     * @param outRequests Output value filled by the method.
     * @return The requested feed.
     */
    FeedStatus feed(const SwByteArray& data, SwList<SwHttpRequest>& outRequests) {
        return feed(data.constData(), data.size(), outRequests);
    }

    FeedStatus feed(const char* data, std::size_t size, SwList<SwHttpRequest>& outRequests) {
        const std::size_t outputStart = outRequests.size();
        m_feedOutputStart = outputStart;
        bool progressed = false;
        if (!data || size == 0) {
            const FeedStatus status = drainAvailable_(outRequests, progressed);
            if (status == FeedStatus::Error) {
                discardFeedOutputs_(outRequests, outputStart);
            }
            return status;
        }

        std::size_t offset = 0;
        while (offset < size) {
            std::size_t take = size - offset;
            const std::size_t maxAppend = maxFeedAppendBytes_();
            if (take > maxAppend) {
                take = maxAppend;
            }
            m_buffer.append(data + offset, take);
            offset += take;

            const FeedStatus status = drainAvailable_(outRequests, progressed);
            if (status == FeedStatus::Error) {
                discardFeedOutputs_(outRequests, outputStart);
                return status;
            }
        }

        if (m_state == State::Error) {
            discardFeedOutputs_(outRequests, outputStart);
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

    /**
     * @brief Returns the number of transport bytes retained by the parser.
     *
     * This is primarily useful when HTTP hands the connection to an upgraded
     * protocol and the transport read coalesced bytes after the HTTP headers.
     */
    std::size_t bufferedDataSize() const {
        return m_buffer.size();
    }

    /**
     * @brief Transfers all bytes not consumed by HTTP framing to the caller.
     */
    SwByteArray takeBufferedData() {
        return m_buffer.read(m_buffer.size());
    }

    /** Returns and clears the one-shot request to emit an HTTP 100 interim response. */
    bool takeContinueNeeded() {
        const bool needed = m_continueNeeded;
        m_continueNeeded = false;
        return needed;
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
    SwByteRingBuffer m_buffer;
    State m_state = State::RequestLine;

    SwHttpRequest m_currentRequest;
    std::size_t m_headerBytes = 0;
    std::size_t m_headerCount = 0;
    std::size_t m_contentLengthRemaining = 0;
    std::size_t m_chunkBytesRemaining = 0;
    bool m_chunkedBody = false;
    std::size_t m_bodyBytesReceived = 0;
    bool m_continueNeeded = false;
    bool m_multipartStreamingActive = false;
    SwHttpMultipartStreamParser m_multipartStreamParser;
    std::size_t m_feedOutputStart = 0;

    int m_errorStatus = 0;
    SwString m_errorMessage;

    static std::size_t maxFeedAppendBytes_() {
        // Large enough to amortize parsing and buffer compaction on uploads,
        // while still bounding the transient amount appended before limits are
        // checked by drainAvailable_().
        return 64U * 1024U;
    }

    static bool isTokenCharacter_(char value) {
        const unsigned char c = static_cast<unsigned char>(value);
        if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z')) {
            return true;
        }
        switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'':
        case '*': case '+': case '-': case '.': case '^': case '_':
        case '`': case '|': case '~':
            return true;
        default:
            return false;
        }
    }

    static bool isValidToken_(const SwString& value) {
        if (value.isEmpty()) {
            return false;
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (!isTokenCharacter_(value[i])) {
                return false;
            }
        }
        return true;
    }

    static bool isValidHeaderValue_(const SwString& value) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if ((c < 0x20u && c != '\t') || c == 0x7fu) {
                return false;
            }
        }
        return true;
    }

    static bool isValidRequestTarget_(const SwString& target) {
        if (target.isEmpty()) {
            return false;
        }
        for (std::size_t i = 0; i < target.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(target[i]);
            if (c <= 0x20u || c == 0x7fu || c == '#') {
                return false;
            }
        }
        return true;
    }

    static bool parseDecimalSize_(const SwString& text, std::size_t& outValue) {
        outValue = 0;
        if (text.isEmpty()) {
            return false;
        }
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') {
                return false;
            }
            const std::size_t digit = static_cast<std::size_t>(c - '0');
            if (outValue > ((std::numeric_limits<std::size_t>::max)() - digit) / 10u) {
                return false;
            }
            outValue = outValue * 10u + digit;
        }
        return true;
    }

    static void discardFeedOutputs_(SwList<SwHttpRequest>& outRequests,
                                    std::size_t outputStart) {
        while (outRequests.size() > outputStart) {
            swHttpCleanupMultipartTemporaryFiles(outRequests[outRequests.size() - 1]);
            outRequests.removeLast();
        }
    }

    FeedStatus drainAvailable_(SwList<SwHttpRequest>& outRequests, bool& progressed) {
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

    void setError_(int status, const SwString& message) {
        if (m_multipartStreamingActive) {
            m_multipartStreamParser.cleanupTemporaryFiles();
            m_multipartStreamingActive = false;
        }
        m_state = State::Error;
        m_continueNeeded = false;
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
        m_continueNeeded = false;
        m_multipartStreamingActive = false;
        m_multipartStreamParser.reset();
    }

    bool popLine_(SwString& outLine) {
        int eol = m_buffer.indexOf("\r\n");
        if (eol < 0) {
            return false;
        }
        SwByteArray line = m_buffer.left(eol);
        m_buffer.consume(static_cast<std::size_t>(eol + 2));
        outLine = SwString(line.toStdString());
        return true;
    }

    bool pendingBufferExceedsLimit_(std::size_t usedBytes, std::size_t maxBytes) const {
        if (usedBytes > maxBytes) {
            return true;
        }
        return m_buffer.size() > maxBytes - usedBytes;
    }

    bool parseRequestLine_() {
        SwString line;
        if (!popLine_(line)) {
            if (pendingBufferExceedsLimit_(0, m_limits.maxRequestLineBytes)) {
                setError_(414, "Request line too long");
            }
            return false;
        }

        if (line.size() > m_limits.maxRequestLineBytes) {
            setError_(414, "Request line too long");
            return false;
        }

        const int firstSpace = line.indexOf(" ");
        const int secondSpace = firstSpace >= 0 ? line.indexOf(" ", firstSpace + 1) : -1;
        const int thirdSpace = secondSpace >= 0 ? line.indexOf(" ", secondSpace + 1) : -1;
        if (firstSpace <= 0 || secondSpace <= firstSpace + 1 ||
            secondSpace + 1 >= static_cast<int>(line.size()) || thirdSpace >= 0) {
            setError_(400, "Malformed request line");
            return false;
        }

        const SwString method = line.left(firstSpace);
        const SwString target = line.mid(firstSpace + 1, secondSpace - firstSpace - 1);
        const SwString protocol = line.mid(secondSpace + 1);
        if (!isValidToken_(method) || !isValidRequestTarget_(target) ||
            (protocol != "HTTP/1.0" && protocol != "HTTP/1.1")) {
            setError_(400, "Malformed request line");
            return false;
        }

        m_currentRequest.method = method.toUpper();
        m_currentRequest.target = target;
        m_currentRequest.protocol = protocol;

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
                if (pendingBufferExceedsLimit_(m_headerBytes, m_limits.maxHeaderBytes)) {
                    setError_(431, "Headers too large");
                }
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

            const SwString rawKey = line.left(colon);
            if (!isValidToken_(rawKey)) {
                setError_(400, "Malformed header name");
                return false;
            }
            SwString key = rawKey.toLower();
            SwString value = line.mid(colon + 1).trimmed();
            if (!isValidHeaderValue_(value)) {
                setError_(400, "Malformed header value");
                return false;
            }
            if (m_currentRequest.headers.contains(key)) {
                if (key == "content-length" || key == "transfer-encoding" || key == "host") {
                    setError_(400, "Duplicate framing/authority header");
                    return false;
                }
                m_currentRequest.headers[key] += ", " + value;
            } else {
                m_currentRequest.headers[key] = value;
            }
        }
    }

    bool finalizeHeaders_() {
        if (m_currentRequest.protocol == "HTTP/1.1") {
            if (!m_currentRequest.headers.contains("host")) {
                setError_(400, "Missing Host header");
                return false;
            }
            const SwString host = m_currentRequest.headers["host"];
            if (host.isEmpty() || host.contains(',') || host.contains(' ') || host.contains('\t')) {
                setError_(400, "Invalid Host header");
                return false;
            }
        }

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
            if (m_currentRequest.protocol != "HTTP/1.1") {
                setError_(400, "Transfer-Encoding requires HTTP/1.1");
                return false;
            }
            const SwString te = m_currentRequest.headers["transfer-encoding"].trimmed().toLower();
            const SwList<SwString> codings = te.split(',');
            if (codings.size() != 1 || codings[0].trimmed() != "chunked") {
                setError_(400, "Unsupported Transfer-Encoding");
                return false;
            }
            chunked = true;
        }

        bool hasLength = false;
        std::size_t contentLength = 0;
        if (m_currentRequest.headers.contains("content-length")) {
            const SwString lengthText = m_currentRequest.headers["content-length"].trimmed();
            if (!parseDecimalSize_(lengthText, contentLength)) {
                setError_(400, "Invalid Content-Length");
                return false;
            }
            hasLength = true;
        }

        if (chunked && hasLength) {
            setError_(400, "Conflicting body framing");
            return false;
        }

        const bool hasBody = chunked || (hasLength && contentLength > 0);
        if (m_currentRequest.headers.contains("expect")) {
            const SwString expectation =
                m_currentRequest.headers["expect"].trimmed().toLower();
            if (m_currentRequest.protocol != "HTTP/1.1" || expectation != "100-continue") {
                setError_(417, "Expectation Failed");
                return false;
            }
        }

        if (hasLength && contentLength > m_limits.maxBodyBytes) {
            setError_(413, "Body too large");
            return false;
        }

        if (hasBody && m_currentRequest.headers.contains("expect")) {
            m_continueNeeded = true;
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
                if (!feedMultipartFromBuffer_(take)) {
                    return false;
                }
            } else {
                m_buffer.readTo(m_currentRequest.body, take);
            }
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
            if (pendingBufferExceedsLimit_(0, m_limits.maxRequestLineBytes)) {
                setError_(400, "Chunk size line too long");
            }
            return false;
        }
        if (line.size() > m_limits.maxRequestLineBytes) {
            setError_(400, "Chunk size line too long");
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
        if (m_bodyBytesReceived > m_limits.maxBodyBytes ||
            chunkSize > m_limits.maxBodyBytes - m_bodyBytesReceived) {
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
            if (!feedMultipartFromBuffer_(take)) {
                return false;
            }
        } else {
            m_buffer.readTo(m_currentRequest.body, take);
        }
        m_chunkBytesRemaining -= take;

        if (m_chunkBytesRemaining == 0) {
            m_state = State::ChunkDataCRLF;
            return true;
        }
        return false;
    }

    bool feedMultipartFromBuffer_(std::size_t bytes) {
        std::size_t remaining = bytes;
        while (remaining > 0) {
            const std::size_t contiguous = (std::min)(remaining, m_buffer.contiguousSize());
            const char* data = m_buffer.contiguousData();
            if (!data || contiguous == 0) {
                setError_(400, "Multipart buffer state mismatch");
                return false;
            }
            SwString streamError;
            if (!m_multipartStreamParser.feed(data, contiguous, streamError)) {
                setError_(400,
                          streamError.isEmpty() ? SwString("Malformed multipart body")
                                                : streamError);
                return false;
            }
            m_buffer.consume(contiguous);
            remaining -= contiguous;
        }
        return true;
    }

    bool parseChunkDataCrlf_() {
        if (m_buffer.size() < 2) {
            return false;
        }
        if (m_buffer[0] != '\r' || m_buffer[1] != '\n') {
            setError_(400, "Malformed chunk delimiter");
            return false;
        }
        m_buffer.consume(2);
        m_state = State::ChunkSize;
        return true;
    }

    bool parseChunkTrailers_() {
        while (true) {
            SwString line;
            if (!popLine_(line)) {
                if (pendingBufferExceedsLimit_(m_headerBytes, m_limits.maxHeaderBytes)) {
                    setError_(431, "Trailers too large");
                }
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
            const int colon = line.indexOf(":");
            if (colon <= 0 || !isValidToken_(line.left(colon)) ||
                !isValidHeaderValue_(line.mid(colon + 1).trimmed())) {
                setError_(400, "Malformed trailer");
                return false;
            }
            const SwString trailerName = line.left(colon).toLower();
            if (trailerName == "content-length" || trailerName == "transfer-encoding" ||
                trailerName == "host") {
                setError_(400, "Forbidden framing trailer");
                return false;
            }
            if (++m_headerCount > m_limits.maxHeaderCount) {
                setError_(431, "Too many headers and trailers");
                return false;
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
        outRequests.append(std::move(m_currentRequest));
        if (outRequests.size() - m_feedOutputStart > m_limits.maxPipelinedRequests) {
            setError_(400, "Too many pipelined requests");
            return;
        }
        resetCurrentRequest();
        m_state = State::RequestLine;
    }
};
