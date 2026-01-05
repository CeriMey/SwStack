#pragma once
#include "HttpRouteHandler.h"
#include "SwList.h"

class HttpRouter {
public:
    void addHandler(HttpRouteHandler* handler) { m_handlers.append(handler); }

    bool route(const HttpRequest& req, SwTcpSocket* socket) {
        for (size_t i = 0; i < m_handlers.size(); ++i) {
            auto* h = m_handlers[i];
            if (h && h->canHandle(req)) {
                h->handle(req, socket);
                return true;
            }
        }
        return false;
    }

private:
    SwList<HttpRouteHandler*> m_handlers;
};
