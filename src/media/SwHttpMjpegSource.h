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

#pragma once

/***************************************************************************************************
 * Simple MJPEG-over-HTTP video source (multipart/x-mixed-replace).
 *
 * Windows-only: uses GDI+ to decode JPEG frames to BGRA and emits RawBGRA SwVideoPackets.
 **************************************************************************************************/

#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "media/SwVideoDecoder.h"
#include "core/io/SwTcpSocket.h"
#include "core/runtime/SwTimer.h"
#include "SwDebug.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <gdiplus.h>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
static constexpr const char* kSwLogCategory_SwHttpMjpegSource = "sw.media.swhttpmjpegsource";


#pragma comment(lib, "gdiplus.lib")

class SwHttpMjpegSource : public SwVideoSource {
public:
    explicit SwHttpMjpegSource(const SwString& url, SwObject* parent = nullptr)
        : m_parent(parent), m_url(url) {
        parseUrl();
        m_socket = new SwTcpSocket(m_parent);
        m_pollTimer = new SwTimer(10, m_parent);
        m_reconnectTimer = new SwTimer(1000, m_parent);
        SwObject::connect(m_pollTimer, &SwTimer::timeout, [this]() { poll(); });
        SwObject::connect(m_reconnectTimer, &SwTimer::timeout, [this]() { attemptReconnect(); });
        SwObject::connect(m_socket, SIGNAL(connected), [this]() {
            m_requestSent = true;
            m_connecting.store(false);
            m_lastFrameTime = {};
            m_lastDataTime = std::chrono::steady_clock::now();
            m_placeholderSent.store(false);
            if (m_reconnectTimer) {
                m_reconnectTimer->stop();
            }
            sendRequest();
            swCDebug(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Connected, request sent";
        });
        SwObject::connect(m_socket, SIGNAL(disconnected), [this]() {
            swCWarning(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Disconnected";
            handleDisconnect();
        });
        SwObject::connect(m_socket, SIGNAL(errorOccurred), [this](int code) {
            swCError(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Socket error: " << code;
            handleDisconnect();
        });
    }

    ~SwHttpMjpegSource() override {
        stop();
        delete m_pollTimer;
        delete m_socket;
    }

    std::string name() const override { return "SwHttpMjpegSource"; }

    bool initialize() { return true; }

    void start() override {
        if (isRunning()) {
            return;
        }
        m_stopping.store(false);
        m_connecting.store(false);
        m_placeholderSent.store(false);
        m_lastFrameTime = {};
        m_lastDataTime = {};
        m_requestSent = false;
        setRunning(true);
        m_pollTimer->start();
        attemptReconnect();
    }

    void stop() override {
        if (m_stopping.exchange(true)) {
            return;
        }
        setRunning(false);
        m_requestSent = false;
        if (m_pollTimer) {
            m_pollTimer->stop();
        }
        if (m_reconnectTimer) {
            m_reconnectTimer->stop();
        }
        if (m_socket) {
            m_socket->close();
        }
        {
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_buffer.clear();
            m_boundary.clear();
            m_headersParsed = false;
            m_contentType.clear();
        }
        m_lastFrameTime = {};
        m_lastDataTime = {};
        m_stopping.store(false);
    }

private:
    void parseUrl() {
        // Very small parser: expects http://host[:port]/path
        std::string s = m_url.toStdString();
        const std::string prefix = "http://";
        if (s.compare(0, prefix.size(), prefix) == 0) {
            s = s.substr(prefix.size());
        }
        std::string hostPort;
        auto slash = s.find('/');
        if (slash == std::string::npos) {
            hostPort = s;
            m_path = "/";
        } else {
            hostPort = s.substr(0, slash);
            m_path = s.substr(slash);
        }
        auto colon = hostPort.find(':');
        if (colon != std::string::npos) {
            m_host = SwString(hostPort.substr(0, colon));
            m_port = std::stoi(hostPort.substr(colon + 1));
        } else {
            m_host = SwString(hostPort);
            m_port = 80;
        }
        if (m_path.isEmpty()) {
            m_path = "/";
        }
    }

    void sendRequest() {
        std::ostringstream oss;
        oss << "GET " << m_path.toStdString() << " HTTP/1.1\r\n"
            << "Host: " << m_host.toStdString() << "\r\n"
            << "Connection: close\r\n"
            << "User-Agent: SwHttpMjpegSource/1.0\r\n\r\n";
        m_socket->write(SwString(oss.str()));
    }

    void poll() {
        if (!isRunning() || !m_socket) {
            return;
        }
        // Read everything available (non-blocking)
        for (;;) {
            SwString chunk = m_socket->read(8192);
            if (chunk.isEmpty()) {
                break;
            }
            m_lastDataTime = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(m_bufferMutex);
            m_buffer.append(chunk.toStdString());
        }
        if (!m_headersParsed && m_buffer.size() > 0) {
            swCDebug(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Buffering headers, size=" << m_buffer.size();
        }
        processBuffer();
        checkFrameTimeout();
    }

    void processBuffer() {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (!m_headersParsed) {
            auto pos = m_buffer.find("\r\n\r\n");
            if (pos == std::string::npos) {
                return;
            }
            std::string header = m_buffer.substr(0, pos);
            m_buffer.erase(0, pos + 4);
            if (!parseMainHeaders(header)) {
                swCError(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Failed to parse headers";
                return;
            }
            m_boundary = extractBoundary(header);
            if (m_boundary.empty()) {
                m_boundary = "--frame";
            }
            swCDebug(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Parsed boundary: " << m_boundary;
            m_headersParsed = true;
        }

        const std::string boundaryMarker = m_boundary;
        for (;;) {
            // Align on boundary
            auto start = m_buffer.find(boundaryMarker);
            if (start == std::string::npos) {
                // keep buffer small
                if (m_buffer.size() > 1024 * 1024) {
                    m_buffer.erase(0, m_buffer.size() - 1024);
                }
                return;
            }
            if (start > 0) {
                m_buffer.erase(0, start);
            }
            if (m_buffer.size() < boundaryMarker.size() + 4) {
                return;
            }
            size_t headerStart = boundaryMarker.size();
            if (m_buffer.compare(boundaryMarker.size(), 2, "\r\n") == 0) {
                headerStart += 2;
            }
            auto headerEnd = m_buffer.find("\r\n\r\n", headerStart);
            if (headerEnd == std::string::npos) {
                return;
            }
            std::string partHeader = m_buffer.substr(headerStart, headerEnd - headerStart);
            size_t payloadStart = headerEnd + 4;

            size_t nextBoundary = m_buffer.find(boundaryMarker, payloadStart);
            if (nextBoundary == std::string::npos) {
                return;
            }
            size_t payloadEnd = nextBoundary;
            // Trim trailing CRLF before boundary if present
            if (payloadEnd >= 2 && m_buffer.substr(payloadEnd - 2, 2) == "\r\n") {
                payloadEnd -= 2;
            }
            std::string payload = m_buffer.substr(payloadStart, payloadEnd - payloadStart);

            m_buffer.erase(0, nextBoundary);

            if (payload.empty()) {
                continue;
            }
            std::string partContentType = partHeader;
            std::transform(partContentType.begin(),
                           partContentType.end(),
                           partContentType.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (partContentType.find("image/jpeg") == std::string::npos &&
                m_contentType.find("image/jpeg") == std::string::npos) {
                swCWarning(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Skipping non-JPEG part";
                continue; // unsupported inner part
            }
            decodeAndEmit(payload);
        }
    }

    bool parseMainHeaders(const std::string& header) {
        // Basic status check
        auto crlf = header.find("\r\n");
        std::string statusLine = (crlf != std::string::npos) ? header.substr(0, crlf) : header;
        if (statusLine.find("200") == std::string::npos) {
            swCError(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] HTTP status not OK: " << statusLine;
            return false;
        }
        // Capture content-type
        std::istringstream iss(header);
        std::string line;
        while (std::getline(iss, line)) {
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (lower.find("content-type:") != std::string::npos) {
                m_contentType = lower;
            }
        }
        if (m_contentType.find("multipart/x-mixed-replace") == std::string::npos) {
            swCError(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Unsupported content-type: " << m_contentType;
            return false;
        }
        swCDebug(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Content-Type: " << m_contentType;
        return true;
    }

    std::string extractBoundary(const std::string& header) {
        std::string ctKey = "content-type:";
        std::istringstream iss(header);
        std::string line;
        while (std::getline(iss, line)) {
            // normalize to lower-case
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            auto pos = lower.find(ctKey);
            if (pos != std::string::npos) {
                auto bpos = lower.find("boundary=", pos);
                if (bpos != std::string::npos) {
                    std::string b = line.substr(bpos + 9);
                    // strip quotes and whitespace
                    b.erase(std::remove_if(b.begin(), b.end(), [](char c) { return c == '"' || c == '\r' || c == '\n' || c == ' ' || c == '\t'; }), b.end());
                    if (b.size() >= 2 && b[0] != '-' && b[1] != '-') {
                        b = "--" + b;
                    } else if (b.size() == 1 && b[0] != '-') {
                        b = "--" + b;
                    }
                    return b;
                }
            }
        }
        return "";
    }

    void decodeAndEmit(const std::string& jpeg) {
        if (jpeg.empty()) {
            return;
        }
        IStream* stream = nullptr;
        HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, jpeg.size());
        if (!hMem) {
            return;
        }
        void* ptr = ::GlobalLock(hMem);
        if (!ptr) {
            ::GlobalFree(hMem);
            return;
        }
        std::memcpy(ptr, jpeg.data(), jpeg.size());
        ::GlobalUnlock(hMem);
        if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
            ::GlobalFree(hMem);
            return;
        }

        Gdiplus::Bitmap bitmap(stream);
        stream->Release();
        if (bitmap.GetLastStatus() != Gdiplus::Ok) {
            swCError(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Failed to decode JPEG";
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const int width = static_cast<int>(bitmap.GetWidth());
        const int height = static_cast<int>(bitmap.GetHeight());
        if (width <= 0 || height <= 0) {
            return;
        }

        Gdiplus::Rect rect(0, 0, width, height);
        Gdiplus::BitmapData data{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
            swCError(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] LockBits failed";
            return;
        }

        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (frame.isValid()) {
            const uint8_t* src = reinterpret_cast<const uint8_t*>(data.Scan0);
            const int srcStride = static_cast<int>(data.Stride);
            uint8_t* dst = frame.planeData(0);
            const int dstStride = frame.planeStride(0);
            const int rowBytes = width * 4;
            for (int y = 0; y < height; ++y) {
                std::memcpy(dst + y * dstStride, src + y * srcStride, rowBytes);
            }
            SwByteArray payload(reinterpret_cast<const char*>(frame.planeData(0)),
                                static_cast<size_t>(dstStride * height));
            SwVideoPacket packet(SwVideoPacket::Codec::RawBGRA,
                                 payload,
                                 0,
                                 0,
                                 true);
            packet.setRawFormat(SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, width, height));
            emitPacket(packet);
            auto count = ++m_framesDecoded;
            m_lastFrameTime = now;
            m_lastDataTime = now;
            m_placeholderSent.store(false);
            if (count <= 3 || (count % 100) == 0) {
                swCDebug(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Decoded frame " << count
                          << " size=" << width << "x" << height
                          << " payload=" << payload.size() << " bytes";
            }
        }
        bitmap.UnlockBits(&data);
    }

    SwObject* m_parent{nullptr};
    SwString m_url;
    SwString m_host;
    int m_port{80};
    SwString m_path{"/"};

    SwTcpSocket* m_socket{nullptr};
    SwTimer* m_pollTimer{nullptr};
    SwTimer* m_reconnectTimer{nullptr};

    std::mutex m_bufferMutex;
    std::string m_buffer;
    std::string m_boundary;
    std::string m_contentType;
    bool m_headersParsed{false};
    bool m_requestSent{false};
    std::atomic<int64_t> m_framesDecoded{0};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_connecting{false};
    std::atomic<bool> m_placeholderSent{false};
    std::chrono::steady_clock::time_point m_lastDataTime{};
    std::chrono::steady_clock::time_point m_lastFrameTime{};

    void handleDisconnect() {
        m_requestSent = false;
        m_connecting.store(false);
        m_lastFrameTime = {};
        m_lastDataTime = {};
        emitPlaceholder(true);
        scheduleReconnect();
    }

    void scheduleReconnect() {
        if (!isRunning() || m_stopping.load()) {
            return;
        }
        if (m_reconnectTimer && !m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(1000);
        }
    }

    void attemptReconnect() {
        if (!isRunning() || m_stopping.load()) {
            return;
        }
        if (m_connecting.exchange(true)) {
            return;
        }
        m_headersParsed = false;
        m_boundary.clear();
        m_contentType.clear();
        m_buffer.clear();
        m_requestSent = false;
        m_socket->close();
        if (!m_socket->connectToHost(m_host, static_cast<uint16_t>(m_port))) {
            swCWarning(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] reconnect failed, retrying...";
            m_connecting.store(false);
            scheduleReconnect();
            return;
        }
        if (m_socket->isOpen()) {
            m_requestSent = true;
            sendRequest();
            swCDebug(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] Reconnected immediately, request sent";
            m_connecting.store(false);
            if (m_reconnectTimer) {
                m_reconnectTimer->stop();
            }
        }
    }

    void checkFrameTimeout() {
        if (!isRunning()) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        constexpr int64_t kNoDataTimeoutMs = 1000;
        constexpr int64_t kNoFrameTimeoutMs = 2000;

        if (m_socket && m_socket->isOpen() && m_lastDataTime.time_since_epoch().count() > 0) {
            auto noDataMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDataTime).count();
            if (noDataMs > kNoDataTimeoutMs && !m_placeholderSent.exchange(true)) {
                swCWarning(kSwLogCategory_SwHttpMjpegSource) << "[SwHttpMjpegSource] No data for " << noDataMs << " ms, emitting placeholder";
                emitPlaceholder(false);
                return;
            }
        }
        if (m_lastFrameTime.time_since_epoch().count() == 0) {
            if (!m_placeholderSent.load() && (!m_socket || !m_socket->isOpen())) {
                emitPlaceholder(false);
            }
            return;
        }
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFrameTime).count();
        if (elapsedMs > kNoFrameTimeoutMs && !m_placeholderSent.exchange(true)) {
            emitPlaceholder(false);
        }
    }

    void emitPlaceholder(bool serverDown) {
        const int width = 640;
        const int height = 360;
        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            return;
        }
        uint8_t* dst = frame.planeData(0);
        const int stride = frame.planeStride(0);
        if (!dst || stride <= 0) {
            return;
        }
        std::array<uint32_t, 7> bars = {
            0xFFFF0000u, // Blue (BGRA)
            0xFF00FF00u, // Green
            0xFF0000FFu, // Red
            0xFFFFFF00u, // Cyan
            0xFFFF00FFu, // Magenta
            0xFF00FFFFu, // Yellow
            serverDown ? 0xFF2222AAu : 0xFF444444u // Server down or waiting
        };
        const int barWidth = width / static_cast<int>(bars.size());
        for (int y = 0; y < height; ++y) {
            uint32_t* row = reinterpret_cast<uint32_t*>(dst + y * stride);
            for (int x = 0; x < width; ++x) {
                int idx = std::min(static_cast<int>(bars.size() - 1), x / barWidth);
                row[x] = bars[idx];
            }
        }
        SwByteArray payload(reinterpret_cast<const char*>(frame.planeData(0)),
                            static_cast<size_t>(stride * height));
        SwVideoPacket packet(SwVideoPacket::Codec::RawBGRA, payload, 0, 0, true);
        packet.setRawFormat(SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, width, height));
        emitPacket(packet);
        m_placeholderSent.store(true);
    }
};
