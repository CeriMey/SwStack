#pragma once

/**
 * @file src/core/io/http/SwHttpContext.h
 * @ingroup core_http
 * @brief Declares the public interface exposed by SwHttpContext in the CoreSw HTTP server layer.
 *
 * This header belongs to the CoreSw HTTP server layer. It exposes the request and response model,
 * parser state machines, routing helpers, per-connection sessions, and static-file helpers used
 * by the non-blocking HTTP stack.
 *
 * Within that layer, this file focuses on the HTTP context interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwHttpContext.
 *
 * Context-level interfaces here collect request-scoped state and helper access so middleware,
 * handlers, and response code can share a consistent view of the active HTTP exchange.
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
#include "SwJsonDocument.h"
#include "SwJsonValue.h"
#include "platform/SwPlatformSelector.h"

class SwHttpContext {
public:
    /**
     * @brief Constructs a `SwHttpContext` instance.
     * @param request Request instance associated with the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwHttpContext(const SwHttpRequest& request)
        : m_request(request) {
        m_response.status = 200;
        m_response.reason = swHttpStatusReason(200);
    }

    /**
     * @brief Returns the current request.
     * @return The current request.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwHttpRequest& request() const {
        return m_request;
    }

    /**
     * @brief Returns the current response.
     * @return The current response.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpResponse& response() {
        return m_response;
    }

    /**
     * @brief Returns the current response.
     * @return The current response.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwHttpResponse& response() const {
        return m_response;
    }

    /**
     * @brief Returns the current handled.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool handled() const {
        return m_handled;
    }

    /**
     * @brief Sets the handled.
     * @param handled Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHandled(bool handled = true) {
        m_handled = handled;
    }

    /**
     * @brief Returns the current method.
     * @return The current method.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString method() const {
        return m_request.method;
    }

    /**
     * @brief Returns the current path.
     * @return The current path.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString path() const {
        return m_request.path;
    }

    /**
     * @brief Performs the `queryValue` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested query Value.
     */
    SwString queryValue(const SwString& key, const SwString& defaultValue = SwString()) const {
        return m_request.queryParams.value(key, defaultValue);
    }

    /**
     * @brief Performs the `pathValue` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested path Value.
     */
    SwString pathValue(const SwString& key, const SwString& defaultValue = SwString()) const {
        return m_request.pathParams.value(key, defaultValue);
    }

    /**
     * @brief Performs the `headerValue` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested header Value.
     */
    SwString headerValue(const SwString& key, const SwString& defaultValue = SwString()) const {
        return m_request.headers.value(key.toLower(), defaultValue);
    }

    /**
     * @brief Performs the `formValue` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested form Value.
     */
    SwString formValue(const SwString& key, const SwString& defaultValue = SwString()) const {
        return m_request.formFields.value(key, defaultValue);
    }

    /**
     * @brief Returns the current body Text.
     * @return The current body Text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString bodyText() const {
        return SwString(m_request.body.toStdString());
    }

    /**
     * @brief Sets the local.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLocal(const SwString& key, const SwString& value) {
        if (key.isEmpty()) {
            return;
        }
        m_locals[key] = value;
    }

    /**
     * @brief Returns whether the object reports local.
     * @param key Value passed to the method.
     * @return `true` when the object reports local; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool hasLocal(const SwString& key) const {
        return m_locals.contains(key);
    }

    /**
     * @brief Performs the `localValue` operation.
     * @param key Value passed to the method.
     * @param defaultValue Value passed to the method.
     * @return The requested local Value.
     */
    SwString localValue(const SwString& key, const SwString& defaultValue = SwString()) const {
        return m_locals.value(key, defaultValue);
    }

    /**
     * @brief Performs the `parseJsonBody` operation.
     * @param outDocument Output value filled by the method.
     * @param outError Output value filled by the method.
     * @return `true` on success; otherwise `false`.
     */
    bool parseJsonBody(SwJsonDocument& outDocument, SwString& outError) const {
        outError.clear();
        outDocument = SwJsonDocument::fromJson(m_request.body.toStdString(), outError);
        return outError.isEmpty();
    }

    /**
     * @brief Returns the current parse Url Encoded Body.
     * @return The current parse Url Encoded Body.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMap<SwString, SwString> parseUrlEncodedBody() const {
        SwMap<SwString, SwString> fields;
        swHttpParseQueryString(SwString(m_request.body.toStdString()), fields);
        return fields;
    }

    /**
     * @brief Sets the status.
     * @param statusCode Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStatus(int statusCode) {
        m_response.status = statusCode;
        m_response.reason = swHttpStatusReason(statusCode);
        m_handled = true;
    }

    /**
     * @brief Sets the header.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHeader(const SwString& key, const SwString& value) {
        if (key.isEmpty()) {
            return;
        }
        m_response.headers[key.toLower()] = value;
        m_handled = true;
    }

    /**
     * @brief Performs the `noContent` operation.
     * @param statusCode Value passed to the method.
     */
    void noContent(int statusCode = 204) {
        m_response.status = statusCode;
        m_response.reason = swHttpStatusReason(statusCode);
        m_response.body.clear();
        m_response.headers["content-length"] = "0";
        m_response.useChunkedTransfer = false;
        m_response.chunkedParts.clear();
        m_response.hasFile = false;
        m_handled = true;
    }

    /**
     * @brief Performs the `send` operation.
     * @param body Value passed to the method.
     * @param contentType Value passed to the method.
     * @param statusCode Value passed to the method.
     */
    void send(const SwByteArray& body,
              const SwString& contentType = SwString("application/octet-stream"),
              int statusCode = 200) {
        m_response.status = statusCode;
        m_response.reason = swHttpStatusReason(statusCode);
        if (!contentType.isEmpty()) {
            m_response.headers["content-type"] = contentType;
        }
        m_response.body = body;
        m_response.hasFile = false;
        m_response.useChunkedTransfer = false;
        m_response.chunkedParts.clear();
        m_handled = true;
    }

    /**
     * @brief Performs the `send` operation.
     * @param body Value passed to the method.
     * @param contentType Value passed to the method.
     * @param statusCode Value passed to the method.
     */
    void send(const SwString& body,
              const SwString& contentType = SwString("text/plain; charset=utf-8"),
              int statusCode = 200) {
        send(SwByteArray(body.toStdString()), contentType, statusCode);
    }

    /**
     * @brief Performs the `text` operation.
     * @param body Value passed to the method.
     * @param statusCode Value passed to the method.
     */
    void text(const SwString& body, int statusCode = 200) {
        send(body, "text/plain; charset=utf-8", statusCode);
    }

    /**
     * @brief Performs the `html` operation.
     * @param body Value passed to the method.
     * @param statusCode Value passed to the method.
     */
    void html(const SwString& body, int statusCode = 200) {
        send(body, "text/html; charset=utf-8", statusCode);
    }

    /**
     * @brief Performs the `json` operation.
     * @param document Value passed to the method.
     * @param statusCode Value passed to the method.
     * @param format Value passed to the method.
     */
    void json(const SwJsonDocument& document,
              int statusCode = 200,
              SwJsonDocument::JsonFormat format = SwJsonDocument::JsonFormat::Compact) {
        send(document.toJson(format), "application/json; charset=utf-8", statusCode);
    }

    /**
     * @brief Performs the `json` operation.
     * @param value Value passed to the method.
     * @param statusCode Value passed to the method.
     */
    void json(const SwJsonValue& value, int statusCode = 200) {
        if (value.isObject()) {
            json(SwJsonDocument(value.toObject()), statusCode, SwJsonDocument::JsonFormat::Compact);
            return;
        }
        if (value.isArray()) {
            json(SwJsonDocument(value.toArray()), statusCode, SwJsonDocument::JsonFormat::Compact);
            return;
        }
        send(SwString(value.toJsonString()), "application/json; charset=utf-8", statusCode);
    }

    /**
     * @brief Performs the `sendFile` operation.
     * @param filePath Path of the target file.
     * @param contentType Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool sendFile(const SwString& filePath, const SwString& contentType = SwString()) {
        SwString absolutePath = swDirPlatform().absolutePath(filePath);
        if (!swFilePlatform().isFile(absolutePath)) {
            return false;
        }

        const std::size_t totalSize = swFileInfoPlatform().size(absolutePath.toStdString());
        m_response.status = 200;
        m_response.reason = swHttpStatusReason(200);
        m_response.headers["content-type"] = contentType.isEmpty() ? swHttpGuessMimeType(absolutePath) : contentType;
        m_response.headers["content-length"] = SwString::number(static_cast<long long>(totalSize));
        m_response.hasFile = true;
        m_response.filePath = absolutePath;
        m_response.fileOffset = 0;
        m_response.fileLength = totalSize;
        m_response.fileTotalSize = totalSize;
        m_response.useChunkedTransfer = false;
        m_response.chunkedParts.clear();
        m_response.body.clear();
        m_handled = true;
        return true;
    }

    /**
     * @brief Performs the `redirect` operation.
     * @param location Value passed to the method.
     * @param statusCode Value passed to the method.
     */
    void redirect(const SwString& location, int statusCode = 302) {
        m_response.status = statusCode;
        m_response.reason = swHttpStatusReason(statusCode);
        m_response.headers["location"] = location;
        m_response.body.clear();
        m_response.hasFile = false;
        m_response.useChunkedTransfer = false;
        m_response.chunkedParts.clear();
        m_handled = true;
    }

    /**
     * @brief Closes the connection handled by the object.
     * @param close Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void closeConnection(bool close = true) {
        m_response.closeConnection = close;
    }

    /**
     * @brief Returns the current take Response.
     * @return The current take Response.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwHttpResponse takeResponse() const {
        return m_response;
    }

private:
    const SwHttpRequest& m_request;
    SwHttpResponse m_response;
    SwMap<SwString, SwString> m_locals;
    bool m_handled = false;
};
