#include "StoreState.hpp"
#include "../JsonSize.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>


SwRealtimeDb::SwRealtimeDb(Transform transform, const SwRealtimeDbOptions& options, PrepareView prepare)
    : state_(new State(std::move(transform), options, std::move(prepare))) {}
SwRealtimeDb::~SwRealtimeDb() = default;

SwJsonObject SwRealtimeDb::execute(const SwJsonObject& request) {
    swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxBytes, 32).object(request);
    state_->expire();
    const auto op = swRealtimeDbDetail::requiredString(request, "op");
    if (op == "hello") return state_->hello(request);
    if (op == "heartbeat") return state_->heartbeat(request);
    if (op == "subscribe") return state_->subscribe(request);
    if (op == "unsubscribe") return state_->unsubscribe(request);
    if (op == "subscription_snapshot") return state_->subscriptionSnapshot(request);
    if (op == "register_table") return state_->registerTable(request, false);
    if (op == "register_view") return state_->registerTable(request, true);
    if (op == "write") return state_->write(request, false);
    if (op == "erase") return state_->write(request, true);
    if (op == "mutate_many") return state_->mutateMany(request);
    if (op == "read") return state_->read(swRealtimeDbDetail::requiredString(request, "table"), request);
    if (op == "read_many") {
        if (!request["tables"].isArray() || request["tables"].toArray().size() > 64)
            throw std::runtime_error("read_many requires at most 64 explicit table names");
        std::set<SwString> names;
        for (const auto& value : request["tables"].toArray()) {
            if (!value.isString()) throw std::runtime_error("read_many table must be a name");
            const auto name = value.toString(); swRealtimeDbDetail::validateName(name);
            if (!names.insert(name).second) throw std::runtime_error("duplicate read_many table");
        }
        auto result = state_->snapshots(names);
        try { swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxBytes).object(result); }
        catch (const std::runtime_error&) {
            throw std::runtime_error("read_many snapshot exceeds 1 MiB; select fewer tables");
        }
        return result;
    }
    if (op == "introspect") return state_->introspect(request);
    if (op == "changes") return state_->changesWithValues(request);
    throw std::runtime_error("unknown operation");
}

void SwRealtimeDb::expire() { state_->expire(); }

const SwRealtimeDb::State::Session& SwRealtimeDb::State::session(const SwJsonObject& request) const {
    const auto found = sessions.find(swRealtimeDbDetail::requiredString(request, "session"));
    if (found == sessions.end() || found->second.deadline <= Clock::now())
        throw std::runtime_error("unknown or expired session");
    return found->second;
}

SwRealtimeDb::State::Table& SwRealtimeDb::State::table(const SwString& name) {
    const auto found = tables.find(name);
    if (found == tables.end()) throw std::runtime_error("unknown table: " + name.toStdString());
    return found->second;
}

SwJsonObject SwRealtimeDb::State::hello(const SwJsonObject& request) {
    const auto actor = swRealtimeDbDetail::requiredString(request, "actor");
    swRealtimeDbDetail::validateActor(actor);
    for (const auto& entry : sessions)
        if (entry.second.actor == actor) throw std::runtime_error("actor already has a live session");
    if (sessions.size() >= 128) throw std::runtime_error("session limit exceeded");
    SwString id;
    do { id = swRealtimeDbDetail::token(); } while (sessions.count(id));
    sessions.emplace(id, Session{actor, Clock::now() + std::chrono::milliseconds(swRealtimeDbDetail::kLeaseMs)});
    SwJsonObject response;
    response["session"] = id;
    response["epoch"] = epoch;
    response["lease_ms"] = swRealtimeDbDetail::kLeaseMs;
    return response;
}

SwJsonObject SwRealtimeDb::State::heartbeat(const SwJsonObject& request) {
    session(request);
    sessions.at(request["session"].toString()).deadline =
        Clock::now() + std::chrono::milliseconds(swRealtimeDbDetail::kLeaseMs);
    SwJsonObject response;
    response["epoch"] = epoch;
    response["revision"] = swRealtimeDbDetail::decimal(revision);
    response["lease_ms"] = swRealtimeDbDetail::kLeaseMs;
    return response;
}

SwJsonObject SwRealtimeDb::State::subscribe(const SwJsonObject& request) {
    session(request);
    const auto owner = swRealtimeDbDetail::requiredString(request, "session");
    const bool catalog = request["scope"].toString() == "catalog";
    const auto name = catalog ? SwString() : swRealtimeDbDetail::requiredString(request, "table");
    if (!catalog) swRealtimeDbDetail::validateName(name);
    const auto mode = swRealtimeDbDetail::requiredString(request, "mode");
    if (catalog ? mode != "create" : (mode != "write" && mode != "change"))
        throw std::runtime_error("invalid subscription mode for scope");
    if (subscriptions.size() >= 1024) throw std::runtime_error("subscription limit exceeded");
    if(request.contains("include_rows") && !request["include_rows"].isBool())
        throw std::runtime_error("subscription include_rows must be boolean");
    Subscription subscription{owner,name,mode,catalog};
    subscription.includeRows=request["include_rows"].toBool();
    if(catalog && subscription.includeRows)throw std::runtime_error("catalog subscription cannot include rows");
    if(request.contains("dependencies")) {
        if(!subscription.includeRows || !request["dependencies"].isArray() || request["dependencies"].toArray().size()>63)
            throw std::runtime_error("value subscription requires at most 63 dependencies");
        for(const auto& value:request["dependencies"].toArray()) {
            if(!value.isString())throw std::runtime_error("subscription dependency must be a table name");
            swRealtimeDbDetail::validateName(value.toString());subscription.dependencies.insert(value.toString());
        }
    }
    SwString id;
    do { id = swRealtimeDbDetail::token(); } while (subscriptions.count(id));
    if (!catalog) materialize({name});
    // Build initial values before registering, so a failed snapshot cannot
    // leak an observer or keep an otherwise lazy view active.
    SwJsonObject initial;
    if(subscription.includeRows)initial=subscriptionValues(subscription);
    subscriptions.emplace(id, std::move(subscription));
    if (!catalog) ++observers[name];
    SwJsonObject response;
    response["subscription"] = id;
    response["epoch"] = epoch;
    if(request["include_rows"].toBool())response["values"]=std::move(initial);
    return response;
}

SwJsonObject SwRealtimeDb::State::unsubscribe(const SwJsonObject& request) {
    session(request);
    const auto id = swRealtimeDbDetail::requiredString(request, "subscription");
    const auto found = subscriptions.find(id);
    if (found == subscriptions.end()) throw std::runtime_error("unknown subscription");
    if (found->second.session != request["session"].toString())
        throw std::runtime_error("subscription belongs to another session");
    if (!found->second.catalog && --observers.at(found->second.table) == 0)
        observers.erase(found->second.table);
    subscriptions.erase(found);
    SwJsonObject response;
    response["epoch"] = epoch;
    return response;
}

bool SwRealtimeDb::State::ownerAlive(const Table& item) const {
    const auto owner = sessions.find(item.ownerSession);
    return owner != sessions.end() && owner->second.actor == item.owner && owner->second.deadline > Clock::now();
}

SwJsonObject SwRealtimeDb::State::registerTable(const SwJsonObject& request, bool view) {
    Table candidate = parseTable(request, view, session(request).actor,
                                 swRealtimeDbDetail::requiredString(request, "session"));
    auto existing = tables.find(candidate.name);
    if (existing != tables.end()) {
        auto& current = existing->second;
        if (current.owner != candidate.owner) throw std::runtime_error("table belongs to another actor");
        if (current.view != candidate.view || current.key != candidate.key ||
            current.columns != candidate.columns || (!view && current.metadata != candidate.metadata) || current.schemaVersion != candidate.schemaVersion ||
            current.maxRows != candidate.maxRows || current.overflowPolicy != candidate.overflowPolicy ||
            current.writers != candidate.writers)
            throw std::runtime_error("registered table descriptor mismatch");
        if (current.dependencies != candidate.dependencies || current.script != candidate.script || current.metadata != candidate.metadata ||
            current.encodeScript != candidate.encodeScript || current.writeTarget != candidate.writeTarget || current.encodeMerge != candidate.encodeMerge) {
            if (!view || !request["replace"].isBool() || !request["replace"].toBool())
                throw std::runtime_error("registered table descriptor mismatch");
            validateGraph(candidate);
            const auto newBytes = tableBytes(candidate, readRows(current));
            checkCapacity(current, newBytes);
            persistDescriptor(candidate);
            bytes = bytes - current.bytes + newBytes; current.bytes = newBytes;
            current.dependencies = candidate.dependencies; current.script = candidate.script; current.metadata = candidate.metadata;
            current.program = std::move(candidate.program);
            current.encodeScript = candidate.encodeScript; current.writeTarget = candidate.writeTarget;
            current.encodeMerge = candidate.encodeMerge;
            current.encodeProgram = std::move(candidate.encodeProgram);
            current.descriptorBytes = candidate.descriptorBytes;
            graphTableCount=static_cast<std::size_t>(-1);
            current.ownerSession = candidate.ownerSession; current.valid = false; current.error = "awaiting dependencies";
            current.dirty = true;
            record(current, "registered", true); recompute({current.name});
            return read(current.name);
        }
        if (current.ownerSession != candidate.ownerSession) {
            current.ownerSession = candidate.ownerSession;
            // Acquiring a new lease never freshens data from the previous lease.
            current.valid = false;
            current.error = view ? "awaiting dependencies" : "awaiting first publication";
            record(current, "registered", true);
            current.dirty = view;
            recompute({current.name});
        }
        return read(current.name);
    }
    if (tables.size() >= swRealtimeDbDetail::kMaxTables) throw std::runtime_error("table limit exceeded");
    if (candidate.bytes > swRealtimeDbDetail::kMaxDatabaseBytes || bytes > swRealtimeDbDetail::kMaxDatabaseBytes - candidate.bytes)
        throw std::runtime_error("RtDb aggregate data limit is 8 MiB");
    if (view) validateGraph(candidate);
    persistDescriptor(candidate);
    const auto name = candidate.name;
    auto& inserted = tables.emplace(name, std::move(candidate)).first->second;
    bytes += inserted.bytes;
    record(inserted, "created", true);
    inserted.dirty = view;
    recompute({name});
    return read(name);
}

SwJsonObject SwRealtimeDb::State::read(const SwString& name, const SwJsonObject& selection) {
    const auto found = tables.find(name);
    if (found == tables.end()) throw std::runtime_error("unknown table: " + name.toStdString());
    const auto& target = found->second;
    // Validate selectors before a lazy read can evaluate and commit views.
    std::set<SwString> keys;
    if (selection.contains("keys")) {
        if (!selection["keys"].isArray() || selection["keys"].toArray().size() > 1024)
            throw std::runtime_error("read keys must be an array of at most 1024 strings");
        for (const auto& key : selection["keys"].toArray()) {
            if (!key.isString()) throw std::runtime_error("read key must be a string");
            keys.insert(key.toString());
        }
    }
    SwString orderColumn, orderType;
    if (selection.contains("order_by")) {
        orderColumn = swRealtimeDbDetail::requiredString(selection, "order_by");
        if (!target.columns.contains(orderColumn)) throw std::runtime_error("unknown order_by column");
        orderType = swRealtimeDbDetail::columnType(target.columns[orderColumn]);
        if (orderType != "integer" && orderType != "number" && orderType != "string")
            throw std::runtime_error("order_by requires a numeric or string column");
    }
    std::size_t limit = swRealtimeDbDetail::kMaxRows;
    if (selection.contains("limit")) {
        if (!selection["limit"].isInt() || selection["limit"].toLongLong() < 0 || selection["limit"].toLongLong() > 1024)
            throw std::runtime_error("read limit must be an integer from 0 to 1024");
        limit = static_cast<std::size_t>(selection["limit"].toInt());
    }
    if (target.dirty) materialize({name});
    SwJsonObject response = swRealtimeDbDetail::qualifiedIdentity(target.name);
    response["epoch"] = epoch;
    response["table"] = target.name;
    const auto sortBy = orderColumn.isEmpty() ? target.key : orderColumn;
    const auto& column = target.columns[sortBy];
    // Preserve RtDb's null-first ordering and equal-number/negative-zero
    // semantics. The native ascending index agrees for non-null integers and
    // strings; other selections retain the existing exact comparison below.
    const bool indexed = sortBy == target.key ||
        ((orderType == "integer" || orderType == "string") && column.isObject() &&
         column.toObject()["indexed"].toBool() && !swRealtimeDbDetail::columnNullable(column));
    const bool singleRow = target.maxRows == 1 && target.writeOrder.size() <= 1;
    const bool useIndex = !singleRow && limit && limit <= 250 && !selection.contains("keys") &&
                          sortBy == sortBy.trimmed() && indexed;
    std::vector<SwJsonObject> selected;
    swRealtimeDbDetail::Rows rows;
    if (useIndex) {
        SwTableQuery query;
        query.sortBy = sortBy;
        query.sortDirection = "asc";
        query.limit = static_cast<int>(limit);
        SwTableQueryResult result;
        const auto status = database.queryRows(storageSchema(target), query, &result);
        if (!status.ok()) throw std::runtime_error("SwTableDb: " + status.message().toStdString());
        for (auto& row : result.rows) {
            row.remove("__sw_rtdb");
            selected.push_back(std::move(row));
        }
    } else if (limit && selection.contains("keys")) {
        const auto& schema = storageSchema(target);
        for (const auto& key : keys) {
            SwJsonObject row;
            const auto status = database.getRow(schema, key, &row);
            if (status.code() == SwDbStatus::NotFound) continue;
            if (!status.ok()) throw std::runtime_error("SwTableDb: " + status.message().toStdString());
            row.remove("__sw_rtdb");
            rows.emplace(key, std::move(row));
        }
    } else if (limit) rows = readRows(target);
    for (auto& row : rows) selected.push_back(std::move(row.second));
    if (!useIndex && !orderColumn.isEmpty()) {
        std::stable_sort(selected.begin(), selected.end(), [&](const auto& a, const auto& b) {
            if (a[orderColumn].isNull() || b[orderColumn].isNull())
                return a[orderColumn].isNull() && !b[orderColumn].isNull();
            if (orderType == "string") return a[orderColumn].toString() < b[orderColumn].toString();
            if (orderType == "integer") return a[orderColumn].toLongLong() < b[orderColumn].toLongLong();
            return a[orderColumn].toDouble() < b[orderColumn].toDouble();
        });
    }
    selected.resize(std::min(selected.size(), limit));
    SwJsonArray output;
    for (auto& row : selected) output.append(std::move(row));
    response["rows"] = std::move(output);
    response["revision"] = swRealtimeDbDetail::decimal(target.revision);
    response["cursor"] = swRealtimeDbDetail::decimal(revision);
    response["valid"] = target.valid;
    response["error"] = target.error;
    response["last_write_ms"] = static_cast<long long>(target.lastWriteMs);
    return response;
}


void SwRealtimeDb::State::record(Table& target, const char* kind, bool changed,
                                const SwJsonArray& keys, const SwJsonArray& evicted) {
    ++revision;
    target.revision = revision;
    if (changed) target.valueRevision = revision;
    SwJsonObject event;
    event["revision"] = swRealtimeDbDetail::decimal(revision);
    event["table"] = target.name;
    event["kind"] = kind;
    event["changed"] = changed;
    event["valid"] = target.valid;
    // Large batches are table invalidations; never multiply a megabyte payload by journal depth.
    if (!keys.isEmpty() && keys.size() <= 64) event["keys"] = keys;
    if (!evicted.isEmpty()) {
        event["evicted_count"] = static_cast<int>(evicted.size());
        if (evicted.size() <= 64) event["evicted_keys"] = evicted;
    }
    const auto eventBytes = swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kJournalBytes).object(event) + 1;
    journal.push_back(Event{revision, target.name, std::move(event), changed, eventBytes});
    journalBytes += eventBytes;
    while (journal.size() > swRealtimeDbDetail::kJournalSize || journalBytes > swRealtimeDbDetail::kJournalBytes) {
        journalBytes -= journal.front().bytes;
        journal.pop_front();
    }
}

SwJsonObject SwRealtimeDb::State::changes(const SwJsonObject& request) const {
    const auto after = swRealtimeDbDetail::parseRevision(swRealtimeDbDetail::requiredString(request, "after", 20));
    const auto requestedEpoch = swRealtimeDbDetail::requiredString(request, "epoch");
    const auto mode = request.contains("mode") ? swRealtimeDbDetail::requiredString(request, "mode") : SwString("write");
    const bool catalog = request["scope"].toString() == "catalog";
    if (catalog ? (mode != "write" && mode != "create") : (mode != "write" && mode != "change"))
        throw std::runtime_error("invalid changes mode for scope");
    const auto name = request.contains("table") ? swRealtimeDbDetail::requiredString(request, "table") : SwString();
    std::set<SwString> selected;
    if (request.contains("tables")) {
        if (!request["tables"].isArray() || request["tables"].toArray().size()>256)
            throw std::runtime_error("changes tables requires at most 256 names");
        for(const auto& value:request["tables"].toArray()) {
            if(!value.isString())throw std::runtime_error("changes table must be a name");
            swRealtimeDbDetail::validateName(value.toString());selected.insert(value.toString());
        }
    }
    const bool resync = requestedEpoch != epoch || after > revision ||
        (!journal.empty() && after < journal.front().revision - 1);
    if (request.contains("summary") && !request["summary"].isBool())
        throw std::runtime_error("changes summary must be a boolean");
    const bool summary = request["summary"].toBool();
    SwJsonArray events;
    std::set<SwString> changedTables;
    bool created = false;
    if (!resync) for (auto it = std::upper_bound(journal.begin(), journal.end(), after,
            [](std::uint64_t cursor, const Event& event) { return cursor < event.revision; }); it != journal.end(); ++it) {
        const auto& event = *it;
        if ((name.isEmpty() || event.table == name) && (!request.contains("tables") || selected.count(event.table)) &&
            (catalog ? event.json["kind"].toString() == "created" : (mode == "write" || event.changed))) {
            if (summary) {
                changedTables.insert(event.table);
                created = created || event.json["kind"].toString() == "created";
            } else events.append(event.json);
        }
    }
    SwJsonObject response;
    response["epoch"] = epoch;
    response["revision"] = swRealtimeDbDetail::decimal(revision);
    response["resync_required"] = resync;
    if (summary) {
        SwJsonArray names;
        for (const auto& table : changedTables) names.append(table);
        response["tables"] = names;
        response["catalog_changed"] = created;
    } else response["events"] = events;
    return response;
}

void SwRealtimeDb::State::expire() {
    const auto now = Clock::now();
    bool expired=false;
    for (auto it = sessions.begin(); it != sessions.end(); ) {
        if (it->second.deadline <= now) {it = sessions.erase(it);expired=true;}
        else ++it;
    }
    // An ordinary read/write cannot change lease ownership. Traverse tables
    // and dependent views only when a session actually expires.
    if (!expired) return;
    for (auto it = subscriptions.begin(); it != subscriptions.end(); ) {
        if (!sessions.count(it->second.session)) {
            if (!it->second.catalog && --observers.at(it->second.table) == 0)
                observers.erase(it->second.table);
            it = subscriptions.erase(it);
        } else ++it;
    }
    std::set<SwString> changed;
    for (auto& entry : tables) {
        auto& target = entry.second;
        if (!ownerAlive(target) && (target.valid || target.error != "owner lease expired")) {
            target.valid = false;
            target.error = "owner lease expired";
            record(target, "stale", true);
            changed.insert(target.name);
        }
    }
    if (!changed.empty()) recompute(changed);
}
