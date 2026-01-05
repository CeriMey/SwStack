#pragma once
#include "http/HttpRouteHandler.h"
#include "SwTcpSocket.h"
#include <fstream>
#include <sstream>

class StaticRouteHandler : public HttpRouteHandler {
public:
    explicit StaticRouteHandler(const SwString& root = SwString("www")) : m_root(root) {}

    bool canHandle(const HttpRequest& req) const override {
        if (req.path == "/" || req.path == "/index.html") {
            return true;
        }
        return req.path.startsWith("/static/");
    }

    void handle(const HttpRequest& req, SwTcpSocket* socket) override {
        if (req.path == "/" || req.path == "/index.html") {
            serve(socket, m_root, SwString("index.html"));
            return;
        }
        SwString rel = req.path.mid(8);
        serve(socket, m_root, rel);
    }

private:
    SwString m_root;

    static SwString guessContentType(const SwString& path) {
        if (path.endsWith(".html")) return SwString("text/html");
        if (path.endsWith(".css")) return SwString("text/css");
        if (path.endsWith(".js")) return SwString("application/javascript");
        if (path.endsWith(".png")) return SwString("image/png");
        if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return SwString("image/jpeg");
        return SwString("application/octet-stream");
    }

    static void sendResponse(SwTcpSocket* socket, int status, const SwString& reason, const SwString& body, const SwString& contentType) {
        if (!socket) return;
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << ' ' << reason.toStdString() << "\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Content-Type: " << contentType.toStdString() << "\r\n";
        oss << "Connection: close\r\n\r\n";
        socket->write(SwString(oss.str()));
        if (!body.isEmpty()) {
            socket->write(body);
        }
        socket->shutdownWrite();
        socket->waitForBytesWritten(3000);
        socket->close();
    }

    static void sendError(SwTcpSocket* socket, int status, const SwString& message) {
        sendResponse(socket, status, message, SwString(), SwString("text/plain"));
    }

    static void serve(SwTcpSocket* socket, const SwString& rootDir, const SwString& relativePath) {
        SwString sanitized = relativePath;
        while (sanitized.contains("..")) {
            sanitized.replace("..", "");
        }
        SwString fullPath = rootDir + "/" + sanitized;
        std::ifstream file(fullPath.toStdString().c_str(), std::ios::binary);
        if (!file) {
            sendError(socket, 404, SwString("Not Found"));
            return;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        SwString data(buffer.str());
        SwString contentType = guessContentType(fullPath);
        sendResponse(socket, 200, SwString("OK"), data, contentType);
    }
};
