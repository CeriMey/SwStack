#pragma once
#include <core/types/SwJsonSize.h>
#include <mutex>

class SwDbWriteBatch;
class SwDbJsonRecord;
class SwEmbeddedDb;
namespace swEmbeddedDbDetail { class MemoryManager_; }

namespace swEmbeddedDbDetail {

// Never exposes its object: even const JSON permits mutable child pointers.
class JsonPayload_ {
public:
    explicit JsonPayload_(const SwJsonObject& object) : object_(object) {}
    void copyTo(SwJsonObject& out) const { out = object_; }
    SwByteArray bytes() const {
        return SwByteArray(SwJsonDocument(object_).toJson(SwJsonDocument::JsonFormat::Compact).toStdString());
    }
    bool equals(const SwJsonObject& candidate, const SwString& excluded) const {
        const bool omit = !excluded.isEmpty();
        auto left = object_.dataRef().begin(), right = candidate.dataRef().begin();
        const auto leftEnd = object_.dataRef().end(), rightEnd = candidate.dataRef().end();
        for (;;) {
            if (left != leftEnd && omit && left->first == excluded) ++left;
            if (right != rightEnd && omit && right->first == excluded) ++right;
            if (left == leftEnd || right == rightEnd) return left == leftEnd && right == rightEnd;
            if (left->first != right->first || left->second != right->second) return false;
            ++left; ++right;
        }
    }

    std::size_t byteSize(const SwString& excluded) const {
        const auto measure = [&] { return SwJsonSize(std::numeric_limits<std::size_t>::max()).object(object_); };
        std::size_t size;
        if (std::fegetround() == FE_TONEAREST) {
            std::call_once(sizeOnce_, [&] { byteSize_ = measure(); });
            size = byteSize_;
        } else size = measure(); // SwJson's writer observes the current rounding mode.
        if (!excluded.isEmpty()) {
            const auto found = object_.dataRef().find(excluded);
            if (found != object_.dataRef().end()) {
                size -= SwJsonSize(std::numeric_limits<std::size_t>::max()).member(found->first, found->second);
                if (object_.size() > 1) --size;
            }
        }
        return size;
    }

private:
    friend class ::SwDbWriteBatch;
    friend class ::SwDbJsonRecord;
    explicit JsonPayload_(SwJsonObject&& object) : object_(std::move(object)) {}
    const SwJsonObject object_;
    mutable std::once_flag sizeOnce_;
    mutable std::size_t byteSize_{0};
};

inline bool parseJsonObject_(const SwByteArray& bytes, SwJsonObject& out) {
    SwString error;
    const auto document = SwJsonDocument::fromJson(SwString(bytes.toStdString()), error);
    if (!error.isEmpty() || !document.isObject()) return false;
    out = document.object();
    return true;
}

} // namespace swEmbeddedDbDetail

// An immutable point-in-time record, retaining only this payload, never a
// database snapshot. Detached JSON is the only way to access its values.
class SwDbJsonRecord {
public:
    bool isValid() const { return bool(payload_); }
    SwJsonObject detach() const {
        SwJsonObject result;
        if (payload_) payload_->copyTo(result);
        return result;
    }
    bool equals(const SwJsonObject& candidate, const SwString& excludedRootField = {}) const {
        return payload_ && payload_->equals(candidate, excludedRootField);
    }
    // Compact finite JSON byte count, at most 64 nesting levels (root at 0).
    // Throws for deeper/non-finite JSON; admission through existing APIs is
    // unchanged. The excluded member is ignored on both sides of equals(),
    // and removed with its comma from byteSize().
    std::size_t byteSize(const SwString& excludedRootField = {}) const {
        return payload_ ? payload_->byteSize(excludedRootField) : 0;
    }
private:
    friend class SwEmbeddedDb;
    friend class swEmbeddedDbDetail::MemoryManager_;
    static bool fromBytes_(const SwByteArray& bytes, SwDbJsonRecord& out) {
        SwJsonObject parsed;
        if (!swEmbeddedDbDetail::parseJsonObject_(bytes, parsed)) return false;
        out.payload_.reset(new swEmbeddedDbDetail::JsonPayload_(std::move(parsed)));
        return true;
    }
    std::shared_ptr<const swEmbeddedDbDetail::JsonPayload_> payload_;
};
