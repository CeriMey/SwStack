#pragma once

#include "SwObject.h"
#include "SwString.h"
#include <memory>

class SwBridgeHttpServer;
class SwBridgeRpcClient;
namespace swros { class Server; }

class SwBridgeApp : public SwObject {
public:
    SwBridgeApp(int argc, char** argv, SwObject* parent = nullptr);
    ~SwBridgeApp() override;
    bool started() const { return started_; }

private:
    bool started_{false};
    std::unique_ptr<SwBridgeRpcClient> rpc_;
    std::unique_ptr<SwBridgeHttpServer> server_;
    std::unique_ptr<swros::Server> rosbridge_;
};
