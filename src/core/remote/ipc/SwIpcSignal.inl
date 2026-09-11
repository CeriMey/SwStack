// Internal implementation, included inside namespace sw::ipc.
// Native delivery and SHM use one cursor per subscription. A native publication
// remains available while its subscribers drain, including reentrant emits.
template <class... Args>
class SwIpcSignal {
public:
    // SharedValues owns an immutable publication. References passed to slots
    // remain valid until that invocation returns, including cooperative yields.
    typedef std::tuple<typename std::decay<Args>::type...> Values;
    typedef std::shared_ptr<const Values> SharedValues;
    typedef std::function<void(const typename std::decay<Args>::type&...)> Callback;
private:
    typedef RingQueueDynamic<Args...> Ring;
    struct Subscriber;
    struct Channel {
        std::shared_ptr<std::recursive_mutex> mutex{new std::recursive_mutex};
        std::vector<std::weak_ptr<Subscriber>> subscribers;
        std::vector<std::weak_ptr<std::function<void()>>> declarations;
        bool declared{false};
        std::map<uint64_t, SharedValues> publications;
        uint64_t origin() const {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) ^
                (static_cast<uint64_t>(detail::currentPid()) << 32);
        }
    };
    static std::shared_ptr<Channel> channel(const SwString& name) {
        // The native layout changed; older modules must use the compatible wire
        // path instead of interpreting this channel with their previous layout.
        return detail::nativeSignalChannel<Channel>(name.toStdString() + ":shared-values-v1");
    }

    struct Subscriber : std::enable_shared_from_this<Subscriber> {
        std::shared_ptr<Channel> channel;
        typename Ring::Subscription wire;
        Callback callback;
        std::atomic_bool active{true};
        SwObject* context{nullptr};
        std::weak_ptr<void> contextLife;
        std::thread::id subscribedThread;
        DeliveryMode mode{DeliveryMode::Replay};
        std::mutex queueMutex;
        std::vector<SharedValues> pending;
        bool scheduled{false};

        bool live() const { return active.load() && (!context || !contextLife.expired()); }
        bool onTargetThread() const {
            return live() && (context ? context->affinityThreadId() : subscribedThread) ==
                std::this_thread::get_id();
        }
        void stop() {
            active.store(false);
            wire.stop();
            std::lock_guard<std::mutex> queueLock(queueMutex);
            pending.clear();
        }
        bool post() {
            const std::weak_ptr<Subscriber> weak = this->shared_from_this();
            std::function<void()> task = [weak]() {
                if (auto self = weak.lock()) self->drainQueued();
            };
            if (!live()) return false;
            return context ? context->postToAffinity(std::move(task)) :
                detail::postNativeSignalThread(subscribedThread, std::move(task));
        }
        bool deliver(const SharedValues& values) {
            if (!live()) return true;
            bool direct = false;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                direct = !scheduled && onTargetThread();
                if (!direct) {
                    if (wire.deliveryMode() == DeliveryMode::LatestOnly) pending.clear();
                    pending.push_back(values);
                    if (scheduled) return true;
                    scheduled = true;
                }
            }
            if (direct) {
                if (live()) detail::invokeWithTuple(callback, *values);
                return true;
            }
            if (post()) return true;
            std::lock_guard<std::mutex> lock(queueMutex);
            scheduled = false;
            pending.clear();
            return false;
        }
        void drainQueued() {
            if (!live()) return;
            if (!onTargetThread()) {
                if (!post()) stop();
                return;
            }
            for (;;) {
                std::vector<SharedValues> batch;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    if (pending.empty()) { scheduled = false; return; }
                    batch.swap(pending);
                }
                for (auto& values : batch) {
                    if (!live()) return;
                    if (!onTargetThread()) {
                        // Affinity changed inside a callback. Preserve order when
                        // returning the remaining batch to the new owner thread.
                        std::lock_guard<std::mutex> lock(queueMutex);
                        pending.insert(pending.begin(), &values, batch.data() + batch.size());
                        if (!post()) { active.store(false); pending.clear(); }
                        return;
                    }
                    try { detail::invokeWithTuple(callback, *values); }
                    catch (...) {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        scheduled = false;
                        throw;
                    }
                }
            }
        }
    };

public:
    // A named subscription can wait without allocating a provisional wire
    // layout. This setup hook runs on the declaring thread before its first
    // emission; it never delivers user signal callbacks.
    struct DeclarationWatch {
        std::shared_ptr<Channel> channel;
        std::shared_ptr<std::function<void()>> callback;
    };
    static DeclarationWatch watchDeclaration(const Registry& registry, const SwString& name,
                                              std::function<void()> callback) {
        DeclarationWatch watch;
        watch.channel = channel(detail::make_shm_name(registry.domain(), registry.object(), name) + "_r3");
        watch.callback = std::make_shared<std::function<void()>>(std::move(callback));
        std::lock_guard<std::recursive_mutex> lock(*watch.channel->mutex);
        watch.channel->declarations.push_back(watch.callback);
        return watch;
    }
    class Subscription {
    public:
        Subscription() = default;
        explicit Subscription(std::shared_ptr<Subscriber> value) : state_(std::move(value)) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept : state_(std::move(other.state_)) {}
        Subscription& operator=(Subscription&& other) noexcept {
            if (this != &other) { stop(); state_ = std::move(other.state_); }
            return *this;
        }
        ~Subscription() { stop(); }
        void stop() { if (state_) { state_->stop(); state_.reset(); } }
    private:
        std::shared_ptr<Subscriber> state_;
    };

    SwIpcSignal(Registry& reg, const SwString& name, uint32_t capacity = 16u,
                uint32_t maxBytes = 0u, DeliveryMode mode = DeliveryMode::FollowPublisher)
        : ring_(reg, name, capacity, payloadSize(maxBytes), true,
                mode == DeliveryMode::FollowPublisher ? DeliveryMode::Replay : mode),
          channel_(channel(ring_.shmName())), defaultMode_(mode) {
        detail::registerNativeSignalThread();
        std::vector<std::shared_ptr<std::function<void()>>> callbacks;
        {
            std::lock_guard<std::recursive_mutex> lock(*channel_->mutex);
            // Only the first declaration announces availability. Reopening a
            // declared signal while binding another subscriber must not invoke
            // setup hooks recursively under that subscriber's setup mutex.
            if (channel_->declared) return;
            channel_->declared = true;
            auto& listeners = channel_->declarations;
            for (auto it = listeners.begin(); it != listeners.end();) {
                if (auto listener = it->lock()) { callbacks.push_back(listener); ++it; }
                else it = listeners.erase(it);
            }
        }
        for (const auto& callback : callbacks) (*callback)();
    }
    SwIpcSignal(const SwIpcSignal&) = delete;
    SwIpcSignal& operator=(const SwIpcSignal&) = delete;

    bool publish(const Args&... args) {
        return publishImpl({}, nullptr, args...);
    }
    // Internal state owners commit their cache after successful publication,
    // before any observer runs. Commit must only update owned state; it must
    // not call observers or publish recursively while the channel is locked.
    bool publishWithCommit(std::function<void()> commit, const Args&... args) {
        return publishImpl(std::move(commit), nullptr, args...);
    }
    // Avoid the initial snapshot copy when the caller already owns immutable
    // values. No mutable alias may modify them while retained by the signal or
    // any receiver. The usual wire codec, payload bound and replay rules apply.
    bool publishShared(SharedValues values) {
        if (!values) return false;
        bool accepted = false;
        auto publish = [this, &values, &accepted](const Args&... args) {
            accepted = publishImpl({}, &values, args...);
        };
        detail::invokeWithTuple(publish, *values);
        return accepted;
    }
    bool operator()(const Args&... args) { return publish(args...); }
    bool readLatest(Args&... args) const { return ring_.readLatest(args...); }
    uint32_t maxBytes() const { return ring_.maxPayload(); }
    uint32_t capacity() const { return ring_.capacity(); }
    DeliveryMode defaultMode() const {
        return defaultMode_ == DeliveryMode::FollowPublisher ? ring_.defaultMode() : defaultMode_;
    }
    const SwString& shmName() const { return ring_.shmName(); }
    Ring& raw() { return ring_; }
    const Ring& raw() const { return ring_; }

    template <typename Fn>
    Subscription connect(Fn callback, bool fireInitial = true, int timeoutMs = 0) {
        return connectImpl(nullptr, std::move(callback), defaultMode_, fireInitial, timeoutMs);
    }
    template <typename Fn>
    Subscription connect(Fn callback, DeliveryMode mode, bool fireInitial = true, int timeoutMs = 0) {
        return connectImpl(nullptr, std::move(callback), mode, fireInitial, timeoutMs);
    }
    template <typename Fn>
    Subscription connect(SwObject* context, Fn callback, bool fireInitial = true, int timeoutMs = 0) {
        return connectImpl(context, std::move(callback), defaultMode_, fireInitial, timeoutMs);
    }
    template <typename Fn>
    Subscription connect(SwObject* context, Fn callback, DeliveryMode mode,
                         bool fireInitial = true, int timeoutMs = 0) {
        return connectImpl(context, std::move(callback), mode, fireInitial, timeoutMs);
    }
private:
    bool publishImpl(std::function<void()> commit, const SharedValues* shared, const Args&... args) {
        const auto local = channel_;
        std::vector<std::shared_ptr<Subscriber>> subscribers;
        SharedValues publication;
        {
            std::lock_guard<std::recursive_mutex> lock(*local->mutex);
            auto& weak = local->subscribers;
            for (auto it = weak.begin(); it != weak.end();) {
                auto sub = it->lock();
                if (!sub || !sub->live()) it = weak.erase(it);
                else { subscribers.push_back(std::move(sub)); ++it; }
            }
            uint64_t sequence = 0;
            if (subscribers.empty()) {
                if (!ring_.pushAs(defaultMode_, args...)) return false;
                if (commit) commit();
                return true;
            }
            publication = shared ? *shared : std::make_shared<const Values>(args...);
            // SHM remains published for external and late subscribers. Local
            // receivers use the typed snapshot instead of copying/decoding SHM.
            if (!ring_.pushStampedAs(defaultMode_, local->origin(), sequence, args...)) return false;
            if (commit) commit();
            local->publications[sequence] = std::move(publication);
            // Retain the bounded native backlog while a reentrant emission is
            // waiting behind the current callback. Replay cursors protect it
            // from being overwritten before the corresponding drain.
            while (!local->publications.empty() &&
                   sequence - local->publications.begin()->first >= ring_.capacity())
                local->publications.erase(local->publications.begin());
        }
        // Arbitrary callbacks must run outside the channel lock: concurrent
        // callbacks on two channels may themselves publish to each other.
        for (const auto& sub : subscribers) if (sub->live()) sub->wire.drain();
        // Publication success describes the accepted SHM write. A receiver
        // whose thread has stopped cannot undo that write or the owner commit;
        // reporting failure here would cause callers to retry an accepted event.
        return true;
    }
    static uint32_t payloadSize(uint32_t requested) {
        if (!requested && !size::IpcWireBound<Args...>::bounded)
            throw std::invalid_argument("SwIpcSignal: variable-size types require maxBytes (SW_IPC_SIGNAL_SIZED)");
        return IpcAutoMaxBytes_<Args...>::resolve(requested);
    }
    template <typename Fn>
    Subscription connectImpl(SwObject* context, Fn callback, DeliveryMode mode,
                             bool fireInitial, int timeoutMs) {
        auto state = std::make_shared<Subscriber>();
        state->channel = channel_;
        state->callback = std::move(callback);
        state->context = context;
        if (context) state->contextLife = context->lifetimeToken();
        detail::registerNativeSignalThread();
        state->subscribedThread = std::this_thread::get_id();
        state->mode = mode;
        std::lock_guard<std::recursive_mutex> lock(*channel_->mutex);
        const std::weak_ptr<Subscriber> weak = state;
        state->wire = ring_.connectStamped([weak](uint64_t, uint64_t, typename std::decay<Args>::type&... args) {
            if (auto sub = weak.lock())
                sub->deliver(std::make_shared<const Values>(std::move(args)...));
        }, fireInitial, 0, mode, channel_->mutex,
        [weak](uint64_t sequence, uint64_t origin) -> std::function<void()> {
            auto sub = weak.lock();
            if (!sub || !sub->live() || origin != sub->channel->origin()) return {};
            const auto it = sub->channel->publications.find(sequence);
            if (it == sub->channel->publications.end()) return {};
            const auto& publication = it->second;
            // The drain owns this short-lived message, not the channel. Retain
            // its subscriber once; deliver() still rejects stopped connections.
            return [sub, publication]() {
                sub->deliver(publication);
            };
        });
        channel_->subscribers.push_back(state);
        if (timeoutMs > 0) SwTimer::singleShot(timeoutMs, [weak]() {
            if (auto sub = weak.lock()) sub->stop();
        });
        return Subscription(std::move(state));
    }
    Ring ring_;
    std::shared_ptr<Channel> channel_;
    DeliveryMode defaultMode_;
};
