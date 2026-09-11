#pragma once
#include <core/types/SwJsonDocument.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>

namespace sw::ipc {
// A bounded JSON request transfer over an existing string RPC. Transfers are
// private to one application object/event thread. No business operation runs
// until commit; tokens are consumed even when business validation fails.
class JsonRequestAssembler {
    using Clock = std::chrono::steady_clock;
    struct Pending { std::size_t bytes; SwString data; Clock::time_point touched; };
    std::map<SwString, Pending> pending_;
    const SwString epoch_{SwString::number(Clock::now().time_since_epoch().count())};
    unsigned long long next_{0};
public:
    static constexpr std::size_t maxBytes = 2 * 1024 * 1024;
    template<class Handler> SwJsonObject dispatch(const SwJsonObject& request, Handler handler) {
        const auto operation = request["operation"].toString();
        if (!operation.startsWith("$request_")) return handler(request);
        const auto now = Clock::now();
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now - it->second.touched > std::chrono::seconds(30)) it = pending_.erase(it); else ++it;
        }
        if (operation == "$request_begin") {
            if (!request["bytes"].isInt() || request["bytes"].toLongLong() < 1 || request["bytes"].toLongLong() > static_cast<long long>(maxBytes))
                throw std::invalid_argument("Invalid JSON request transfer size");
            if (pending_.size() >= 8) throw std::runtime_error("Too many JSON request transfers");
            const auto token = epoch_ + "-" + SwString::number(++next_);
            pending_.emplace(token, Pending{static_cast<std::size_t>(request["bytes"].toLongLong()), {}, now});
            SwJsonObject out; out["request_token"] = SwJsonValue(token); return out;
        }
        const auto found = pending_.find(request["request_token"].toString());
        if (operation == "$request_abort") {
            if (found != pending_.end()) pending_.erase(found);
            return {};
        }
        if (found == pending_.end()) throw std::invalid_argument("Unknown or expired JSON request transfer");
        auto& pending = found->second;
        if (operation == "$request_chunk") {
            const auto hex = request["hex"].toString();
            if (!request["offset"].isInt() || request["offset"].toLongLong() != static_cast<long long>(pending.data.size()) ||
                hex.isEmpty() || hex.size() > 2200 || hex.size() % 2 || hex.size()/2 > pending.bytes - pending.data.size())
                throw std::invalid_argument("Invalid JSON request chunk");
            auto nibble = [](char c) -> unsigned { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; throw std::invalid_argument("Invalid request encoding"); };
            SwString chunk;
            for (std::size_t i = 0; i < hex.size(); i += 2) chunk += static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i+1]));
            pending.data += chunk; pending.touched = now; return {};
        }
        if (operation != "$request_commit") throw std::invalid_argument("Unknown JSON request transfer operation");
        if (pending.data.size() != pending.bytes) throw std::invalid_argument("Incomplete JSON request transfer");
        auto data = std::move(pending.data); pending_.erase(found);
        SwJsonDocument document; SwString error;
        if (!document.loadFromJson(data, error) || !document.isObject()) throw std::invalid_argument("Invalid transferred JSON request");
        if (document.object()["operation"].toString().startsWith("$request_")) throw std::invalid_argument("Nested JSON request transfer");
        return handler(document.object());
    }
};

// Transport-independent upload state machine, usable by blocking or async RPC
// clients. Every intermediate reply is checked before issuing the next packet.
class JsonRequestUpload {
    SwString encoded_, token_;
    std::size_t offset_{0};
    bool final_{false};
    static SwString encode(const SwJsonObject& value) { return SwJsonDocument(value).toJson(SwJsonDocument::JsonFormat::Compact); }
public:
    explicit JsonRequestUpload(SwString encoded) : encoded_(std::move(encoded)) {
        if (encoded_.size() > JsonRequestAssembler::maxBytes) throw std::invalid_argument("JSON request exceeds 2 MiB");
    }
    SwString abortPacket() const {
        if (token_.isEmpty()) return {};
        SwJsonObject value; value["operation"] = SwJsonValue("$request_abort");
        value["request_token"] = SwJsonValue(token_); return encode(value);
    }
    SwString first() {
        final_ = encoded_.size() <= 3000;
        if (final_) return encoded_;
        SwJsonObject value; value["operation"] = SwJsonValue("$request_begin");
        value["bytes"] = SwJsonValue(static_cast<long long>(encoded_.size())); return encode(value);
    }
    bool advance(const SwString& response, SwString& next) {
        if (final_) return false;
        SwJsonDocument document; SwString error;
        if (!document.loadFromJson(response, error) || !document.isObject() || !document.object()["ok"].isBool())
            throw std::runtime_error("Invalid JSON upload response");
        const auto envelope = document.object();
        if (!envelope["ok"].toBool()) throw std::runtime_error(envelope["error"].toString().c_str());
        if (token_.isEmpty()) {
            token_ = envelope["data"].toObject()["request_token"].toString();
            if (token_.isEmpty()) throw std::runtime_error("Missing JSON upload token");
        }
        SwJsonObject value; value["request_token"] = SwJsonValue(token_);
        if (offset_ < encoded_.size()) {
            value["operation"] = SwJsonValue("$request_chunk"); value["offset"] = SwJsonValue(static_cast<long long>(offset_));
            const auto end = std::min(encoded_.size(), offset_ + 1100); SwString hex;
            constexpr char digits[] = "0123456789abcdef";
            while (offset_ < end) { const auto byte = static_cast<unsigned char>(encoded_[offset_++]); hex += digits[byte >> 4]; hex += digits[byte & 15]; }
            value["hex"] = SwJsonValue(hex);
        } else { value["operation"] = SwJsonValue("$request_commit"); final_ = true; }
        next = encode(value); return true;
    }
};
}
