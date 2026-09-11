#include <core/storage/SwRealtimeDbComponent.h>
#include <core/storage/realtimedb/SwRealtimeDbJson.h>
#include <core/storage/realtimedb/ChangeBatch.hpp>
using swRealtimeDbDetail::json;
using swRealtimeDbDetail::parseObject;
using swRealtimeDbDetail::operation;
#include <core/runtime/SwTimer.h>
#include <core/remote/SwSharedMemorySignal.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <vector>

class SwRealtimeDbComponent::Impl {
    using Clock = std::chrono::steady_clock;
    struct Watch {
        SwString table, mode, epoch, remoteSubscription;
        std::uint64_t revision{0};
        SwString wantedEpoch;
        std::uint64_t wantedRevision{0};
        Subscription callback;
        bool snapshot{false}, busy{false}, active{true};
        bool catalog{false};
        bool values{false};
        SwJsonArray dependencies;
        Clock::time_point retryAt{};
    };
public:
    Impl(const SwString& sys, const SwString& actor, const SwString& endpoint)
        : client_(sys, actor, endpoint), actor_(actor), timer_(100) {
        client_.setNativeChangedHandler([this](const swRealtimeDbDetail::ChangeBatch& value) { announced(value); });
        SwObject::connect(&timer_, &SwTimer::timeout, [this] { tick(); }, DirectConnection);
        timer_.start();
    }
    ~Impl() {
        alive_.reset();
        timer_.stop();
        for (auto& entry : watches_) entry.second->active = false;
        // Let SwRealtimeDbClient destruction suppress outstanding callbacks. Explicit
        // cancelAll() completes them and could re-enter an already destroyed
        // derived actor through a pending read or raw database request.
        // The server lease expires if an actor dies; no best-effort destructor
        // RPC is needed for correctness, including a container unloading us.
    }

    void declare(SwJsonObject definition, bool view) {
        const auto name = definition["table"].toString();
        if (name.isEmpty()) throw std::invalid_argument("Declaration needs a table name");
        definition.remove("session");
        definition["op"] = SwJsonValue(view ? "register_view" : "register_table");
        for (auto& previous : declarations_) {
            if (previous["table"].toString() != name) continue;
            if (json(previous) == json(definition)) return;
            if (!view || previous["op"].toString() != "register_view" || !definition["replace"].toBool())
                throw std::invalid_argument("Conflicting local table declaration");
            previous = std::move(definition);
            ready_ = false; registrationIndex_ = 0; registrationBusy_ = false; heartbeatBusy_ = false; changesBusy_ = false; ++generation_;
            return;
        }
        if (declarations_.size() >= 512) throw std::invalid_argument("Too many actor declarations");
        declarations_.push_back(std::move(definition));
        ready_ = false;
    }
    bool ready() const { return ready_; }
    SwRealtimeDbClient& database() { return client_; }
    void setIncludeWriteRows(bool include) { includeWriteRows_ = include; }
    void mutate(SwJsonObject request, SwRealtimeDbCompletion complete) {
        if (!ready_) { if (complete) complete({false, {}, "RtDb actor is not ready"}); return; }
        request["session"] = SwJsonValue(session_);
        request["include_rows"] = includeWriteRows_;
        request["priority"] = "high";
        const std::weak_ptr<int> alive = alive_;
        client_.requestOwnedPreferDirect(std::move(request), [alive, complete = std::move(complete)](SwRealtimeDbReply reply) {
            if (!alive.expired() && complete) complete(std::move(reply));
        });
    }
    void writeDefined(SwJsonObject definition, const SwJsonArray& rows, SwRealtimeDbCompletion complete) {
        auto remembered = definition;
        remembered.remove("session"); remembered["op"] = "register_table";
        bool known = false;
        for (const auto& previous : declarations_) {
            if (previous["table"] != remembered["table"]) continue;
            known = true;
            if (previous != remembered) {
                if (complete) complete({false, {}, "Conflicting local table declaration"});
                return;
            }
        }
        if (!known && declarations_.size() >= 512) {
            if (complete) complete({false, {}, "Too many actor declarations"});
            return;
        }
        auto request = operation("write");
        request["table"] = definition["table"];
        request["rows"] = rows;
        // Known descriptors are already replayed before ready() becomes true.
        // Only the first write needs atomic creation and descriptor retention.
        if (known) { mutate(std::move(request), std::move(complete)); return; }
        request["definition"] = definition;
        const auto generation = generation_;
        mutate(std::move(request), [this, generation, definition, complete = std::move(complete)](SwRealtimeDbReply reply) mutable {
            if (reply.ok) {
                const bool wasReady = ready_;
                try {
                    declare(definition, false);
                    // A late success still establishes a declaration to replay.
                    // Only the session that accepted the write may skip replay.
                    if (wasReady && generation == generation_) {
                        registrationIndex_ = declarations_.size(); ready_ = true;
                    }
                } catch (const std::exception& error) {
                    reply.ok = false;
                    reply.error = SwString("Write accepted, but retaining its declaration failed: ") + error.what();
                }
            }
            if (complete) complete(std::move(reply));
        });
    }
    void mutateMany(SwJsonArray operations, SwRealtimeDbCompletion complete) {
        if (!ready_) { if (complete) complete({false, {}, "RtDb actor is not ready"}); return; }
        if (operations.isEmpty() || operations.size() > 64) {
            if (complete) complete({false, {}, "mutate_many requires 1 to 64 operations"});
            return;
        }
        // Apply the same local descriptor checks as writeDefined before sending
        // anything. Repeated descriptors occupy one reconnection declaration.
        std::map<SwString, SwJsonObject> pending;
        std::vector<std::pair<std::size_t, SwJsonObject>> remember;
        for (std::size_t index = 0; index < operations.size(); ++index) {
            if (!operations[index].isObject()) continue; // Ordered server validation.
            auto operation = operations[index].toObject();
            if (operation["op"].toString() != "write" || !operation["definition"].isObject()) continue;
            auto definition = operation["definition"].toObject();
            if (!definition.contains("table")) definition["table"] = operation["table"];
            if (definition["table"] != operation["table"]) continue; // Server reports mismatch.
            definition.remove("session"); definition["op"] = "register_table";
            const auto name = definition["table"].toString();
            const SwJsonObject* previous = nullptr;
            const auto proposed = pending.find(name);
            if (proposed != pending.end()) previous = &proposed->second;
            else for (const auto& declared : declarations_)
                if (declared["table"].toString() == name) { previous = &declared; break; }
            if (previous && *previous != definition) {
                if (complete) complete({false, {}, "Conflicting local table declaration"});
                return;
            }
            if (previous) operation.remove("definition");
            else {
                if (declarations_.size() + pending.size() >= 512) {
                    if (complete) complete({false, {}, "Too many actor declarations"});
                    return;
                }
                pending.emplace(name, definition);
                remember.emplace_back(index, definition);
            }
            operations[index] = operation;
        }
        auto request = operation("mutate_many"); request["operations"] = std::move(operations);
        const auto generation = generation_;
        mutate(std::move(request), [this, generation, remember = std::move(remember),
                                  complete = std::move(complete)](SwRealtimeDbReply reply) mutable {
            // mutate() guards lifetime before entering this closure. Outcomes
            // remain useful when an ordered batch partially failed.
            const auto results = reply.data["results"].toArray();
            const bool wasReady = ready_;
            for (const auto& item : remember) {
                if (item.first >= results.size()) continue;
                const auto result = results[item.first].toObject();
                if (result["index"].toInt(-1) != static_cast<int>(item.first) ||
                    result["outcome"].toString() != "applied") continue;
                try { declare(item.second, false); }
                catch (const std::exception& error) {
                    reply.ok = false;
                    reply.error = SwString("Mutation accepted, but retaining its declaration failed: ") + error.what();
                }
            }
            if (wasReady && generation == generation_) {
                registrationIndex_ = declarations_.size(); ready_ = true;
            }
            if (reply.ok && !reply.data["complete"].toBool()) {
                reply.ok = false;
                reply.error = reply.data["error"].toString();
            }
            if (complete) complete(std::move(reply));
        });
    }
    std::uint64_t subscribe(const SwString& table, const SwString& mode, Subscription callback, bool catalog = false,
                            bool values = false, const SwJsonArray& dependencies = {}) {
        if ((!catalog && (table.isEmpty() || (mode != "write" && mode != "change"))) || !callback)
            throw std::invalid_argument("Subscription needs a table, write/change mode and callback");
        if (values && dependencies.size() > 63)
            throw std::invalid_argument("Value subscription accepts at most 63 dependencies");
        for (const auto& dependency : dependencies)
            if (!dependency.isString() || dependency.toString().isEmpty())
                throw std::invalid_argument("Subscription dependency must be a table name");
        if (watches_.size() >= 256) throw std::invalid_argument("Too many actor subscriptions");
        auto watch = std::make_shared<Watch>();
        watch->table = table; watch->mode = mode; watch->callback = std::move(callback);
        watch->catalog = catalog;
        watch->values=values;watch->dependencies=dependencies;
        const auto id = ++nextWatch_;
        watches_[id] = std::move(watch);
        scheduleWatches();
        return id;
    }
    void unsubscribe(std::uint64_t id) {
        const auto found = watches_.find(id);
        if (found == watches_.end()) return;
        found->second->active = false;
        unregisterWatch(found->second->remoteSubscription);
        watches_.erase(found);
    }

private:
    void tick() {
        const auto now = Clock::now();
        if (session_.isEmpty()) {
            if (!registrationBusy_ && now >= retryAt_) connect();
            return;
        }
        if (!registrationBusy_ && !ready_ && now >= retryAt_) registerNext();
        if (!heartbeatBusy_ && now >= heartbeatAt_) heartbeat();
        pollWatches();
    }
    void connect() {
        registrationBusy_ = true;
        auto request = operation("hello"); request["actor"] = SwJsonValue(actor_);
        const std::weak_ptr<int> alive = alive_;
        client_.requestOwned(std::move(request), [this, alive](SwRealtimeDbReply reply) {
            if (alive.expired()) return;
            registrationBusy_ = false;
            retryAt_ = Clock::now() + std::chrono::seconds(1);
            if (!reply.ok) return;
            session_ = reply.data["session"].toString();
            epoch_ = reply.data["epoch"].toString();
            if (session_.isEmpty() || epoch_.isEmpty()) { session_.clear(); return; }
            registrationIndex_ = 0;
            heartbeatAt_ = Clock::now() + std::chrono::seconds(1);
            registerNext();
        }, 1000);
    }
    void registerNext() {
        if (registrationIndex_ == declarations_.size()) {
            ready_ = true;
            swCDebug("sw.core.storage.realtimedb.client") << "Actor ready: " << actor_ << " epoch=" << epoch_;
            scheduleWatches();
            return;
        }
        registrationBusy_ = true;
        auto request = declarations_[registrationIndex_]; request["session"] = SwJsonValue(session_);
        const auto generation = generation_;
        const std::weak_ptr<int> alive = alive_;
        client_.requestOwned(std::move(request), [this, alive, generation](SwRealtimeDbReply reply) {
            if (alive.expired() || generation != generation_) return;
            registrationBusy_ = false;
            if (!reply.ok) {
                retryAt_ = Clock::now() + std::chrono::seconds(1);
                swCWarning("sw.core.storage.realtimedb.client") << "Declaration rejected: actor=" << actor_ << " error=" << reply.error;
                return;
            }
            ++registrationIndex_;
            registerNext();
        });
    }
    void heartbeat() {
        heartbeatBusy_ = true;
        heartbeatAt_ = Clock::now() + std::chrono::seconds(1);
        auto request = operation("heartbeat"); request["session"] = SwJsonValue(session_);
        request["priority"] = "high";
        const auto generation = generation_;
        const std::weak_ptr<int> alive = alive_;
        client_.requestOwned(std::move(request), [this, alive, generation](SwRealtimeDbReply reply) {
            if (alive.expired() || generation != generation_) return;
            heartbeatBusy_ = false;
            if (reply.ok && reply.data["epoch"].toString() == epoch_) {
                // The lease reply repairs a lost wakeup; it is not a data poll.
                if (reply.data.contains("revision")) announced(swRealtimeDbDetail::ChangeBatch(std::move(reply.data)));
                return;
            }
            ready_ = false; session_.clear(); registrationBusy_ = false; changesBusy_=false; ++generation_;
            retryAt_ = Clock::now();
            // Preserve the old subscription epoch until changes() reports a gap.
            for (auto& entry : watches_) {
                entry.second->busy = false;
                entry.second->remoteSubscription.clear();
            }
        }, 1000);
    }
    static void notify(const std::shared_ptr<Watch>& watch, const SwJsonObject& event) {
        if (!watch->active) return;
        try { watch->callback(event); }
        catch (const std::exception& error) { swCWarning("sw.core.storage.realtimedb.client") << "Subscription callback: " << error.what(); }
        catch (...) { swCWarning("sw.core.storage.realtimedb.client") << "Subscription callback failed"; }
    }
    void scheduleWatches() {
        if (watchesScheduled_) return;
        watchesScheduled_ = true;
        const std::weak_ptr<int> alive = alive_;
        if (!SwCoreApplication::instance()->tryPostEvent([this, alive] {
            if (alive.expired()) return;
            watchesScheduled_ = false; pollWatches();
        })) { watchesScheduled_ = false; pollWatches(); }
    }
    void announced(const swRealtimeDbDetail::ChangeBatch& event) {
        const auto& epoch = event.epoch();
        const auto revision = event.revision();
        if (announcedEpoch_ == epoch && revision <= announcedRevision_) return;
        const bool contiguous = announcedEpoch_ == epoch && event.hasFrom() &&
            event.from() == announcedRevision_;
        announcedEpoch_=epoch; announcedRevision_=revision;
        const auto from = event.from();
        const bool batch = ready_ && !changesBusy_ && event.complete();
        std::vector<std::shared_ptr<Watch>> local;
        bool pollNeeded = false;
        for (auto& entry : watches_) {
            auto& watch = entry.second;
            bool covered = false;
            if (batch && watch->active && !watch->busy && watch->snapshot && !watch->catalog &&
                !watch->remoteSubscription.isEmpty() && watch->epoch == epoch) {
                const auto cursor = watch->revision;
                covered = cursor >= from && cursor <= revision;
                if (covered) {
                    // A complete native batch advances every covered cursor.
                    // Unrelated tables need no event index or callback dispatch.
                    if (event.hasNames() && !event.containsTable(watch->table)) watch->revision = revision;
                    else local.push_back(watch);
                }
            }
            if (!covered && contiguous && watch->epoch == epoch && event.hasTables() &&
                (watch->catalog ? !event.catalogChanged() : !event.containsTable(watch->table))) {
                if (watch->active && !watch->busy && pending(*watch)) pollNeeded = true;
                continue;
            }
            if (watch->wantedEpoch != epoch) watch->wantedRevision = 0;
            watch->wantedEpoch = epoch;
            watch->wantedRevision = std::max(watch->wantedRevision, revision);
            if (!covered && watch->active && !watch->busy && pending(*watch)) pollNeeded = true;
        }
        const std::weak_ptr<int> alive = alive_;
        if (!local.empty()) dispatchChanges(event, local, generation_);
        // Covered callbacks finish their cursors synchronously. Busy reads
        // already schedule their own continuation; subscribe() also schedules
        // newly created watches, including subscriptions added by a callback.
        if (!alive.expired() && pollNeeded) {
            swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " recover notice " << revision
                << " from=" << from << " complete=" << event.complete() << " busy=" << changesBusy_;
            scheduleWatches();
        }
    }
    static bool pending(const Watch& watch) {
        return !watch.snapshot || watch.remoteSubscription.isEmpty() ||
            (!watch.wantedEpoch.isEmpty() && (watch.epoch != watch.wantedEpoch ||
             watch.revision < watch.wantedRevision));
    }
    void pollWatches() {
        if (!ready_) return;
        std::vector<std::shared_ptr<Watch>> initial, changes;
        size_t busy=0;
        const auto now = Clock::now();
        for (auto& entry : watches_) {
            const auto& watch=entry.second;
            if (watch->busy) { ++busy; continue; }
            if (!watch->active || now<watch->retryAt || !pending(*watch)) continue;
            if (watch->remoteSubscription.isEmpty() || !watch->snapshot || watch->catalog) initial.push_back(watch);
            else changes.push_back(watch);
        }
        const std::weak_ptr<int> alive=alive_;
        // Bound startup work independently of the number of message tables.
        for (auto& watch:initial) {
            if (alive.expired()) return;
            if (busy>=4) break;
            if (watch->active) { ++busy; poll(watch); }
        }
        if (alive.expired() || changes.empty() || changesBusy_) return;
        pollChanges(changes);
    }
    void pollChanges(const std::vector<std::shared_ptr<Watch>>& watches) {
        auto request=operation("changes"); SwJsonArray names; std::set<SwString> seen;
        SwJsonArray values;
        auto after=watches.front()->revision;
        const auto epoch=watches.front()->epoch;
        for(const auto& watch:watches) {
            after=std::min(after,watch->revision);
            if(seen.insert(watch->table).second)names.append(watch->table);
            if(watch->values)values.append(watch->remoteSubscription);
        }
        request["after"]=SwString::number(after); request["epoch"]=epoch;
        request["tables"]=names; request["mode"]="write";
        request["priority"]="high";
        if(!values.isEmpty()){request["include_values"]=true;request["value_subscriptions"]=std::move(values);}
        changesBusy_=true;
        swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " changes begin after=" << after << " tables=" << watches.size();
        const auto generation=generation_; const std::weak_ptr<int> alive=alive_;
        client_.requestOwnedPreferDirect(std::move(request),[this,alive,generation,watches](SwRealtimeDbReply reply) {
            if(alive.expired() || generation!=generation_)return;
            changesBusy_=false;
            swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " changes end revision=" << reply.data["revision"].toString();
            if(!reply.ok) {
                swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " changes: " << reply.error;
                for (const auto& watch : watches) watch->retryAt=Clock::now()+std::chrono::milliseconds(100);
                return;
            }
            dispatchChanges(swRealtimeDbDetail::ChangeBatch(std::move(reply.data)), watches, generation);
            // A notification may have arrived during this RPC. Drain it now,
            // without waiting for the maintenance/retry timer.
            if (!alive.expired()) scheduleWatches();
        });
    }
    void dispatchChanges(const swRealtimeDbDetail::ChangeBatch& data,
                         const std::vector<std::shared_ptr<Watch>>& watches, std::uint64_t generation) {
        const std::weak_ptr<int> alive = alive_;
        const auto& epoch = data.epoch();
        const auto& revision = data.revisionText();
        const auto end = data.revision();
        // A latest value can replace several journal entries. Deliver these
        // snapshots in their committed revision order across command tables.
        std::vector<std::pair<std::uint64_t, std::shared_ptr<Watch>>> ordered;
        ordered.reserve(watches.size());
        for (const auto& watch : watches)
            ordered.emplace_back(watch->values ? data.snapshotRevision(watch->table) : 0, watch);
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& entry : ordered) {
            const auto& watch = entry.second;
            if (alive.expired() || generation != generation_ || !ready_) return;
            if (!watch->active) continue;
            const auto previous = watch->revision;
            if (watch->epoch == epoch && previous > end) continue;
            const bool gap = data.resync() || watch->epoch != epoch;
            watch->epoch = epoch;
            if (gap) {
                watch->revision = end; watch->snapshot = false;
                SwJsonObject event; event["kind"] = "resync"; event["table"] = watch->table;
                event["epoch"] = epoch; event["revision"] = revision;
                notify(watch, event); continue;
            }
            const auto* matching = data.eventsFor(watch->table);
            if (!matching) { watch->revision = end; continue; }
            if(watch->values) {
                bool deliver=false;
                for(const auto& value:*matching)if(value.revision>previous && (watch->mode=="write" || value.changed))deliver=true;
                if(deliver) {
                    bool complete=data.hasSnapshot(watch->table);
                    for(const auto& name:watch->dependencies)complete=complete && data.hasSnapshot(name.toString());
                    if(!complete) {watch->snapshot=false;scheduleWatches();continue;}
                    auto event=data.detachSnapshot(watch->table);event["kind"]="write";
                    event["epoch"]=epoch;event["table"]=watch->table;
                    if(!event.contains("revision"))event["revision"]="0";
                    SwJsonObject dependencies;
                    for(const auto& name:watch->dependencies)dependencies[name.toString()]=data.detachSnapshot(name.toString());
                    event["dependencies"]=std::move(dependencies);
                    watch->revision=end;notify(watch,event);
                } else watch->revision=end;
                continue;
            }
            for (const auto& value : *matching) {
                if (alive.expired() || generation != generation_ || !ready_) return;
                if (!watch->active) break;
                if (value.revision <= previous || (watch->mode != "write" && !value.changed)) continue;
                auto event = data.detachEvent(value.index);
                // A callback may suspend registration. Keep the last delivered
                // cursor so the remaining writes can be replayed after resume.
                watch->revision = value.revision;
                notify(watch, event);
            }
            if (alive.expired() || generation != generation_ || !ready_) return;
            watch->revision = end;
        }
    }
    void poll(const std::shared_ptr<Watch>& watch) {
        watch->busy = true;
        if (watch->remoteSubscription.isEmpty()) {
            auto request = operation("subscribe"); request["session"] = SwJsonValue(session_);
            if (watch->catalog) request["scope"] = "catalog";
            else request["table"] = SwJsonValue(watch->table);
            request["mode"] = SwJsonValue(watch->mode);
            if(watch->values){request["include_rows"]=true;request["dependencies"]=watch->dependencies;}
            const auto generation = generation_;
            const std::weak_ptr<int> alive = alive_;
            client_.requestOwned(std::move(request), [this, alive, generation, watch](SwRealtimeDbReply reply) {
                if (alive.expired() || generation != generation_) return;
                watch->busy = false;
                if (!reply.ok) { swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " subscribe " << watch->table << ": " << reply.error; watch->retryAt=Clock::now()+std::chrono::milliseconds(100); return; }
                watch->remoteSubscription = reply.data["subscription"].toString();
                if (!watch->active) unregisterWatch(watch->remoteSubscription);
                else if(watch->values)deliverValues(watch,reply.data["values"].toObject(),"snapshot");
                if(alive.expired())return;
                pollWatches();
            });
            return;
        }
        if(watch->values) {
            auto request=operation("subscription_snapshot");request["session"]=session_;
            request["subscription"]=watch->remoteSubscription;
            const auto generation=generation_;const std::weak_ptr<int> alive=alive_;
            swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " snapshot begin " << watch->table;
            client_.requestOwnedPreferDirect(std::move(request),[this,alive,generation,watch](SwRealtimeDbReply reply){
                if(alive.expired() || generation!=generation_ || !watch->active)return;
                watch->busy=false;
                if(!reply.ok){swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " snapshot " << watch->table << ": " << reply.error;watch->retryAt=Clock::now()+std::chrono::milliseconds(100);return;}
                swCDebug("sw.core.storage.realtimedb.client.retry") << actor_ << " snapshot end " << watch->table;
                deliverValues(watch,reply.data,"snapshot");
                if(!alive.expired())scheduleWatches();
            });return;
        }
        auto request = operation(watch->snapshot ? "changes" : (watch->catalog ? "introspect" : "read"));
        if (watch->catalog) request["scope"] = "catalog";
        else request["table"] = SwJsonValue(watch->table);
        if (watch->snapshot) {
            request["after"] = SwString::number(watch->revision);
            request["epoch"] = SwJsonValue(watch->epoch);
            request["mode"] = SwJsonValue(watch->mode);
        }
        const std::weak_ptr<int> alive = alive_;
        const auto generation = generation_;
        client_.requestOwned(std::move(request), [this, alive, generation, watch](SwRealtimeDbReply reply) {
            if (alive.expired() || generation != generation_ || !watch->active) return;
            watch->busy = false;
            if (!reply.ok) { watch->retryAt=Clock::now()+std::chrono::milliseconds(100); return; }
            watch->epoch = reply.data["epoch"].toString();
            watch->revision = std::stoull(reply.data[watch->snapshot || watch->catalog ? "revision" : "cursor"].toString().toStdString());
            if (!watch->snapshot) {
                watch->snapshot = true;
                reply.data["kind"] = SwJsonValue(watch->catalog ? "catalog_snapshot" : "snapshot");
                notify(watch, reply.data);
                if (!alive.expired()) pollWatches();
                return;
            }
            if (reply.data["resync_required"].toBool()) {
                watch->snapshot = false;
                reply.data["kind"] = SwJsonValue("resync");
                if (watch->catalog) reply.data["scope"] = "catalog";
                else reply.data["table"] = SwJsonValue(watch->table);
                notify(watch, reply.data);
                if (!alive.expired()) pollWatches();
                return;
            }
            const auto events = reply.data["events"].toArray();
            for (const auto& event : events) {
                if (alive.expired() || !watch->active) return;
                auto notification = event.toObject();
                notification["epoch"] = reply.data["epoch"];
                notify(watch, notification);
            }
            if (!alive.expired()) scheduleWatches();
        });
    }

    void deliverValues(const std::shared_ptr<Watch>& watch,const SwJsonObject& values,const char* kind) {
        const auto tables=values["tables"].toObjectPtr();
        if(!tables || !tables->contains(watch->table))return;
        watch->epoch=values["epoch"].toString();
        watch->revision=std::stoull(values["cursor"].toString().toStdString());watch->snapshot=true;
        auto event=(*tables)[watch->table].toObject();event["kind"]=kind;event["epoch"]=watch->epoch;
        if(!event.contains("revision"))event["revision"]="0";
        SwJsonObject dependencies;
        for(const auto& name:watch->dependencies)dependencies[name.toString()]=(*tables)[name.toString()];
        event["dependencies"]=std::move(dependencies);notify(watch,event);
    }

    void unregisterWatch(const SwString& token) {
        if (token.isEmpty() || session_.isEmpty()) return;
        auto request = operation("unsubscribe"); request["session"] = SwJsonValue(session_);
        request["subscription"] = SwJsonValue(token);
        client_.requestOwned(std::move(request), {});
    }

    SwRealtimeDbClient client_;
    const SwString actor_;
    SwString session_, epoch_;
    SwTimer timer_;
    std::shared_ptr<int> alive_{std::make_shared<int>(0)};
    std::vector<SwJsonObject> declarations_;
    std::map<std::uint64_t, std::shared_ptr<Watch>> watches_;
    std::uint64_t nextWatch_{0}, generation_{0};
    std::size_t registrationIndex_{0};
    bool ready_{false}, registrationBusy_{false}, heartbeatBusy_{false}, changesBusy_{false};
    bool includeWriteRows_{true};
    bool watchesScheduled_{false};
    SwString announcedEpoch_;
    std::uint64_t announcedRevision_{0};
    Clock::time_point retryAt_{}, heartbeatAt_{};
};

SwRealtimeDbComponent::SwRealtimeDbComponent(const SwString& sys, const SwString& ns, const SwString& name, SwObject* parent)
    : SwRealtimeDbComponent(sys, ns, name, "rtdb", parent) {}
SwRealtimeDbComponent::SwRealtimeDbComponent(const SwString& sys, const SwString& ns, const SwString& name,
                                           const SwString& endpoint, SwObject* parent)
    : SwRemoteObject(sys, ns, name, parent),
      impl_(std::make_unique<Impl>(sys, ns.isEmpty() ? name : ns + "/" + name, endpoint)) {}
SwRealtimeDbComponent::~SwRealtimeDbComponent() = default;
void SwRealtimeDbComponent::declareTable(SwJsonObject definition) { impl_->declare(std::move(definition), false); }
void SwRealtimeDbComponent::declareView(SwJsonObject definition) { impl_->declare(std::move(definition), true); }
bool SwRealtimeDbComponent::ready() const { return impl_->ready(); }
SwRealtimeDbClient& SwRealtimeDbComponent::database() { return impl_->database(); }
void SwRealtimeDbComponent::setIncludeWriteRows(bool include) { impl_->setIncludeWriteRows(include); }
void SwRealtimeDbComponent::writeRows(SwJsonObject definition, const SwJsonArray& rows, SwRealtimeDbCompletion complete) {
    impl_->writeDefined(std::move(definition), rows, std::move(complete));
}
void SwRealtimeDbComponent::writeRows(const SwString& table, const SwJsonArray& rows, SwRealtimeDbCompletion complete) {
    auto request = operation("write"); request["table"] = SwJsonValue(table); request["rows"] = SwJsonValue(rows);
    impl_->mutate(std::move(request), std::move(complete));
}
void SwRealtimeDbComponent::eraseRows(const SwString& table, const SwJsonArray& keys, SwRealtimeDbCompletion complete) {
    auto request = operation("erase"); request["table"] = SwJsonValue(table); request["keys"] = SwJsonValue(keys);
    impl_->mutate(std::move(request), std::move(complete));
}
void SwRealtimeDbComponent::mutateMany(const SwJsonArray& operations, SwRealtimeDbCompletion complete) {
    impl_->mutateMany(operations, std::move(complete));
}
void SwRealtimeDbComponent::patchRows(const SwString& table, const SwJsonArray& rows,
                                     const SwJsonArray& increments, SwRealtimeDbCompletion complete) {
    auto request = operation("write"); request["table"] = table; request["rows"] = rows;
    request["merge"] = true;
    if (!increments.isEmpty()) request["increments"] = increments;
    impl_->mutate(std::move(request), std::move(complete));
}
void SwRealtimeDbComponent::readTable(const SwString& table, SwRealtimeDbCompletion complete) {
    auto request = operation("read"); request["table"] = SwJsonValue(table);
    impl_->database().requestOwnedPreferDirect(std::move(request), std::move(complete));
}
void SwRealtimeDbComponent::readTables(const SwJsonArray& tables, SwRealtimeDbCompletion complete) {
    auto request = operation("read_many"); request["tables"] = tables;
    impl_->database().requestOwnedPreferDirect(std::move(request), std::move(complete));
}
std::uint64_t SwRealtimeDbComponent::subscribe(const SwString& table, const SwString& mode, Subscription callback) {
    return impl_->subscribe(table, mode, std::move(callback));
}
std::uint64_t SwRealtimeDbComponent::subscribeValues(const SwString& table,const SwString& mode,Subscription callback,
                                                    const SwJsonArray& dependencies) {
    return impl_->subscribe(table,mode,std::move(callback),false,true,dependencies);
}
std::uint64_t SwRealtimeDbComponent::subscribeTables(Subscription callback) {
    return impl_->subscribe({}, "create", std::move(callback), true);
}
void SwRealtimeDbComponent::unsubscribe(std::uint64_t subscription) { impl_->unsubscribe(subscription); }
