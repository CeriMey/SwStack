#include "SwRosBridgeSignal.h"

namespace swros {
using namespace swapi::wire;

Signal::Signal(const Binding& binding) {
    SwString domain, object;
    Catalog::splitTarget(binding.target, domain, object);
    if (binding.channel.startsWith("__")) throw std::runtime_error("internal channels cannot be exposed as topics");
    if (!findSignalInRegistryForTarget(domain, object, binding.channel, info_)) throw std::runtime_error("signal not found");
    types_ = parseArgTypesFromTypeName(info_.typeName.toStdString());
    uint32_t capacity = 0;
    sw::ipc::DeliveryMode mode = sw::ipc::DeliveryMode::Replay;
    if (!sw::ipc::RingQueueDynamic<>::inspect(info_.shmName, info_.typeId, capacity, maxPayload_, mode))
        throw std::runtime_error("signal ring unavailable");
}

bool Signal::read(uint64_t& sequence, SwJsonArray& values) {
    std::vector<uint8_t> bytes;
    uint64_t next = 0, origin = 0;
    if (!sw::ipc::RingQueueDynamic<>::readLatestBytes(info_.shmName, info_.typeId, bytes, next, origin)
        || next == sequence) return false;
    sw::ipc::detail::Decoder dec(bytes.data(), bytes.size());
    values = SwJsonArray();
    for (const auto& type : types_) {
        SwJsonValue value; SwString error;
        if (!decodeJsonValueByType(dec, type, value, error)) throw std::runtime_error(error.toStdString());
        values.append(value);
    }
    sequence = next;
    return true;
}

void Signal::publish(const SwJsonArray& values) {
    if (values.size() != types_.size()) throw std::runtime_error("signal argument count mismatch");
    std::vector<uint8_t> bytes(maxPayload_);
    sw::ipc::detail::Encoder enc(bytes.data(), bytes.size());
    for (size_t i = 0; i < values.size(); ++i) {
        SwString error;
        if (!encodeJsonArg(enc, types_[i], values[i], error)) throw std::runtime_error(error.toStdString());
    }
    if (!sw::ipc::RingQueueDynamic<>::publishBytes(info_.shmName, info_.typeId, bytes.data(), enc.size()))
        throw std::runtime_error("signal ring full or payload exceeds capacity");
}
}
