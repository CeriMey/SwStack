#pragma once
#include "HttpRouter.h"
#include "HttpRouteHandler.h"
#include "../HttpSession.h"
#include "SwTcpServer.h"
#include "SwTcpSocket.h"
#include "SwObject.h"
#include "SwList.h"
#include "SwDebug.h"
#include <cstdint>

class HttpServer : public SwObject {
    SW_OBJECT(HttpServer, SwObject)
public:
    explicit HttpServer(SwObject* parent = nullptr) : SwObject(parent) {}

    void addHandler(HttpRouteHandler* handler) {
        m_router.addHandler(handler);
    }

    bool listen(uint16_t port) {
        if (!m_server.listen(port)) {
            swError() << "[HttpServer] Unable to listen on port " << port;
            return false;
        }
        SwObject::connect(&m_server, SIGNAL(newConnection), [this]() {
            SwTcpSocket* sock = nullptr;
            while ((sock = m_server.nextPendingConnection()) != nullptr) {
                swDebug() << "[HttpServer] accepted new TCP client";
                HttpSession* session = new HttpSession(sock, &m_router, this);
                m_sessions.append(session);
                session->setFinishedCallback([this](HttpSession* s) {
                    swDebug() << "[HttpServer] session finished, cleaning";
                    SwList<HttpSession*> next;
                    for (size_t i = 0; i < m_sessions.size(); ++i) {
                        if (m_sessions[i] != s) next.append(m_sessions[i]);
                    }
                    m_sessions = next;
                    s->deleteLater();
                });
            }
        });
        return true;
    }

private:
    HttpRouter m_router;
    SwTcpServer m_server;
    SwList<HttpSession*> m_sessions;
};
