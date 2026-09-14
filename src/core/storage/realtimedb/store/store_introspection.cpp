#include "StoreState.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

SwJsonObject SwRealtimeDb::State::introspect(const SwJsonObject& request) const {
    const auto bounded = [&](const char* key, int fallback, int maximum) {
        if (!request.contains(key)) return fallback;
        const auto& value = request[key];
        if (!value.isInt() || value.toLongLong() < 0 || value.toLongLong() > maximum)
            throw std::runtime_error(SwString("Invalid introspect ") + key);
        return value.toInt();
    };
    for (const auto* key : {"summary", "include_runtime", "include_scripts"})
        if (request.contains(key) && !request[key].isBool())
            throw std::runtime_error(SwString("Introspect boolean required: ") + key);
    const int offset = bounded("offset", 0, 100000);
    const int limit = bounded("limit", swRealtimeDbDetail::kMaxTables, swRealtimeDbDetail::kMaxTables);
    const bool summary = request["summary"].toBool();
    const bool includeRuntime = request["include_runtime"].toBool(true);
    const bool filtered = request.contains("namespace") || request.contains("schema");
    int total = 0;
    SwJsonArray descriptors;
    auto first = tables.begin(), last = tables.end();
    if (request.contains("table")) {
        first = tables.find(request["table"].toString());
        if (first != last) last = std::next(first);
    }
    for (auto it = first; it != last; ++it) {
        const auto& item = it->second;
        SwJsonObject descriptor;
        if (filtered) descriptor = swRealtimeDbDetail::qualifiedIdentity(item.name);
        if (request.contains("namespace") && descriptor["namespace"]!=request["namespace"]) continue;
        if (request.contains("schema") && descriptor["schema"]!=request["schema"]) continue;
        // Count matching names, but build heavy descriptors only for this page.
        if (total++ < offset || descriptors.size() >= static_cast<std::size_t>(limit)) continue;
        if (!filtered) descriptor = swRealtimeDbDetail::qualifiedIdentity(item.name);
        descriptor["table"] = item.name;
        descriptor["owner"] = item.owner;
        descriptor["kind"] = item.view ? "view" : "table";
        descriptor["row_count"] = static_cast<int>(item.writeOrder.size());
        descriptor["dirty"] = item.dirty;
        descriptor["valid"] = item.valid && !item.dirty;
        descriptor["owner_online"] = ownerAlive(item);
        const auto pendingError = item.dirty ? pendingViewError(item) : SwString();
        descriptor["error"] = item.dirty
            ? (pendingError.isEmpty() ? SwString("awaiting evaluation") : pendingError)
            : item.error;
        descriptor["revision"] = swRealtimeDbDetail::decimal(item.revision);
        descriptor["last_write_ms"] = static_cast<long long>(item.lastWriteMs);
        if (summary) {
            if (item.view) descriptor["writable"] = !item.encodeScript.isEmpty();
            descriptors.append(std::move(descriptor));
            continue;
        }
        descriptor["metadata"] = item.metadata;
        descriptor["key"] = item.key;
        descriptor["columns"] = item.columns;
        descriptor["schema_version"] = item.schemaVersion;
        descriptor["max_rows"] = static_cast<int>(item.maxRows);
        descriptor["overflow_policy"] = item.overflowPolicy;
        descriptor["evicted_rows"] = swRealtimeDbDetail::decimal(item.evictedRows);
        SwJsonArray dependencies;
        for (const auto& name : item.dependencies) dependencies.append(name);
        descriptor["dependencies"] = dependencies;
        SwJsonArray writers;
        for (const auto& actor : item.writers) writers.append(actor);
        descriptor["writers"] = writers;
        if (item.view) {
            descriptor["allow_invalid_sources"] = item.allowInvalidSources;
            descriptor["writable"] = !item.encodeScript.isEmpty();
            if (!item.writeTarget.isEmpty()) {
                descriptor["write_target"] = item.writeTarget;
                descriptor["write_mode"] = item.encodeMerge ? "merge" : "replace";
            }
            if (request["include_scripts"].toBool()) {
                descriptor["script"] = item.script; descriptor["decode"] = item.script;
                if (!item.encodeScript.isEmpty()) descriptor["encode"] = item.encodeScript;
            }
        }
        if (item.view) {
            descriptor["js_evaluations"] = swRealtimeDbDetail::decimal(item.jsEvaluations);
            descriptor["encode_evaluations"] = swRealtimeDbDetail::decimal(item.encodeEvaluations);
        }
        descriptors.append(descriptor);
    }
    SwJsonObject response;
    response["epoch"] = epoch;
    response["revision"] = swRealtimeDbDetail::decimal(revision);
    response["total"] = total;
    if (!descriptors.isEmpty() && offset + static_cast<int>(descriptors.size()) < total)
        response["next_offset"] = offset + static_cast<int>(descriptors.size());
    response["tables"] = std::move(descriptors);
    if (!includeRuntime) return response;
    SwJsonArray actors;
    const auto now = Clock::now();
    for (const auto& entry : sessions) {
        SwJsonObject actor;
        actor["actor"] = entry.second.actor;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(entry.second.deadline - now).count();
        actor["lease_remaining_ms"] = static_cast<long long>(std::max<std::int64_t>(0, remaining));
        actors.append(actor);
    }
    SwJsonArray observers;
    for (const auto& entry : subscriptions) {
        SwJsonObject observer;
        observer["actor"] = sessions.at(entry.second.session).actor;
        observer["table"] = entry.second.table;
        observer["mode"] = entry.second.mode;
        observer["include_rows"] = entry.second.includeRows;
        if (entry.second.includeRows) {
            SwJsonArray dependencies;
            for (const auto& name : entry.second.dependencies) dependencies.append(name);
            observer["dependencies"] = std::move(dependencies);
        }
        if (entry.second.catalog) observer["scope"] = "catalog";
        observers.append(observer);
    }
    response["actors"] = actors;
    response["subscriptions"] = observers;
    response["data_bytes"] = static_cast<int>(bytes);
    response["persistent"] = options.storage.persistent;
    response["storage"] = "SwTableDb";
    return response;
}
