#include <core/storage/SwRealtimeDbNode.h>
#include <core/storage/SwRealtimeDb.h>
#include <core/storage/SwRealtimeDbJsEvaluator.h>
#include <core/storage/realtimedb/SwRealtimeDbJson.h>
#include <core/storage/realtimedb/ChangeNotice.hpp>
#include <core/storage/realtimedb/JsonSize.hpp>
using swRealtimeDbDetail::json;
using swRealtimeDbDetail::parseObject;
using swRealtimeDbDetail::operation;
#include <core/remote/SwJsonRpcTransfer.h>
#include <core/fs/SwLockFile.h>
#include <core/runtime/SwTimer.h>
#include <core/runtime/SwCoreApplication.h>
#include <chrono>
#include <deque>
#include <array>
#include <cstdlib>
#include <set>
#include <mutex>

class SwRealtimeDbNode::Impl {
    using Clock = std::chrono::steady_clock;
    struct Profile {
        bool enabled{std::getenv("SW_RTDB_PROFILE") != nullptr};
        std::array<uint64_t, 5> count{};
        std::array<double, 5> milliseconds{};
        std::mutex mutex;
        Clock::time_point last{Clock::now()};
        struct Span {
            Profile& profile; size_t slot; Clock::time_point start;
            Span(Profile& p, size_t s) : profile(p), slot(s), start(p.enabled ? Clock::now() : Clock::time_point()) {}
            ~Span() { if (profile.enabled) {
                const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
                std::lock_guard<std::mutex> lock(profile.mutex);
                ++profile.count[slot]; profile.milliseconds[slot] += elapsed;
            } }
        };
        void report() {
            if (!enabled || Clock::now()-last < std::chrono::seconds(1)) return;
            std::lock_guard<std::mutex> lock(mutex);
            swCDebug("sw.core.storage.realtimedb.profile")
                << "query=" << count[0] << "/" << milliseconds[0] << "ms"
                << " store=" << count[1] << "/" << milliseconds[1] << "ms"
                << " js=" << count[2] << "/" << milliseconds[2] << "ms"
                << " notify=" << count[3] << "/" << milliseconds[3] << "ms"
                << " encode=" << count[4] << "/" << milliseconds[4] << "ms";
            count.fill(0); milliseconds.fill(0); last=Clock::now();
        }
    };
    struct Result { SwString token, text; Clock::time_point expires; };
public:
    Impl(SwRealtimeDbNode& owner, const SwString& sys, const SwString& ns, const SwString& name,
         const SwRealtimeDbOptions& options)
        : owner_(owner), domain_(sys), endpoint_(ns.isEmpty() ? name : ns + "/" + name),
          lock_(SwLockFile::userRuntimePath("rtdb-" + sys + "-" + ns + "-" + name)),
          evaluator_(options.viewBudgetMs, options.viewHeapLimitBytes),
          store_({}, options, [this](const SwString& script) {
              const auto program = evaluator_.prepare(script);
              return [this, program](const SwJsonObject& tables) {
                  Profile::Span span(profile_,2); return evaluator_.evaluate(program, tables);
              };
          }),
          timer_(250) {
        if (!lock_.tryLock()) throw std::runtime_error("RtDb endpoint already owned");
        const auto state = store_.execute(operation("introspect"));
        epoch_ = state["epoch"].toString();
        lastRevision_ = state["revision"].toString();
        nativeQuery_ = NativeQuery::expose(domain_, endpoint_, "query", &owner_,
            [&owner](sw::ipc::RpcContext, SwJsonObject request) { return owner.executeRequest(request); });
        SwObject::connect(&timer_, &SwTimer::timeout, [this] { store_.expire(); notify(); prune(); profile_.report(); }, DirectConnection);
        timer_.start();
        swCDebug("sw.core.storage.realtimedb.lifecycle") << "RtDb ready: " << sys << "/" << name << " epoch=" << epoch_;
    }
    ~Impl() {
        nativeQuery_.stop();
        alive_.reset(); timer_.stop();
    }
    SwRealtimeDbReply queryTyped(const SwJsonObject& request) {
        if (owner_.affinityThreadId() != std::this_thread::get_id())
            return {false, {}, "RtDb query requires its owning thread"};
        Profile::Span span(profile_, 0);
        try {
            auto data = execute(request);
            // Account for the exact {"ok":true,"data":...} envelope too.
            swRealtimeDbDetail::JsonSize(2 * 1024 * 1024 - 19).object(data);
            return {true, std::move(data), {}};
        } catch (const std::exception& error) { return {false, {}, SwString(error.what()).left(512)}; }
    }
    SwString query(const SwString& packet) {
        Profile::Span span(profile_,0);
        try {
            if (packet.size() > 3000) throw std::invalid_argument("Use chunked upload for requests above 3000 bytes");
            const auto data = uploads_.dispatch(parseObject(packet), [this](const SwJsonObject& request) {
                auto reply = owner_.executeRequest(request);
                if (!reply.ok) throw std::runtime_error(reply.error.toStdString());
                return std::move(reply.data);
            });
            return respond(data);
        } catch (const std::exception& error) {
            SwJsonObject result; result["ok"] = SwJsonValue(false); result["error"] = SwJsonValue(SwString(error.what()).left(512));
            return json(result);
        }
    }
    SwString readResult(const SwString& token, int offset) {
        prune();
        if (offset < 0) throw std::invalid_argument("Negative result offset");
        for (auto it = results_.begin(); it != results_.end(); ++it) {
            const auto& result = *it;
            if (result.token != token) continue;
            if (static_cast<std::size_t>(offset) >= result.text.size()) throw std::invalid_argument("Result offset out of range");
            auto end = std::min(result.text.size(), static_cast<std::size_t>(offset) + 2000);
            while (end < result.text.size() && end > static_cast<std::size_t>(offset) &&
                   (static_cast<unsigned char>(result.text[end]) & 0xc0) == 0x80) --end;
            const auto chunk = result.text.mid(offset, static_cast<int>(end - offset));
            if (end == result.text.size()) results_.erase(it);
            return chunk;
        }
        throw std::runtime_error("Result expired; read again");
    }
private:
    SwJsonObject execute(const SwJsonObject& request) {
        if (executing_) throw std::runtime_error("Reentrant RtDb query is unavailable");
        struct Guard { bool& flag; Guard(bool& flag) : flag(flag) { flag = true; } ~Guard() { flag = false; } } guard(executing_);
        SwJsonObject result;
        { Profile::Span span(profile_,1); result = store_.execute(request); }
        const auto op = request["op"].toString();
        const bool reading = op == "read" || op == "read_many";
        if (reading ? result["cursor"].toString() != lastRevision_
                    : op != "changes" && op != "introspect" && op != "heartbeat" && op != "hello") notify();
        return result;
    }
    void prune() {
        while (!results_.empty() && results_.front().expires <= Clock::now()) results_.pop_front();
    }
    SwString respond(const SwJsonObject& data) {
        Profile::Span span(profile_,4);
        SwJsonObject response; response["ok"] = SwJsonValue(true); response["data"] = SwJsonValue(data);
        auto encoded = json(response);
        if (encoded.size() <= 3000) return encoded;
        if (encoded.size() > 2 * 1024 * 1024) throw std::runtime_error("Result exceeds 2 MiB");
        prune();
        if (results_.size() == 16) throw std::runtime_error("RtDb result transfer queue full; read again");
        const auto token = epoch_ + "-" + SwString::number(++nextResult_);
        const auto bytes = encoded.size();
        results_.push_back({token, std::move(encoded), Clock::now() + std::chrono::seconds(5)});
        response.remove("data"); response["result_id"] = SwJsonValue(token);
        response["bytes"] = SwJsonValue(static_cast<long long>(bytes));
        return json(response);
    }
    void notify() {
        if (notificationQueued_) return;
        notificationQueued_ = true;
        const std::weak_ptr<int> alive = alive_;
        // Finish the query before direct observers run. Batching this wakeup
        // does not discard mutations: the revision journal retains their order.
        if (!owner_.postToAffinity([this, alive] {
            if (alive.expired()) return;
            notificationQueued_ = false;
            notifyNow();
        })) {
            notificationQueued_ = false;
            ++lostWakeups_;
        }
    }
    std::optional<swRealtimeDbDetail::ChangeNotice> collectNotice() {
        Profile::Span span(profile_,3);
        auto request = operation("changes"); request["epoch"] = SwJsonValue(epoch_);
        request["after"] = SwJsonValue(lastRevision_); request["mode"] = SwJsonValue("write");
        request["include_values"] = true;
        // One immutable index serves all native receivers, on any thread.
        // The same signal's wire codec retains the compact external wakeup.
        auto state = store_.execute(request);
        const auto revision = state["revision"].toString();
        if (revision == lastRevision_) return std::nullopt;
        SwJsonObject notification; notification["epoch"] = SwJsonValue(epoch_); notification["revision"] = SwJsonValue(revision);
        notification["from_revision"]=lastRevision_;
        lastRevision_ = revision;
        if (!state["resync_required"].toBool()) {
            std::set<SwString> changedTables;
            bool created = false;
            const std::shared_ptr<const SwJsonArray> events = state["events"].toArrayPtr();
            if (events) for (const auto& value : *events) {
                const std::shared_ptr<const SwJsonObject> event = value.toObjectPtr();
                if (!event) continue;
                changedTables.insert((*event)["table"].toString());
                created = created || (*event)["kind"].toString() == "created";
            }
            SwJsonArray names;
            for (const auto& name : changedTables) names.append(name);
            notification["tables"] = std::move(names); notification["catalog_changed"] = created;
            // Large catalog changes use the same safe unfiltered wakeup.
            if (swRealtimeDbDetail::JsonSize(2 * 1024 * 1024).object(notification)>3000) notification.remove("tables");
        }
        auto wire = json(notification);
        notification["resync_required"] = state["resync_required"];
        notification["events"] = std::move(state["events"]);
        if(state.contains("snapshots")) {
            notification["snapshots"]=std::move(state["snapshots"]);
            notification["value_tables"]=std::move(state["value_tables"]);
            // Push the bounded complete value batch over native SHM too. Large
            // payloads retain the compact wakeup and recover by subscription,
            // through the existing paged RPC transport (no truncated rows).
            try {
                swRealtimeDbDetail::JsonSize(swRealtimeDbDetail::changeNoticeCapacity-64).object(notification);
                wire=json(notification);
            } catch(const std::runtime_error&) {}
        }
        auto typed = std::make_shared<const swRealtimeDbDetail::ChangeBatch>(std::move(notification));
        return swRealtimeDbDetail::ChangeNotice(std::move(typed), wire);
    }
    void notifyNow() {
        const auto notice = collectNotice();
        if (!notice) return;
        // Profiling and store guards have ended. A direct callback may destroy
        // this node, so only touch Impl again while its lifetime is still valid.
        const std::weak_ptr<int> alive = alive_;
        if (!owner_.changed(*notice) && !alive.expired()) ++lostWakeups_;
    }
    Profile profile_;
    SwRealtimeDbNode& owner_;
    SwString domain_, endpoint_;
    using NativeQuery = sw::ipc::NativeRpcEndpoint<SwRealtimeDbReply, SwJsonObject>;
    NativeQuery::Registration nativeQuery_;
    bool executing_{false};
    SwLockFile lock_;
    SwRealtimeDbJsEvaluator evaluator_;
    SwRealtimeDb store_;
    sw::ipc::JsonRequestAssembler uploads_;
    SwTimer timer_;
    SwString epoch_, lastRevision_;
    std::shared_ptr<int> alive_{std::make_shared<int>(0)};
    bool notificationQueued_{false};
    std::deque<Result> results_;
    std::uint64_t nextResult_{0}, lostWakeups_{0};
};
SwRealtimeDbNode::SwRealtimeDbNode(const SwString& sys, const SwString& ns, const SwString& name, SwObject* parent)
    : SwRealtimeDbNode(sys, ns, name, SwRealtimeDbOptions{}, parent) {}
SwRealtimeDbNode::SwRealtimeDbNode(const SwString& sys, const SwString& ns, const SwString& name,
                                 const SwRealtimeDbOptions& options, SwObject* parent)
    : SwRemoteObject(sys, ns, name, parent), impl_(std::make_unique<Impl>(*this, sys, ns, name, options)) {
    queryToken_ = ipcExposeRpc(query, this, &SwRealtimeDbNode::query, false);
    readResultToken_ = ipcExposeRpc(readResult, this, &SwRealtimeDbNode::readResult, false);
}
SwRealtimeDbNode::~SwRealtimeDbNode() {
    ipcDisconnect(queryToken_);
    ipcDisconnect(readResultToken_);
    impl_.reset();
}
SwRealtimeDbReply SwRealtimeDbNode::executeRequest(const SwJsonObject& request) { return impl_->queryTyped(request); }
SwString SwRealtimeDbNode::query(SwString request) { return impl_->query(request); }
SwString SwRealtimeDbNode::readResult(SwString token, int offset) { return impl_->readResult(token, offset); }
