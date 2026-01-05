#pragma once
#include "http/HttpRouteHandler.h"
#include "../MapDatabase.h"
#include "SwJsonDocument.h"
#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include <sstream>

class MapApiRouteHandler : public HttpRouteHandler {
public:
    bool canHandle(const HttpRequest& req) const override {
        return req.path.startsWith("/api/maps");
    }

    void handle(const HttpRequest& req, SwTcpSocket* socket) override {
        if (!socket) return;
        if (req.method.toLower() == "get") {
            sendList(socket);
            return;
        }
        if (req.method.toLower() == "post" && (req.path == "/api/maps" || req.path == "/api/maps/")) {
            handleCreate(req, socket);
            return;
        }
        if (req.method.toLower() == "delete") {
            handleDelete(req, socket);
            return;
        }
        sendPlain(socket, 405, SwString("Method Not Allowed"), SwString(), true);
    }

private:
    void sendList(SwTcpSocket* socket) {
        SwJsonArray arr;
        SwList<MapEntry> entries = MapDatabase::instance()->all();
        for (size_t i = 0; i < entries.size(); ++i) {
            SwJsonObject obj;
            obj["name"] = SwJsonValue(entries[i].name.toStdString());
            obj["url"] = SwJsonValue(entries[i].url.toStdString());
            arr.append(obj);
        }
        SwJsonDocument doc(arr);
        SwString body = doc.toJson(SwJsonDocument::JsonFormat::Pretty);
        sendPlain(socket, 200, SwString("OK"), body, false, SwString("application/json"));
    }

    void handleCreate(const HttpRequest& req, SwTcpSocket* socket) {
        SwString err;
        SwJsonDocument doc = SwJsonDocument::fromJson(req.body.toStdString(), err);
        if (!err.isEmpty() || !doc.isObject()) {
            sendPlain(socket, 400, SwString("Bad Request"), SwString(), true);
            return;
        }
        SwJsonObject obj = doc.object();
        if (!obj.contains("name") || !obj.contains("url")) {
            sendPlain(socket, 400, SwString("Bad Request"), SwString(), true);
            return;
        }
        SwString name = SwString(obj["name"].toString()).trimmed().toLower();
        SwString url = SwString(obj["url"].toString()).trimmed();
        if (name.isEmpty() || url.isEmpty()) {
            sendPlain(socket, 400, SwString("Bad Request"), SwString(), true);
            return;
        }
        MapDatabase::instance()->addOrUpdate(name, url);
        sendPlain(socket, 201, SwString("Created"), SwString(), false);
    }

    void handleDelete(const HttpRequest& req, SwTcpSocket* socket) {
        // expected /api/maps/{name}
        SwList<SwString> parts = req.path.split('/');
        SwList<SwString> tokens;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (!parts[i].isEmpty()) tokens.append(parts[i]);
        }
        if (tokens.size() < 3) {
            sendPlain(socket, 400, SwString("Bad Request"), SwString(), true);
            return;
        }
        SwString name = tokens[2].trimmed().toLower();
        if (!MapDatabase::instance()->contains(name)) {
            sendPlain(socket, 404, SwString("Not Found"), SwString(), true);
            return;
        }
        MapDatabase::instance()->remove(name);
        sendPlain(socket, 200, SwString("OK"), SwString(), false);
    }

    void sendPlain(SwTcpSocket* socket, int status, const SwString& reason, const SwString& body, bool closeAfter, const SwString& contentType = SwString("text/plain")) {
        if (!socket) return;
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << reason.toStdString() << "\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Content-Type: " << contentType.toStdString() << "\r\n";
        if (closeAfter) oss << "Connection: close\r\n\r\n";
        else oss << "Connection: keep-alive\r\n\r\n";
        socket->write(SwString(oss.str()));
        if (!body.isEmpty()) socket->write(body);
        socket->waitForBytesWritten(1000);
        if (closeAfter) {
            socket->shutdownWrite();
            socket->close();
        }
    }
};
