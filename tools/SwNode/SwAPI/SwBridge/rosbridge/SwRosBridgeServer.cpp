#include "SwRosBridgeServer.h"
#include "SwRosBridgeSession.h"
#include "http/SwHttpTypes.h"

namespace swros {
Server::Server(const SwString& domain, const SwString& mapping, const SwString& apiKey,
               SwBridgeRpcClient& rpc, SwObject* parent)
    : SwObject(parent), catalog_(domain), server_(this), apiKey_(apiKey), rpc_(rpc) {
    catalog_.load(mapping);
    server_.setMaxConnections(64);
    connect(&server_, &SwWebSocketServer::newConnection, this, [this] {
        while (auto* socket = server_.nextPendingConnection()) {
            // Browser WebSocket clients cannot set HTTP Authorization headers.
            if (!apiKey_.isEmpty()) {
                const SwString path = socket->requestPath();
                const int question = path.indexOf('?');
                bool authorized = false;
                if (question >= 0) {
                    for (const auto& field : path.mid(question + 1).split('&')) {
                        const int equal = field.indexOf('=');
                        SwString decoded;
                        if (equal >= 0 && field.left(equal) == "api_key" &&
                            swHttpPercentDecode(field.mid(equal + 1), decoded, true) && decoded == apiKey_) authorized = true;
                    }
                }
                if (!authorized) {
                    socket->close(SwWebSocket::CloseCodePolicyViolation, "API key required");
                    continue;
                }
            }
            new Session(socket, catalog_, rpc_, this);
        }
    });
}
Server::~Server() { server_.close(); }
}
