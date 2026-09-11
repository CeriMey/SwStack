#include "SwRosBridgeCatalog.h"
#include "SwFile.h"

namespace swros {
using namespace swapi::wire;

SwString Catalog::name(SwString value) {
    if (value.isEmpty()) throw std::runtime_error("empty ROS name");
    if (!value.startsWith("/")) value = "/" + value;
    if (value.contains("//") || value.endsWith("/") || value.contains("#") || value.contains("|"))
        throw std::runtime_error("invalid ROS name");
    return value;
}

void Catalog::splitTarget(const SwString& target, SwString& domain, SwString& object) {
    const int slash = target.indexOf('/');
    if (slash <= 0 || slash + 1 >= static_cast<int>(target.size()))
        throw std::runtime_error("binding target must be domain/object");
    domain = target.left(slash); object = target.mid(slash + 1);
}

void Catalog::load(const SwString& file) {
    if (file.isEmpty()) return;
    SwFile input(file);
    if (!input.open(SwFile::Read)) throw std::runtime_error("cannot open rosbridge mapping file");
    SwJsonDocument doc; SwString error;
    if (!doc.loadFromJson(input.readAll().toStdString(), error) || !doc.isObject())
        throw std::runtime_error("rosbridge mapping must be a JSON object");
    aliases_ = doc.object();
    for (auto it = aliases_.begin(); it != aliases_.end(); ++it) {
        if (it.key() != "services" && it.key() != "topics" && it.key() != "parameters")
            throw std::runtime_error("unknown rosbridge mapping section");
        if (!it.value().isObject()) throw std::runtime_error("mapping sections must be objects");
    }
    refresh();
}

void Catalog::refresh() {
    services_.clear(); topics_.clear(); parameters_.clear(); nodes_.clear();
    for (const auto& value : sw::ipc::shmRegistrySnapshot(domain_)) {
        const auto entry = value.toObject();
        const SwString object = entry["object"].toString(), signal = entry["signal"].toString();
        if (object.isEmpty()) continue;
        Binding b;
        b.target = domain_ + "/" + object; b.channel = signal;
        b.types = parseArgTypesFromTypeName(entry["typeName"].toString().toStdString());
        if (signal.startsWith("__config__|")) nodes_["/" + object] = b.target;
        if (signal.startsWith("__rpc__|")) {
            SwString method = signal.mid(8);
            const int pipe = method.indexOf('|');
            if (pipe > 0 && method.left(pipe).isInt()) method = method.mid(pipe + 1);
            if (b.types.size() < 3) continue;
            b.types.erase(b.types.begin(), b.types.begin() + 3);
            b.name = "/" + object + "/" + method; b.channel = method; b.type = "swstack/srv/Rpc";
            b.json = b.types.size() == 1 && b.types[0] == "SwString";
            services_[b.name] = b;
        } else if (signal == "__config_schema__") {
            sw::ipc::Registry registry(domain_, object);
            sw::ipc::SwIpcSignal<SwString> schema(registry, signal, 1u, 4096u, sw::ipc::DeliveryMode::LatestOnly);
            SwString text, error; SwJsonDocument document;
            if (schema.readLatest(text) && document.loadFromJson(text.toStdString(), error) && document.isObject()) {
                for (const auto& field : document.object().data()) {
                    Binding parameter = b; parameter.channel = field.first;
                    parameter.name = "/" + object + "/" + parameter.channel;
                    parameters_[parameter.name] = parameter;
                }
            }
        } else if (signal.startsWith("__cfg__|")) {
            b.channel = signal.mid(8); b.name = "/" + object + "/" + b.channel;
            parameters_[b.name] = b;
        } else if (!signal.startsWith("__")) {
            // Native signal types are stored separately from the dynamic ring layout.
            if (!entry["typeName"].toString().startsWith("sw::ipc::tuple<")) continue;
            b.name = "/" + object + "/" + signal; b.type = "swstack/msg/Signal";
            if (b.types.size() == 1) {
                const auto& type = b.types[0];
                if (type == "SwString") { b.type = "swstack/msg/Json"; b.json = true; }
                else if (type == "bool") b.type = "std_msgs/msg/Bool";
                else if (type == "int" || type == "int32_t") b.type = "std_msgs/msg/Int32";
                else if (type == "double") b.type = "std_msgs/msg/Float64";
                else if (type == "float") b.type = "std_msgs/msg/Float32";
            }
            topics_[b.name] = b;
        }
    }
    auto overlay = [&](const char* section, std::map<SwString, Binding>& bindings, const char* channelKey) {
        const auto native = bindings;
        const auto entries = aliases_[section].toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (!it.value().isObject()) throw std::runtime_error("mapping entry must be an object");
            const auto row = it.value().toObject();
            Binding b;
            b.name = name(it.key()); b.target = row["target"].toString(); b.channel = row[channelKey].toString();
            SwString domain, object; splitTarget(b.target, domain, object);
            if (domain != domain_) throw std::runtime_error("mapping target must use --rosbridge-domain");
            if (b.channel.isEmpty() || b.channel.startsWith("__")) throw std::runtime_error("invalid mapping channel");
            for (const auto& current : native) {
                if (current.second.target == b.target && current.second.channel == b.channel) {
                    b = current.second; b.name = name(it.key()); break;
                }
            }
            if (row.contains("type")) {
                if (!row["type"].isString()) throw std::runtime_error("mapping type must be a string");
                b.type = row["type"].toString();
            }
            if (row.contains("json")) {
                if (!row["json"].isBool()) throw std::runtime_error("mapping json must be a boolean");
                b.json = row["json"].toBool();
            }
            if (row.contains("fields")) {
                if (!row["fields"].isArray()) throw std::runtime_error("mapping fields must be an array");
                for (const auto& field : row["fields"].toArray()) {
                    if (!field.isString() || field.toString().isEmpty()) throw std::runtime_error("invalid mapping field");
                    if (std::find(b.fields.begin(), b.fields.end(), field.toString()) != b.fields.end())
                        throw std::runtime_error("duplicate mapping field");
                    b.fields.push_back(field.toString());
                }
            }
            if (row.contains("response_field") && (!row["response_field"].isString() || row["response_field"].toString().isEmpty()))
                throw std::runtime_error("response_field must be a non-empty string");
            b.responseField = row["response_field"].toString();
            bindings[b.name] = b;
        }
    };
    overlay("services", services_, "method");
    overlay("topics", topics_, "signal");
    overlay("parameters", parameters_, "path");
}

Binding Catalog::service(const SwString& raw) {
    refresh(); const auto it = services_.find(name(raw));
    if (it == services_.end()) throw std::runtime_error("service not found");
    return it->second;
}
Binding Catalog::topic(const SwString& raw) {
    refresh(); const auto it = topics_.find(name(raw));
    if (it == topics_.end()) throw std::runtime_error("topic not found");
    return it->second;
}
Binding Catalog::parameter(const SwString& raw) {
    refresh(); const auto it = parameters_.find(name(raw));
    if (it == parameters_.end()) throw std::runtime_error("parameter not found or not registered for remote updates");
    return it->second;
}

SwJsonArray Catalog::arguments(const Binding& b, const SwJsonValue& value) {
    if (value.isArray()) return value.toArray();
    if (!value.isObject()) throw std::runtime_error("args/msg must be an object or an argument array");
    const auto object = value.toObject();
    SwJsonArray result;
    if (b.json && b.fields.empty()) {
        result.append(SwJsonValue(SwString(value.toJsonString())));
        return result;
    }
    const size_t count = b.types.size();
    if (!b.fields.empty() && b.fields.size() != count) throw std::runtime_error("mapping fields do not match IPC argument count");
    for (size_t i = 0; i < count; ++i) {
        const SwString field = !b.fields.empty() ? b.fields[i] : count == 1 ? SwString("data") : "arg" + SwString::number(i);
        if (!object.contains(field)) throw std::runtime_error(("missing request field: " + field).toStdString());
        auto argument = object[field];
        if (b.types[i] == "SwString" && !argument.isString()) argument = SwJsonValue(SwString(argument.toJsonString()));
        result.append(argument);
    }
    if (object.size() != count) throw std::runtime_error("unexpected request fields; configure fields/json mapping");
    return result;
}

SwJsonObject Catalog::message(const Binding& b, const SwJsonArray& values) {
    if (b.json && values.size() == 1 && values[0].isString()) {
        SwJsonDocument doc; SwString error;
        if (doc.loadFromJson(values[0].toString().toStdString(), error) && doc.isObject()) return doc.object();
    }
    if (!b.fields.empty() && b.fields.size() != values.size()) throw std::runtime_error("mapping fields do not match signal");
    SwJsonObject result;
    for (size_t i = 0; i < values.size(); ++i) {
        const SwString field = !b.fields.empty() ? b.fields[i] : values.size() == 1 ? SwString("data") : "arg" + SwString::number(i);
        result[field] = values[i];
    }
    return result;
}

SwJsonObject Catalog::response(const Binding& b, const SwJsonValue& value, bool hasReturn) {
    SwJsonObject result;
    if (!hasReturn) return result;
    if (b.responseField.isEmpty() && value.isString()) {
        SwJsonDocument doc; SwString error;
        if (doc.loadFromJson(value.toString().toStdString(), error) && doc.isObject()) return doc.object();
    }
    result[b.responseField.isEmpty() ? SwString("result") : b.responseField] = value;
    return result;
}
}
