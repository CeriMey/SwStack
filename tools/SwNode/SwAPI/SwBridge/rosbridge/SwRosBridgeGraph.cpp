#include "SwRosBridgeSession.h"
#include <chrono>

namespace swros {
std::map<SwString, SwString> graphServices() {
    return {{"/rosapi/nodes", "rosapi_msgs/srv/Nodes"},
        {"/rosapi/topics", "rosapi_msgs/srv/Topics"},
        {"/rosapi/services", "rosapi_msgs/srv/Services"},
        {"/rosapi/topic_type", "rosapi_msgs/srv/TopicType"},
        {"/rosapi/service_type", "rosapi_msgs/srv/ServiceType"},
        {"/rosapi/topics_for_type", "rosapi_msgs/srv/TopicsForType"},
        {"/rosapi/services_for_type", "rosapi_msgs/srv/ServicesForType"},
        {"/rosapi/service_node", "rosapi_msgs/srv/ServiceNode"},
        {"/rosapi/node_details", "rosapi_msgs/srv/NodeDetails"},
        {"/rosapi/get_time", "rosapi_msgs/srv/GetTime"},
        {"/rosapi/action_servers", "rosapi_msgs/srv/GetActionServers"},
        {"/rosapi/get_param", "rosapi_msgs/srv/GetParam"},
        {"/rosapi/set_param", "rosapi_msgs/srv/SetParam"},
        {"/rosapi/has_param", "rosapi_msgs/srv/HasParam"},
        {"/rosapi/get_param_names", "rosapi_msgs/srv/GetParamNames"}};
}

bool Session::graphService(const SwString& service, const SwJsonObject& args, SwJsonObject& result) {
    if (!service.startsWith("/rosapi/")) return false;
    catalog_.refresh();
    SwJsonArray names, types;
    if (service == "/rosapi/nodes") {
        for (const auto& node : catalog_.nodes()) names.append(SwJsonValue(node.first));
        result["nodes"] = SwJsonValue(names);
    } else if (service == "/rosapi/topics" || service == "/rosapi/topics_for_type") {
        for (const auto& topic : catalog_.topics()) {
            if (service.endsWith("for_type") && topic.second.type != args["type"].toString()) continue;
            names.append(SwJsonValue(topic.first)); types.append(SwJsonValue(topic.second.type));
        }
        result["topics"] = SwJsonValue(names);
        if (service == "/rosapi/topics") result["types"] = SwJsonValue(types);
    } else if (service == "/rosapi/services" || service == "/rosapi/services_for_type" || service == "/rosapi/service_type") {
        auto services = graphServices();
        for (const auto& entry : catalog_.services()) services[entry.first] = entry.second.type;
        for (const auto& node : catalog_.nodes())
            for (const auto& param : parameterServices()) services[node.first + "/" + param.first] = param.second;
        if (service == "/rosapi/service_type") {
            const auto it = services.find(Catalog::name(args["service"].toString()));
            if (it == services.end()) throw std::runtime_error("service not found");
            result["type"] = SwJsonValue(it->second);
        } else {
            for (const auto& entry : services) {
                if (service.endsWith("for_type") && entry.second != args["type"].toString()) continue;
                names.append(SwJsonValue(entry.first));
            }
            result["services"] = SwJsonValue(names);
        }
    } else if (service == "/rosapi/topic_type") {
        result["type"] = SwJsonValue(catalog_.topic(args["topic"].toString()).type);
    } else if (service == "/rosapi/service_node") {
        const auto binding = catalog_.service(args["service"].toString());
        SwString domain, object; Catalog::splitTarget(binding.target, domain, object);
        result["node"] = SwJsonValue("/" + object);
    } else if (service == "/rosapi/node_details") {
        const auto node = Catalog::name(args["node"].toString());
        const auto it = catalog_.nodes().find(node);
        if (it == catalog_.nodes().end()) throw std::runtime_error("node not found");
        SwJsonArray publishing, subscribing, services;
        for (const auto& topic : catalog_.topics())
            if (topic.second.target == it->second) publishing.append(SwJsonValue(topic.first));
        std::set<SwString> subscriptions;
        for (const auto& value : sw::ipc::shmSubscribersSnapshot(catalog_.domain())) {
            const auto entry = value.toObject();
            if (entry["subTarget"].toString() != it->second) continue;
            const auto target = catalog_.domain() + "/" + entry["object"].toString();
            for (const auto& topic : catalog_.topics())
                if (topic.second.target == target && topic.second.channel == entry["signal"].toString()) subscriptions.insert(topic.first);
        }
        for (const auto& topic : subscriptions) subscribing.append(SwJsonValue(topic));
        for (const auto& entry : catalog_.services())
            if (entry.second.target == it->second) services.append(SwJsonValue(entry.first));
        for (const auto& entry : parameterServices()) services.append(SwJsonValue(node + "/" + entry.first));
        result["publishing"] = SwJsonValue(publishing); result["subscribing"] = SwJsonValue(subscribing);
        result["services"] = SwJsonValue(services);
    } else if (service == "/rosapi/action_servers") {
        result["action_servers"] = SwJsonValue(names);
    } else if (service == "/rosapi/get_time") {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        SwJsonObject time;
        time["sec"] = SwJsonValue(static_cast<long long>(ns / 1000000000));
        time["nanosec"] = SwJsonValue(static_cast<long long>(ns % 1000000000));
        result["time"] = SwJsonValue(time);
    } else return false;
    return true;
}
}
