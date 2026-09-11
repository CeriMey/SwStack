#pragma once
#include "SwRosBridgeCatalog.h"
#include "SwWebSocketServer.h"
class SwBridgeRpcClient;
namespace swros {
class Server : public SwObject {
public:
    Server(const SwString& domain, const SwString& mapping, const SwString& apiKey,
           SwBridgeRpcClient& rpc, SwObject* parent = nullptr);
    ~Server() override;
    bool listen(uint16_t port) { return server_.listen(port); }
private:
    Catalog catalog_;
    SwWebSocketServer server_;
    SwString apiKey_;
    SwBridgeRpcClient& rpc_;
};
}
