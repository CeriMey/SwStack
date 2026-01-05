#pragma once
#include "HttpMessage.h"
#include "SwTcpSocket.h"

class HttpRouteHandler {
public:
    virtual ~HttpRouteHandler() = default;
    virtual bool canHandle(const HttpRequest& req) const = 0;
    virtual void handle(const HttpRequest& req, SwTcpSocket* socket) = 0;
};
