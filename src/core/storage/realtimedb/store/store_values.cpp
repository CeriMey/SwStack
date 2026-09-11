#include "StoreState.hpp"
#include "../JsonSize.hpp"

SwJsonObject SwRealtimeDb::State::snapshots(const std::set<SwString>& names) {
    // Complete lazy dependencies before copying any row. This is a transient
    // publication snapshot; authoritative values remain in SwTableDb.
    materialize(names);
    SwJsonObject values;
    for(const auto& name:names) {
        if(tables.count(name))values[name]=read(name);
        else {
            SwJsonObject missing;missing["table"]=name;missing["valid"]=false;
            missing["error"]="unknown table: "+name;missing["rows"]=SwJsonArray();values[name]=std::move(missing);
        }
    }
    SwJsonObject result;result["tables"]=std::move(values);result["epoch"]=epoch;
    result["cursor"]=swRealtimeDbDetail::decimal(revision);return result;
}

SwJsonObject SwRealtimeDb::State::subscriptionValues(const Subscription& subscription) {
    auto names=subscription.dependencies;names.insert(subscription.table);
    auto result=snapshots(names);
    swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxBytes).object(result);
    return result;
}

SwJsonObject SwRealtimeDb::State::subscriptionSnapshot(const SwJsonObject& request) {
    session(request);
    const auto found=subscriptions.find(swRealtimeDbDetail::requiredString(request,"subscription"));
    if(found==subscriptions.end() || found->second.session!=request["session"].toString())
        throw std::runtime_error("unknown subscription for session");
    if(!found->second.includeRows)throw std::runtime_error("subscription does not include values");
    return subscriptionValues(found->second);
}

SwJsonObject SwRealtimeDb::State::changesWithValues(const SwJsonObject& request) {
    auto result=changes(request);
    if(!request["include_values"].toBool())return result;
    std::set<SwString> selected;
    if(request.contains("value_subscriptions")) {
        if(!request["value_subscriptions"].isArray() || request["value_subscriptions"].toArray().size()>1024)
            throw std::runtime_error("value_subscriptions requires at most 1024 subscription ids");
        for(const auto& value:request["value_subscriptions"].toArray()) {
            if(!value.isString())throw std::runtime_error("value subscription must be an id");
            selected.insert(value.toString());
        }
    }
    std::map<SwString,bool> changed;
    for(const auto& value:result["events"].toArray()) {
        const auto event=value.toObject();const auto name=event["table"].toString();
        changed[name]=changed[name] || event["changed"].toBool();
    }
    std::set<SwString> targets,names;
    for(const auto& entry:subscriptions) {
        const auto& watch=entry.second;
        if(!watch.includeRows || (request.contains("value_subscriptions") && !selected.count(entry.first)))continue;
        const auto event=changed.find(watch.table);
        if(!result["resync_required"].toBool() &&
           (event==changed.end() || (watch.mode=="change" && !event->second)))continue;
        targets.insert(watch.table);names.insert(watch.table);
        names.insert(watch.dependencies.begin(),watch.dependencies.end());
    }
    if(names.empty())return result;
    materialize(names);
    SwJsonObject values;
    std::size_t totalBytes = 2;
    for (const auto& name : names) {
        SwJsonObject value;
        if (tables.count(name)) value = read(name);
        else {
            value["table"] = name; value["valid"] = false;
            value["error"] = "unknown table: " + name; value["rows"] = SwJsonArray();
        }
        // Bound a publication even when many subscribers request different
        // large tables. Missing attachments use subscription_snapshot recovery.
        try {
            const auto size = swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::kMaxBytes).object(value);
            if (totalBytes + size + name.size() + 8 > swRealtimeDbDetail::kMaxBytes) continue;
            totalBytes += size + name.size() + 8;
            values[name] = std::move(value);
        } catch (const std::runtime_error&) {}
    }
    // Materializing requested dependencies can commit lazy views. Use the
    // resulting journal boundary for both the events and their snapshots.
    result=changes(request);
    result["snapshots"]=std::move(values);
    SwJsonArray included;for(const auto& table:targets)included.append(table);
    result["value_tables"]=std::move(included);
    return result;
}
