#include "SwRosBridgeSession.h"
#include "SwWebSocket.h"
#include "SwTimer.h"
#include <cmath>

namespace swros {
namespace {
SwString requiredString(const SwJsonObject& object, const char* field) {
    if (!object[field].isString() || object[field].toString().isEmpty())
        throw std::runtime_error((SwString("missing string field: ") + field).toStdString());
    return object[field].toString();
}
SwString fullType(SwString type, const SwString& kind) {
    if (type.count('/') == 1) type.insert(type.indexOf('/') + 1, kind + "/");
    return type;
}
void checkType(const Binding& b, const SwJsonObject& request, const SwString& kind) {
    if (!request.contains("type")) return;
    const auto type = requiredString(request, "type");
    if (!b.type.isEmpty() && fullType(type, kind) != fullType(b.type, kind))
        throw std::runtime_error("type does not match binding; configure the old ROS type in --rosbridge-map");
}
void checkOptions(const SwJsonObject& request) {
    if (request.contains("compression") && request["compression"].toString() != "none")
        throw std::runtime_error("only JSON with compression=none is supported");
    if (request.contains("fragment_size") && request["fragment_size"].toInt() != 0)
        throw std::runtime_error("rosbridge JSON fragmentation is not supported");
    if (request.contains("qos")) throw std::runtime_error("DDS QoS is unavailable on the SwStack transport");
}
}

Session::Session(SwWebSocket* socket, Catalog& catalog, SwBridgeRpcClient& rpc, SwObject* parent)
    : SwObject(parent), socket_(socket), catalog_(catalog), rpc_(rpc), timer_(new SwTimer(10, this)) {
    socket->setMaxIncomingMessageSize(1024 * 1024);
    socket->setMaxBufferedIncomingBytes(2 * 1024 * 1024);
    connect(socket, &SwWebSocket::textMessageReceived, this, [this](const SwString& text) { receive(text); });
    connect(socket, &SwWebSocket::binaryMessageReceived, this, [this](const SwByteArray&) {
        status(SwJsonObject(), "only rosbridge JSON text messages are supported");
    });
    connect(socket, &SwWebSocket::disconnected, this, [this] {
        timer_->stop();
        for (auto id : calls_) rpc_.cancel(id);
        calls_.clear();
        socket_ = nullptr;
        deleteLater();
    });
    connect(timer_, &SwTimer::timeout, this, [this] { poll(); });
}

Session::~Session() {
    timer_->stop();
    for (auto id : calls_) rpc_.cancel(id);
}

void Session::send(const SwJsonObject& message) {
    if (!socket_) return;
    if (socket_->bytesToWrite() > 2 * 1024 * 1024) {
        socket_->close(SwWebSocket::CloseCodePolicyViolation, "client is too slow");
        return;
    }
    socket_->sendTextMessage(SwJsonDocument(message).toJson(SwJsonDocument::JsonFormat::Compact));
}

void Session::status(const SwJsonObject& request, const SwString& message) {
    SwJsonObject out;
    out["op"] = SwJsonValue("status"); out["level"] = SwJsonValue("error"); out["msg"] = SwJsonValue(message);
    if (request.contains("id")) out["id"] = request["id"];
    send(out);
}

void Session::respond(const SwJsonObject& request, bool ok, const SwJsonValue& values) {
    SwJsonObject out;
    out["op"] = SwJsonValue("service_response"); out["service"] = request["service"];
    out["result"] = SwJsonValue(ok); out["values"] = values;
    if (request.contains("id")) out["id"] = request["id"];
    send(out);
}

void Session::receive(const SwString& text) {
    SwJsonObject request;
    try {
        SwJsonDocument doc; SwString error;
        if (!doc.loadFromJson(text.toStdString(), error) || !doc.isObject())
            throw std::runtime_error("invalid rosbridge JSON object");
        request = doc.object();
        if (request.contains("id") && !request["id"].isString()) throw std::runtime_error("id must be a string");
        dispatch(request);
    } catch (const std::exception& error) {
        if (request["op"].toString() == "call_service") respond(request, false, SwJsonValue(error.what()));
        else status(request, error.what());
    }
}

void Session::dispatch(const SwJsonObject& request) {
    const auto op = requiredString(request, "op");
    if (op == "call_service") { callService(request); return; }
    if (op == "set_level") return; // This adapter only emits errors.
    if (op == "subscribe" || op == "advertise" || op == "publish") {
        checkOptions(request);
        const auto topic = Catalog::name(requiredString(request, "topic"));
        Binding binding;
        try { binding = catalog_.topic(topic); }
        catch (const std::exception& error) {
            if (op != "subscribe" || SwString(error.what()) != "topic not found") throw;
            binding.name = topic; // rosbridge clients can subscribe before the native publisher starts.
        }
        checkType(binding, request, "msg");
        if (op == "subscribe") {
            if (subscriptions_.size() >= 128 && !subscriptions_.count(topic)) throw std::runtime_error("too many subscriptions");
            int throttle = 0;
            if (request.contains("throttle_rate")) {
                if (!request["throttle_rate"].isInt()) throw std::runtime_error("throttle_rate must be integer milliseconds");
                throttle = request["throttle_rate"].toInt();
                if (throttle < 0 || throttle > 60000) throw std::runtime_error("throttle_rate must be between 0 and 60000");
            }
            std::unique_ptr<Signal> signal;
            if (!binding.target.isEmpty()) {
                try { signal.reset(new Signal(binding)); }
                catch (const std::exception& error) { if (SwString(error.what()) != "signal not found") throw; }
            }
            auto& sub = subscriptions_[topic];
            if (!sub.requestedType.isEmpty() && request.contains("type") &&
                fullType(sub.requestedType, "msg") != fullType(request["type"].toString(), "msg"))
                throw std::runtime_error("conflicting subscription types for the same topic");
            if (request.contains("type")) sub.requestedType = request["type"].toString();
            if (!sub.signal) { sub.binding = binding; sub.signal = std::move(signal); }
            if (sub.requests.size() >= 128) throw std::runtime_error("too many subscription IDs");
            sub.requests[request["id"].toString()] = throttle;
            timer_->start();
            return;
        }
        if (op == "advertise") {
            requiredString(request, "type");
            if (publishers_.size() >= 128 && !publishers_.count(topic)) throw std::runtime_error("too many publishers");
            Signal existing(binding); // Validate the native endpoint now.
            publishers_[topic] = binding;
            return;
        }
        // rosbridge permits implicit advertising on publish when the type is known.
        if (!request["msg"].isObject()) throw std::runtime_error("msg must be an object");
        Signal signal(binding);
        signal.publish(Catalog::arguments(binding, request["msg"]));
        return;
    }
    if (op == "unsubscribe" || op == "unadvertise") {
        const auto topic = Catalog::name(requiredString(request, "topic"));
        if (op == "unadvertise") publishers_.erase(topic);
        else {
            auto it = subscriptions_.find(topic);
            if (it != subscriptions_.end()) {
                if (request.contains("id")) it->second.requests.erase(request["id"].toString());
                else it->second.requests.clear();
                if (it->second.requests.empty()) subscriptions_.erase(it);
            }
        }
        return;
    }
    if (op.contains("action")) throw std::runtime_error("actions are not supported by this system");
    throw std::runtime_error(("unsupported rosbridge operation: " + op).toStdString());
}

void Session::callService(const SwJsonObject& request) {
    const auto service = Catalog::name(requiredString(request, "service"));
    checkOptions(request);
    const SwJsonValue args = request.contains("args") ? request["args"] : SwJsonValue(SwJsonObject());
    if (!args.isObject() && !args.isArray()) throw std::runtime_error("args must be an object or array");
    SwJsonObject result;
    if (service.startsWith("/rosapi/") && !args.isObject()) throw std::runtime_error("rosapi args must be named fields");
    if (graphService(service, args.toObject(), result)) { respond(request, true, SwJsonValue(result)); return; }
    if (parameterService(service, request)) return;
    if (calls_.size() >= 64) throw std::runtime_error("too many pending service calls");
    const auto binding = catalog_.service(service);
    checkType(binding, request, "srv");
    auto values = Catalog::arguments(binding, args);
    int timeoutMs = 2000;
    if (request.contains("timeout")) {
        if (!request["timeout"].isDouble()) throw std::runtime_error("timeout must be seconds");
        const double seconds = request["timeout"].toDouble();
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > 60) throw std::runtime_error("timeout must be > 0 and <= 60 seconds");
        timeoutMs = std::max(1, static_cast<int>(seconds * 1000));
    }
    SwPointer<Session> weak(this);
    const uint64_t id = rpc_.call(binding.target, binding.channel, values, timeoutMs, "SwBridge/rosbridge",
        [weak, request, binding](const SwBridgeRpcClient::Result& response) {
            if (!weak) return;
            weak->calls_.erase(response.callId);
            if (!response.ok) { weak->respond(request, false, SwJsonValue(response.error)); return; }
            weak->respond(request, true, SwJsonValue(Catalog::response(binding, response.value, response.hasReturn)));
        });
    if (id) calls_.insert(id);
}

void Session::poll() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        auto& sub = it->second;
        if (now < sub.next) { ++it; continue; }
        try {
            if (!sub.signal) {
                try {
                    auto binding = catalog_.topic(it->first);
                    SwJsonObject typeCheck;
                    if (!sub.requestedType.isEmpty()) typeCheck["type"] = SwJsonValue(sub.requestedType);
                    checkType(binding, typeCheck, "msg");
                    sub.signal.reset(new Signal(binding)); sub.binding = binding;
                } catch (const std::exception& error) {
                    if (SwString(error.what()) != "topic not found" && SwString(error.what()) != "signal not found") throw;
                    sub.next = now + std::chrono::milliseconds(250);
                    ++it; continue;
                }
            }
            SwJsonArray values;
            if (sub.signal->read(sub.sequence, values)) {
                SwJsonObject out;
                out["op"] = SwJsonValue("publish"); out["topic"] = SwJsonValue(it->first);
                out["msg"] = SwJsonValue(Catalog::message(sub.binding, values));
                send(out);
                int throttle = 60000;
                for (const auto& request : sub.requests) throttle = std::min(throttle, request.second);
                sub.next = now + std::chrono::milliseconds(throttle);
            }
            ++it;
        } catch (const std::exception& error) {
            SwJsonObject request;
            for (const auto& item : sub.requests) {
                if (!item.first.isEmpty()) request["id"] = SwJsonValue(item.first);
                status(request, error.what());
            }
            it = subscriptions_.erase(it);
        }
    }
    pollParameters();
    if (subscriptions_.empty() && configRequests_.empty()) timer_->stop();
}
}
