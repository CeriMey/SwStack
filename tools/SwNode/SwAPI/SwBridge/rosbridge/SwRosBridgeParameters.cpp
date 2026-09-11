#include "SwRosBridgeSession.h"
#include "../../SwApi/SwApiJson.h"
#include "SwTimer.h"
#include <cmath>

namespace swros {
namespace {
const char* valueFields[] = {"", "bool_value", "integer_value", "double_value", "string_value",
    "byte_array_value", "bool_array_value", "integer_array_value", "double_array_value", "string_array_value"};

int parameterType(const SwJsonValue& value) {
    if (value.isBool()) return 1;
    if (value.isInt()) return 2;
    if (value.isDouble()) return 3;
    if (value.isString()) return 4;
    if (value.isArray()) {
        const auto array = value.toArray();
        if (array.isEmpty()) return 9; // No element schema in JSON: empty arrays default to string[].
        const int first = parameterType(array[0]);
        if (first < 1 || first > 4) throw std::runtime_error("configuration array is not a ROS parameter type");
        for (const auto& item : array) if (parameterType(item) != first)
            throw std::runtime_error("heterogeneous configuration array is not a ROS parameter type");
        return first + 5;
    }
    if (value.isNull()) return 0;
    throw std::runtime_error("configuration objects have no ROS parameter type; use rosapi/get_param");
}

SwJsonObject parameterValue(const SwJsonValue& value, int hint = 0) {
    const int type = hint && !value.isNull() ? hint : parameterType(value);
    SwJsonObject out;
    out["type"] = SwJsonValue(type);
    out["bool_value"] = SwJsonValue(false); out["integer_value"] = SwJsonValue(0);
    out["double_value"] = SwJsonValue(0.0); out["string_value"] = SwJsonValue("");
    for (int i = 5; i <= 9; ++i) out[valueFields[i]] = SwJsonValue(SwJsonArray());
    if (type) out[valueFields[type]] = value;
    return out;
}

SwJsonValue decodeParameter(const SwJsonValue& value) {
    if (!value.isObject()) throw std::runtime_error("value must be a ParameterValue object");
    const auto object = value.toObject();
    if (!object["type"].isInt()) throw std::runtime_error("parameter type must be an integer");
    const int type = object["type"].toInt();
    if (type == 0) throw std::runtime_error("registered configuration cannot be undeclared");
    if (type < 1 || type > 9) throw std::runtime_error("unknown parameter type");
    const auto decoded = object[valueFields[type]];
    if (type == 5) throw std::runtime_error("byte-array parameters require an explicit native schema");
    if (type >= 6 && decoded.isArray() && decoded.toArray().isEmpty()) return decoded;
    if (type == 3 && decoded.isInt()) return SwJsonValue(decoded.toDouble());
    if (parameterType(decoded) != type) throw std::runtime_error("parameter value does not match its type");
    return decoded;
}

SwString paramName(const SwJsonObject& args) {
    SwString name = args["name"].toString();
    if (name.isEmpty()) throw std::runtime_error("missing parameter name");
    const int colon = name.indexOf(':');
    if (colon >= 0) {
        SwString path = name.mid(colon + 1); path.replace(".", "/");
        if (path.isEmpty() || path.contains(':')) throw std::runtime_error("expected node:parameter name");
        return Catalog::name(name.left(colon)) + "/" + path;
    }
    const SwString node = args["node"].toString();
    if (!node.isEmpty()) {
        name.replace(".", "/");
        return Catalog::name(node) + "/" + name;
    }
    return Catalog::name(name);
}

SwJsonObject setResult(bool ok, const SwString& error) {
    SwJsonObject result;
    result["successful"] = SwJsonValue(ok); result["reason"] = SwJsonValue(error);
    return result;
}
}

std::map<SwString, SwString> parameterServices() {
    return {{"get_parameters", "rcl_interfaces/srv/GetParameters"},
        {"get_parameter_types", "rcl_interfaces/srv/GetParameterTypes"},
        {"list_parameters", "rcl_interfaces/srv/ListParameters"},
        {"describe_parameters", "rcl_interfaces/srv/DescribeParameters"},
        {"set_parameters", "rcl_interfaces/srv/SetParameters"}};
}

SwJsonValue Session::readParameter(const Binding& b) {
    SwString domain, object; Catalog::splitTarget(b.target, domain, object);
    for (const auto& value : sw::ipc::shmRegistrySnapshot(domain)) {
        const auto entry = value.toObject();
        if (entry["object"].toString() != object || !entry["signal"].toString().startsWith("__config__|")) continue;
        sw::ipc::Registry reg(domain, object);
        sw::ipc::SwIpcSignal<uint64_t, SwString> signal(reg, entry["signal"].toString(), 1u, 4096u, sw::ipc::DeliveryMode::LatestOnly);
        uint64_t publisher = 0; SwString text, error;
        if (!signal.readLatest(publisher, text)) throw std::runtime_error("configuration snapshot unavailable");
        SwJsonDocument doc;
        if (!doc.loadFromJson(text.toStdString(), error) || !doc.isObject()) throw std::runtime_error("invalid configuration snapshot");
        SwJsonValue result;
        if (!SwApiJson::tryGetPath(doc.toJsonValue(), b.channel, result, error)) throw std::runtime_error(error.toStdString());
        return result;
    }
    throw std::runtime_error("node has no configuration snapshot");
}

SwJsonObject Session::parameterDescriptor(const Binding& binding) {
    SwString domain, object; Catalog::splitTarget(binding.target, domain, object);
    swapi::wire::RpcQueueInfo info;
    if (!swapi::wire::findSignalInRegistryForTarget(domain, object, "__config_schema__", info)) return {};
    sw::ipc::Registry registry(domain, object); sw::ipc::SwIpcSignal<SwString> signal(registry, info.signal, 1u, 4096u, sw::ipc::DeliveryMode::LatestOnly);
    SwString text, error; SwJsonDocument document;
    if (!signal.readLatest(text) || !document.loadFromJson(text.toStdString(), error) || !document.isObject()) return {};
    return document.object()[binding.channel].toObject();
}

Session::ConfigWrite Session::writeParameter(const Binding& b, const SwJsonValue& value, int requestedType) {
    ConfigWrite write; write.binding = b; write.expected = value;
    try {
        const auto descriptor = parameterDescriptor(b);
        if (descriptor["read_only"].toBool()) throw std::runtime_error(descriptor["description"].toString("Read-only configuration").toStdString());
        const auto previous = readParameter(b);
        if (requestedType && requestedType != descriptor["type"].toInt(parameterType(previous)))
            throw std::runtime_error("parameter type does not match native configuration");
        // Preserve native types; SwRemoteObject's text conversion must not silently coerce input.
        if ((previous.isBool() && !value.isBool()) || (previous.isInt() && !value.isInt() && descriptor["type"].toInt() != 3) ||
            (previous.isDouble() && !previous.isInt() && !value.isDouble()) ||
            (previous.isString() && !value.isString()) || (previous.isArray() && !value.isArray()) ||
            (previous.isObject() && !value.isObject()) || value.isNull())
            throw std::runtime_error("parameter type does not match native configuration");
        SwString domain, object; Catalog::splitTarget(b.target, domain, object);
        swapi::wire::RpcQueueInfo info; SwString method;
        if (swapi::wire::findRpcRequestQueueForMethod(domain, object, "writeConfiguration", info, method)) {
            if (calls_.size() >= 64) throw std::runtime_error("too many pending native calls");
            write.ack = std::make_shared<ConfigAck>();
            const auto ack = write.ack; SwPointer<Session> weak(this);
            SwJsonArray args; args.append(b.channel); args.append(SwString(value.toJsonString()));
            const uint64_t id = rpc_.call(b.target, "writeConfiguration", args, 5000, "SwBridge/parameters",
                [weak, ack](const SwBridgeRpcClient::Result& response) {
                    if (!weak) return;
                    weak->calls_.erase(response.callId);
                    ack->done = true;
                    if (!response.ok) { ack->error = response.error; return; }
                    SwJsonDocument document; SwString error;
                    if (!response.value.isString() || !document.loadFromJson(response.value.toString().toStdString(), error) || !document.isObject())
                        ack->error = "invalid native configuration acknowledgement";
                    else if (!document.object()["ok"].toBool())
                        ack->error = document.object()["error"].toString("Native configuration rejected");
                });
            if (id) calls_.insert(id);
            return write;
        }
        if (previous == value) { write.done = true; return write; }
        if (!swapi::wire::findSignalInRegistryForTarget(domain, object, "__cfg__|" + b.channel, info))
            throw std::runtime_error("parameter is not registered for remote updates");
        sw::ipc::Registry reg(domain, object);
        sw::ipc::SwIpcSignal<uint64_t, SwString> signal(reg, info.signal, 1u, 4096u, sw::ipc::DeliveryMode::LatestOnly);
        const SwString payload = value.isString() ? value.toString() : SwString(value.toJsonString());
        if (!signal.publish(0, payload)) throw std::runtime_error("configuration exceeds IPC payload capacity");
    } catch (const std::exception& error) { write.done = true; write.error = error.what(); }
    return write;
}

bool Session::parameterService(const SwString& service, const SwJsonObject& request) {
    const auto argsValue = request.contains("args") ? request["args"] : SwJsonValue(SwJsonObject());
    const auto args = argsValue.toObject();
    if (service == "/rosapi/get_param_names") {
        catalog_.refresh(); SwJsonArray names;
        std::set<SwString> unique;
        for (const auto& item : catalog_.parameters()) {
            SwString domain, object; Catalog::splitTarget(item.second.target, domain, object);
            SwString path = item.second.channel; path.replace("/", ".");
            unique.insert("/" + object + ":" + path);
        }
        for (const auto& name : unique) names.append(SwJsonValue(name));
        SwJsonObject out; out["names"] = SwJsonValue(names);
        respond(request, true, SwJsonValue(out)); return true;
    }
    if (service == "/rosapi/get_param" || service == "/rosapi/has_param") {
        SwJsonValue value; bool found = false; SwString error;
        try { value = readParameter(catalog_.parameter(paramName(args))); found = true; }
        catch (const std::exception& exception) { error = exception.what(); }
        SwJsonObject out;
        if (service == "/rosapi/has_param") out["exists"] = SwJsonValue(found);
        else {
            out["successful"] = SwJsonValue(found); out["reason"] = SwJsonValue(error);
            if (!found) {
                value = SwJsonValue("");
                if (args["default_value"].isString() && !args["default_value"].toString().isEmpty()) {
                    SwJsonDocument doc; SwString parseError;
                    if (doc.loadFromJson(args["default_value"].toString().toStdString(), parseError)) value = doc.toJsonValue();
                }
            }
            out["value"] = SwJsonValue(SwString(value.toJsonString()));
        }
        respond(request, true, SwJsonValue(out)); return true;
    }
    ConfigRequest pending;
    pending.request = request; pending.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    if (service == "/rosapi/set_param") {
        if (configRequests_.size() >= 32) throw std::runtime_error("too many pending parameter requests");
        if (!args["value"].isString()) throw std::runtime_error("rosapi value must be a JSON-encoded string");
        SwJsonDocument doc; SwString error;
        if (!doc.loadFromJson(args["value"].toString().toStdString(), error)) throw std::runtime_error("invalid JSON parameter value");
        pending.rosapi = true;
        pending.writes.push_back(writeParameter(catalog_.parameter(paramName(args)), doc.toJsonValue()));
        configRequests_.push_back(std::move(pending)); timer_->start(); return true;
    }
    const int slash = static_cast<int>(service.lastIndexOf('/'));
    const SwString node = service.left(slash), method = service.mid(slash + 1);
    if (!parameterServices().count(method)) return false;
    catalog_.refresh();
    if (!catalog_.nodes().count(node)) throw std::runtime_error("parameter node not found");
    if (!argsValue.isObject()) throw std::runtime_error("parameter service args must be named fields");
    SwJsonObject out; SwJsonArray results;
    if (method == "list_parameters") {
        if (args.contains("depth") && (!args["depth"].isInt() || args["depth"].toInt() < 0))
            throw std::runtime_error("depth must be a non-negative integer");
        if (args.contains("prefixes") && !args["prefixes"].isArray()) throw std::runtime_error("prefixes must be an array");
        const int depth = args["depth"].toInt();
        const auto prefixes = args["prefixes"].toArray();
        std::set<SwString> seen, parents;
        for (const auto& item : catalog_.parameters()) {
            if (item.second.target != catalog_.nodes().at(node)) continue;
            SwString name = item.second.channel; name.replace("/", ".");
            bool match = prefixes.isEmpty() && (depth == 0 || name.count('.') < depth);
            for (const auto& prefix : prefixes) {
                const SwString p = prefix.toString();
                if (name == p || (name.startsWith(p + ".") && (depth == 0 || name.mid(p.size() + 1).count('.') < depth))) match = true;
            }
            if (!match || !seen.insert(name).second) continue;
            results.append(SwJsonValue(name));
            const int dot = static_cast<int>(name.lastIndexOf('.'));
            if (dot >= 0) parents.insert(name.left(dot));
        }
        SwJsonArray prefixResult; for (const auto& p : parents) prefixResult.append(SwJsonValue(p));
        SwJsonObject listed; listed["names"] = SwJsonValue(results); listed["prefixes"] = SwJsonValue(prefixResult);
        out["result"] = SwJsonValue(listed);
    } else if (method == "set_parameters") {
        if (configRequests_.size() >= 32) throw std::runtime_error("too many pending parameter requests");
        if (!args["parameters"].isArray() || args["parameters"].toArray().size() > 128)
            throw std::runtime_error("parameters must be an array of at most 128 entries");
        std::set<SwString> seen;
        for (const auto& item : args["parameters"].toArray()) {
            ConfigWrite write;
            try {
                const auto param = item.toObject(); SwString name = param["name"].toString();
                name.replace(".", "/");
                if (!seen.insert(name).second) throw std::runtime_error("duplicate parameter in one request");
                write = writeParameter(catalog_.parameter(node + "/" + name), decodeParameter(param["value"]),
                    param["value"].toObject()["type"].toInt());
            } catch (const std::exception& error) { write.done = true; write.error = error.what(); }
            pending.writes.push_back(std::move(write));
        }
        configRequests_.push_back(std::move(pending)); timer_->start(); return true;
    } else {
        if (!args["names"].isArray()) throw std::runtime_error("names must be an array");
        for (const auto& entry : args["names"].toArray()) {
            if (!entry.isString()) throw std::runtime_error("parameter names must be strings");
            SwString path = entry.toString(); path.replace(".", "/");
            SwJsonValue value; SwJsonObject metadata;
            try { const auto binding = catalog_.parameter(node + "/" + path); value = readParameter(binding); metadata = parameterDescriptor(binding); }
            catch (const std::exception&) {}
            const int type = value.isNull() ? 0 : metadata["type"].toInt(parameterType(value));
            if (method == "get_parameters") results.append(SwJsonValue(parameterValue(value, type)));
            else if (method == "get_parameter_types") results.append(SwJsonValue(type));
            else {
                SwJsonObject descriptor;
                descriptor["name"] = entry; descriptor["type"] = SwJsonValue(type);
                descriptor["description"] = metadata["description"].toString(); descriptor["additional_constraints"] = SwJsonValue("");
                descriptor["read_only"] = metadata["read_only"].toBool(); descriptor["dynamic_typing"] = SwJsonValue(false);
                descriptor["floating_point_range"] = SwJsonValue(SwJsonArray()); descriptor["integer_range"] = SwJsonValue(SwJsonArray());
                results.append(SwJsonValue(descriptor));
            }
        }
        out[method == "get_parameters" ? "values" : method == "get_parameter_types" ? "types" : "descriptors"] = SwJsonValue(results);
    }
    respond(request, true, SwJsonValue(out)); return true;
}

void Session::pollParameters() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = configRequests_.begin(); it != configRequests_.end();) {
        bool complete = true;
        for (auto& write : it->writes) {
            if (!write.done) {
                if (write.ack) { write.done = write.ack->done; write.error = write.ack->error; }
                else try { write.done = readParameter(write.binding) == write.expected; if (write.done) write.error.clear(); }
                catch (const std::exception& error) { write.error = error.what(); }
                if (!write.done && now >= it->deadline) {
                    write.done = true; write.error = "timeout waiting for native configuration to apply";
                }
            }
            complete = complete && write.done;
        }
        if (!complete) { ++it; continue; }
        if (it->rosapi) {
            const auto& write = it->writes.front();
            respond(it->request, true, SwJsonValue(setResult(write.error.isEmpty(), write.error)));
        } else {
            SwJsonArray results;
            for (const auto& write : it->writes) results.append(SwJsonValue(setResult(write.error.isEmpty(), write.error)));
            SwJsonObject out; out["results"] = SwJsonValue(results);
            respond(it->request, true, SwJsonValue(out));
        }
        it = configRequests_.erase(it);
    }
}
}
