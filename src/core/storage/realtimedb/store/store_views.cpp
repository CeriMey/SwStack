#include "StoreState.hpp"
#include <core/storage/SwRealtimeDbEvaluationScope.h>

#include <functional>
#include <limits>
#include <stdexcept>
#include <future>
#include <core/runtime/SwThreadPool.h>


void SwRealtimeDb::State::validateGraph(const Table& candidate) const {
    std::set<SwString> visiting;
    std::set<SwString> visited;
    std::function<void(const SwString&)> visit = [&](const SwString& name) {
        if (visited.count(name)) return;
        if (!visiting.insert(name).second) throw std::runtime_error("cyclic view dependency");
        const Table* current = nullptr;
        if (name == candidate.name) current = &candidate;
        else {
            const auto found = tables.find(name);
            if (found != tables.end()) current = &found->second;
        }
        if (current) for (const auto& dependency : current->dependencies) visit(dependency);
        visiting.erase(name);
        visited.insert(name);
    };
    visit(candidate.name);
    for (const auto& entry : tables) visit(entry.first);
}

SwString SwRealtimeDb::State::viewInputs(const Table& target, SwJsonObject& inputs) const {
    SwString failure;
    if (!ownerAlive(target)) failure = "owner lease expired";
    if (failure.isEmpty()) for (const auto& dependency : target.dependencies) {
        const auto found = tables.find(dependency);
        if (found == tables.end()) {
            failure = SwString("missing dependency: ") + dependency;
            break;
        }
        if (!found->second.valid && (!target.allowInvalidSources || found->second.view || found->second.writeOrder.empty())) {
            failure = SwString("invalid dependency: ") + dependency;
            break;
        }
        inputs[dependency] = swRealtimeDbDetail::rowArray(readRows(found->second));
    }

    return failure;
}

SwJsonArray SwRealtimeDb::State::evaluateView(const SwString& script, const ViewProgram& program, const SwJsonObject& inputs) const {
    if (program) return program(inputs);
    if (!transform) throw std::runtime_error("view evaluator unavailable");
    return transform(script, inputs);
}

bool SwRealtimeDb::State::reuseView(Table& target) {
    std::map<SwString, std::uint64_t> versions;
    for (const auto& name : target.dependencies) {
        const auto source = tables.find(name);
        if (source == tables.end() || source->second.dirty ||
            (!source->second.valid && (!target.allowInvalidSources || source->second.view || source->second.writeOrder.empty()))) return false;
        versions.emplace(name, source->second.valueRevision);
    }
    if (target.valid && target.inputVersions == versions) {
        target.dirty = false;
        return true;
    }
    target.inputVersions = std::move(versions);
    return false;
}

void SwRealtimeDb::State::refreshView(Table& target) {
    if (reuseView(target)) return;
    SwJsonObject inputs;auto failure=viewInputs(target,inputs);SwJsonArray output;
    bool evaluated = false;
    if(failure.isEmpty())try {
        evaluated = true;
        output=evaluateView(target.script,target.program,inputs);
    } catch(const std::exception& error) {failure=SwString(error.what()).left(512);}
    if (evaluated) ++target.jsEvaluations;
    publishView(target,std::move(output),failure);
}

void SwRealtimeDb::State::publishView(Table& target,SwJsonArray output,SwString failure) {
    target.dirty = false;
    if (failure.isEmpty()) {
        try {
            SwJsonArray keys;
            auto replacement = validateRows(target, output, &keys);
            std::map<SwString, std::uint64_t> order;
            auto nextSequence = writeSequence;
            for (const auto& key : keys) {
                if (nextSequence == std::numeric_limits<std::uint64_t>::max())
                    throw std::runtime_error("write sequence exhausted");
                order[key.toString()] = ++nextSequence;
            }
            const auto evicted = retainRows(target, replacement, order);
            const auto newBytes = tableBytes(target, replacement);
            checkCapacity(target, newBytes);
            const bool changed = !target.valid || !rowsEqual(target, replacement);
            // Retention metadata already lists every stored key. Apply the
            // complete output and explicit removals atomically, avoiding a
            // second database scan to discover the same omitted rows.
            std::set<SwString> erased;
            for (const auto& previous : target.writeOrder)
                if (!order.count(previous.first)) erased.insert(previous.first);
            storeDelta(target, std::move(replacement), order, erased);
            writeSequence = nextSequence;
            bytes = bytes - target.bytes + newBytes;
            target.bytes = newBytes;
            target.writeOrder = std::move(order);
            target.evictedRows += evicted.size();
            target.valid = true;
            target.error = "";
            target.lastWriteMs = swRealtimeDbDetail::wallTimeMs();
            record(target, "view", changed, {}, evicted);
            return;
        } catch (const std::exception& error) {
            failure = SwString(std::string(error.what()).substr(0, 512));
            if (failure.isEmpty()) failure = "view evaluation failed";
        }
    }
    if (target.valid || target.error != failure) {
        target.valid = false;
        target.error = failure;
        record(target, "invalid", true);
    }
}

void SwRealtimeDb::State::updateViewGraph() {
    if (graphTableCount!=tables.size()) {
        // The descriptor graph changes only at registration/replacement. Keep
        // topology metadata, not another copy of table values.
        std::set<SwString> visited;std::vector<SwString> order;
        std::function<void(const SwString&)> visit=[&](const SwString& name) {
            if(!visited.insert(name).second)return;
            const auto found=tables.find(name);
            if(found==tables.end() || !found->second.view)return;
            for(const auto& dependency:found->second.dependencies)visit(dependency);
            order.push_back(name);
        };
        for(const auto& entry:tables)visit(entry.first);
        viewRank.clear();viewLevel.clear();dependents.clear();
        for(std::size_t i=0;i<order.size();++i) {
            viewRank[order[i]]=i;
            std::size_t level=0;
            for(const auto& dependency:tables.at(order[i]).dependencies) {
                dependents[dependency].push_back(order[i]);
                const auto parent=viewLevel.find(dependency);
                if(parent!=viewLevel.end())level=std::max(level,parent->second+1);
            }
            viewLevel[order[i]]=level;
        }
        graphTableCount=tables.size();
    }
}

SwString SwRealtimeDb::State::pendingViewError(const Table& target) const {
    std::map<SwString, SwString> failures;
    std::function<SwString(const Table&)> inspect = [&](const Table& current) -> SwString {
        const auto cached = failures.find(current.name);
        if (cached != failures.end()) return cached->second;
        SwString failure;
        if (!ownerAlive(current)) failure = "owner lease expired";
        if (failure.isEmpty()) for (const auto& dependency : current.dependencies) {
            const auto found = tables.find(dependency);
            if (found == tables.end()) { failure = "missing dependency: " + dependency; break; }
            const auto& parent = found->second;
            if ((parent.dirty ? !inspect(parent).isEmpty() : !parent.valid) &&
                (!current.allowInvalidSources || parent.view || parent.writeOrder.empty())) {
                failure = "invalid dependency: " + dependency;
                break;
            }
        }
        failures.emplace(current.name, failure);
        return failure;
    };
    return inspect(target);
}

void SwRealtimeDb::State::recompute(const std::set<SwString>& changed) {
    updateViewGraph();
    std::set<std::pair<std::size_t,SwString>> pending;
    std::set<SwString> scheduled;
    auto enqueue=[&](const SwString& source) {
        const auto found=dependents.find(source);if(found==dependents.end())return;
        for(const auto& name:found->second)if(scheduled.insert(name).second)pending.emplace(viewRank.at(name),name);
    };
    for(const auto& name:changed)enqueue(name);
    std::set<SwString> requested;
    for (const auto& name : changed) if (observers.count(name)) requested.insert(name);
    while(!pending.empty()) {
        const auto name=pending.begin()->second;pending.erase(pending.begin());
        tables.at(name).dirty = true;
        if (observers.count(name)) requested.insert(name);
        enqueue(name);
    }
    // Unobserved views retain their last materialization. Each subscribed view
    // and its ancestors are evaluated for every write, preserving both write
    // notifications and comparisons of transformed results for change mode.
    if (!requested.empty()) materialize(requested);
}

void SwRealtimeDb::State::materialize(const std::set<SwString>& names) {
    updateViewGraph();
    std::set<SwString> visited;
    std::map<std::size_t, std::vector<SwString>> levels;
    std::function<void(const SwString&)> collect = [&](const SwString& name) {
        if (!visited.insert(name).second) return;
        const auto found = tables.find(name);
        if (found == tables.end() || !found->second.dirty) return;
        for (const auto& dependency : found->second.dependencies) collect(dependency);
        levels[viewLevel.at(name)].push_back(name);
    };
    for (const auto& name : names) collect(name);
    if (!viewWorkers) {
        for (const auto& level : levels) for (const auto& name : level.second)
            refreshView(tables.at(name));
        return;
    }
    // Independent transforms use SwThreadPool; committing values and notifying
    // consumers remains serialized in the database's existing event loop.
    struct Evaluation {SwJsonArray rows;SwString error;};
    for(const auto& level:levels)for(std::size_t start=0;start<level.second.size();start+=options.viewWorkerCount) {
        if (level.second.size() == 1) {
            refreshView(tables.at(level.second.front()));
            continue;
        }
        std::vector<std::pair<SwString,std::future<Evaluation>>> work;
        const auto end=std::min(level.second.size(),start+options.viewWorkerCount);
        for(std::size_t i=start;i<end;++i) {
            const auto name=level.second[i];auto& target=tables.at(name);
            if (reuseView(target)) continue;
            SwJsonObject inputs;const auto failure=viewInputs(target,inputs);
            if(!failure.isEmpty()){publishView(target,{},failure);continue;}
            const auto script=target.script; const auto program=target.program;
            auto task=std::make_shared<std::packaged_task<Evaluation()>>([this,script,program,inputs=std::move(inputs)] {
                Evaluation result;
                try {
                    result.rows=evaluateView(script,program,inputs);
                } catch(const std::exception& error) {result.error=SwString(error.what()).left(512);}
                return result;
            });
            work.emplace_back(name,task->get_future());viewWorkers->start([task]{(*task)();});
        }
        for(auto& item:work) {
            auto result=item.second.get();auto& target=tables.at(item.first);
            ++target.jsEvaluations;
            publishView(target,std::move(result.rows),result.error);
        }
    }
}
