#pragma once

#include "../../SwApiRpcWire.h"
#include <map>

namespace swros {

struct Binding {
    SwString name, target, channel, type, responseField;
    std::vector<SwString> fields;
    std::vector<std::string> types;
    bool json{false}; // One SwString carrying a complete JSON message/request.
};

// The selected IPC domain is exposed as a ROS namespace rooted at '/'.
// Explicit bindings supply old ROS names and field order absent from IPC RTTI.
class Catalog {
public:
    explicit Catalog(SwString domain) : domain_(std::move(domain)) {}
    void load(const SwString& file);
    void refresh();
    Binding service(const SwString& name);
    Binding topic(const SwString& name);
    Binding parameter(const SwString& name);
    const std::map<SwString, Binding>& services() const { return services_; }
    const std::map<SwString, Binding>& topics() const { return topics_; }
    const std::map<SwString, Binding>& parameters() const { return parameters_; }
    const std::map<SwString, SwString>& nodes() const { return nodes_; }
    const SwString& domain() const { return domain_; }
    static SwString name(SwString value);
    static void splitTarget(const SwString& target, SwString& domain, SwString& object);
    static SwJsonArray arguments(const Binding& binding, const SwJsonValue& value);
    static SwJsonObject message(const Binding& binding, const SwJsonArray& values);
    static SwJsonObject response(const Binding& binding, const SwJsonValue& value, bool hasReturn);
private:
    SwString domain_;
    SwJsonObject aliases_;
    std::map<SwString, Binding> services_, topics_, parameters_;
    std::map<SwString, SwString> nodes_;
};

}
