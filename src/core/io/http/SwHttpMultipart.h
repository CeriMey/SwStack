#pragma once

/**
 * @file src/core/io/http/SwHttpMultipart.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpMultipart in the CoreSw HTTP server
 * layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP multipart interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpMultipartStreamParser.
 *
 * HTTP-facing declarations in this header are intended to make incremental request processing and
 * response generation explicit enough for production hardening and testing.
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
#include "SwByteRingBuffer.h"
#include "SwDir.h"
#include "SwFile.h"
#include "platform/SwPlatformSelector.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>

inline SwString swHttpTrimQuotes_(const SwString& value) {
    SwString out = value.trimmed();
    if (out.size() >= 2 && out.startsWith("\"") && out.endsWith("\"")) {
        out = out.mid(1, static_cast<int>(out.size()) - 2);
    }
    return out;
}

inline void swHttpParseHeaderParams_(const SwString& headerValue,
                                     SwString& mainValue,
                                     SwMap<SwString, SwString>& params) {
    mainValue.clear();
    params.clear();

    SwList<SwString> tokens = headerValue.split(';');
    if (tokens.isEmpty()) {
        return;
    }

    mainValue = tokens[0].trimmed().toLower();
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        SwString token = tokens[i].trimmed();
        if (token.isEmpty()) {
            continue;
        }

        int eq = token.indexOf("=");
        if (eq <= 0) {
            continue;
        }
        SwString key = token.left(eq).trimmed().toLower();
        SwString value = swHttpTrimQuotes_(token.mid(eq + 1));
        if (!key.isEmpty()) {
            params[key] = value;
        }
    }
}

inline bool swHttpExtractMultipartBoundary(const SwString& contentType, SwString& outBoundary) {
    outBoundary.clear();

    SwString mainType;
    SwMap<SwString, SwString> params;
    swHttpParseHeaderParams_(contentType, mainType, params);
    if (mainType != "multipart/form-data") {
        return false;
    }
    if (!params.contains("boundary")) {
        return false;
    }

    SwString boundary = params["boundary"].trimmed();
    if (boundary.isEmpty() || boundary.size() > 70) {
        return false;
    }

    // RFC 2046 bchars: spaces are permitted internally, but not as the final
    // character. Reject controls and punctuation that can alter MIME framing.
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(boundary[i]);
        const bool alphaNumeric = (c >= '0' && c <= '9') ||
                                  (c >= 'A' && c <= 'Z') ||
                                  (c >= 'a' && c <= 'z');
        const bool punctuation = c == '\'' || c == '(' || c == ')' || c == '+' ||
                                 c == '_' || c == ',' || c == '-' || c == '.' ||
                                 c == '/' || c == ':' || c == '=' || c == '?';
        if (!alphaNumeric && !punctuation && c != ' ') {
            return false;
        }
    }
    if (boundary[boundary.size() - 1] == ' ') {
        return false;
    }

    outBoundary = boundary;
    return true;
}

inline bool swHttpParseMultipartHeaders_(const SwByteArray& rawHeaders,
                                         const SwHttpLimits& limits,
                                         SwMap<SwString, SwString>& outHeaders,
                                         SwString& outError) {
    outHeaders.clear();
    outError.clear();

    if (rawHeaders.size() > limits.maxMultipartPartHeadersBytes) {
        outError = "Multipart part headers too large";
        return false;
    }

    SwString text(rawHeaders.toStdString());
    SwList<SwString> lines = text.split("\r\n");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const SwString line = lines[i];
        if (line.isEmpty()) {
            continue;
        }

        int colon = line.indexOf(":");
        if (colon <= 0) {
            outError = "Malformed multipart part header";
            return false;
        }

        const SwString rawKey = line.left(colon);
        if (rawKey.isEmpty()) {
            outError = "Malformed multipart part header";
            return false;
        }
        for (std::size_t j = 0; j < rawKey.size(); ++j) {
            const unsigned char c = static_cast<unsigned char>(rawKey[j]);
            const bool alphaNumeric = (c >= '0' && c <= '9') ||
                                      (c >= 'A' && c <= 'Z') ||
                                      (c >= 'a' && c <= 'z');
            const bool punctuation = c == '!' || c == '#' || c == '$' || c == '%' ||
                                     c == '&' || c == '\'' || c == '*' || c == '+' ||
                                     c == '-' || c == '.' || c == '^' || c == '_' ||
                                     c == '`' || c == '|' || c == '~';
            if (!alphaNumeric && !punctuation) {
                outError = "Malformed multipart part header";
                return false;
            }
        }
        SwString key = rawKey.toLower();
        SwString value = line.mid(colon + 1).trimmed();
        for (std::size_t j = 0; j < value.size(); ++j) {
            const unsigned char c = static_cast<unsigned char>(value[j]);
            if ((c < 0x20u && c != '\t') || c == 0x7fu) {
                outError = "Malformed multipart part header";
                return false;
            }
        }
        if (outHeaders.contains(key)) {
            outError = "Duplicate multipart part header";
            return false;
        }
        outHeaders[key] = value;
    }
    return true;
}

inline void swHttpCleanupMultipartTemporaryFiles(SwHttpRequest& request) {
    for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
        SwHttpRequest::MultipartPart& part = request.multipartParts[i];
        if (!part.storedOnDisk || part.tempFilePath.isEmpty()) {
            continue;
        }
#if defined(_WIN32)
        const std::wstring widePath = part.tempFilePath.toStdWString();
        (void)_wremove(widePath.c_str());
#else
        (void)std::remove(part.tempFilePath.toStdString().c_str());
#endif
        part.tempFilePath.clear();
        part.storedOnDisk = false;
    }
}

class SwHttpMultipartStreamParser {
public:
    /**
     * @brief Constructs a `SwHttpMultipartStreamParser` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHttpMultipartStreamParser() = default;

    /**
     * @brief Destroys the `SwHttpMultipartStreamParser` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwHttpMultipartStreamParser() {
        reset();
    }

    /**
     * @brief Resets the object to a baseline state.
     */
    void reset() {
        cleanupTemporaryFiles();
        m_state = State::NeedInitialBoundary;
        m_limits = SwHttpLimits();
        m_tempDirectory.clear();
        m_delimiter.clear();
        m_boundaryMarker.clear();
        m_buffer.clear();
        m_parts.clear();
        m_formFields.clear();
        m_currentPart = SwHttpRequest::MultipartPart();
        m_currentPartBytes = 0;
        m_hasCurrentPart = false;
        m_totalInputBytes = 0;
    }

    /**
     * @brief Performs the `begin` operation.
     * @param boundary Value passed to the method.
     * @param limits Limit configuration to apply.
     * @param tempDirectory Value passed to the method.
     * @param outError Output value filled by the method.
     * @return `true` on success; otherwise `false`.
     */
    bool begin(const SwString& boundary,
               const SwHttpLimits& limits,
               const SwString& tempDirectory,
               SwString& outError) {
        outError.clear();
        reset();

        if (boundary.isEmpty() || boundary.size() > 70) {
            outError = "Missing multipart boundary";
            return false;
        }

        m_limits = limits;
        m_tempDirectory = tempDirectory;
        if (m_tempDirectory.isEmpty()) {
            m_tempDirectory = "http_multipart_tmp";
        }

        if (m_limits.enableMultipartFileStreaming) {
            m_tempDirectory = swDirPlatform().absolutePath(m_tempDirectory);
            if (!SwDir::mkpathAbsolute(m_tempDirectory, true)) {
                outError = "Unable to create multipart temp directory";
                return false;
            }
        }

        const SwString delimiterText = "--" + boundary;
        m_delimiter = SwByteArray(delimiterText.toStdString());
        m_boundaryMarker = SwByteArray("\r\n" + delimiterText.toStdString());
        return true;
    }

    /**
     * @brief Performs the `feed` operation.
     * @param bytes Value passed to the method.
     * @param outError Output value filled by the method.
     * @return `true` on success; otherwise `false`.
     */
    bool feed(const SwByteArray& bytes, SwString& outError) {
        return feed(bytes.constData(), bytes.size(), outError);
    }

    bool feed(const char* data, std::size_t size, SwString& outError) {
        outError.clear();
        if (size > 0) {
            if (!data) {
                outError = "Invalid multipart input";
                return false;
            }
            if (m_totalInputBytes > m_limits.maxBodyBytes ||
                size > m_limits.maxBodyBytes - m_totalInputBytes) {
                outError = "Multipart body too large";
                failAndCleanup_();
                return false;
            }
            m_totalInputBytes += size;
            m_buffer.append(data, size);
        }
        return process_(outError, false);
    }

    /**
     * @brief Performs the `finish` operation.
     * @param outParts Output value filled by the method.
     * @param outFormFields Output value filled by the method.
     * @param outError Output value filled by the method.
     * @return `true` on success; otherwise `false`.
     */
    bool finish(SwList<SwHttpRequest::MultipartPart>& outParts,
                 SwMap<SwString, SwString>& outFormFields,
                 SwString& outError) {
        outParts.clear();
        outFormFields.clear();
        outError.clear();
        if (!process_(outError, true)) {
            return false;
        }

        if (m_state != State::Done) {
            outError = "Incomplete multipart body";
            failAndCleanup_();
            return false;
        }
        if (m_hasCurrentPart) {
            outError = "Incomplete multipart part";
            failAndCleanup_();
            return false;
        }

        outParts = std::move(m_parts);
        outFormFields = std::move(m_formFields);
        return true;
    }

    /**
     * @brief Performs the `cleanupTemporaryFiles` operation.
     */
    void cleanupTemporaryFiles() {
        const SwString currentPath =
            (m_hasCurrentPart && m_currentPart.storedOnDisk) ? m_currentPart.tempFilePath
                                                             : SwString();
        closeCurrentFile_();
        if (!currentPath.isEmpty()) {
            removeFile_(currentPath);
        }

        for (std::size_t i = 0; i < m_parts.size(); ++i) {
            const SwHttpRequest::MultipartPart& part = m_parts[i];
            if (!part.storedOnDisk || part.tempFilePath.isEmpty()) {
                continue;
            }
            removeFile_(part.tempFilePath);
        }
    }

private:
    enum class State {
        NeedInitialBoundary,
        NeedPartHeaders,
        NeedPartData,
        Done,
        Error
    };

    State m_state = State::NeedInitialBoundary;
    SwHttpLimits m_limits;
    SwString m_tempDirectory;
    SwByteArray m_delimiter;      // --boundary
    SwByteArray m_boundaryMarker; // \r\n--boundary
    SwByteRingBuffer m_buffer;

    SwList<SwHttpRequest::MultipartPart> m_parts;
    SwMap<SwString, SwString> m_formFields;

    SwHttpRequest::MultipartPart m_currentPart;
    std::size_t m_currentPartBytes = 0;
    std::size_t m_totalInputBytes = 0;
    bool m_hasCurrentPart = false;
    std::uint64_t m_fileCounter = 0;
    SwFile* m_currentFile = nullptr;

    bool process_(SwString& outError, bool finalInput) {
        while (true) {
            if (m_state == State::Error) {
                outError = "Malformed multipart body";
                return false;
            }
            if (m_state == State::Done) {
                return true;
            }

            bool changed = false;
            if (m_state == State::NeedInitialBoundary) {
                if (!consumeInitialBoundary_(changed, outError, finalInput)) {
                    failAndCleanup_();
                    return false;
                }
            } else if (m_state == State::NeedPartHeaders) {
                if (!consumePartHeaders_(changed, outError)) {
                    failAndCleanup_();
                    return false;
                }
            } else if (m_state == State::NeedPartData) {
                if (!consumePartData_(changed, outError, finalInput)) {
                    failAndCleanup_();
                    return false;
                }
            }

            if (!changed) {
                return true;
            }
        }
    }

    bool consumeInitialBoundary_(bool& changed, SwString& outError, bool finalInput) {
        changed = false;

        std::size_t prefixBytes = 0;
        if (m_buffer.size() > 0 && m_buffer[0] == '\r') {
            if (m_buffer.size() < 2) {
                return true;
            }
            if (m_buffer[1] != '\n') {
                outError = "Malformed multipart boundary";
                return false;
            }
            prefixBytes = 2;
        }

        const std::size_t availableDelimiterBytes =
            m_buffer.size() > prefixBytes
                ? (std::min)(m_delimiter.size(), m_buffer.size() - prefixBytes)
                : 0;
        for (std::size_t i = 0; i < availableDelimiterBytes; ++i) {
            if (m_buffer[prefixBytes + i] != m_delimiter[i]) {
                outError = "Malformed multipart boundary";
                return false;
            }
        }
        if (m_buffer.size() < prefixBytes + m_delimiter.size() + 2) {
            return true;
        }

        const std::size_t suffix = prefixBytes + m_delimiter.size();
        if (m_buffer[suffix] == '-' && m_buffer[suffix + 1] == '-') {
            const std::size_t closeEnd = suffix + 2;
            if (m_buffer.size() == closeEnd) {
                if (!finalInput) {
                    return true;
                }
            } else if (m_buffer.size() == closeEnd + 1) {
                if (!finalInput && m_buffer[closeEnd] == '\r') {
                    return true;
                }
                outError = "Malformed multipart closing boundary";
                return false;
            } else if (m_buffer[closeEnd] != '\r' || m_buffer[closeEnd + 1] != '\n') {
                outError = "Malformed multipart closing boundary";
                return false;
            }

            std::size_t consume = closeEnd;
            if (m_buffer.size() >= closeEnd + 2 && m_buffer[closeEnd] == '\r' &&
                m_buffer[closeEnd + 1] == '\n') {
                consume += 2;
            }
            m_buffer.consume(consume);
            m_state = State::Done;
            changed = true;
            return true;
        }

        if (m_buffer[suffix] != '\r' || m_buffer[suffix + 1] != '\n') {
            outError = "Malformed multipart boundary delimiter";
            return false;
        }

        m_buffer.consume(suffix + 2);
        m_state = State::NeedPartHeaders;
        changed = true;
        return true;
    }

    bool consumePartHeaders_(bool& changed, SwString& outError) {
        changed = false;

        const int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (m_buffer.size() > m_limits.maxMultipartPartHeadersBytes + 4) {
                outError = "Multipart part headers too large";
                return false;
            }
            return true;
        }

        SwMap<SwString, SwString> partHeaders;
        if (!swHttpParseMultipartHeaders_(m_buffer.left(headerEnd), m_limits, partHeaders, outError)) {
            return false;
        }
        m_buffer.consume(static_cast<std::size_t>(headerEnd + 4));

        if (!partHeaders.contains("content-disposition")) {
            outError = "Missing Content-Disposition in multipart part";
            return false;
        }

        SwString dispositionType;
        SwMap<SwString, SwString> dispositionParams;
        swHttpParseHeaderParams_(partHeaders["content-disposition"], dispositionType, dispositionParams);
        if (dispositionType != "form-data") {
            outError = "Unsupported multipart Content-Disposition";
            return false;
        }

        m_currentPart = SwHttpRequest::MultipartPart();
        m_currentPart.headers = partHeaders;
        if (partHeaders.contains("content-type")) {
            m_currentPart.contentType = partHeaders["content-type"];
        }
        if (dispositionParams.contains("name")) {
            m_currentPart.name = dispositionParams["name"];
        }
        if (dispositionParams.contains("filename")) {
            m_currentPart.fileName = dispositionParams["filename"];
            m_currentPart.isFile = !m_currentPart.fileName.isEmpty();
        }
        m_currentPartBytes = 0;
        m_hasCurrentPart = true;

        if (m_currentPart.isFile && m_limits.enableMultipartFileStreaming) {
            m_currentPart.storedOnDisk = true;
            for (int attempt = 0; attempt < 16 && !m_currentFile; ++attempt) {
                m_currentPart.tempFilePath = makeTempFilePath_();
                SwFile* candidate = new SwFile(m_currentPart.tempFilePath);
                if (candidate->openBinaryExclusive()) {
                    m_currentFile = candidate;
                } else {
                    delete candidate;
                }
            }
            if (!m_currentFile) {
                outError = "Unable to open multipart temp file";
                return false;
            }
        }

        m_state = State::NeedPartData;
        changed = true;
        return true;
    }

    bool consumePartData_(bool& changed, SwString& outError, bool finalInput) {
        changed = false;
        if (!m_hasCurrentPart) {
            outError = "Multipart state mismatch";
            return false;
        }

        while (true) {
            const int markerPos = m_buffer.indexOf(m_boundaryMarker);
            if (markerPos < 0) {
                const std::size_t keep = m_boundaryMarker.isEmpty()
                                             ? 0
                                             : m_boundaryMarker.size() - 1;
                if (m_buffer.size() > keep) {
                    if (!appendAndConsumeCurrentData_(m_buffer.size() - keep, outError)) {
                        return false;
                    }
                    changed = true;
                }
                return true;
            }

            if (markerPos > 0) {
                if (!appendAndConsumeCurrentData_(static_cast<std::size_t>(markerPos), outError)) {
                    return false;
                }
                changed = true;
            }

            const std::size_t markerEnd = m_boundaryMarker.size();
            if (m_buffer.size() < markerEnd + 2) {
                return true;
            }

            bool closingBoundary = false;
            std::size_t delimiterBytes = markerEnd + 2;
            if (m_buffer[markerEnd] == '\r' && m_buffer[markerEnd + 1] == '\n') {
                closingBoundary = false;
            } else if (m_buffer[markerEnd] == '-' && m_buffer[markerEnd + 1] == '-') {
                closingBoundary = true;
                const std::size_t closeEnd = markerEnd + 2;
                if (m_buffer.size() == closeEnd) {
                    if (!finalInput) {
                        return true;
                    }
                } else if (m_buffer.size() == closeEnd + 1) {
                    if (!finalInput && m_buffer[closeEnd] == '\r') {
                        return true;
                    }
                    // A boundary-looking prefix followed by payload is data.
                    if (!appendAndConsumeCurrentData_(markerEnd, outError)) {
                        return false;
                    }
                    changed = true;
                    continue;
                } else if (m_buffer[closeEnd] == '\r' && m_buffer[closeEnd + 1] == '\n') {
                    delimiterBytes += 2;
                } else {
                    if (!appendAndConsumeCurrentData_(markerEnd, outError)) {
                        return false;
                    }
                    changed = true;
                    continue;
                }
            } else {
                // Not a delimiter: retain the complete false marker as data and
                // continue searching without discarding its suffix bytes.
                if (!appendAndConsumeCurrentData_(markerEnd, outError)) {
                    return false;
                }
                changed = true;
                continue;
            }

            m_buffer.consume(delimiterBytes);

            if (!finalizeCurrentPart_(outError)) {
                return false;
            }

            if (closingBoundary) {
                m_state = State::Done;
            } else {
                m_state = State::NeedPartHeaders;
            }
            changed = true;
            return true;
        }
    }

    bool appendCurrentData_(const char* data, std::size_t len, SwString& outError) {
        if (len == 0) {
            return true;
        }
        if (!m_hasCurrentPart) {
            outError = "Multipart state mismatch";
            return false;
        }

        if (m_currentPartBytes > (std::numeric_limits<std::size_t>::max)() - len) {
            outError = "Multipart part too large";
            return false;
        }
        m_currentPartBytes += len;
        m_currentPart.sizeBytes = m_currentPartBytes;

        if (m_currentPart.isFile && m_limits.enableMultipartFileStreaming) {
            if (!m_currentFile) {
                outError = "Multipart file stream not open";
                return false;
            }
            if (!m_currentFile->writeBuffered(data, len)) {
                outError = "Unable to write multipart file data";
                return false;
            }
            return true;
        }

        if (!m_currentPart.isFile &&
            (m_currentPart.data.size() > m_limits.maxMultipartFieldBytes ||
             len > m_limits.maxMultipartFieldBytes - m_currentPart.data.size())) {
            outError = "Multipart form field too large";
            return false;
        }

        m_currentPart.data.append(data, len);
        return true;
    }

    bool appendAndConsumeCurrentData_(std::size_t len, SwString& outError) {
        std::size_t remaining = len;
        while (remaining > 0) {
            const std::size_t chunk = (std::min)(remaining, m_buffer.contiguousSize());
            const char* data = m_buffer.contiguousData();
            if (!data || chunk == 0) {
                outError = "Multipart buffer state mismatch";
                return false;
            }
            if (!appendCurrentData_(data, chunk, outError)) {
                return false;
            }
            m_buffer.consume(chunk);
            remaining -= chunk;
        }
        return true;
    }

    bool finalizeCurrentPart_(SwString& outError) {
        if (!m_hasCurrentPart) {
            outError = "Multipart state mismatch";
            return false;
        }

        closeCurrentFile_();

        if (!m_currentPart.isFile && !m_currentPart.name.isEmpty()) {
            m_formFields[m_currentPart.name] =
                SwString(m_currentPart.data.constData(), m_currentPart.data.size());
        }

        if (m_parts.size() >= m_limits.maxMultipartParts) {
            outError = "Too many multipart parts";
            return false;
        }

        m_parts.append(std::move(m_currentPart));
        m_currentPart = SwHttpRequest::MultipartPart();
        m_currentPartBytes = 0;
        m_hasCurrentPart = false;
        return true;
    }

    void closeCurrentFile_() {
        if (m_currentFile) {
            m_currentFile->close();
            delete m_currentFile;
            m_currentFile = nullptr;
        }
    }

    void failAndCleanup_() {
        m_state = State::Error;
        cleanupTemporaryFiles();
    }

    SwString makeTempFilePath_() {
        SwString directory = m_tempDirectory;
        directory.replace("\\", "/");
        if (!directory.endsWith("/")) {
            directory += "/";
        }
        static std::atomic<std::uint64_t> sequence(1);
        const std::uint64_t clockTag = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const std::uint64_t uniqueCounter = sequence.fetch_add(1, std::memory_order_relaxed);
        std::uint64_t randomTag = clockTag ^ uniqueCounter ^
                                  static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
        try {
            std::random_device entropy;
            const std::uint64_t randomHigh = static_cast<std::uint64_t>(entropy()) << 32;
            const std::uint64_t randomLow = static_cast<std::uint64_t>(entropy());
            randomTag ^= randomHigh ^ randomLow;
        } catch (...) {
            // O_EXCL remains the collision authority if the platform entropy source is absent.
        }
        SwString fileName = "multipart_"
                            + SwString::number(static_cast<long long>(randomTag))
                            + "_"
                            + SwString::number(static_cast<long long>(clockTag))
                            + "_"
                            + SwString::number(static_cast<long long>(uniqueCounter))
                            + "_"
                            + SwString::number(static_cast<long long>(m_fileCounter++))
                            + ".part";
        return swDirPlatform().absolutePath(directory + fileName);
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
};

inline bool swHttpParseMultipartFormData(const SwByteArray& body,
                                         const SwString& boundary,
                                         const SwHttpLimits& limits,
                                         SwList<SwHttpRequest::MultipartPart>& outParts,
                                         SwString& outError) {
    outParts.clear();
    outError.clear();

    SwHttpLimits memoryLimits = limits;
    memoryLimits.enableMultipartFileStreaming = false;

    SwHttpMultipartStreamParser parser;
    if (!parser.begin(boundary, memoryLimits, SwString(), outError)) {
        return false;
    }
    if (!parser.feed(body, outError)) {
        return false;
    }
    SwMap<SwString, SwString> ignoredFormFields;
    return parser.finish(outParts, ignoredFormFields, outError);
}

inline bool swHttpParseMultipartRequest(SwHttpRequest& request,
                                        const SwHttpLimits& limits,
                                        SwString& outError) {
    outError.clear();

    // Request may already be parsed in streaming mode directly by SwHttpParser.
    if (request.isMultipartFormData && (request.body.isEmpty() || !request.multipartParts.isEmpty())) {
        return true;
    }

    request.isMultipartFormData = false;
    request.multipartParts.clear();
    request.formFields.clear();

    if (!request.headers.contains("content-type")) {
        return true;
    }

    SwString contentTypeLower = request.headers["content-type"].toLower();
    if (!contentTypeLower.startsWith("multipart/form-data")) {
        return true;
    }

    SwString boundary;
    if (!swHttpExtractMultipartBoundary(request.headers["content-type"], boundary)) {
        outError = "Missing multipart boundary";
        return false;
    }

    if (!swHttpParseMultipartFormData(request.body, boundary, limits, request.multipartParts, outError)) {
        return false;
    }

    request.isMultipartFormData = true;
    for (std::size_t i = 0; i < request.multipartParts.size(); ++i) {
        const SwHttpRequest::MultipartPart& part = request.multipartParts[i];
        if (part.isFile) {
            continue;
        }
        if (part.name.isEmpty()) {
            continue;
        }
        request.formFields[part.name] = SwString(part.data.toStdString());
    }

    return true;
}
