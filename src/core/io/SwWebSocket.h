#pragma once

/**
 * @file src/core/io/SwWebSocket.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwWebSocket in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the web socket interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwWebSocket.
 *
 * Socket-oriented declarations here abstract OS-level descriptors and expose the read, write,
 * connection, and readiness semantics that higher layers build upon.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
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
#include "SwSslSocket.h"
#include "SwTcpSocket.h"
#include "SwString.h"
#include "SwByteArray.h"
#include "SwList.h"
#include "SwMap.h"
#include "SwCrypto.h"
#include "SwDebug.h"
#include "SwTimer.h"
#include "third_party/miniz/miniz.h"

#include <cstdint>
#include <chrono>
#include <cstring>
#include <limits>

#if !defined(_WIN32)
  #include <openssl/rand.h>
#endif

static constexpr const char* kSwLogCategory_SwWebSocket = "sw.core.io.swwebsocket";

/**
 * @brief Lightweight RFC6455 WebSocket client built on top of SwAbstractSocket.
 *
 * Goals:
 * - Header-only and cross-platform (Windows/Linux).
 * - Signal-driven API surface (open/close/sendTextMessage/sendBinaryMessage/ping).
 * - Uses Sw types (SwString/SwByteArray/SwList/SwMap) as much as possible.
 *
 * Notes:
 * - Implements a client (masked frames when sending).
 * - Supports ws:// and wss:// URLs (`SwTcpSocket` for plain TCP, `SwSslSocket` for TLS).
 */
class SwWebSocket : public SwObject {
    SW_OBJECT(SwWebSocket, SwObject)

public:
    enum Role {
        ClientRole,
        ServerRole
    };

    enum CloseCode {
        CloseCodeNormal           = 1000,
        CloseCodeGoingAway        = 1001,
        CloseCodeProtocolError    = 1002,
        CloseCodeUnsupportedData  = 1003,
        CloseCodeNoStatusRcvd     = 1005,
        CloseCodeAbnormalClosure  = 1006,
        CloseCodeInvalidPayload   = 1007,
        CloseCodePolicyViolation  = 1008,
        CloseCodeMessageTooBig    = 1009,
        CloseCodeMandatoryExt     = 1010,
        CloseCodeInternalError    = 1011
    };

    /**
     * @brief Constructs a `SwWebSocket` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwWebSocket(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    /**
     * @brief Constructs a `SwWebSocket` instance.
     * @param role Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param role Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwWebSocket(Role role, SwObject* parent = nullptr)
        : SwObject(parent),
          m_role(role) {
    }

    /**
     * @brief Constructs a `SwWebSocket` instance.
     * @param origin Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param origin Value passed to the method.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwWebSocket(const SwString& origin, SwObject* parent = nullptr)
        : SwObject(parent),
          m_origin(origin) {
    }

    /**
     * @brief Destroys the `SwWebSocket` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwWebSocket() override {
        abort();
    }

    /**
     * @brief Sets the origin.
     * @param origin Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOrigin(const SwString& origin) {
        m_origin = origin;
    }

    void setTrustedCaFile(const SwString& path) {
        m_trustedCaFile = path;
    }

    /**
     * @brief Returns the current origin.
     * @return The current origin.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& origin() const {
        return m_origin;
    }

    /**
     * @brief Returns the current role.
     * @return The current role.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Role role() const {
        return m_role;
    }

    /**
     * @brief Sets the requested Subprotocols.
     * @param subprotocols Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRequestedSubprotocols(const SwList<SwString>& subprotocols) {
        m_requestedSubprotocols = subprotocols;
    }

    /**
     * @brief Returns the current requested Subprotocols.
     * @return The current requested Subprotocols.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwList<SwString>& requestedSubprotocols() const {
        return m_requestedSubprotocols;
    }

    // Server-side subprotocol selection (intersection with client's Sec-WebSocket-Protocol offer).
    /**
     * @brief Sets the supported Subprotocols.
     * @param subprotocols Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSupportedSubprotocols(const SwList<SwString>& subprotocols) {
        m_supportedSubprotocols = subprotocols;
    }

    /**
     * @brief Returns the current supported Subprotocols.
     * @return The current supported Subprotocols.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwList<SwString>& supportedSubprotocols() const {
        return m_supportedSubprotocols;
    }

    /**
     * @brief Returns the current subprotocol.
     * @return The current subprotocol.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& subprotocol() const {
        return m_acceptedSubprotocol;
    }

    /**
     * @brief Sets the raw Header.
     * @param key Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRawHeader(const SwString& key, const SwString& value) {
        m_requestHeaderMap[key] = value;
    }

    /**
     * @brief Sets the max Incoming Message Size.
     * @param bytes Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaxIncomingMessageSize(uint64_t bytes) {
        m_maxIncomingMessageSize = bytes;
    }

    /**
     * @brief Returns the current max Incoming Message Size.
     * @return The current max Incoming Message Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    uint64_t maxIncomingMessageSize() const {
        return m_maxIncomingMessageSize;
    }

    /**
     * @brief Sets the close Timeout Ms.
     * @param ms Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCloseTimeoutMs(int ms) {
        m_closeTimeoutMs = ms;
    }

    /**
     * @brief Returns the current timeout Ms.
     * @return The current timeout Ms.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int closeTimeoutMs() const {
        return m_closeTimeoutMs;
    }

    // permessage-deflate (RFC7692). Enabled by default; negotiated during handshake.
    /**
     * @brief Sets the per Message Deflate Enabled.
     * @param enabled Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPerMessageDeflateEnabled(bool enabled) {
        m_pmd.requested = enabled;
    }

    /**
     * @brief Returns the current per Message Deflate Enabled.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool perMessageDeflateEnabled() const {
        return m_pmd.requested;
    }

    /**
     * @brief Returns whether the object reports per Message Deflate Negotiated.
     * @return `true` when the object reports per Message Deflate Negotiated; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isPerMessageDeflateNegotiated() const {
        return m_pmd.negotiated;
    }

    // Raw value of Sec-WebSocket-Extensions returned by the peer (if any).
    /**
     * @brief Returns the current extensions.
     * @return The current extensions.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString extensions() const {
        return m_acceptedExtensions;
    }

    // Proxy configuration simplified to HTTP CONNECT.
    enum ProxyType {
        NoProxy,
        HttpProxy
    };

    struct ProxySettings {
        ProxyType type = NoProxy;
        SwString host;
        uint16_t port = 0;
        SwString username;
        SwString password;
    };

    /**
     * @brief Sets the proxy.
     * @param proxy Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setProxy(const ProxySettings& proxy) {
        m_proxy = proxy;
    }

    /**
     * @brief Sets the proxy.
     * @param type Value passed to the method.
     * @param host Value passed to the method.
     * @param port Local port used by the operation.
     * @param username Value passed to the method.
     * @param password Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setProxy(ProxyType type,
                  const SwString& host,
                  uint16_t port,
                  const SwString& username = SwString(),
                  const SwString& password = SwString()) {
        m_proxy.type = type;
        m_proxy.host = host;
        m_proxy.port = port;
        m_proxy.username = username;
        m_proxy.password = password;
    }

    /**
     * @brief Returns the current proxy.
     * @return The current proxy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    ProxySettings proxy() const {
        return m_proxy;
    }

    /**
     * @brief Sets the follow Redirects.
     * @param enabled Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFollowRedirects(bool enabled) {
        m_followRedirects = enabled;
    }

    /**
     * @brief Returns the current follow Redirects.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool followRedirects() const {
        return m_followRedirects;
    }

    /**
     * @brief Sets the maximum Redirects.
     * @param maxRedirects Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaximumRedirects(int maxRedirects) {
        m_maxRedirects = maxRedirects;
    }

    /**
     * @brief Returns the current maximum Redirects.
     * @return The current maximum Redirects.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maximumRedirects() const {
        return m_maxRedirects;
    }

    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const {
        return m_handshakeDone && m_socket && m_state == SwAbstractSocket::ConnectedState;
    }

    /**
     * @brief Returns the current state.
     * @return The current state.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwAbstractSocket::SocketState state() const {
        return m_state;
    }

    /**
     * @brief Returns the current request Url.
     * @return The current request Url.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& requestUrl() const {
        return m_requestUrl;
    }

    /**
     * @brief Returns the current code.
     * @return The current code.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    /** @brief Returns the request path (server-side: from the client's GET request). */
    const SwString& requestPath() const {
        return m_path;
    }

    uint16_t closeCode() const {
        return m_closeCode;
    }

    /**
     * @brief Returns the current reason.
     * @return The current reason.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwString& closeReason() const {
        return m_closeReason;
    }

    /**
     * @brief Returns the current last Error.
     * @return The current last Error.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int lastError() const {
        return m_lastError;
    }

    SwString lastErrorText() const {
        return m_lastErrorText;
    }

    /**
     * @brief Opens the underlying resource managed by the object.
     * @param url Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void open(const SwString& url) {
        openInternal_(url, false);
    }

    // Server-side: adopt an already-connected stream socket and perform the WebSocket handshake.
    // The socket will be re-parented to this SwWebSocket instance.
    /**
     * @brief Performs the `accept` operation.
     * @param socket Socket instance affected by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool accept(SwAbstractSocket* socket, bool secure = false) {
        if (!attachAcceptedSocket_(socket)) {
            return false;
        }

        m_secure = secure;

        // If the client already sent the handshake request, process it immediately.
        onTcpReadyRead();
        return true;
    }

    /**
     * @brief Accepts a WebSocket upgrade whose HTTP request has already been parsed by another layer.
     * @param socket Connected transport socket to adopt.
     * @param requestPath Request path extracted from the HTTP upgrade request.
     * @param requestHeaders Parsed HTTP headers associated with the upgrade request.
     * @param secure `true` when the transport is already protected by TLS.
     * @return `true` on success; otherwise `false`.
     */
    bool acceptHttpUpgrade(SwAbstractSocket* socket,
                           const SwString& requestPath,
                           const SwMap<SwString, SwString>& requestHeaders,
                           bool secure = false) {
        if (!attachAcceptedSocket_(socket)) {
            return false;
        }

        m_secure = secure;
        if (!finishAcceptedHandshake_(requestPath, requestHeaders)) {
            reportError_(kErrorHandshakeFailed);
            abort();
            return false;
        }

        m_handshakeDone = true;
        m_handshakeStage = StageConnected;
        setState(SwAbstractSocket::ConnectedState);
        emit connected();
        return true;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     * @param code Value passed to the method.
     * @param reason Value passed to the method.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void close(CloseCode code = CloseCodeNormal, const SwString& reason = SwString()) {
        if (!m_socket) {
            return;
        }
        if (!m_handshakeDone) {
            abort();
            return;
        }

        if (m_state == SwAbstractSocket::ClosingState || m_state == SwAbstractSocket::UnconnectedState) {
            return;
        }

        emitAboutToCloseOnce_();
        setState(SwAbstractSocket::ClosingState);
        sendCloseFrame(static_cast<uint16_t>(code), reason);
        m_closeSent = true;
        startCloseTimer_();
    }

    /**
     * @brief Performs the `abort` operation.
     */
    void abort() {
        stopCloseTimer_();
        stopCloseReplyTimer_();
        cleanupSocket();
        resetProtocolState(false);
        m_handshakeStage = StageIdle;
        setState(SwAbstractSocket::UnconnectedState);
    }

    /**
     * @brief Performs the `sendTextMessage` operation.
     * @param message Value passed to the method.
     * @return The requested send Text Message.
     */
    int64_t sendTextMessage(const SwString& message) {
        SwByteArray payload(message.toStdString());
        bool rsv1 = false;
        if (m_pmd.negotiated) {
            SwByteArray compressed;
            if (!deflateMessage_(payload, compressed)) {
                reportError_(kErrorCompressionFailed);
                return -1;
            }
            payload = compressed;
            rsv1 = true;
        }
        return sendDataFrame(kOpText, payload, rsv1);
    }

    /**
     * @brief Performs the `sendBinaryMessage` operation.
     * @param message Value passed to the method.
     * @return The requested send Binary Message.
     */
    int64_t sendBinaryMessage(const SwByteArray& message) {
        SwByteArray payload = message;
        bool rsv1 = false;
        if (m_pmd.negotiated) {
            SwByteArray compressed;
            if (!deflateMessage_(payload, compressed)) {
                reportError_(kErrorCompressionFailed);
                return -1;
            }
            payload = compressed;
            rsv1 = true;
        }
        return sendDataFrame(kOpBinary, payload, rsv1);
    }

    /**
     * @brief Performs the `ping` operation.
     * @param payload Value passed to the method.
     */
    void ping(const SwByteArray& payload = SwByteArray()) {
        if (!m_socket || !m_handshakeDone) {
            return;
        }
        if (payload.size() > 125) {
            reportError_(kErrorPingPayloadTooBig);
            return;
        }

        PendingPing pending;
        pending.payload = payload;
        pending.sentAt = std::chrono::steady_clock::now();
        m_pendingPings.append(pending);

        sendControlFrame(kOpPing, payload);
    }

signals:
    DECLARE_SIGNAL_VOID(connected)
    DECLARE_SIGNAL_VOID(disconnected)
    DECLARE_SIGNAL_VOID(aboutToClose)
    DECLARE_SIGNAL(stateChanged, SwAbstractSocket::SocketState)
    DECLARE_SIGNAL(errorOccurred, int)
    DECLARE_SIGNAL(textMessageReceived, const SwString&)
    DECLARE_SIGNAL(binaryMessageReceived, const SwByteArray&)
    DECLARE_SIGNAL(pong, uint64_t, const SwByteArray&)

private:
    struct PendingPing {
        SwByteArray payload;
        std::chrono::steady_clock::time_point sentAt{};
    };

    struct PerMessageDeflateState {
        bool requested = true;
        bool negotiated = false;

        // Parameters that affect how *we* compress outgoing messages.
        bool outgoingNoContextTakeover = true;
        int outgoingMaxWindowBits = 15;

        // Parameters that affect how we decompress *incoming* messages from the peer.
        bool incomingNoContextTakeover = true;
        int incomingMaxWindowBits = 15;

        // Streaming contexts (used when no_context_takeover is not negotiated).
        bool deflaterInit = false;
        mz_stream deflater{};
        bool inflaterInit = false;
        mz_stream inflater{};
    };

    enum : int {
        kErrorInvalidUrl          = -100,
        kErrorConnectFailed       = -101,
        kErrorHandshakeFailed     = -102,
        kErrorProtocolError       = -103,
        kErrorMessageTooBig       = -104,
        kErrorPingPayloadTooBig   = -105,
        kErrorCloseTimeout        = -106,
        kErrorInvalidUtf8         = -107,
        kErrorCompressionFailed   = -108,
        kErrorProxyInvalid        = -109,
        kErrorProxyFailed         = -110,
        kErrorRedirectLimit       = -111,
        kErrorRedirectFailed      = -112
    };

    enum : uint8_t {
        kOpContinuation = 0x0,
        kOpText         = 0x1,
        kOpBinary       = 0x2,
        kOpClose        = 0x8,
        kOpPing         = 0x9,
        kOpPong         = 0xA
    };

    enum HandshakeStage {
        StageIdle,
        StageTcpConnecting,
        StageProxyHandshake,
        StageTlsHandshake,
        StageWebSocketHandshake,
        StageConnected
    };

    static bool parseUrl(const SwString& url, SwString& scheme, SwString& host, uint16_t& port, SwString& path) {
        SwString lower = url.toLower();
        int offset = -1;
        if (lower.startsWith("ws://")) {
            scheme = "ws";
            offset = 5;
            port = 80;
        } else if (lower.startsWith("wss://")) {
            scheme = "wss";
            offset = 6;
            port = 443;
        } else {
            return false;
        }

        SwString remainder = url.mid(offset);
        int slashPos = remainder.indexOf("/");
        SwString hostPortPart;
        if (slashPos >= 0) {
            hostPortPart = remainder.left(slashPos);
            path = remainder.mid(slashPos);
        } else {
            hostPortPart = remainder;
            path = "/";
        }

        int colonPos = hostPortPart.indexOf(":");
        if (colonPos >= 0) {
            host = hostPortPart.left(colonPos);
            SwString portStr = hostPortPart.mid(colonPos + 1);
            bool ok = false;
            int p = portStr.toInt(&ok);
            if (!ok || p <= 0 || p > 65535) {
                return false;
            }
            port = static_cast<uint16_t>(p);
        } else {
            host = hostPortPart;
        }

        if (path.isEmpty()) {
            path = "/";
        }
        return !host.isEmpty();
    }

    SwString hostHeaderValue() const {
        bool defaultPort = (!m_secure && m_port == 80) || (m_secure && m_port == 443);
        if (defaultPort || m_port == 0) {
            return m_host;
        }
        return m_host + ":" + SwString::number(static_cast<int>(m_port));
    }

    static bool fillRandomBytes(unsigned char* data, size_t len) {
        if (!data || len == 0) {
            return false;
        }
#if defined(_WIN32)
        NTSTATUS st = BCryptGenRandom(nullptr,
                                      reinterpret_cast<PUCHAR>(data),
                                      static_cast<ULONG>(len),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return st == 0;
#else
        return RAND_bytes(data, static_cast<int>(len)) == 1;
#endif
    }

    static SwString makeClientKeyBase64() {
        unsigned char key[16] = {0};
        if (!fillRandomBytes(key, sizeof(key))) {
            // fallback: deterministic but still valid (server must accept any 16-byte value)
            for (size_t i = 0; i < sizeof(key); ++i) {
                key[i] = static_cast<unsigned char>(i * 23u + 17u);
            }
        }

        SwByteArray raw(reinterpret_cast<const char*>(key), sizeof(key));
        SwByteArray base64 = raw.toBase64();
        return SwString(base64);
    }

    static SwString computeAcceptKey(const SwString& clientKeyBase64) {
        static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        SwString input = clientKeyBase64 + kGuid;
        std::vector<unsigned char> sha1 = SwCrypto::generateHashSHA1(input.toStdString());
        return SwString(SwCrypto::base64Encode(sha1));
    }

    static bool isValidCloseCode_(uint16_t code) {
        if (code < 1000) {
            return false;
        }
        // 1004,1005,1006,1015 are reserved and must not appear on the wire.
        if (code == 1004 || code == 1005 || code == 1006 || code == 1015) {
            return false;
        }
        if (code >= 5000) {
            return false;
        }
        return true;
    }

    static bool isValidUtf8_(const SwByteArray& bytes) {
        const size_t len = bytes.size();
        const unsigned char* s = reinterpret_cast<const unsigned char*>(bytes.constData());
        size_t i = 0;
        while (i < len) {
            const uint8_t c = s[i];
            if (c <= 0x7F) {
                ++i;
                continue;
            }

            if ((c & 0xE0) == 0xC0) {
                if (i + 1 >= len) return false;
                const uint8_t c1 = s[i + 1];
                if ((c1 & 0xC0) != 0x80) return false;
                const uint32_t codePoint = ((c & 0x1F) << 6) | (c1 & 0x3F);
                if (codePoint < 0x80) return false; // overlong
                i += 2;
                continue;
            }

            if ((c & 0xF0) == 0xE0) {
                if (i + 2 >= len) return false;
                const uint8_t c1 = s[i + 1];
                const uint8_t c2 = s[i + 2];
                if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
                const uint32_t codePoint = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
                if (codePoint < 0x800) return false; // overlong
                if (codePoint >= 0xD800 && codePoint <= 0xDFFF) return false; // surrogate
                i += 3;
                continue;
            }

            if ((c & 0xF8) == 0xF0) {
                if (i + 3 >= len) return false;
                const uint8_t c1 = s[i + 1];
                const uint8_t c2 = s[i + 2];
                const uint8_t c3 = s[i + 3];
                if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
                const uint32_t codePoint = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                if (codePoint < 0x10000) return false; // overlong
                if (codePoint > 0x10FFFF) return false;
                i += 4;
                continue;
            }

            return false;
        }
        return true;
    }

    struct ParsedExtension {
        SwString nameLower;
        SwMap<SwString, SwString> params; // key->value (value can be empty)
    };

    static SwList<ParsedExtension> parseExtensionsHeader_(const SwString& headerValue) {
        SwList<ParsedExtension> out;
        if (headerValue.isEmpty()) {
            return out;
        }

        const SwList<SwString> extensions = headerValue.split(",");
        for (size_t i = 0; i < extensions.size(); ++i) {
            const SwString extPart = extensions[i].trimmed();
            if (extPart.isEmpty()) {
                continue;
            }
            const SwList<SwString> tokens = extPart.split(";");
            if (tokens.isEmpty()) {
                continue;
            }

            ParsedExtension ext;
            ext.nameLower = tokens[0].trimmed().toLower();
            for (size_t t = 1; t < tokens.size(); ++t) {
                SwString param = tokens[t].trimmed();
                if (param.isEmpty()) {
                    continue;
                }
                int eq = param.indexOf("=");
                SwString key;
                SwString value;
                if (eq >= 0) {
                    key = param.left(eq).trimmed().toLower();
                    value = param.mid(eq + 1).trimmed();
                    if (value.size() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
                        value = value.mid(1, static_cast<int>(value.size()) - 2);
                    }
                } else {
                    key = param.toLower();
                    value = "";
                }
                if (!key.isEmpty()) {
                    ext.params[key] = value;
                }
            }

            out.append(ext);
        }

        return out;
    }

    static bool parseWindowBits_(const SwString& value, int& outBits) {
        if (value.isEmpty()) {
            return false;
        }
        bool ok = false;
        int bits = value.toInt(&ok);
        if (!ok || bits < 8 || bits > 15) {
            return false;
        }
        outBits = bits;
        return true;
    }

    void resetPerMessageDeflate_() {
        if (m_pmd.deflaterInit) {
            mz_deflateEnd(&m_pmd.deflater);
            m_pmd.deflaterInit = false;
        }
        if (m_pmd.inflaterInit) {
            mz_inflateEnd(&m_pmd.inflater);
            m_pmd.inflaterInit = false;
        }
        m_pmd.negotiated = false;
        m_pmd.outgoingNoContextTakeover = true;
        m_pmd.incomingNoContextTakeover = true;
        m_pmd.outgoingMaxWindowBits = 15;
        m_pmd.incomingMaxWindowBits = 15;
        std::memset(&m_pmd.deflater, 0, sizeof(m_pmd.deflater));
        std::memset(&m_pmd.inflater, 0, sizeof(m_pmd.inflater));
        m_currentMessageCompressed = false;
    }

    bool shouldUseProxy_() const {
        return m_proxy.type == HttpProxy;
    }

    static bool parseHttpResponseHeaders_(const SwString& headers,
                                         int& outStatusCode,
                                         SwMap<SwString, SwString>& outHeaderMap) {
        outStatusCode = 0;
        outHeaderMap.clear();

        SwList<SwString> lines = headers.split("\r\n");
        if (lines.isEmpty()) {
            return false;
        }

        const SwString statusLine = lines[0].trimmed();
        int firstSpace = statusLine.indexOf(" ");
        if (firstSpace < 0) {
            return false;
        }
        SwString codeStr = statusLine.mid(firstSpace + 1, 3);
        bool ok = false;
        int code = codeStr.toInt(&ok);
        if (!ok) {
            return false;
        }
        outStatusCode = code;

        for (size_t i = 1; i < lines.size(); ++i) {
            SwString line = lines[i];
            if (line.isEmpty()) {
                continue;
            }
            int colon = line.indexOf(":");
            if (colon < 0) {
                continue;
            }
            SwString key = line.left(colon).trimmed().toLower();
            SwString value = line.mid(colon + 1).trimmed();
            outHeaderMap[key] = value;
        }

        return true;
    }

    static bool isRedirectStatus_(int code) {
        return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    }

    bool resolveRedirectUrl_(const SwString& location, SwString& outUrl) const {
        SwString loc = location.trimmed();
        if (loc.isEmpty()) {
            return false;
        }

        const SwString lower = loc.toLower();
        if (lower.startsWith("ws://") || lower.startsWith("wss://")) {
            outUrl = loc;
            return true;
        }
        if (lower.startsWith("http://")) {
            outUrl = "ws://" + loc.mid(7);
            return true;
        }
        if (lower.startsWith("https://")) {
            outUrl = "wss://" + loc.mid(8);
            return true;
        }
        if (lower.startsWith("//")) {
            outUrl = m_scheme + ":" + loc;
            return true;
        }

        SwString newPath;
        if (loc.startsWith("/")) {
            newPath = loc;
        } else {
            SwString basePath = m_path;
            int q = basePath.indexOf("?");
            if (q >= 0) basePath = basePath.left(q);
            int h = basePath.indexOf("#");
            if (h >= 0) basePath = basePath.left(h);

            size_t slash = basePath.lastIndexOf('/');
            if (slash == static_cast<size_t>(-1)) {
                basePath = "/";
            } else {
                basePath = basePath.left(static_cast<int>(slash) + 1);
            }
            newPath = basePath + loc;
            if (!newPath.startsWith("/")) {
                newPath = "/" + newPath;
            }
        }

        outUrl = m_scheme + "://" + hostHeaderValue() + newPath;
        return true;
    }

    bool sendWebSocketHandshake_() {
        if (!m_socket) {
            return false;
        }
        SwString request = buildHandshakeRequest();
        return m_socket->write(request);
    }

    bool sendProxyConnectRequest_() {
        if (!m_socket) {
            return false;
        }
        if (m_proxy.type != HttpProxy || m_proxy.host.isEmpty() || m_proxy.port == 0) {
            return false;
        }

        const SwString target = m_host + ":" + SwString::number(static_cast<int>(m_port));
        SwString request = "CONNECT " + target + " HTTP/1.1\r\n";
        request += "Host: " + target + "\r\n";
        request += "Proxy-Connection: Keep-Alive\r\n";
        request += "Connection: Keep-Alive\r\n";

        if (!m_proxy.username.isEmpty()) {
            const SwString userpass = m_proxy.username + ":" + m_proxy.password;
            SwByteArray raw(userpass.toStdString());
            request += "Proxy-Authorization: Basic " + SwString(raw.toBase64()) + "\r\n";
        }

        request += "\r\n";
        return m_socket->write(request);
    }

    void openInternal_(const SwString& url, bool isRedirect) {
        if (!isRedirect) {
            m_redirectCount = 0;
        } else {
            if (m_maxRedirects >= 0 && m_redirectCount >= m_maxRedirects) {
                reportError_(kErrorRedirectLimit);
                return;
            }
            ++m_redirectCount;
        }

        abort();
        m_lastError = 0;
        m_lastErrorText.clear();
        m_role = ClientRole;

        m_requestUrl = url;
        if (!parseUrl(url, m_scheme, m_host, m_port, m_path)) {
            swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Invalid URL: " << url;
            reportError_(kErrorInvalidUrl);
            return;
        }

        m_secure = (m_scheme.toLower() == "wss");

        if (shouldUseProxy_()) {
            if (m_proxy.host.isEmpty() || m_proxy.port == 0) {
                reportError_(kErrorProxyInvalid);
                return;
            }
        }

        m_sslSocket = m_secure ? new SwSslSocket(this) : nullptr;
        m_socket = m_sslSocket ? static_cast<SwAbstractSocket*>(m_sslSocket) : new SwTcpSocket(this);
        if (m_sslSocket) {
            m_sslSocket->setPeerHostName(m_host);
            if (!m_trustedCaFile.isEmpty()) {
                m_sslSocket->setTrustedCaFile(m_trustedCaFile);
            }
            SwObject::connect(m_sslSocket, &SwSslSocket::sslErrors, [this](const SwSslErrorList& errors) {
                if (!errors.isEmpty()) {
                    m_lastErrorText = errors.first();
                    swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] TLS error: " << errors.first();
                }
            });
        }

        connect(m_socket, &SwAbstractSocket::disconnected, this, &SwWebSocket::onTcpDisconnected);
        connect(m_socket, &SwAbstractSocket::errorOccurred, this, &SwWebSocket::onTcpError);
        connect(m_socket, &SwIODevice::readyRead, this, &SwWebSocket::onTcpReadyRead);
        if (m_sslSocket && !shouldUseProxy_()) {
            connect(m_sslSocket, &SwSslSocket::encrypted, this, &SwWebSocket::onTcpConnected);
        } else if (m_sslSocket && shouldUseProxy_()) {
            connect(m_socket, &SwAbstractSocket::connected, this, &SwWebSocket::onTcpConnected);
            connect(m_sslSocket, &SwSslSocket::encrypted, this, &SwWebSocket::onTcpConnected);
        } else {
            connect(m_socket, &SwAbstractSocket::connected, this, &SwWebSocket::onTcpConnected);
        }

        m_handshakeStage = StageTcpConnecting;
        setState(SwAbstractSocket::ConnectingState);

        const SwString connectHost = shouldUseProxy_() ? m_proxy.host : m_host;
        const uint16_t connectPort = shouldUseProxy_() ? m_proxy.port : m_port;
        const bool connectOk =
            (m_sslSocket && !shouldUseProxy_()) ? m_sslSocket->connectToHostEncrypted(connectHost, connectPort)
                                                : m_socket->connectToHost(connectHost, connectPort);
        if (!connectOk) {
            swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] connectToHost failed";
            cleanupSocket();
            setState(SwAbstractSocket::UnconnectedState);
            reportError_(kErrorConnectFailed);
            return;
        }
    }

    SwString buildHandshakeRequest() {
        m_clientKeyBase64 = makeClientKeyBase64();
        m_expectedAccept = computeAcceptKey(m_clientKeyBase64);

        SwString request = "GET " + (m_path.isEmpty() ? SwString("/") : m_path) + " HTTP/1.1\r\n";
        request += "Host: " + hostHeaderValue() + "\r\n";
        request += "Upgrade: websocket\r\n";
        request += "Connection: Upgrade\r\n";
        request += "Sec-WebSocket-Version: 13\r\n";
        request += "Sec-WebSocket-Key: " + m_clientKeyBase64 + "\r\n";

        if (!m_origin.isEmpty()) {
            request += "Origin: " + m_origin + "\r\n";
        }

        if (!m_requestedSubprotocols.isEmpty()) {
            SwString protoLine;
            for (size_t i = 0; i < m_requestedSubprotocols.size(); ++i) {
                if (i != 0) protoLine += ", ";
                protoLine += m_requestedSubprotocols[i];
            }
            request += "Sec-WebSocket-Protocol: " + protoLine + "\r\n";
        }

        if (m_pmd.requested) {
            // Conservative default: request permessage-deflate and explicitly disable context takeover.
            // Servers that don't support it will ignore it; if accepted, RSV1 will be used for data frames.
            request += "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; server_no_context_takeover\r\n";
        }

        for (const auto& header : m_requestHeaderMap) {
            SwString lowerKey = header.first.toLower();
            if (lowerKey == "host" ||
                lowerKey == "upgrade" ||
                lowerKey == "connection" ||
                lowerKey == "sec-websocket-version" ||
                lowerKey == "sec-websocket-key") {
                continue;
            }
            request += header.first + ": " + header.second + "\r\n";
        }

        request += "\r\n";
        return request;
    }

    bool handleServerHandshakeRequest_(const SwString& headers) {
        SwList<SwString> lines = headers.split("\r\n");
        if (lines.isEmpty()) {
            return false;
        }

        const SwString requestLine = lines[0].trimmed();
        if (!requestLine.startsWith("GET ")) {
            return false;
        }
        int sp1 = requestLine.indexOf(" ");
        if (sp1 < 0) return false;
        int sp2 = requestLine.indexOf(" ", static_cast<size_t>(sp1 + 1));
        if (sp2 < 0) return false;

        SwString path = requestLine.mid(sp1 + 1, sp2 - (sp1 + 1));
        if (path.isEmpty()) {
            path = "/";
        }

        SwMap<SwString, SwString> headerMap;
        for (size_t i = 1; i < lines.size(); ++i) {
            SwString line = lines[i];
            if (line.isEmpty()) {
                continue;
            }
            int colon = line.indexOf(":");
            if (colon < 0) {
                continue;
            }
            SwString key = line.left(colon).trimmed().toLower();
            SwString value = line.mid(colon + 1).trimmed();
            headerMap[key] = value;
        }

        return finishAcceptedHandshake_(path, headerMap);
    }

    bool finishAcceptedHandshake_(const SwString& requestPath,
                                  const SwMap<SwString, SwString>& requestHeaders) {
        SwMap<SwString, SwString> headerMap;
        for (const auto& header : requestHeaders) {
            headerMap[header.first.toLower()] = header.second.trimmed();
        }

        SwString path = requestPath.trimmed();
        if (path.isEmpty()) {
            path = "/";
        }

        SwString upgrade = headerMap.value("upgrade").trimmed();
        if (!upgrade.contains("websocket", Sw::CaseInsensitive)) {
            return false;
        }

        SwString connection = headerMap.value("connection").toLower();
        if (!connection.contains("upgrade", Sw::CaseInsensitive)) {
            return false;
        }

        SwString version = headerMap.value("sec-websocket-version").trimmed();
        if (version != "13") {
            return false;
        }

        SwString clientKey = headerMap.value("sec-websocket-key").trimmed();
        if (clientKey.isEmpty()) {
            return false;
        }

        // Subprotocol selection (server picks one from the intersection).
        m_acceptedSubprotocol.clear();
        if (!m_supportedSubprotocols.isEmpty()) {
            SwString offered = headerMap.value("sec-websocket-protocol").trimmed();
            if (!offered.isEmpty()) {
                SwList<SwString> offeredList = offered.split(",");
                for (size_t i = 0; i < offeredList.size(); ++i) {
                    SwString candidate = offeredList[i].trimmed();
                    if (candidate.isEmpty()) {
                        continue;
                    }
                    for (size_t j = 0; j < m_supportedSubprotocols.size(); ++j) {
                        if (candidate == m_supportedSubprotocols[j]) {
                            m_acceptedSubprotocol = candidate;
                            break;
                        }
                    }
                    if (!m_acceptedSubprotocol.isEmpty()) {
                        break;
                    }
                }
            }
        }

        // Extensions negotiation (currently only permessage-deflate).
        resetPerMessageDeflate_();
        m_acceptedExtensions.clear();
        if (m_pmd.requested) {
            SwString extHeader = headerMap.value("sec-websocket-extensions").trimmed();
            if (!extHeader.isEmpty()) {
                const SwList<ParsedExtension> parsed = parseExtensionsHeader_(extHeader);
                for (size_t i = 0; i < parsed.size(); ++i) {
                    if (parsed[i].nameLower != "permessage-deflate") {
                        continue;
                    }

                    m_pmd.negotiated = true;

                    // Prefer no_context_takeover when offered, otherwise allow context takeover.
                    const bool offerClientNo = parsed[i].params.contains("client_no_context_takeover");
                    const bool offerServerNo = parsed[i].params.contains("server_no_context_takeover");
                    m_pmd.incomingNoContextTakeover = offerClientNo;
                    m_pmd.outgoingNoContextTakeover = offerServerNo;

                    // Window bits (optional). In the server role:
                    // - client_max_window_bits impacts how we inflate client->server messages (incoming).
                    // - server_max_window_bits impacts how we deflate server->client messages (outgoing).
                    if (parsed[i].params.contains("client_max_window_bits")) {
                        const SwString v = parsed[i].params.value("client_max_window_bits");
                        int bits = 15;
                        if (!v.isEmpty() && !parseWindowBits_(v, bits)) {
                            return false;
                        }
                        m_pmd.incomingMaxWindowBits = bits;
                    }
                    if (parsed[i].params.contains("server_max_window_bits")) {
                        const SwString v = parsed[i].params.value("server_max_window_bits");
                        int bits = 15;
                        if (!v.isEmpty() && !parseWindowBits_(v, bits)) {
                            return false;
                        }
                        m_pmd.outgoingMaxWindowBits = bits;
                    }

                    SwString resp = "permessage-deflate";
                    if (m_pmd.incomingNoContextTakeover) {
                        resp += "; client_no_context_takeover";
                    }
                    if (m_pmd.outgoingNoContextTakeover) {
                        resp += "; server_no_context_takeover";
                    }
                    if (parsed[i].params.contains("client_max_window_bits") &&
                        !parsed[i].params.value("client_max_window_bits").isEmpty()) {
                        resp += "; client_max_window_bits=" + SwString::number(m_pmd.incomingMaxWindowBits);
                    }
                    if (parsed[i].params.contains("server_max_window_bits") &&
                        !parsed[i].params.value("server_max_window_bits").isEmpty()) {
                        resp += "; server_max_window_bits=" + SwString::number(m_pmd.outgoingMaxWindowBits);
                    }
                    m_acceptedExtensions = resp;
                    break;
                }
            }
        }

        const SwString accept = computeAcceptKey(clientKey);
        SwString response = "HTTP/1.1 101 Switching Protocols\r\n";
        response += "Upgrade: websocket\r\n";
        response += "Connection: Upgrade\r\n";
        response += "Sec-WebSocket-Accept: " + accept + "\r\n";
        if (!m_acceptedSubprotocol.isEmpty()) {
            response += "Sec-WebSocket-Protocol: " + m_acceptedSubprotocol + "\r\n";
        }
        if (m_pmd.negotiated && !m_acceptedExtensions.isEmpty()) {
            response += "Sec-WebSocket-Extensions: " + m_acceptedExtensions + "\r\n";
        }
        response += "\r\n";

        if (!m_socket || !m_socket->write(response)) {
            return false;
        }

        m_path = path;
        m_scheme = m_secure ? "wss" : "ws";
        const SwString host = headerMap.value("host").trimmed();
        if (!host.isEmpty()) {
            m_requestUrl = m_scheme + "://" + host + m_path;
        } else {
            m_requestUrl.clear();
        }
        return true;
    }

    bool parseHandshakeResponse(const SwString& headers) {
        SwList<SwString> lines = headers.split("\r\n");
        if (lines.isEmpty()) {
            return false;
        }

        const SwString statusLine = lines[0].trimmed();
        int firstSpace = statusLine.indexOf(" ");
        if (firstSpace < 0) {
            return false;
        }
        SwString codeStr = statusLine.mid(firstSpace + 1, 3);
        bool ok = false;
        int code = codeStr.toInt(&ok);
        if (!ok || code != 101) {
            swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Handshake status not 101: " << statusLine;
            return false;
        }

        SwMap<SwString, SwString> headerMap;
        for (size_t i = 1; i < lines.size(); ++i) {
            SwString line = lines[i];
            if (line.isEmpty()) {
                continue;
            }
            int colon = line.indexOf(":");
            if (colon < 0) {
                continue;
            }
            SwString key = line.left(colon).trimmed().toLower();
            SwString value = line.mid(colon + 1).trimmed();
            headerMap[key] = value;
        }

        SwString upgrade = headerMap.value("upgrade").trimmed();
        if (!upgrade.contains("websocket", Sw::CaseInsensitive)) {
            swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Missing/invalid Upgrade header: " << upgrade;
            return false;
        }

        SwString connection = headerMap.value("connection").toLower();
        if (!connection.contains("upgrade", Sw::CaseInsensitive)) {
            swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Missing/invalid Connection header: " << connection;
            return false;
        }

        SwString accept = headerMap.value("sec-websocket-accept").trimmed();
        if (accept.isEmpty() || accept != m_expectedAccept) {
            swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Accept mismatch expected=" << m_expectedAccept
                                                 << " got=" << accept;
            return false;
        }

        m_acceptedSubprotocol = headerMap.value("sec-websocket-protocol").trimmed();
        int comma = m_acceptedSubprotocol.indexOf(",");
        if (comma >= 0) {
            m_acceptedSubprotocol = m_acceptedSubprotocol.left(comma).trimmed();
        }
        if (!m_acceptedSubprotocol.isEmpty() && !m_requestedSubprotocols.isEmpty()) {
            bool allowed = false;
            for (size_t i = 0; i < m_requestedSubprotocols.size(); ++i) {
                if (m_requestedSubprotocols[i] == m_acceptedSubprotocol) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Server returned unsupported subprotocol: "
                                                     << m_acceptedSubprotocol;
                return false;
            }
        }

        m_acceptedExtensions = headerMap.value("sec-websocket-extensions").trimmed();
        resetPerMessageDeflate_();
        if (!m_acceptedExtensions.isEmpty()) {
            const SwList<ParsedExtension> parsed = parseExtensionsHeader_(m_acceptedExtensions);
            for (size_t i = 0; i < parsed.size(); ++i) {
                if (parsed[i].nameLower != "permessage-deflate") {
                    continue;
                }
                if (!m_pmd.requested) {
                    swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Server negotiated permessage-deflate but it wasn't requested";
                    return false;
                }

                m_pmd.negotiated = true;
                m_pmd.outgoingNoContextTakeover = parsed[i].params.contains("client_no_context_takeover");
                m_pmd.incomingNoContextTakeover = parsed[i].params.contains("server_no_context_takeover");

                // Window bits parameters are optional; default is 15.
                if (parsed[i].params.contains("client_max_window_bits")) {
                    const SwString v = parsed[i].params.value("client_max_window_bits");
                    int bits = 15;
                    if (!v.isEmpty() && !parseWindowBits_(v, bits)) {
                        swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Invalid client_max_window_bits: " << v;
                        return false;
                    }
                    m_pmd.outgoingMaxWindowBits = bits;
                }
                if (parsed[i].params.contains("server_max_window_bits")) {
                    const SwString v = parsed[i].params.value("server_max_window_bits");
                    int bits = 15;
                    if (!v.isEmpty() && !parseWindowBits_(v, bits)) {
                        swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Invalid server_max_window_bits: " << v;
                        return false;
                    }
                    m_pmd.incomingMaxWindowBits = bits;
                }
                break;
            }
        }
        return true;
    }

    static SwByteArray buildFrame(uint8_t opcode,
                                  const SwByteArray& payload,
                                  bool mask,
                                  bool fin,
                                  bool rsv1,
                                  const unsigned char maskKey[4]) {
        const uint64_t len = static_cast<uint64_t>(payload.size());

        SwByteArray frame;
        frame.reserve(static_cast<size_t>(2 + (len <= 125 ? 0 : (len <= 65535 ? 2 : 8)) + (mask ? 4 : 0) + len));

        const uint8_t b0 = static_cast<uint8_t>((fin ? 0x80 : 0x00) | (rsv1 ? 0x40 : 0x00) | (opcode & 0x0F));
        frame.append(static_cast<char>(b0));

        uint8_t b1 = static_cast<uint8_t>(mask ? 0x80 : 0x00);
        if (len <= 125) {
            b1 |= static_cast<uint8_t>(len);
            frame.append(static_cast<char>(b1));
        } else if (len <= 65535) {
            b1 |= 126;
            frame.append(static_cast<char>(b1));
            frame.append(static_cast<char>((len >> 8) & 0xFF));
            frame.append(static_cast<char>(len & 0xFF));
        } else {
            b1 |= 127;
            frame.append(static_cast<char>(b1));
            for (int i = 7; i >= 0; --i) {
                frame.append(static_cast<char>((len >> (8 * i)) & 0xFF));
            }
        }

        SwByteArray payloadOut = payload;
        if (mask) {
            frame.append(static_cast<char>(maskKey[0]));
            frame.append(static_cast<char>(maskKey[1]));
            frame.append(static_cast<char>(maskKey[2]));
            frame.append(static_cast<char>(maskKey[3]));
            for (size_t i = 0; i < payloadOut.size(); ++i) {
                payloadOut[i] = static_cast<char>(
                    static_cast<unsigned char>(payloadOut[i]) ^ maskKey[i % 4]);
            }
        }

        if (!payloadOut.isEmpty()) {
            frame.append(payloadOut.constData(), payloadOut.size());
        }
        return frame;
    }

    static bool parseOneFrame(SwByteArray& buffer,
                              uint8_t& outOpcode,
                              bool& outFin,
                              bool& outRsv1,
                              SwByteArray& outPayload,
                              bool& outWasMasked,
                              bool& outNeedMoreData,
                              uint64_t& outPayloadLen,
                              bool allowRsv1) {
        outNeedMoreData = false;
        outPayloadLen = 0;
        outWasMasked = false;
        if (buffer.size() < 2) {
            outNeedMoreData = true;
            return false;
        }

        const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer.constData());
        const uint8_t b0 = data[0];
        const uint8_t b1 = data[1];

        outFin = (b0 & 0x80) != 0;
        outRsv1 = (b0 & 0x40) != 0;
        outOpcode = static_cast<uint8_t>(b0 & 0x0F);
        const bool masked = (b1 & 0x80) != 0;
        outWasMasked = masked;

        uint64_t payloadLen = static_cast<uint64_t>(b1 & 0x7F);
        size_t pos = 2;

        const bool rsv2 = (b0 & 0x20) != 0;
        const bool rsv3 = (b0 & 0x10) != 0;
        if (rsv2 || rsv3) {
            return false;
        }
        if (outRsv1 && !allowRsv1) {
            return false;
        }

        if (payloadLen == 126) {
            if (buffer.size() < pos + 2) {
                outNeedMoreData = true;
                return false;
            }
            payloadLen = (static_cast<uint64_t>(data[pos]) << 8) |
                         (static_cast<uint64_t>(data[pos + 1]));
            pos += 2;
        } else if (payloadLen == 127) {
            if (buffer.size() < pos + 8) {
                outNeedMoreData = true;
                return false;
            }
            if ((data[pos] & 0x80) != 0) {
                // Most significant bit MUST be 0 (length is 63-bit unsigned).
                return false;
            }
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLen = (payloadLen << 8) | static_cast<uint64_t>(data[pos + i]);
            }
            pos += 8;
        }

        outPayloadLen = payloadLen;
        if ((outOpcode & 0x08) != 0) {
            // Control frames MUST have payload <= 125.
            if (payloadLen > 125) {
                return false;
            }
        }

        unsigned char maskKey[4] = {0, 0, 0, 0};
        if (masked) {
            if (buffer.size() < pos + 4) {
                outNeedMoreData = true;
                return false;
            }
            maskKey[0] = data[pos + 0];
            maskKey[1] = data[pos + 1];
            maskKey[2] = data[pos + 2];
            maskKey[3] = data[pos + 3];
            pos += 4;
        }

        if (payloadLen > static_cast<uint64_t>(buffer.size() - pos)) {
            outNeedMoreData = true;
            return false;
        }

        if (payloadLen > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            // SwByteArray::mid and remove use int indices.
            return false;
        }

        if (payloadLen > 0) {
            outPayload = buffer.mid(static_cast<int>(pos), static_cast<int>(payloadLen));
            if (masked) {
                for (size_t i = 0; i < outPayload.size(); ++i) {
                    outPayload[i] = static_cast<char>(
                        static_cast<unsigned char>(outPayload[i]) ^ maskKey[i % 4]);
                }
            }
        } else {
            outPayload.clear();
        }

        buffer.remove(0, static_cast<int>(pos + payloadLen));
        return true;
    }

    bool ensureDeflater_() {
        if (!m_pmd.negotiated) {
            return false;
        }
        if (m_pmd.deflaterInit) {
            return true;
        }
        std::memset(&m_pmd.deflater, 0, sizeof(m_pmd.deflater));
        int rc = mz_deflateInit2(&m_pmd.deflater,
                                MZ_DEFAULT_COMPRESSION,
                                MZ_DEFLATED,
                                -m_pmd.outgoingMaxWindowBits,
                                8,
                                MZ_DEFAULT_STRATEGY);
        if (rc != MZ_OK) {
            return false;
        }
        m_pmd.deflaterInit = true;
        return true;
    }

    bool ensureInflater_() {
        if (!m_pmd.negotiated) {
            return false;
        }
        if (m_pmd.inflaterInit) {
            return true;
        }
        std::memset(&m_pmd.inflater, 0, sizeof(m_pmd.inflater));
        int rc = mz_inflateInit2(&m_pmd.inflater, -m_pmd.incomingMaxWindowBits);
        if (rc != MZ_OK) {
            return false;
        }
        m_pmd.inflaterInit = true;
        return true;
    }

    bool deflateMessage_(const SwByteArray& input, SwByteArray& outCompressed) {
        if (!m_pmd.negotiated) {
            return false;
        }

        if (m_pmd.outgoingNoContextTakeover) {
            mz_stream stream;
            std::memset(&stream, 0, sizeof(stream));
            int rc = mz_deflateInit2(&stream,
                                    MZ_DEFAULT_COMPRESSION,
                                    MZ_DEFLATED,
                                    -m_pmd.outgoingMaxWindowBits,
                                    8,
                                    MZ_DEFAULT_STRATEGY);
            if (rc != MZ_OK) {
                return false;
            }

            stream.next_in = reinterpret_cast<const unsigned char*>(input.constData());
            stream.avail_in = static_cast<unsigned int>(input.size());

            SwByteArray out;
            out.reserve(input.size() + 64);

            while (true) {
                const int oldSize = static_cast<int>(out.size());
                out.resize(oldSize + 4096);
                stream.next_out = reinterpret_cast<unsigned char*>(out.data() + oldSize);
                stream.avail_out = 4096;

                int ret = mz_deflate(&stream, MZ_SYNC_FLUSH);
                if (ret != MZ_OK) {
                    mz_deflateEnd(&stream);
                    return false;
                }

                const int produced = 4096 - static_cast<int>(stream.avail_out);
                out.resize(oldSize + produced);

                if (stream.avail_out != 0) {
                    break;
                }
            }

            mz_deflateEnd(&stream);

            if (out.size() < 4) {
                return false;
            }
            // Remove tail 0x00 0x00 0xFF 0xFF (added by SYNC_FLUSH).
            const size_t n = out.size();
            if (!(static_cast<unsigned char>(out[n - 4]) == 0x00 &&
                  static_cast<unsigned char>(out[n - 3]) == 0x00 &&
                  static_cast<unsigned char>(out[n - 2]) == 0xFF &&
                  static_cast<unsigned char>(out[n - 1]) == 0xFF)) {
                return false;
            }
            out.remove(static_cast<int>(n - 4), 4);
            outCompressed = out;
            return true;
        }

        if (!ensureDeflater_()) {
            return false;
        }

        mz_stream& stream = m_pmd.deflater;
        stream.next_in = reinterpret_cast<const unsigned char*>(input.constData());
        stream.avail_in = static_cast<unsigned int>(input.size());

        SwByteArray out;
        out.reserve(input.size() + 64);

        while (true) {
            const int oldSize = static_cast<int>(out.size());
            out.resize(oldSize + 4096);
            stream.next_out = reinterpret_cast<unsigned char*>(out.data() + oldSize);
            stream.avail_out = 4096;

            int ret = mz_deflate(&stream, MZ_SYNC_FLUSH);
            if (ret != MZ_OK) {
                return false;
            }

            const int produced = 4096 - static_cast<int>(stream.avail_out);
            out.resize(oldSize + produced);
            if (stream.avail_out != 0) {
                break;
            }
        }

        if (out.size() < 4) {
            return false;
        }
        const size_t n = out.size();
        if (!(static_cast<unsigned char>(out[n - 4]) == 0x00 &&
              static_cast<unsigned char>(out[n - 3]) == 0x00 &&
              static_cast<unsigned char>(out[n - 2]) == 0xFF &&
              static_cast<unsigned char>(out[n - 1]) == 0xFF)) {
            return false;
        }
        out.remove(static_cast<int>(n - 4), 4);
        outCompressed = out;
        return true;
    }

    bool inflateMessage_(const SwByteArray& compressed, SwByteArray& out) {
        if (!m_pmd.negotiated) {
            return false;
        }

        auto inflateWithStream = [&](mz_stream& stream) -> bool {
            stream.next_in = reinterpret_cast<const unsigned char*>(compressed.constData());
            stream.avail_in = static_cast<unsigned int>(compressed.size());

            static const unsigned char kTail[4] = {0x00, 0x00, 0xFF, 0xFF};
            bool tailPending = true;

            SwByteArray result;
            result.reserve(compressed.size() * 2 + 64);

            while (true) {
                if (stream.avail_in == 0 && tailPending) {
                    stream.next_in = kTail;
                    stream.avail_in = 4;
                    tailPending = false;
                }

                const int oldSize = static_cast<int>(result.size());
                result.resize(oldSize + 4096);
                stream.next_out = reinterpret_cast<unsigned char*>(result.data() + oldSize);
                stream.avail_out = 4096;

                int ret = mz_inflate(&stream, MZ_SYNC_FLUSH);
                if (ret != MZ_OK && ret != MZ_STREAM_END && ret != MZ_BUF_ERROR) {
                    return false;
                }

                const int produced = 4096 - static_cast<int>(stream.avail_out);
                result.resize(oldSize + produced);

                if (m_maxIncomingMessageSize > 0 &&
                    static_cast<uint64_t>(result.size()) > m_maxIncomingMessageSize) {
                    reportError_(kErrorMessageTooBig);
                    close(CloseCodeMessageTooBig);
                    return false;
                }

                if (ret == MZ_STREAM_END) {
                    break;
                }

                if (stream.avail_in == 0 && !tailPending) {
                    break;
                }

                if (ret == MZ_BUF_ERROR && produced == 0 && stream.avail_out != 0) {
                    // No progress possible.
                    break;
                }
            }

            out = result;
            return true;
        };

        if (m_pmd.incomingNoContextTakeover) {
            mz_stream stream;
            std::memset(&stream, 0, sizeof(stream));
            int rc = mz_inflateInit2(&stream, -m_pmd.incomingMaxWindowBits);
            if (rc != MZ_OK) {
                return false;
            }
            bool ok = inflateWithStream(stream);
            mz_inflateEnd(&stream);
            return ok;
        }

        if (!ensureInflater_()) {
            return false;
        }
        return inflateWithStream(m_pmd.inflater);
    }

    int64_t sendDataFrame(uint8_t opcode, const SwByteArray& payload, bool rsv1) {
        if (!m_socket || !m_handshakeDone || m_state != SwAbstractSocket::ConnectedState) {
            return -1;
        }

        const bool mask = (m_role == ClientRole);
        unsigned char maskKey[4] = {0, 0, 0, 0};
        if (mask) {
            if (!fillRandomBytes(maskKey, sizeof(maskKey))) {
                maskKey[0] = 0x12; maskKey[1] = 0x34; maskKey[2] = 0x56; maskKey[3] = 0x78;
            }
        }

        SwByteArray frame = buildFrame(opcode, payload, mask, true, rsv1, maskKey);
        if (!m_socket->write(frame)) {
            return -1;
        }
        return static_cast<int64_t>(payload.size());
    }

    void sendControlFrame(uint8_t opcode, const SwByteArray& payload) {
        if (!m_socket || !m_handshakeDone) {
            return;
        }
        if (payload.size() > 125) {
            reportError_(kErrorProtocolError);
            return;
        }

        const bool mask = (m_role == ClientRole);
        unsigned char maskKey[4] = {0, 0, 0, 0};
        if (mask) {
            if (!fillRandomBytes(maskKey, sizeof(maskKey))) {
                maskKey[0] = 0xAA; maskKey[1] = 0xBB; maskKey[2] = 0xCC; maskKey[3] = 0xDD;
            }
        }

        SwByteArray frame = buildFrame(opcode, payload, mask, true, false, maskKey);
        m_socket->write(frame);
    }

    void sendCloseFrame(uint16_t code, const SwString& reason) {
        SwByteArray payload;
        payload.append(static_cast<char>((code >> 8) & 0xFF));
        payload.append(static_cast<char>(code & 0xFF));
        if (!reason.isEmpty()) {
            SwByteArray reasonBytes(reason.toStdString());
            if (reasonBytes.size() <= 123) {
                payload.append(reasonBytes.constData(), reasonBytes.size());
            }
        }
        sendControlFrame(kOpClose, payload);
    }

    void setState(SwAbstractSocket::SocketState newState) {
        if (m_state == newState) {
            return;
        }
        m_state = newState;
        emit stateChanged(m_state);
    }

private slots:
    void onCloseTimeout_() {
        if (m_state != SwAbstractSocket::ClosingState) {
            return;
        }
        reportError_(kErrorCloseTimeout);
        m_closeCode = static_cast<uint16_t>(CloseCodeAbnormalClosure);
        m_closeReason.clear();
        stopCloseTimer_();
        stopCloseReplyTimer_();
        cleanupSocket();
        resetProtocolState(true);
        m_handshakeStage = StageIdle;
        setState(SwAbstractSocket::UnconnectedState);
        emit disconnected();
    }

    void onCloseReplyTimeout_() {
        stopCloseReplyTimer_();
        if (!m_socket) {
            return;
        }
        cleanupSocket();
        resetProtocolState(true);
        m_handshakeStage = StageIdle;
        setState(SwAbstractSocket::UnconnectedState);
        emit disconnected();
    }

    void onTcpConnected() {
        if (!m_socket) {
            return;
        }

        if (m_handshakeStage == StageTcpConnecting) {
            if (shouldUseProxy_()) {
                swCDebug(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Connected to proxy, sending CONNECT";
                if (!sendProxyConnectRequest_()) {
                    reportError_(kErrorProxyFailed);
                    abort();
                    return;
                }
                m_handshakeStage = StageProxyHandshake;
                return;
            }

            swCDebug(kSwLogCategory_SwWebSocket) << "[SwWebSocket] TCP connected, sending handshake";
            if (!sendWebSocketHandshake_()) {
                reportError_(kErrorHandshakeFailed);
                abort();
                return;
            }
            m_handshakeStage = StageWebSocketHandshake;
            return;
        }

        if (m_handshakeStage == StageTlsHandshake) {
            swCDebug(kSwLogCategory_SwWebSocket) << "[SwWebSocket] TLS ready, sending handshake";
            if (!sendWebSocketHandshake_()) {
                reportError_(kErrorHandshakeFailed);
                abort();
                return;
            }
            m_handshakeStage = StageWebSocketHandshake;
            return;
        }
    }

    void onTcpReadyRead() {
        if (!m_socket) {
            return;
        }

        while (true) {
            SwString chunk = m_socket->read();
            if (chunk.isEmpty()) {
                break;
            }
            m_buffer.append(chunk.data(), chunk.size());
        }

        if (m_handshakeStage == StageProxyHandshake) {
            int boundary = m_buffer.indexOf("\r\n\r\n");
            if (boundary < 0) {
                return;
            }

            SwByteArray headerBytes = m_buffer.left(boundary);
            m_buffer.remove(0, boundary + 4);

            int statusCode = 0;
            SwMap<SwString, SwString> headerMap;
            if (!parseHttpResponseHeaders_(SwString(headerBytes), statusCode, headerMap)) {
                reportError_(kErrorProxyFailed);
                abort();
                return;
            }

            if (statusCode != 200) {
                swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] Proxy CONNECT failed status=" << statusCode;
                reportError_(kErrorProxyFailed);
                abort();
                return;
            }

            if (m_secure) {
                m_handshakeStage = StageTlsHandshake;
                if (!m_sslSocket || !m_sslSocket->startClientEncryption()) {
                    reportError_(kErrorProxyFailed);
                    abort();
                    return;
                }
                // TLS completion will trigger onTcpConnected (connected signal).
                return;
            }

            if (!sendWebSocketHandshake_()) {
                reportError_(kErrorHandshakeFailed);
                abort();
                return;
            }
            m_handshakeStage = StageWebSocketHandshake;
        }

        if (m_handshakeStage == StageWebSocketHandshake && !m_handshakeDone) {
            int boundary = m_buffer.indexOf("\r\n\r\n");
            if (boundary < 0) {
                return;
            }

            SwByteArray headerBytes = m_buffer.left(boundary);
            m_buffer.remove(0, boundary + 4);
            const SwString headerText(headerBytes);

            if (m_role == ServerRole) {
                if (!handleServerHandshakeRequest_(headerText)) {
                    reportError_(kErrorHandshakeFailed);
                    abort();
                    return;
                }

                m_handshakeDone = true;
                m_handshakeStage = StageConnected;
                setState(SwAbstractSocket::ConnectedState);
                emit connected();
                // Fallthrough: process any already-buffered frames.
            } else {
            int statusCode = 0;
            SwMap<SwString, SwString> headerMap;
            if (!parseHttpResponseHeaders_(headerText, statusCode, headerMap)) {
                reportError_(kErrorHandshakeFailed);
                abort();
                return;
            }

            if (statusCode != 101) {
                if (m_followRedirects && isRedirectStatus_(statusCode)) {
                    const SwString location = headerMap.value("location").trimmed();
                    if (location.isEmpty()) {
                        reportError_(kErrorRedirectFailed);
                        abort();
                        return;
                    }

                    SwString newUrl;
                    if (!resolveRedirectUrl_(location, newUrl)) {
                        reportError_(kErrorRedirectFailed);
                        abort();
                        return;
                    }

                    openInternal_(newUrl, true);
                    return;
                }

                reportError_(kErrorHandshakeFailed);
                abort();
                return;
            }

            if (!parseHandshakeResponse(headerText)) {
                reportError_(kErrorHandshakeFailed);
                abort();
                return;
            }

            m_handshakeDone = true;
            m_handshakeStage = StageConnected;
            setState(SwAbstractSocket::ConnectedState);
            emit connected();
            }
        }

        if (m_handshakeStage == StageConnected && m_handshakeDone) {
            processFrames();
        }
    }

    void onTcpDisconnected() {
        stopCloseTimer_();
        stopCloseReplyTimer_();
        bool wasConnected = (m_state == SwAbstractSocket::ConnectedState || m_state == SwAbstractSocket::ClosingState);
        cleanupSocket();
        resetProtocolState(true);
        m_handshakeStage = StageIdle;
        setState(SwAbstractSocket::UnconnectedState);
        if (wasConnected) {
            emit disconnected();
        }
    }

    void onTcpError(int err) {
        swCError(kSwLogCategory_SwWebSocket) << "[SwWebSocket] socket error " << err;
        reportError_(err);
        abort();
    }

private:
    void reportError_(int err) {
        m_lastError = err;
        emit errorOccurred(err);
    }

    void emitAboutToCloseOnce_() {
        if (m_aboutToCloseEmitted) {
            return;
        }
        m_aboutToCloseEmitted = true;
        emit aboutToClose();
    }

    void startCloseTimer_() {
        if (m_closeTimeoutMs <= 0) {
            return;
        }
        if (!m_closeTimer) {
            m_closeTimer = new SwTimer(this);
            m_closeTimer->setSingleShot(true);
            connect(m_closeTimer, &SwTimer::timeout, this, &SwWebSocket::onCloseTimeout_);
        }
        m_closeTimer->stop();
        m_closeTimer->start(m_closeTimeoutMs);
    }

    void stopCloseTimer_() {
        if (m_closeTimer && m_closeTimer->isActive()) {
            m_closeTimer->stop();
        }
    }

    void startCloseReplyTimer_() {
        if (m_closeReplyDelayMs <= 0) {
            onCloseReplyTimeout_();
            return;
        }
        if (!m_closeReplyTimer) {
            m_closeReplyTimer = new SwTimer(this);
            m_closeReplyTimer->setSingleShot(true);
            connect(m_closeReplyTimer, &SwTimer::timeout, this, &SwWebSocket::onCloseReplyTimeout_);
        }
        m_closeReplyTimer->stop();
        m_closeReplyTimer->start(m_closeReplyDelayMs);
    }

    void stopCloseReplyTimer_() {
        if (m_closeReplyTimer && m_closeReplyTimer->isActive()) {
            m_closeReplyTimer->stop();
        }
    }

    void cleanupSocket() {
        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->close();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        m_sslSocket = nullptr;
        m_buffer.clear();
    }

    bool attachAcceptedSocket_(SwAbstractSocket* socket) {
        if (!socket) {
            reportError_(kErrorConnectFailed);
            return false;
        }

        abort();
        m_lastError = 0;
        m_role = ServerRole;
        m_secure = false;
        m_scheme.clear();
        m_requestUrl.clear();
        m_path.clear();

        m_socket = socket;
        m_socket->setParent(this);

        connect(m_socket, &SwAbstractSocket::disconnected, this, &SwWebSocket::onTcpDisconnected);
        connect(m_socket, &SwAbstractSocket::errorOccurred, this, &SwWebSocket::onTcpError);
        connect(m_socket, &SwIODevice::readyRead, this, &SwWebSocket::onTcpReadyRead);

        m_handshakeDone = false;
        m_handshakeStage = StageWebSocketHandshake;
        setState(SwAbstractSocket::ConnectingState);
        return true;
    }

    void resetProtocolState(bool keepCloseInfo) {
        m_handshakeDone = false;
        m_clientKeyBase64.clear();
        m_expectedAccept.clear();
        m_acceptedSubprotocol.clear();
        m_acceptedExtensions.clear();
        resetPerMessageDeflate_();
        m_messageOpcode = -1;
        m_messageBuffer.clear();
        m_closeSent = false;
        m_closeReceived = false;
        m_aboutToCloseEmitted = false;
        m_pendingPings.clear();
        if (!keepCloseInfo) {
            m_closeCode = static_cast<uint16_t>(CloseCodeNoStatusRcvd);
            m_closeReason.clear();
        }
    }

    void processFrames() {
        while (!m_buffer.isEmpty()) {
            uint8_t opcode = 0;
            bool fin = false;
            bool rsv1 = false;
            bool masked = false;
            bool needMore = false;
            uint64_t payloadLen = 0;
            SwByteArray payload;

            if (!parseOneFrame(m_buffer, opcode, fin, rsv1, payload, masked, needMore, payloadLen, m_pmd.negotiated)) {
                if (needMore) {
                    if (m_maxIncomingMessageSize > 0 &&
                        (opcode == kOpText || opcode == kOpBinary || opcode == kOpContinuation) &&
                        payloadLen > m_maxIncomingMessageSize) {
                        reportError_(kErrorMessageTooBig);
                        close(CloseCodeMessageTooBig);
                        return;
                    }
                    return;
                }
                reportError_(kErrorProtocolError);
                abort();
                return;
            }

            const bool expectMasked = (m_role == ServerRole);
            if (expectMasked && !masked) {
                reportError_(kErrorProtocolError);
                close(CloseCodeProtocolError);
                return;
            }
            if (!expectMasked && masked) {
                reportError_(kErrorProtocolError);
                close(CloseCodeProtocolError);
                return;
            }

            // Control frames must be unfragmented and <= 125 bytes.
            if (opcode == kOpPing || opcode == kOpPong || opcode == kOpClose) {
                if (rsv1) {
                    reportError_(kErrorProtocolError);
                    close(CloseCodeProtocolError);
                    return;
                }
                if (!fin || payload.size() > 125) {
                    reportError_(kErrorProtocolError);
                    close(CloseCodeProtocolError);
                    return;
                }
            }

            // Control frames must be unfragmented and <= 125 bytes; parseOneFrame already ensured length <= available.
            if (opcode == kOpPing) {
                // Respond immediately with pong.
                sendControlFrame(kOpPong, payload);
                continue;
            }
            if (opcode == kOpPong) {
                uint64_t elapsedMs = 0;
                const auto now = std::chrono::steady_clock::now();
                if (!m_pendingPings.isEmpty()) {
                    bool found = false;
                    for (size_t i = 0; i < m_pendingPings.size(); ++i) {
                        if (m_pendingPings[i].payload == payload) {
                            elapsedMs = static_cast<uint64_t>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pendingPings[i].sentAt).count());
                            m_pendingPings.removeAt(i);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        elapsedMs = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pendingPings[0].sentAt).count());
                        m_pendingPings.removeAt(0);
                    }
                }
                emit pong(elapsedMs, payload);
                continue;
            }
            if (opcode == kOpClose) {
                m_closeReceived = true;
                uint16_t closeCode = static_cast<uint16_t>(CloseCodeNoStatusRcvd);
                SwString closeReason;
                bool sentCloseReply = false;

                if (payload.size() == 1) {
                    closeCode = static_cast<uint16_t>(CloseCodeProtocolError);
                    closeReason = "";
                    reportError_(kErrorProtocolError);
                    if (!m_closeSent) {
                        emitAboutToCloseOnce_();
                        setState(SwAbstractSocket::ClosingState);
                        sendCloseFrame(static_cast<uint16_t>(CloseCodeProtocolError), SwString());
                        m_closeSent = true;
                        sentCloseReply = true;
                    }
                } else if (payload.size() >= 2) {
                    closeCode = (static_cast<uint16_t>(static_cast<unsigned char>(payload[0])) << 8) |
                                static_cast<uint16_t>(static_cast<unsigned char>(payload[1]));
                    SwByteArray reasonBytes = payload.mid(2);
                    if (!isValidCloseCode_(closeCode)) {
                        reportError_(kErrorProtocolError);
                        closeCode = static_cast<uint16_t>(CloseCodeProtocolError);
                        closeReason = "";
                        if (!m_closeSent) {
                            emitAboutToCloseOnce_();
                            setState(SwAbstractSocket::ClosingState);
                            sendCloseFrame(static_cast<uint16_t>(CloseCodeProtocolError), SwString());
                            m_closeSent = true;
                            sentCloseReply = true;
                        }
                    } else if (!reasonBytes.isEmpty() && !isValidUtf8_(reasonBytes)) {
                        reportError_(kErrorInvalidUtf8);
                        closeCode = static_cast<uint16_t>(CloseCodeInvalidPayload);
                        closeReason = "";
                        if (!m_closeSent) {
                            emitAboutToCloseOnce_();
                            setState(SwAbstractSocket::ClosingState);
                            sendCloseFrame(static_cast<uint16_t>(CloseCodeInvalidPayload), SwString());
                            m_closeSent = true;
                            sentCloseReply = true;
                        }
                    } else {
                        closeReason = SwString(reasonBytes);
                    }
                }

                m_closeCode = closeCode;
                m_closeReason = closeReason;

                if (!m_closeSent) {
                    // Echo close payload as per RFC.
                    emitAboutToCloseOnce_();
                    setState(SwAbstractSocket::ClosingState);
                    sendControlFrame(kOpClose, payload);
                    m_closeSent = true;
                    sentCloseReply = true;
                }
                stopCloseTimer_();
                if (sentCloseReply) {
                    // Give the close reply a chance to flush before closing the TCP connection.
                    startCloseReplyTimer_();
                    return;
                }
                cleanupSocket();
                resetProtocolState(true);
                m_handshakeStage = StageIdle;
                setState(SwAbstractSocket::UnconnectedState);
                emit disconnected();
                return;
            }

            if (opcode == kOpContinuation) {
                if (rsv1) {
                    reportError_(kErrorProtocolError);
                    close(CloseCodeProtocolError);
                    return;
                }
                if (m_messageOpcode < 0) {
                    reportError_(kErrorProtocolError);
                    abort();
                    return;
                }
                if (!payload.isEmpty()) {
                    m_messageBuffer.append(payload.constData(), payload.size());
                }
                if (m_maxIncomingMessageSize > 0 && m_messageBuffer.size() > m_maxIncomingMessageSize) {
                    reportError_(kErrorMessageTooBig);
                    close(CloseCodeMessageTooBig);
                    return;
                }
                if (fin) {
                    if (!finalizeMessage_()) {
                        return;
                    }
                }
                continue;
            }

            if (opcode == kOpText || opcode == kOpBinary) {
                if (m_messageOpcode >= 0) {
                    reportError_(kErrorProtocolError);
                    abort();
                    return;
                }

                if (fin) {
                    if (m_maxIncomingMessageSize > 0 && payload.size() > m_maxIncomingMessageSize) {
                        reportError_(kErrorMessageTooBig);
                        close(CloseCodeMessageTooBig);
                        return;
                    }

                    SwByteArray messagePayload = payload;
                    if (rsv1) {
                        if (!m_pmd.negotiated) {
                            reportError_(kErrorProtocolError);
                            close(CloseCodeProtocolError);
                            return;
                        }
                        SwByteArray inflated;
                        if (!inflateMessage_(payload, inflated)) {
                            reportError_(kErrorProtocolError);
                            close(CloseCodeProtocolError);
                            return;
                        }
                        messagePayload = inflated;
                    }

                    if (opcode == kOpText) {
                        if (!isValidUtf8_(messagePayload)) {
                            reportError_(kErrorInvalidUtf8);
                            close(CloseCodeInvalidPayload);
                            return;
                        }
                        emit textMessageReceived(SwString(messagePayload));
                    } else {
                        emit binaryMessageReceived(messagePayload);
                    }
                } else {
                    m_messageOpcode = static_cast<int>(opcode);
                    m_messageBuffer = payload;
                    m_currentMessageCompressed = rsv1;
                    if (m_maxIncomingMessageSize > 0 && m_messageBuffer.size() > m_maxIncomingMessageSize) {
                        reportError_(kErrorMessageTooBig);
                        close(CloseCodeMessageTooBig);
                        return;
                    }
                }
                continue;
            }

            // Unknown opcode
            reportError_(kErrorProtocolError);
            close(CloseCodeProtocolError);
            return;
        }
    }

    bool finalizeMessage_() {
        SwByteArray payload = m_messageBuffer;
        if (m_currentMessageCompressed) {
            SwByteArray inflated;
            if (!inflateMessage_(m_messageBuffer, inflated)) {
                reportError_(kErrorProtocolError);
                close(CloseCodeProtocolError);
                return false;
            }
            payload = inflated;
        }

        if (m_messageOpcode == kOpText) {
            if (!isValidUtf8_(payload)) {
                reportError_(kErrorInvalidUtf8);
                close(CloseCodeInvalidPayload);
                return false;
            }
            emit textMessageReceived(SwString(payload));
        } else if (m_messageOpcode == kOpBinary) {
            emit binaryMessageReceived(payload);
        }
        m_messageOpcode = -1;
        m_messageBuffer.clear();
        m_currentMessageCompressed = false;
        return true;
    }

private:
    SwAbstractSocket* m_socket = nullptr;
    SwSslSocket* m_sslSocket = nullptr;
    SwString m_trustedCaFile;

    SwString m_requestUrl;
    SwString m_scheme;
    SwString m_host;
    uint16_t m_port = 0;
    SwString m_path;
    bool m_secure = false;

    HandshakeStage m_handshakeStage = StageIdle;

    ProxySettings m_proxy;
    bool m_followRedirects = true;
    int m_maxRedirects = 5;
    int m_redirectCount = 0;

    Role m_role = ClientRole;
    SwList<SwString> m_supportedSubprotocols;

    SwString m_origin;
    SwList<SwString> m_requestedSubprotocols;
    SwString m_acceptedSubprotocol;
    SwString m_acceptedExtensions;
    SwMap<SwString, SwString> m_requestHeaderMap;

    SwAbstractSocket::SocketState m_state = SwAbstractSocket::UnconnectedState;

    SwByteArray m_buffer;
    bool m_handshakeDone = false;
    SwString m_clientKeyBase64;
    SwString m_expectedAccept;

    int m_messageOpcode = -1; // text/binary opcode for fragmented messages
    SwByteArray m_messageBuffer;
    bool m_currentMessageCompressed = false;

    PerMessageDeflateState m_pmd;

    uint64_t m_maxIncomingMessageSize = 16ULL * 1024ULL * 1024ULL; // 16 MiB default

    int m_closeTimeoutMs = 3000;
    SwTimer* m_closeTimer = nullptr;
    int m_closeReplyDelayMs = 200;
    SwTimer* m_closeReplyTimer = nullptr;
    int m_lastError = 0;
    SwString m_lastErrorText;

    bool m_closeSent = false;
    bool m_closeReceived = false;
    bool m_aboutToCloseEmitted = false;

    uint16_t m_closeCode = static_cast<uint16_t>(CloseCodeNoStatusRcvd);
    SwString m_closeReason;

    SwList<PendingPing> m_pendingPings;
};
