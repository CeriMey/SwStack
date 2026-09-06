#pragma once
#include <cstdint>
#include <deque>
#include <vector>

namespace sw { namespace ipc { namespace detail {
// One reusable buffer per active serialization on each thread. A deque keeps
// outer leases stable if a user codec publishes another signal recursively.
class ScratchBuffer {
    struct Pool { std::deque<std::vector<uint8_t>> buffers; size_t depth{0}; };
    static Pool& pool() { static thread_local Pool value; return value; }
public:
    explicit ScratchBuffer(size_t size) {
        auto& storage = pool();
        if (storage.depth == storage.buffers.size()) storage.buffers.emplace_back();
        buffer_ = &storage.buffers[storage.depth];
        buffer_->resize(size);
        ++storage.depth;
    }
    ~ScratchBuffer() { --pool().depth; }
    ScratchBuffer(const ScratchBuffer&) = delete;
    ScratchBuffer& operator=(const ScratchBuffer&) = delete;
    uint8_t* data() { return buffer_->data(); }
    size_t size() const { return buffer_->size(); }
private:
    std::vector<uint8_t>* buffer_;
};
}}}
