#pragma once

#include "core/storage/SwRealtimeDb.h"
#include "SwTableDb.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>


namespace swRealtimeDbDetail {
using Rows = std::map<SwString, SwJsonObject>;
constexpr std::size_t kMaxBytes = 1024 * 1024;
constexpr std::size_t kMaxTables = 1024;
constexpr std::size_t kMaxDatabaseBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxRows = 1024;
constexpr std::size_t kMaxColumns = 64;
constexpr std::size_t kJournalSize = 256;
constexpr std::size_t kJournalBytes = 256 * 1024;
constexpr int kLeaseMs = 5000;

SwString requiredString(const SwJsonObject& object, const char* field,
                        std::size_t maxLength = 128);
void validateName(const SwString& name);
void validateActor(const SwString& name);
SwString columnType(const SwJsonValue& descriptor);
bool columnNullable(const SwJsonValue& descriptor);
SwJsonObject qualifiedIdentity(const SwString& name);
void validateJson(const SwJsonValue& value, unsigned depth = 0);
void validateJson(const SwJsonObject& object, unsigned depth = 0);
SwString decimal(std::uint64_t value);
std::uint64_t parseRevision(const SwString& text);
SwString token();
std::int64_t wallTimeMs();
SwJsonArray rowArray(Rows rows);
} // namespace swRealtimeDbDetail

class SwThreadPool;
struct SwRealtimeDb::State {
    using Clock = std::chrono::steady_clock;
    struct Session {
        SwString actor;
        Clock::time_point deadline;
    };
    struct Table {
        enum class ColumnType { String, Bool, Number, Integer, Object, Array, Json };
        struct Column { SwString name; ColumnType type; bool nullable; };
        SwString name;
        SwString owner;
        SwString ownerSession;
        SwString key;
        SwJsonObject columns;
        // Parsed once with the immutable descriptor; no row values are held.
        std::vector<Column> validationColumns;
        SwJsonObject metadata;
        // Schema metadata is immutable after parsing and shared by write plans.
        // This holds no row values or retained database snapshot.
        mutable std::shared_ptr<const SwTableDbPreparedSchema> storage;
        mutable std::size_t descriptorBytes = 0;
        int schemaVersion = 1;
        std::size_t maxRows = swRealtimeDbDetail::kMaxRows;
        SwString overflowPolicy{"reject"};
        std::set<SwString> writers;
        std::vector<SwString> dependencies;
        SwString script;
        ViewProgram program;
        SwString encodeScript, writeTarget;
        bool encodeMerge{false};
        ViewProgram encodeProgram;
        std::uint64_t encodeEvaluations{0};
        std::uint64_t jsEvaluations{0};
        std::uint64_t valueRevision{0};
        std::map<SwString, std::uint64_t> inputVersions;
        bool view = false;
        bool dirty = false;
        bool valid = false;
        SwString error = "awaiting first publication";
        std::uint64_t revision = 0;
        std::int64_t lastWriteMs = 0;
        std::map<SwString, std::uint64_t> writeOrder;
        std::uint64_t evictedRows{0};
        std::size_t bytes = 0;
    };
    struct Event {
        std::uint64_t revision;
        SwString table;
        SwJsonObject json;
        bool changed;
        std::size_t bytes;
    };
    struct Subscription {
        SwString session;
        SwString table;
        SwString mode;
        bool catalog{false};
        bool includeRows{false};
        std::set<SwString> dependencies;
    };

    explicit State(Transform evaluator, const SwRealtimeDbOptions& settings, PrepareView compiler);
    ~State();
    SwRealtimeDbOptions options;
    mutable SwTableDb database;
    Transform transform;
    PrepareView prepare;
    SwString epoch = swRealtimeDbDetail::token();
    std::uint64_t revision = 0;
    std::map<SwString, Session> sessions;
    std::map<SwString, Table> tables;
    std::map<SwString, Subscription> subscriptions;
    std::map<SwString, std::size_t> observers;
    std::deque<Event> journal;
    std::size_t journalBytes = 0;
    std::size_t bytes = 0;
    std::uint64_t writeSequence = 0;
    std::size_t graphTableCount = static_cast<std::size_t>(-1);
    std::map<SwString, std::size_t> viewRank;
    std::map<SwString, std::size_t> viewLevel;
    std::map<SwString, std::vector<SwString>> dependents;
    std::unique_ptr<SwThreadPool> viewWorkers;

    const Session& session(const SwJsonObject& request) const;
    Table& table(const SwString& name);
    SwJsonObject hello(const SwJsonObject& request);
    SwJsonObject heartbeat(const SwJsonObject& request);
    SwJsonObject subscribe(const SwJsonObject& request);
    SwJsonObject unsubscribe(const SwJsonObject& request);
    SwJsonObject subscriptionSnapshot(const SwJsonObject& request);
    SwJsonObject subscriptionValues(const Subscription& subscription);
    SwJsonObject snapshots(const std::set<SwString>& names);
    SwJsonObject changesWithValues(const SwJsonObject& request);
    SwJsonObject registerTable(const SwJsonObject& request, bool view);
    enum class MutationOutcome { Rejected, Unknown, Applied };
    SwJsonObject write(const SwJsonObject& request, bool erase, MutationOutcome* outcome = nullptr,
                       const SwJsonObject* batchEnvelope = nullptr);
    SwJsonObject writeView(Table& view, const SwJsonObject& request, bool erase,
                          MutationOutcome* outcome, const SwJsonObject* batchEnvelope);
    SwJsonObject mutateMany(const SwJsonObject& request);
    SwJsonObject read(const SwString& name, const SwJsonObject& selection = {});
    SwJsonObject introspect(const SwJsonObject& request) const;
    SwJsonObject changes(const SwJsonObject& request) const;
    void expire();

    Table parseTable(const SwJsonObject& request, bool view,
                     const SwString& owner, const SwString& ownerSession) const;
    swRealtimeDbDetail::Rows validateRows(const Table& table, const SwJsonArray& values,
                                         SwJsonArray* orderedKeys = nullptr) const;
    swRealtimeDbDetail::Rows validateRowsOwned(const Table& table, SwJsonArray&& values,
                                              SwJsonArray* orderedKeys = nullptr) const;
    swRealtimeDbDetail::Rows validateRowsImpl(const Table& table, const SwJsonArray& values,
                                             SwJsonArray* owned, SwJsonArray* orderedKeys) const;
    bool rowsEqual(const Table& table, const swRealtimeDbDetail::Rows& rows) const;
    SwJsonArray mergeRows(const Table& table, const SwJsonArray& patches,
                         const SwJsonArray& increments, std::map<SwString, SwDbJsonRecord>& previous) const;
    void validateGraph(const Table& candidate) const;
    std::size_t tableBytes(const Table& table, const swRealtimeDbDetail::Rows& rows) const;
    void checkCapacity(const Table& table, std::size_t replacementBytes) const;
    void record(Table& table, const char* kind, bool changed,
                const SwJsonArray& keys = SwJsonArray(),
                const SwJsonArray& evicted = SwJsonArray());
    void recompute(const std::set<SwString>& changed);
    void updateViewGraph();
    void materialize(const std::set<SwString>& names);
    SwString pendingViewError(const Table& table) const;
    bool reuseView(Table& table);
    void refreshView(Table& table);
    SwJsonArray evaluateView(const SwString& script, const ViewProgram& program, const SwJsonObject& inputs) const;
    SwString viewInputs(const Table& table, SwJsonObject& inputs) const;
    void publishView(Table& table, SwJsonArray output, SwString failure);
    bool ownerAlive(const Table& table) const;
    const SwTableDbPreparedSchema& storageSchema(const Table& table) const;
    swRealtimeDbDetail::Rows readRows(const Table& table) const;
    void storeRows(const Table& table, swRealtimeDbDetail::Rows rows,
                   const std::map<SwString, std::uint64_t>& order);
    void storeDelta(const Table& table, swRealtimeDbDetail::Rows rows,
                    const std::map<SwString, std::uint64_t>& order,
                    const std::set<SwString>& erased);
    SwJsonArray retainKeys(const Table& table, std::map<SwString, std::uint64_t>& order) const;
    SwJsonArray retainRows(const Table& table, swRealtimeDbDetail::Rows& rows,
                          std::map<SwString, std::uint64_t>& order) const;
    void persistDescriptor(const Table& table);
    void restore();
};
