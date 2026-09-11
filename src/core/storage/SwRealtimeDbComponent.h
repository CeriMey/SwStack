#pragma once
#include <core/storage/SwRealtimeDbClient.h>
#include <core/remote/SwRemoteObject.h>
#include <core/types/SwJsonArray.h>
#include <cstdint>
#include <memory>

// Common SwNode component for producers and consumers. Concrete actors own
// acquisition and declarations; this class owns IPC, leases and subscriptions.
class SwRealtimeDbComponent : public SwRemoteObject {
    SW_OBJECT(SwRealtimeDbComponent, SwRemoteObject)
public:
    using Subscription = std::function<void(const SwJsonObject& event)>;
    SwRealtimeDbComponent(const SwString& sys, const SwString& ns, const SwString& name,
                  SwObject* parent = nullptr);
    SwRealtimeDbComponent(const SwString& sys, const SwString& ns, const SwString& name,
                          const SwString& endpoint, SwObject* parent = nullptr);
    ~SwRealtimeDbComponent() override;
    void declareTable(SwJsonObject definition);
    void declareView(SwJsonObject definition);
    bool ready() const;
    SwRealtimeDbClient& database();
    // Default true preserves snapshot replies. False returns only write/erase
    // acknowledgement metadata, without downloading the whole modified table.
    void setIncludeWriteRows(bool include);
    void writeRows(const SwString& table, const SwJsonArray& rows, SwRealtimeDbCompletion complete = {});
    // Ensures the table on first successful write, then remembers its descriptor
    // for reconnection. The actor must be connected (ready()) before publishing.
    void writeRows(SwJsonObject definition, const SwJsonArray& rows, SwRealtimeDbCompletion complete = {});
    void eraseRows(const SwString& table, const SwJsonArray& keys, SwRealtimeDbCompletion complete = {});
    // Merge sparse rows atomically in the owning database. Objects merge
    // recursively, arrays/scalars replace, null is a value. Optional increments:
    // [{key: rowKey, path: [column, nestedField, ...], amount: number}] (max 64).
    void patchRows(const SwString& table, const SwJsonArray& rows,
                   const SwJsonArray& increments = {}, SwRealtimeDbCompletion complete = {});
    // One RPC for 1..64 ordered {op:write|erase, table, rows|keys, definition?}.
    // Stops on the first failure; previous writes remain committed. Replies are
    // compact (no rows), with per-operation applied/failed/unknown/not_run
    // outcomes. Partial failure sets reply.ok=false and preserves reply.data.
    // Local descriptor conflicts are rejected before dispatch.
    // A transport error without outcomes must not be retried as an atomic unit.
    void mutateMany(const SwJsonArray& operations, SwRealtimeDbCompletion complete = {});
    void readTable(const SwString& table, SwRealtimeDbCompletion complete);
    // One snapshot of an explicit set (up to 64). Missing tables have valid=false.
    void readTables(const SwJsonArray& tables, SwRealtimeDbCompletion complete);
    // First callback: kind=snapshot with rows + revision. Subsequent callbacks
    // carry journal metadata, not a historical row copy. Read to get latest rows.
    // kind=resync explicitly reports a journal gap or service restart.
    std::uint64_t subscribe(const SwString& table, const SwString& mode, Subscription callback);
    // Latest-value subscription: snapshot/write callbacks include rows, valid,
    // epoch/revision and selected dependency snapshots under "dependencies".
    // Multiple writes before delivery coalesce to the latest committed value.
    // write/change filtering is preserved; callbacks own detached values.
    std::uint64_t subscribeValues(const SwString& table, const SwString& mode, Subscription callback,
                                  const SwJsonArray& dependencies = {});
    // First callback is catalog_snapshot; later created events name a table/view.
    // A resync callback precedes a new catalog snapshot after a journal gap.
    std::uint64_t subscribeTables(Subscription callback);
    void unsubscribe(std::uint64_t subscription);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
