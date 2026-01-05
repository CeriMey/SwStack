#pragma once
#include "SwObject.h"
#include "SwTcpSocket.h"
#include "http/HttpRouter.h"
#include "http/HttpMessage.h"
#include "SwDebug.h"

#include <sstream>
#include <iostream>
#include <functional>
#include <vector>

class HttpSession : public SwObject {
    SW_OBJECT(HttpSession, SwObject)
public:
    HttpSession(SwTcpSocket* socket, HttpRouter* router, SwObject* parent = nullptr)
        : SwObject(parent), m_socket(socket), m_router(router)
    {
        if (!m_socket || !m_router) {
            deleteLater();
            return;
        }
        m_socket->setParent(this);
        connect(m_socket, SIGNAL(readyRead), this, &HttpSession::onReadyRead);
        connect(m_socket, SIGNAL(disconnected), this, &HttpSession::cleanup);
        connect(m_socket, SIGNAL(errorOccurred), this, &HttpSession::cleanupError);
    }

    void setFinishedCallback(const std::function<void(HttpSession*)>& cb) { m_onFinished = cb; }
    void addCleanupHook(const std::function<void()>& hook) { m_cleanupHooks.push_back(hook); }
    void closeSession() { cleanup(); }

private slots:
    void onReadyRead() {
        if (!m_socket) return;
        SwString chunk = m_socket->read();
        if (chunk.isEmpty()) return;
        m_buffer.append(chunk);

        int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;

        SwString rawHeaders = m_buffer.left(headerEnd);
        m_buffer.erase(0, static_cast<size_t>(headerEnd + 4));

        HttpRequest req;
        size_t contentLength = 0;
        if (!parseRequest(rawHeaders, req, contentLength)) {
            sendSimple(400, SwString("Bad Request"));
            return;
        }
        if (contentLength > m_buffer.size()) {
            // wait for body completion
            return;
        }
        if (contentLength > 0) {
            req.body = m_buffer.left(static_cast<int>(contentLength));
            m_buffer.erase(0, contentLength);
        }

        bool routed = m_router->route(req, m_socket);
        if (!routed) {
            sendSimple(404, SwString("Not Found"));
            return;
        }
    }

    void cleanup() {
        if (m_cleaned) return;
        m_cleaned = true;
        swDebug() << "[HttpSession] cleanup start";
        for (const auto& hook : m_cleanupHooks) {
            try { hook(); } catch (...) {}
        }
        m_cleanupHooks.clear();
        if (m_socket) {
            m_socket->disconnectAllSlots();
            m_socket->deleteLater();
            m_socket = nullptr;
        }
        auto cb = m_onFinished;
        m_onFinished = nullptr;
        if (cb) {
            cb(this);
        } else {
            deleteLater();
        }
    }

    void cleanupError(int) {
        cleanup();
    }

private:
    bool parseRequest(const SwString& rawHeaders, HttpRequest& out, size_t& contentLength) {
        contentLength = 0;
        SwList<SwString> lines = rawHeaders.split("\r\n");
        if (lines.isEmpty()) return false;
        SwList<SwString> first = lines[0].split(' ');
        if (first.size() < 3) return false;
        out.method = first[0];
        out.path = first[1];
        out.protocol = first[2];
        out.headers.clear();
        for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
            SwString line = lines[i];
            if (line.isEmpty()) continue;
            int colon = line.indexOf(":");
            if (colon < 0) continue;
            SwString key = line.left(colon).toLower();
            SwString value = line.mid(colon + 1).trimmed();
            out.headers[key] = value;
        }
        if (out.headers.contains("content-length")) {
            int len = out.headers["content-length"].toInt();
            if (len > 0) contentLength = static_cast<size_t>(len);
        }
        // Force close after each response to simplify
        out.keepAlive = false;
        return true;
    }

    void sendSimple(int status, const SwString& reason) {
        if (!m_socket) return;
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << reason.toStdString() << "\r\n";
        oss << "Content-Length: 0\r\nConnection: close\r\n\r\n";
        m_socket->write(SwString(oss.str()));
        m_socket->shutdownWrite();
        m_socket->waitForBytesWritten(3000);
        m_socket->close();
        m_socket = nullptr;
        cleanup();
    }

    SwTcpSocket* m_socket = nullptr;
    HttpRouter* m_router = nullptr;
    SwString m_buffer;
    std::function<void(HttpSession*)> m_onFinished;
    bool m_cleaned = false;
    std::vector<std::function<void()>> m_cleanupHooks;
};
