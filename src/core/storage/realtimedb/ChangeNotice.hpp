#pragma once
#include <core/remote/SwSharedMemorySignal.h>
#include "ChangeBatch.hpp"
#include "SwRealtimeDbJson.h"
#include <memory>
#include <utility>

namespace swRealtimeDbDetail {
inline constexpr std::size_t changeNoticeCapacity = 65536;
// One signal has two representations: native readers share the complete,
// immutable batch; the wire carries the existing compact JSON wakeup. Sharing
// the envelope also avoids copying its encoded hint for each native receiver.
class ChangeNotice final {
public:
    ChangeNotice() = default;
    ChangeNotice(std::shared_ptr<const ChangeBatch> batch, SwString wire)
        : contents_(std::make_shared<const Contents>(std::move(batch), std::move(wire))) {}

    const std::shared_ptr<const ChangeBatch>& batch() const {
        static const std::shared_ptr<const ChangeBatch> empty;
        return contents_ ? contents_->batch : empty;
    }
    const SwString& wire() const {
        static const SwString empty;
        return contents_ ? contents_->wire : empty;
    }
private:
    struct Contents {
        Contents(std::shared_ptr<const ChangeBatch> batch, SwString wire)
            : batch(std::move(batch)), wire(std::move(wire)) {}
        const std::shared_ptr<const ChangeBatch> batch;
        const SwString wire;
    };
    std::shared_ptr<const Contents> contents_;
};
} // namespace swRealtimeDbDetail

namespace sw { namespace ipc { namespace detail {
// The SHM payload and its advertised identity deliberately remain SwString.
// NativeSignalRegistry keys channels by the actual C++ Channel type, so it
// cannot confuse a ChangeNotice envelope with a SwString subscriber. The latter
// follows the normal wire path and remains compatible with existing tools.
template <> inline uint64_t type_id<swRealtimeDbDetail::ChangeNotice>() {
    return type_id<SwString>();
}
template <> inline SwString type_name<swRealtimeDbDetail::ChangeNotice>() {
    return type_name<SwString>();
}
template <> struct Codec<swRealtimeDbDetail::ChangeNotice> {
    static bool write(Encoder& encoder, const swRealtimeDbDetail::ChangeNotice& value) {
        return value.batch() && Codec<SwString>::write(encoder, value.wire());
    }
    static bool read(Decoder& decoder, swRealtimeDbDetail::ChangeNotice& value) {
        // Reject an invalid length before the string codec allocates its buffer.
        auto probe = decoder;
        uint32_t length = 0;
        if (!probe.readPOD(length) || length > probe.cap - probe.pos) return false;
        try {
            SwString wire;
            if (!Codec<SwString>::read(decoder, wire)) return false;
            auto batch = std::make_shared<const swRealtimeDbDetail::ChangeBatch>(
                swRealtimeDbDetail::parseObject(wire));
            value = swRealtimeDbDetail::ChangeNotice(std::move(batch), std::move(wire));
            return true;
        } catch (const std::exception&) { return false; }
    }
};
}}} // namespace sw::ipc::detail
