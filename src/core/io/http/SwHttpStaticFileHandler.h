#pragma once

/**
 * @file src/core/io/http/SwHttpStaticFileHandler.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpStaticFileHandler in the CoreSw HTTP
 * server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP static file handler interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpStaticFileHandler.
 *
 * Static-file interfaces in this area are expected to cover path normalization, safe file
 * selection, large-file streaming, and HTTP features such as content ranges.
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

class SwHttpStaticFileHandler {
public:
    /**
     * @brief Constructs a `SwHttpStaticFileHandler` instance.
     * @param prefix Prefix used by the operation.
     * @param rootDir Root directory used by the operation.
     * @param options Option set controlling the operation.
     * @param options Option set controlling the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwHttpStaticFileHandler(const SwString& prefix,
                            const SwString& rootDir,
                            const SwHttpStaticOptions& options = SwHttpStaticOptions())
        : m_prefix(normalizePrefix_(prefix)),
          m_rootDir(rootDir),
          m_options(options) {
    }

    /**
     * @brief Returns whether the object reports handle.
     * @param request Request instance associated with the operation.
     * @return `true` when the object reports handle; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool canHandle(const SwHttpRequest& request) const {
        SwString method = request.method.toUpper();
        if (method != "GET" && method != "HEAD") {
            return false;
        }
        SwString path = swHttpNormalizePath(request.path);
        if (m_prefix == "/") {
            return true;
        }
        if (!path.startsWith(m_prefix)) {
            return false;
        }
        if (path.size() == m_prefix.size()) {
            return true;
        }
        return path[m_prefix.size()] == '/';
    }

    /**
     * @brief Performs the `tryHandle` operation.
     * @param request Request instance associated with the operation.
     * @param response Response instance associated with the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool tryHandle(const SwHttpRequest& request, SwHttpResponse& response) const {
        if (!canHandle(request)) {
            return false;
        }

        SwString relativePath = relativePathForRequest_(request.path);
        if (!sanitizeRelativePath_(relativePath)) {
            response = swHttpTextResponse(403, "Forbidden");
            response.closeConnection = !request.keepAlive;
            return true;
        }

        SwString absoluteRoot = swDirPlatform().absolutePath(m_rootDir);
        SwString absoluteTarget = buildAbsoluteTarget_(absoluteRoot, relativePath);
        if (!isPathInsideRoot_(absoluteRoot, absoluteTarget)) {
            response = swHttpTextResponse(403, "Forbidden");
            response.closeConnection = !request.keepAlive;
            return true;
        }

        if (!swFilePlatform().isFile(absoluteTarget)) {
            response = swHttpTextResponse(404, "Not Found");
            response.closeConnection = !request.keepAlive;
            return true;
        }

        std::size_t totalSize = swFileInfoPlatform().size(absoluteTarget.toStdString());
        std::size_t offset = 0;
        std::size_t length = totalSize;

        bool partial = false;
        if (m_options.enableRange && request.headers.contains("range")) {
            SwString contentRange;
            if (!parseRange_(request.headers["range"], totalSize, offset, length, contentRange)) {
                response.status = 416;
                response.reason = swHttpStatusReason(416);
                response.headers["content-range"] = "bytes */" + SwString::number(static_cast<long long>(totalSize));
                response.headers["content-length"] = "0";
                response.closeConnection = !request.keepAlive;
                response.headOnly = (request.method.toUpper() == "HEAD");
                return true;
            }
            partial = true;
            response.headers["content-range"] = contentRange;
        }

        response.status = partial ? 206 : 200;
        response.reason = swHttpStatusReason(response.status);
        response.headers["accept-ranges"] = "bytes";
        response.headers["content-type"] = swHttpGuessMimeType(absoluteTarget);
        response.headers["content-length"] = SwString::number(static_cast<long long>(length));
        if (!m_options.cacheControl.isEmpty()) {
            response.headers["cache-control"] = m_options.cacheControl;
        }

        response.hasFile = true;
        response.filePath = absoluteTarget;
        response.fileOffset = offset;
        response.fileLength = length;
        response.fileTotalSize = totalSize;
        response.streamChunkBytes = m_options.ioChunkBytes;
        response.headOnly = (request.method.toUpper() == "HEAD");
        response.closeConnection = !request.keepAlive;
        return true;
    }

private:
    SwString m_prefix;
    SwString m_rootDir;
    SwHttpStaticOptions m_options;

    static SwString normalizePrefix_(const SwString& prefix) {
        SwString normalized = swHttpNormalizePath(prefix);
        if (normalized.size() > 1 && normalized.endsWith("/")) {
            normalized.chop(1);
        }
        return normalized;
    }

    SwString relativePathForRequest_(const SwString& requestPath) const {
        SwString path = swHttpNormalizePath(requestPath);
        SwString relative = path;
        if (m_prefix != "/" && path.startsWith(m_prefix)) {
            relative = path.mid(static_cast<int>(m_prefix.size()));
            if (relative.isEmpty()) {
                relative = "/";
            }
        }
        if (relative.startsWith("/")) {
            relative = relative.mid(1);
        }

        if (relative.isEmpty()) {
            for (std::size_t i = 0; i < m_options.indexFiles.size(); ++i) {
                if (!m_options.indexFiles[i].isEmpty()) {
                    return m_options.indexFiles[i];
                }
            }
            return SwString("index.html");
        }
        return relative;
    }

    static bool sanitizeRelativePath_(const SwString& relativePath) {
        if (relativePath.isEmpty()) {
            return true;
        }

        // Detect real embedded NUL bytes. Using contains("\0") is invalid here
        // because "\0" is seen as an empty C-string by string-based APIs.
        for (std::size_t i = 0; i < relativePath.size(); ++i) {
            if (relativePath[i] == '\0') {
                return false;
            }
        }

        SwString path = relativePath;
        path.replace("\\", "/");
        while (path.contains("//")) {
            path.replace("//", "/");
        }
        SwList<SwString> segments = path.split('/');
        for (std::size_t i = 0; i < segments.size(); ++i) {
            SwString seg = segments[i].trimmed();
            if (seg == "." || seg == "..") {
                return false;
            }
        }
        return true;
    }

    static SwString buildAbsoluteTarget_(const SwString& absoluteRoot, const SwString& relativePath) {
        SwString root = absoluteRoot;
        SwString rel = relativePath;
        root.replace("\\", "/");
        rel.replace("\\", "/");
        while (root.endsWith("/") && root.size() > 1) {
            root.chop(1);
        }
        while (rel.startsWith("/")) {
            rel = rel.mid(1);
        }
        if (rel.isEmpty()) {
            return root;
        }
        // Keep a plain absolute path here. Long-path normalization (\\?\ prefix)
        // can break downstream file checks on some Win32 code paths.
        return root + "/" + rel;
    }

    static bool isPathInsideRoot_(const SwString& absoluteRoot, const SwString& absoluteTarget) {
        SwString root = swDirPlatform().normalizePath(absoluteRoot);
        SwString target = swDirPlatform().normalizePath(absoluteTarget);
        root.replace("\\", "/");
        target.replace("\\", "/");
#if defined(_WIN32)
        root = root.toLower();
        target = target.toLower();
#endif
        if (!target.startsWith(root)) {
            return false;
        }
        if (target.size() == root.size()) {
            return true;
        }
        char sep = target[root.size()];
        return sep == '/' || sep == '\\';
    }

    static bool parseRange_(const SwString& rangeHeader,
                            std::size_t totalSize,
                            std::size_t& outOffset,
                            std::size_t& outLength,
                            SwString& outContentRange) {
        SwString value = rangeHeader.trimmed().toLower();
        if (!value.startsWith("bytes=")) {
            return false;
        }
        SwString spec = value.mid(6).trimmed();
        int comma = spec.indexOf(",");
        if (comma >= 0) {
            // Multi-range not supported.
            return false;
        }

        int dash = spec.indexOf("-");
        if (dash < 0) {
            return false;
        }

        SwString startText = spec.left(dash).trimmed();
        SwString endText = spec.mid(dash + 1).trimmed();

        if (totalSize == 0) {
            return false;
        }

        std::size_t start = 0;
        std::size_t end = totalSize - 1;
        if (startText.isEmpty()) {
            bool ok = false;
            long long suffix = endText.toLongLong(&ok);
            if (!ok || suffix <= 0) {
                return false;
            }
            std::size_t suffixSize = static_cast<std::size_t>(suffix);
            if (suffixSize > totalSize) {
                suffixSize = totalSize;
            }
            start = totalSize - suffixSize;
            end = totalSize - 1;
        } else {
            bool okStart = false;
            long long startInt = startText.toLongLong(&okStart);
            if (!okStart || startInt < 0) {
                return false;
            }
            start = static_cast<std::size_t>(startInt);
            if (start >= totalSize) {
                return false;
            }

            if (!endText.isEmpty()) {
                bool okEnd = false;
                long long endInt = endText.toLongLong(&okEnd);
                if (!okEnd || endInt < 0) {
                    return false;
                }
                end = static_cast<std::size_t>(endInt);
                if (end < start) {
                    return false;
                }
                if (end >= totalSize) {
                    end = totalSize - 1;
                }
            }
        }

        outOffset = start;
        outLength = end - start + 1;
        outContentRange = "bytes " + SwString::number(static_cast<long long>(start)) +
                          "-" + SwString::number(static_cast<long long>(end)) +
                          "/" + SwString::number(static_cast<long long>(totalSize));
        return true;
    }
};
