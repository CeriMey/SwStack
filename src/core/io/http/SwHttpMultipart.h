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
#include "SwDir.h"
#include "SwFile.h"
#include "platform/SwPlatformSelector.h"

#include <cstdint>
#include <cstdio>

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
    if (boundary.isEmpty()) {
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

        SwString key = line.left(colon).trimmed().toLower();
        SwString value = line.mid(colon + 1).trimmed();
        if (key.isEmpty()) {
            outError = "Malformed multipart part header";
            return false;
        }
        outHeaders[key] = value;
    }
    return true;
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
        cleanupTemporaryFiles();
        reset();
    }

    /**
     * @brief Resets the object to a baseline state.
     */
    void reset() {
        closeCurrentFile_();
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
        m_fileCounter = 0;
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

        if (boundary.isEmpty()) {
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
        outError.clear();
        if (!bytes.isEmpty()) {
            m_buffer.append(bytes);
        }
        return process_(outError);
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
        outError.clear();
        if (!process_(outError)) {
            return false;
        }

        if (m_state != State::Done) {
            outError = "Incomplete multipart body";
            return false;
        }
        if (m_hasCurrentPart) {
            outError = "Incomplete multipart part";
            return false;
        }

        outParts = m_parts;
        outFormFields = m_formFields;
        return true;
    }

    /**
     * @brief Performs the `cleanupTemporaryFiles` operation.
     */
    void cleanupTemporaryFiles() {
        if (m_hasCurrentPart && m_currentPart.storedOnDisk && !m_currentPart.tempFilePath.isEmpty()) {
            removeFile_(m_currentPart.tempFilePath);
        }
        closeCurrentFile_();

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
    SwByteArray m_buffer;

    SwList<SwHttpRequest::MultipartPart> m_parts;
    SwMap<SwString, SwString> m_formFields;

    SwHttpRequest::MultipartPart m_currentPart;
    std::size_t m_currentPartBytes = 0;
    bool m_hasCurrentPart = false;
    int m_fileCounter = 0;
    SwFile* m_currentFile = nullptr;

    bool process_(SwString& outError) {
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
                if (!consumeInitialBoundary_(changed, outError)) {
                    m_state = State::Error;
                    return false;
                }
            } else if (m_state == State::NeedPartHeaders) {
                if (!consumePartHeaders_(changed, outError)) {
                    m_state = State::Error;
                    return false;
                }
            } else if (m_state == State::NeedPartData) {
                if (!consumePartData_(changed, outError)) {
                    m_state = State::Error;
                    return false;
                }
            }

            if (!changed) {
                return true;
            }
        }
    }

    bool consumeInitialBoundary_(bool& changed, SwString& outError) {
        changed = false;

        if (m_buffer.size() >= 2 && m_buffer[0] == '\r' && m_buffer[1] == '\n') {
            m_buffer.remove(0, 2);
            changed = true;
        }

        if (m_buffer.size() < m_delimiter.size()) {
            return true;
        }
        if (!m_buffer.startsWith(m_delimiter)) {
            outError = "Malformed multipart boundary";
            return false;
        }

        m_buffer.remove(0, static_cast<int>(m_delimiter.size()));
        changed = true;

        if (m_buffer.size() < 2) {
            return true;
        }

        if (m_buffer[0] == '-' && m_buffer[1] == '-') {
            m_buffer.remove(0, 2);
            if (m_buffer.size() >= 2 && m_buffer[0] == '\r' && m_buffer[1] == '\n') {
                m_buffer.remove(0, 2);
            }
            m_state = State::Done;
            return true;
        }

        if (m_buffer[0] != '\r' || m_buffer[1] != '\n') {
            outError = "Malformed multipart boundary delimiter";
            return false;
        }

        m_buffer.remove(0, 2);
        m_state = State::NeedPartHeaders;
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
        m_buffer.remove(0, headerEnd + 4);

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
            m_currentPart.tempFilePath = makeTempFilePath_();
            m_currentFile = new SwFile(m_currentPart.tempFilePath);
            if (!m_currentFile->openBinary(SwFile::Write)) {
                closeCurrentFile_();
                outError = "Unable to open multipart temp file";
                return false;
            }
        }

        m_state = State::NeedPartData;
        changed = true;
        return true;
    }

    bool consumePartData_(bool& changed, SwString& outError) {
        changed = false;
        if (!m_hasCurrentPart) {
            outError = "Multipart state mismatch";
            return false;
        }

        const int markerPos = m_buffer.indexOf(m_boundaryMarker);
        if (markerPos < 0) {
            std::size_t keep = 0;
            if (m_boundaryMarker.size() > 0) {
                keep = m_boundaryMarker.size() - 1;
            }
            if (m_buffer.size() > keep) {
                const std::size_t flushBytes = m_buffer.size() - keep;
                if (!appendCurrentData_(m_buffer.constData(), flushBytes, outError)) {
                    return false;
                }
                m_buffer.remove(0, static_cast<int>(flushBytes));
                changed = true;
            }
            return true;
        }

        if (markerPos > 0) {
            if (!appendCurrentData_(m_buffer.constData(), static_cast<std::size_t>(markerPos), outError)) {
                return false;
            }
            m_buffer.remove(0, markerPos);
            changed = true;
        }

        if (m_buffer.size() < m_boundaryMarker.size() + 2) {
            return true;
        }

        m_buffer.remove(0, 2); // consume CRLF before "--boundary"
        if (!m_buffer.startsWith(m_delimiter)) {
            outError = "Malformed multipart boundary";
            return false;
        }
        m_buffer.remove(0, static_cast<int>(m_delimiter.size()));

        if (m_buffer.size() < 2) {
            return true;
        }

        const bool closingBoundary = (m_buffer[0] == '-' && m_buffer[1] == '-');
        if (closingBoundary) {
            m_buffer.remove(0, 2);
        } else {
            if (m_buffer[0] != '\r' || m_buffer[1] != '\n') {
                outError = "Malformed multipart boundary delimiter";
                return false;
            }
            m_buffer.remove(0, 2);
        }

        if (!finalizeCurrentPart_(outError)) {
            return false;
        }

        if (closingBoundary) {
            if (m_buffer.size() >= 2 && m_buffer[0] == '\r' && m_buffer[1] == '\n') {
                m_buffer.remove(0, 2);
            }
            m_state = State::Done;
        } else {
            m_state = State::NeedPartHeaders;
        }
        changed = true;
        return true;
    }

    bool appendCurrentData_(const char* data, std::size_t len, SwString& outError) {
        if (len == 0) {
            return true;
        }
        if (!m_hasCurrentPart) {
            outError = "Multipart state mismatch";
            return false;
        }

        m_currentPartBytes += len;
        m_currentPart.sizeBytes = m_currentPartBytes;

        if (m_currentPart.isFile && m_limits.enableMultipartFileStreaming) {
            if (!m_currentFile) {
                outError = "Multipart file stream not open";
                return false;
            }
            SwByteArray chunk(data, len);
            if (!m_currentFile->write(chunk)) {
                outError = "Unable to write multipart file data";
                return false;
            }
            return true;
        }

        if (!m_currentPart.isFile && (m_currentPart.data.size() + len > m_limits.maxMultipartFieldBytes)) {
            outError = "Multipart form field too large";
            return false;
        }

        m_currentPart.data.append(data, len);
        return true;
    }

    bool finalizeCurrentPart_(SwString& outError) {
        if (!m_hasCurrentPart) {
            outError = "Multipart state mismatch";
            return false;
        }

        closeCurrentFile_();

        if (!m_currentPart.isFile && !m_currentPart.name.isEmpty()) {
            m_formFields[m_currentPart.name] = SwString(m_currentPart.data.toStdString());
        }

        if (m_parts.size() >= m_limits.maxMultipartParts) {
            outError = "Too many multipart parts";
            return false;
        }

        m_parts.append(m_currentPart);
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

    SwString makeTempFilePath_() {
        SwString directory = m_tempDirectory;
        directory.replace("\\", "/");
        if (!directory.endsWith("/")) {
            directory += "/";
        }
        const std::uintptr_t tag = reinterpret_cast<std::uintptr_t>(this);
        SwString fileName = "multipart_"
                            + SwString::number(static_cast<long long>(tag))
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

    if (boundary.isEmpty()) {
        outError = "Missing multipart boundary";
        return false;
    }

    const SwString delimiterText = "--" + boundary;
    const SwString closeDelimiterText = delimiterText + "--";
    const SwByteArray delimiter(delimiterText.toStdString());
    const SwByteArray closeDelimiter(closeDelimiterText.toStdString());
    const SwByteArray nextMarker("\r\n" + delimiterText.toStdString());

    int pos = 0;
    if (body.size() >= 2 && body[0] == '\r' && body[1] == '\n') {
        pos = 2;
    }

    while (true) {
        if (body.size() < static_cast<std::size_t>(pos) + delimiter.size()) {
            outError = "Malformed multipart body";
            return false;
        }

        if (body.mid(pos, static_cast<int>(closeDelimiter.size())) == closeDelimiter) {
            pos += static_cast<int>(closeDelimiter.size());
            if (body.size() >= static_cast<std::size_t>(pos) + 2 &&
                body[pos] == '\r' && body[pos + 1] == '\n') {
                pos += 2;
            }
            return true;
        }

        if (body.mid(pos, static_cast<int>(delimiter.size())) != delimiter) {
            outError = "Malformed multipart boundary";
            return false;
        }
        pos += static_cast<int>(delimiter.size());

        if (body.size() < static_cast<std::size_t>(pos) + 2 ||
            body[pos] != '\r' || body[pos + 1] != '\n') {
            outError = "Malformed multipart boundary delimiter";
            return false;
        }
        pos += 2;

        const int headerEnd = body.indexOf("\r\n\r\n", pos);
        if (headerEnd < 0) {
            outError = "Malformed multipart headers";
            return false;
        }

        const int headersLen = headerEnd - pos;
        if (headersLen < 0) {
            outError = "Malformed multipart headers";
            return false;
        }

        SwMap<SwString, SwString> partHeaders;
        if (!swHttpParseMultipartHeaders_(body.mid(pos, headersLen), limits, partHeaders, outError)) {
            return false;
        }
        pos = headerEnd + 4;

        const int markerPos = body.indexOf(nextMarker, pos);
        if (markerPos < 0) {
            outError = "Multipart closing boundary not found";
            return false;
        }

        const int partDataLen = markerPos - pos;
        if (partDataLen < 0) {
            outError = "Malformed multipart body";
            return false;
        }

        SwHttpRequest::MultipartPart part;
        part.headers = partHeaders;
        part.data = body.mid(pos, partDataLen);
        part.sizeBytes = part.data.size();
        if (partHeaders.contains("content-type")) {
            part.contentType = partHeaders["content-type"];
        }

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

        if (dispositionParams.contains("name")) {
            part.name = dispositionParams["name"];
        }
        if (dispositionParams.contains("filename")) {
            part.fileName = dispositionParams["filename"];
            part.isFile = !part.fileName.isEmpty();
        }

        outParts.append(part);
        if (outParts.size() > limits.maxMultipartParts) {
            outError = "Too many multipart parts";
            return false;
        }

        pos = markerPos + 2; // skip CRLF and position on next "--boundary"
    }
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
