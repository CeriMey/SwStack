#pragma once
#include <core/types/SwJsonObject.h>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace swRealtimeDbDetail {
// One immutable metadata index for a native delivery, shared by its readers.
// Never expose the JSON tree: const SwJsonObject still permits mutable child
// pointers. Public callbacks receive detached packets/events instead.
class ChangeBatch final {
public:
    struct Entry { std::size_t index; std::uint64_t revision; bool changed; };
    explicit ChangeBatch(SwJsonObject packet) : packet_(std::move(packet)),
        epoch_(packet_["epoch"].toString()), revisionText_(packet_["revision"].toString()),
        revision_(std::stoull(revisionText_.toStdString())),
        hasFrom_(packet_.contains("from_revision")),
        from_(hasFrom_ ? std::stoull(packet_["from_revision"].toString().toStdString()) : 0),
        resync_(packet_["resync_required"].toBool()),
        hasTables_(packet_.contains("tables")), catalogChanged_(packet_["catalog_changed"].toBool()) {
        const auto names = packet_["tables"].toArrayPtr();
        hasNames_ = bool(names);
        if (names) for (const auto& name : *names) tables_.insert(name.toString());
        const auto values = packet_["events"].toArrayPtr();
        complete_ = hasFrom_ && !resync_ && values && from_ <= revision_ &&
                    values->size() == revision_ - from_;
        auto expected = from_;
        if (values) for (std::size_t i = 0; i < values->size(); ++i) {
            const auto value = (*values)[i].toObjectPtr();
            if (!value) { complete_ = false; continue; }
            const auto number = std::stoull((*value)["revision"].toString().toStdString());
            if (complete_ && number != ++expected) complete_ = false;
            events_[(*value)["table"].toString()].push_back({i, number, (*value)["changed"].toBool()});
        }
    }
    const SwString& epoch() const { return epoch_; }
    const SwString& revisionText() const { return revisionText_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t from() const { return from_; }
    bool hasFrom() const { return hasFrom_; }
    bool resync() const { return resync_; }
    bool complete() const { return complete_; }
    bool hasTables() const { return hasTables_; }
    bool hasNames() const { return hasNames_; }
    bool catalogChanged() const { return catalogChanged_; }
    bool containsTable(const SwString& table) const { return tables_.count(table) != 0; }
    const std::vector<Entry>* eventsFor(const SwString& table) const {
        const auto found = events_.find(table);
        return found == events_.end() ? nullptr : &found->second;
    }
    SwJsonObject detachPacket() const { return packet_; }
    SwJsonObject detachEvent(std::size_t index) const {
        const auto values = packet_["events"].toArrayPtr();
        auto event = (*values)[index].toObject();
        event["epoch"] = epoch_;
        return event;
    }
    // Only explicit value subscriptions use these snapshots. Public callbacks
    // receive detached trees, never mutable aliases into the shared notice.
    bool hasSnapshot(const SwString& table) const {
        const auto snapshots=packet_["snapshots"].toObjectPtr();
        return snapshots && snapshots->contains(table);
    }
    std::uint64_t snapshotRevision(const SwString& table) const {
        const auto snapshots = packet_["snapshots"].toObjectPtr();
        const auto snapshot = snapshots ? (*snapshots)[table].toObjectPtr() : nullptr;
        const auto value = snapshot ? (*snapshot)["revision"].toString() : SwString();
        return value.isEmpty() ? 0 : std::stoull(value.toStdString());
    }
    SwJsonObject detachSnapshot(const SwString& table) const {
        const auto snapshots=packet_["snapshots"].toObjectPtr();
        return snapshots?(*snapshots)[table].toObject():SwJsonObject();
    }
private:
    // Public construction detaches an lvalue; owned service packets are moved.
    const SwJsonObject packet_;
    const SwString epoch_, revisionText_;
    const std::uint64_t revision_;
    const bool hasFrom_;
    const std::uint64_t from_;
    const bool resync_, hasTables_, catalogChanged_;
    bool hasNames_{false}, complete_{false};
    std::set<SwString> tables_;
    std::map<SwString, std::vector<Entry>> events_;
};
}
