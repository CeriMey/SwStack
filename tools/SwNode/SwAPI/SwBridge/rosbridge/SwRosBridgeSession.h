#pragma once

#include "SwRosBridgeCatalog.h"
#include "SwRosBridgeSignal.h"
#include "../SwBridgeRpcClient.h"
#include "SwPointer.h"
#include "SwWebSocket.h"
#include <set>

class SwTimer;

namespace swros {
class Session : public SwObject {
public:
    Session(SwWebSocket* socket, Catalog& catalog, SwBridgeRpcClient& rpc, SwObject* parent);
    ~Session() override;
private:
    struct Subscription {
        Binding binding;
        SwString requestedType;
        std::unique_ptr<Signal> signal;
        std::map<SwString, int> requests; // subscription ID -> throttle milliseconds
        uint64_t sequence{0};
        std::chrono::steady_clock::time_point next;
    };
    struct ConfigAck { bool done{false}; SwString error; };
    struct ConfigWrite {
        std::shared_ptr<ConfigAck> ack;
        Binding binding;
        SwJsonValue expected;
        SwString error;
        bool done{false};
    };
    struct ConfigRequest {
        SwJsonObject request;
        std::vector<ConfigWrite> writes;
        std::chrono::steady_clock::time_point deadline;
        bool rosapi{false};
    };
    void receive(const SwString& text);
    void dispatch(const SwJsonObject& request);
    void callService(const SwJsonObject& request);
    bool graphService(const SwString& service, const SwJsonObject& args, SwJsonObject& result);
    bool parameterService(const SwString& service, const SwJsonObject& request);
    void poll();
    void pollParameters();
    void send(const SwJsonObject& message);
    void status(const SwJsonObject& request, const SwString& message);
    void respond(const SwJsonObject& request, bool ok, const SwJsonValue& values);
    static SwJsonValue readParameter(const Binding& binding);
    static SwJsonObject parameterDescriptor(const Binding& binding);
    ConfigWrite writeParameter(const Binding& binding, const SwJsonValue& value, int requestedType = 0);

    SwPointer<SwWebSocket> socket_;
    Catalog& catalog_;
    SwBridgeRpcClient& rpc_;
    SwTimer* timer_;
    std::map<SwString, Subscription> subscriptions_;
    std::map<SwString, Binding> publishers_;
    std::set<uint64_t> calls_;
    std::vector<ConfigRequest> configRequests_;
};

// Services synthesized from the native config and registry, exposed via call_service.
std::map<SwString, SwString> graphServices();
std::map<SwString, SwString> parameterServices();
}
