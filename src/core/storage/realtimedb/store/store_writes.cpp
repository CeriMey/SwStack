#include "StoreState.hpp"
#include "../JsonSize.hpp"
#include <limits>
#include <stdexcept>

SwJsonObject SwRealtimeDb::State::write(const SwJsonObject& request, bool erase, MutationOutcome* outcome,
                                           const SwJsonObject* batchEnvelope) {
    if (outcome) *outcome = MutationOutcome::Rejected;
    if (request.contains("include_rows") && !request["include_rows"].isBool())
        throw std::runtime_error("include_rows must be a boolean");
    const bool includeRows = !batchEnvelope &&
        (!request.contains("include_rows") || request["include_rows"].toBool());
    if (request.contains("merge") && (!request["merge"].isBool() || erase))
        throw std::runtime_error("merge must be a boolean on a write");
    const bool merge = request["merge"].toBool();
    if (request.contains("increments") && (!merge || !request["increments"].isArray()))
        throw std::runtime_error("increments require a merging write and an array");
    const auto& credentials = batchEnvelope ? *batchEnvelope : request;
    const auto actor = session(credentials).actor;
    const auto name = swRealtimeDbDetail::requiredString(request, "table");
    const auto existing = tables.find(name);
    if (existing != tables.end() && existing->second.view)
        return writeView(existing->second, request, erase, outcome, batchEnvelope);
    const bool create = existing == tables.end();
    Table candidate;
    Table* plan = create ? nullptr : &existing->second;
    if (request.contains("definition")) {
        if (erase || !request["definition"].isObject())
            throw std::runtime_error("write definition must be a table descriptor");
        auto definition = request["definition"].toObject();
        if (!definition.contains("table")) definition["table"] = name;
        if (definition["table"].toString() != name) throw std::runtime_error("definition table name mismatch");
        candidate = parseTable(definition, false, actor,
                             swRealtimeDbDetail::requiredString(credentials, "session"));
        plan = &candidate;
        if (!create) {
            const auto& current = existing->second;
            if (current.owner != actor || current.view || current.key != candidate.key ||
                current.columns != candidate.columns || current.metadata != candidate.metadata || current.maxRows != candidate.maxRows ||
                current.overflowPolicy != candidate.overflowPolicy || current.writers != candidate.writers)
                throw std::runtime_error("registered table descriptor mismatch");
            candidate = current;
            const auto lease = swRealtimeDbDetail::requiredString(credentials, "session");
            if (candidate.ownerSession != lease) {
                // A definition-bearing first write also reclaims this actor's
                // persistent/expired table. Old rows remain stale until this
                // validated fresh snapshot commits successfully.
                candidate.ownerSession = lease;
                candidate.valid = false;
            }
        }
    } else {
        if (create) throw std::runtime_error("unknown table; first write requires its definition");
    }
    auto& planned = *plan;
    if (create && tables.size() >= swRealtimeDbDetail::kMaxTables)
        throw std::runtime_error("table limit exceeded");
    if (planned.view) throw std::runtime_error("views are read-only");
    if (!ownerAlive(planned)) throw std::runtime_error("table owner is unavailable or must register again");
    if (actor != planned.owner && !planned.writers.count(actor))
        throw std::runtime_error("actor is not an authorized writer");
    if (actor != planned.owner && !planned.valid)
        throw std::runtime_error("table owner must publish a fresh snapshot before external writes");
    if (erase && !planned.valid) throw std::runtime_error("publish a fresh snapshot before erasing stale data");

    swRealtimeDbDetail::Rows replacement;
    std::map<SwString, SwDbJsonRecord> previousRows;
    std::set<SwString> erased;
    // Immutable descriptors remain in the catalog. Only mutable publication
    // state is staged; a rejected write cannot alter the existing table.
    auto order = planned.valid ? planned.writeOrder : std::map<SwString, std::uint64_t>{};
    auto nextSequence = writeSequence;
    SwJsonArray keys;
    if (erase) {
        if (!request["keys"].isArray()) throw std::runtime_error("keys must be an array");
        const auto values = request["keys"].toArray();
        if (values.size() > swRealtimeDbDetail::kMaxRows) throw std::runtime_error("batch key limit exceeded");
        std::set<SwString> seen;
        for (const auto& value : values) {
            if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 128)
                throw std::runtime_error("erase keys must be nonempty strings of at most 128 bytes");
            const auto key = value.toString();
            if (!seen.insert(key).second) throw std::runtime_error("duplicate key within batch");
            erased.insert(key); order.erase(key); keys.append(key);
        }
    } else {
        if (!request["rows"].isArray()) throw std::runtime_error("rows must be an array");
        const SwJsonArray empty;
        const auto supplied = request["rows"].toArrayPtr();
        const auto increments = request["increments"].toArrayPtr();
        SwJsonArray merged;
        if (merge) merged = mergeRows(planned, supplied ? *supplied : empty,
                                     increments ? *increments : empty, previousRows);
        auto validated = merge ? validateRowsOwned(planned, std::move(merged), &keys)
                               : validateRows(planned, supplied ? *supplied : empty, &keys);
        // Array order, not lexicographic key order, defines recency within a batch.
        for (const auto& value : keys) {
            const auto key = value.toString();
            if (nextSequence == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("write sequence exhausted");
            replacement.insert_or_assign(key, std::move(validated.at(key)));
            order[key] = ++nextSequence;
        }
    }
    const auto evicted = retainKeys(planned, order);
    for (const auto& value : evicted) {
        const auto key = value.toString();
        replacement.erase(key);
        erased.insert(key);
    }
    bool changed = !planned.valid;
    std::size_t newBytes;
    if (planned.valid) {
        // Existing tables need only the old values of touched/evicted keys.
        // No snapshot or database iterator survives into the atomic write,
        // avoiding a copy of the memory database on every mutation.
        const auto& schema = storageSchema(planned);
        newBytes = planned.bytes;
        auto account = [&](const SwString& key, const SwJsonObject* next) {
            SwDbJsonRecord previous;
            const bool existed = existing->second.writeOrder.count(key) != 0;
            if (existed) {
                const auto loaded = previousRows.find(key);
                if (loaded != previousRows.end()) previous = std::move(loaded->second);
                else {
                    const auto status = database.getRowRecord(schema, key, &previous);
                    if (!status.ok()) throw std::runtime_error("SwTableDb: " + status.message().toStdString());
                }
                const auto oldBytes = previous.byteSize("__sw_rtdb") + key.size() + 4;
                if (oldBytes > newBytes) throw std::logic_error("invalid table byte accounting");
                newBytes -= oldBytes;
            }
            if (next) newBytes += swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxDatabaseBytes)
                .object(*next) + key.size() + 4;
            changed = changed || (next ? (!existed || !previous.equals(*next, "__sw_rtdb")) : existed);
        };
        for (const auto& row : replacement) account(row.first, &row.second);
        for (const auto& key : erased) account(key, nullptr);
    } else {
        // Initial publication and producer recovery replace stale retained
        // rows with the complete fresh snapshot supplied by the owner.
        newBytes = tableBytes(planned, replacement);
    }
    const auto retainedBytes = bytes - (create ? 0 : planned.bytes);
    if (newBytes > swRealtimeDbDetail::kMaxBytes || retainedBytes > swRealtimeDbDetail::kMaxDatabaseBytes - newBytes)
        throw std::runtime_error("RtDb aggregate data limit is 8 MiB");
    // All request validation and retention decisions precede storage and catalog
    // publication. Updates and evictions share one SwTableDb atomic batch.
    if (outcome) *outcome = MutationOutcome::Unknown;
    if (planned.valid) storeDelta(planned, std::move(replacement), order, erased);
    else storeRows(planned, std::move(replacement), order);
    if (create) persistDescriptor(planned);
    planned.bytes = newBytes;
    planned.writeOrder = std::move(order);
    planned.evictedRows += evicted.size();
    planned.valid = true; planned.error = "";
    planned.lastWriteMs = swRealtimeDbDetail::wallTimeMs();
    writeSequence = nextSequence;
    bytes = retainedBytes + newBytes;
    if (create) tables.emplace(name, std::move(candidate));
    else if (plan == &candidate) existing->second = std::move(candidate);
    auto& target = tables.at(name);
    if (create) record(target, "created", true);
    record(target, erase ? "erase" : "write", changed, keys, evicted);
    if (outcome) *outcome = MutationOutcome::Applied;
    if (changed) recompute({name});
    SwJsonObject response;
    if (batchEnvelope) {
        // mutateMany returns only a mutation receipt. Avoid constructing and
        // immediately discarding a full read envelope for every child write.
        response["revision"] = swRealtimeDbDetail::decimal(target.revision);
        response["cursor"] = swRealtimeDbDetail::decimal(revision);
    } else {
        SwJsonObject selection; if (!includeRows) selection["limit"] = 0;
        response = read(name, selection);
        if (!includeRows) response.remove("rows");
    }
    response["row_count"] = static_cast<long long>(target.writeOrder.size());
    response["evicted_keys"] = evicted;
    return response;
}
