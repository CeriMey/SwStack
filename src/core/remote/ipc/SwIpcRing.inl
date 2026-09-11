// Internal implementation, included inside namespace sw::ipc.
// Replay protects every unread message; LatestOnly never holds back a producer.
// Mixing both modes preserves Replay's backpressure on the shared channel.
enum class DeliveryMode { Replay = 0, LatestOnly = 1, FollowPublisher = 2 };

template <class... Args>
class RingQueueDynamic {
public:
    static const uint32_t kDefaultMaxPayload = 4096u;
    static const uint32_t kMaxSubscriberCursors = 64u;
private:
    struct SubscriberCursor {
        uint64_t token;
        uint64_t readSeq;
        uint32_t subPid;
        uint32_t active;
        uint32_t mode;
        uint32_t reserved;
    };
    struct Header {
        uint32_t magic;
        uint32_t version;
        uint64_t typeId;
        uint32_t capacity;
        uint32_t maxPayload;
        uint32_t cursorCount;
        uint32_t cursorCapacity;
        uint32_t defaultMode;
        uint32_t reserved;
#ifndef _WIN32
        pthread_mutex_t mtx;
        pthread_cond_t cv;
#endif
        uint64_t seq;
        uint64_t nextToken;
        SubscriberCursor cursors[kMaxSubscriberCursors];
        static const uint32_t kMagic = 0x51554431u;
        static const uint32_t kVersion = 3u;
    };
    struct DynamicSlot {
        uint64_t seq;
        uint64_t origin;
        uint32_t size;
        uint32_t reserved;
    };
#include "RingMapping.inl"

    struct SubscriptionControl {
        std::shared_ptr<DynamicMapping> map;
        SwString domain, object, signal, subscriberObject;
        uint32_t pid{0};
        uint64_t token{0};
        DeliveryMode mode{DeliveryMode::Replay};
        size_t pollerId{0};
        std::atomic_bool active{true};
        std::function<void()> dispatch;
        void stop() {
            if (!active.exchange(false, std::memory_order_acq_rel)) return;
            detail::LoopPoller::instance().remove(pollerId);
            if (map && token) {
                MappingLock lock(map);
                if (lock) {
                    Header* header = map->header();
                    SubscriberCursor* cursor = findCursorLocked_(header, token);
                    if (cursor) { *cursor = SubscriberCursor{}; --header->cursorCount; }
                }
            }
            detail::SubscribersRegistryTable<>::unregisterSubscription(domain, object, signal, pid,
                                                                        subscriberObject);
        }
    };
public:
    class Subscription {
    public:
        Subscription() = default;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& other) noexcept
            : control_(std::atomic_exchange_explicit(&other.control_, std::shared_ptr<SubscriptionControl>(),
                                                     std::memory_order_acq_rel)) {}
        Subscription& operator=(Subscription&& other) noexcept {
            if (this != &other) {
                stop();
                auto next = std::atomic_exchange_explicit(&other.control_, std::shared_ptr<SubscriptionControl>(),
                                                          std::memory_order_acq_rel);
                std::atomic_store_explicit(&control_, std::move(next), std::memory_order_release);
            }
            return *this;
        }
        ~Subscription() { stop(); }
        void stop() {
            auto control = std::atomic_exchange_explicit(&control_, std::shared_ptr<SubscriptionControl>(),
                                                         std::memory_order_acq_rel);
            if (control) control->stop();
        }
        DeliveryMode deliveryMode() const {
            auto control = std::atomic_load_explicit(&control_, std::memory_order_acquire);
            if (!control) return DeliveryMode::Replay;
            if (control->mode != DeliveryMode::FollowPublisher) return control->mode;
            MappingLock lock(control->map);
            return lock ? static_cast<DeliveryMode>(control->map->header()->defaultMode) : DeliveryMode::Replay;
        }
        void drain() {
            auto control = std::atomic_load_explicit(&control_, std::memory_order_acquire);
            if (control && control->active.load(std::memory_order_acquire) && control->dispatch)
                control->dispatch();
        }
    private:
        friend class RingQueueDynamic;
        explicit Subscription(std::shared_ptr<SubscriptionControl> control) : control_(std::move(control)) {}
        std::shared_ptr<SubscriptionControl> control_;
    };

    RingQueueDynamic(Registry& reg, const SwString& signalName, uint32_t capacity,
                     uint32_t maxPayload = kDefaultMaxPayload, bool discoverExisting = false,
                     DeliveryMode channelMode = DeliveryMode::Replay)
        : reg_(reg), signalName_(signalName),
          shmName_(detail::make_shm_name(reg.domain(), reg.object(), signalName) + "_r3"),
          map_(openMapping_(shmName_, detail::type_id<Args...>(), capacity, maxPayload,
                            true, discoverExisting, channelMode)) {
        detail::RegistryTable<>::registerSignal(reg_.domain(), reg_.object(), signalName_, shmName_,
                                               detail::type_id<Args...>(), detail::type_name<Args...>());
    }
    const SwString& shmName() const { return shmName_; }
    uint32_t capacity() const { return map_->header()->capacity; }
    uint32_t maxPayload() const { return map_->header()->maxPayload; }
    DeliveryMode defaultMode() const {
        MappingLock lock(map_);
        return lock ? static_cast<DeliveryMode>(map_->header()->defaultMode) : DeliveryMode::Replay;
    }
    uint64_t sequence() const {
        MappingLock lock(map_);
        return lock ? map_->header()->seq : 0;
    }
    bool push(const Args&... args) {
        uint64_t sequence = 0;
        return pushStamped(0, sequence, args...);
    }
    bool pushAs(DeliveryMode mode, const Args&... args) {
        uint64_t sequence = 0;
        return pushStampedAs(mode, 0, sequence, args...);
    }
    bool pushStamped(uint64_t origin, uint64_t& sequence, const Args&... args) {
        return pushStampedAs(DeliveryMode::FollowPublisher, origin, sequence, args...);
    }
    bool pushStampedAs(DeliveryMode mode, uint64_t origin, uint64_t& sequence, const Args&... args) {
        sequence = 0;
        detail::ScratchBuffer bytes(maxPayload());
        detail::Encoder encoder(bytes.data(), bytes.size());
        if (!detail::writeAll(encoder, args...)) return false;
        return publishBytes_(map_, origin, sequence, bytes.data(), encoder.size(), mode);
    }
    bool operator()(const Args&... args) { return push(args...); }
    bool readLatest(Args&... args) const {
        uint64_t sequence = 0, origin = 0;
        return readLatestStamped(sequence, origin, args...);
    }
    bool readLatestStamped(uint64_t& sequence, uint64_t& origin, Args&... args) const {
        std::vector<uint8_t> bytes;
        if (!readLatestBytes_(map_, bytes, sequence, origin)) return false;
        std::tuple<typename std::decay<Args>::type...> values;
        detail::Decoder decoder(bytes.data(), bytes.size());
        if (!detail::readTuple(decoder, values)) return false;
        std::tie(args...) = std::move(values);
        return true;
    }

    // Type-erased tools use the registry's mapping name and type ID. They never
    // create a channel, and validate the layout before inspecting its payload.
    // Named connections may wait for an absent publisher or its uninitialized
    // mapping shell. An incompatible initialized channel must fail immediately.
    static bool inspectExisting(const SwString& shmName, uint64_t expectedTypeId, uint32_t& capacity,
                                uint32_t& maxPayload, DeliveryMode& mode) {
        try {
            auto mapping = openMapping_(shmName, expectedTypeId, 0, 0, false, true);
            MappingLock lock(mapping);
            if (!lock) throw std::runtime_error("RingQueueDynamic: cannot inspect locked mapping");
            capacity = mapping->header()->capacity; maxPayload = mapping->header()->maxPayload;
            mode = static_cast<DeliveryMode>(mapping->header()->defaultMode);
            return true;
        } catch (const MissingMapping&) { return false; }
          catch (const MappingPending&) { return false; }
    }
    static bool inspect(const SwString& shmName, uint64_t expectedTypeId, uint32_t& capacity,
                        uint32_t& maxPayload, DeliveryMode& mode) {
        try { return inspectExisting(shmName, expectedTypeId, capacity, maxPayload, mode); }
        catch (const std::exception&) { return false; }
    }
    static bool readLatestBytes(const SwString& shmName, uint64_t expectedTypeId,
                                std::vector<uint8_t>& bytes, uint64_t& sequence, uint64_t& origin) {
        try {
            return readLatestBytes_(openMapping_(shmName, expectedTypeId, 0, 0, false, true),
                                    bytes, sequence, origin);
        } catch (const std::exception&) { return false; }
    }
    static bool publishBytes(const SwString& shmName, uint64_t expectedTypeId,
                              const uint8_t* bytes, size_t size) {
        try {
            uint64_t sequence = 0;
            return publishBytes_(openMapping_(shmName, expectedTypeId, 0, 0, false, true),
                                 0, sequence, bytes, size);
        } catch (const std::exception&) { return false; }
    }

    template <typename Fn>
    Subscription connect(Fn cb, bool fireInitial = true, int timeoutMs = 0,
                         DeliveryMode mode = DeliveryMode::Replay) {
        return connectStamped([cb](uint64_t, uint64_t, Args... args) mutable { cb(args...); },
                              fireInitial, timeoutMs, mode);
    }
    template <typename Fn>
    Subscription connectStamped(Fn cb, bool fireInitial = true, int timeoutMs = 0,
                                DeliveryMode mode = DeliveryMode::Replay,
                                std::shared_ptr<std::recursive_mutex> dispatchMutex = {},
                                std::function<std::function<void()>(uint64_t, uint64_t)> nativeDelivery = {}) {
        if (mode != DeliveryMode::Replay && mode != DeliveryMode::LatestOnly && mode != DeliveryMode::FollowPublisher)
            throw std::runtime_error("RingQueueDynamic: invalid subscription mode");
        typedef typename std::decay<Fn>::type Callback;
        struct State {
            std::shared_ptr<SubscriptionControl> control;
            Callback cb;
            std::shared_ptr<std::recursive_mutex> dispatchMutex;
            std::function<std::function<void()>(uint64_t, uint64_t)> nativeDelivery;
            std::atomic_bool inCallback{false};
            State(std::shared_ptr<SubscriptionControl> c, Callback f,
                  std::shared_ptr<std::recursive_mutex> mutex,
                  std::function<std::function<void()>(uint64_t, uint64_t)> native)
                : control(std::move(c)), cb(std::move(f)), dispatchMutex(std::move(mutex)),
                  nativeDelivery(std::move(native)) {}
        };
        auto control = std::make_shared<SubscriptionControl>();
        control->map = map_;
        control->domain = reg_.domain(); control->object = reg_.object(); control->signal = signalName_;
        control->pid = detail::currentPid();
        control->mode = mode;
        control->subscriberObject = detail::currentSubscriberObject_();
        {
            MappingLock lock(map_);
            if (!lock) throw std::runtime_error("RingQueueDynamic: cannot lock subscription");
            SubscriberCursor* cursor = createCursorLocked_(map_->header(), control->pid, fireInitial, mode);
            if (!cursor) throw std::runtime_error("RingQueueDynamic: subscriber capacity exhausted");
            control->token = cursor->token;
        }
        auto state = std::make_shared<State>(control, Callback(std::move(cb)), std::move(dispatchMutex),
                                             std::move(nativeDelivery));
        // The control's callable captures weakly, while LoopPoller owns State.
        // This lets stop() release the registration without a reference cycle.
        std::weak_ptr<State> weakState(state);
        control->dispatch = [weakState]() {
            auto state = weakState.lock();
            if (!state) return;
            std::unique_lock<std::recursive_mutex> dispatchLock;
            if (state->dispatchMutex) dispatchLock = std::unique_lock<std::recursive_mutex>(*state->dispatchMutex);
            auto control = state->control;
            if (!control->active.load(std::memory_order_acquire) ||
                state->inCallback.exchange(true, std::memory_order_acq_rel)) return;
            struct Reset {
                std::atomic_bool& flag;
                ~Reset() { flag.store(false, std::memory_order_release); }
            } reset{state->inCallback};
            struct Message {
                uint64_t sequence{0}, origin{0};
                std::vector<uint8_t> bytes;
                std::function<void()> native;
            };
            // A callback may publish recursively. Pick up its new messages before
            // returning, after the current ordered batch has completed.
            while (control->active.load(std::memory_order_acquire)) {
                if (state->dispatchMutex && !dispatchLock.owns_lock())
                    dispatchLock = std::unique_lock<std::recursive_mutex>(*state->dispatchMutex);
                std::vector<Message> messages;
                {
                    MappingLock lock(control->map);
                    if (!lock) return;
                    Header* header = control->map->header();
                    SubscriberCursor* cursor = findCursorLocked_(header, control->token);
                    if (!cursor || cursor->readSeq >= header->seq) return;
                    const uint64_t latest = header->seq;
                    const uint64_t start = effectiveModeLocked_(header, *cursor) == DeliveryMode::LatestOnly
                        ? latest : cursor->readSeq + 1;
                    for (uint64_t sequence = start; sequence <= latest; ++sequence) {
                        DynamicSlot* slot = control->map->slotAt(sequence % header->capacity);
                        if (slot->seq != sequence || slot->size > header->maxPayload) continue;
                        Message message;
                        message.sequence = sequence; message.origin = slot->origin;
                        if (state->nativeDelivery) message.native = state->nativeDelivery(sequence, slot->origin);
                        if (!message.native) {
                            message.bytes.resize(slot->size);
                            if (slot->size) std::memcpy(message.bytes.data(), control->map->slotData(slot), slot->size);
                        }
                        messages.push_back(std::move(message));
                    }
                    cursor->readSeq = latest;
                }
                // User callbacks can publish into another channel. Never keep a
                // channel lock across them: two emitters could otherwise deadlock.
                if (dispatchLock.owns_lock()) dispatchLock.unlock();
                for (auto& message : messages) {
                    if (!control->active.load(std::memory_order_acquire)) return;
                    if (message.native) { message.native(); continue; }
                    std::tuple<typename std::decay<Args>::type...> values;
                    detail::Decoder decoder(message.bytes.data(), message.bytes.size());
                    if (detail::readTuple(decoder, values)) {
                        auto stamped = std::tuple_cat(std::make_tuple(message.sequence, message.origin), std::move(values));
                        detail::invokeWithTuple(state->cb, stamped);
                    }
                }
            }
        };
        try {
            detail::SubscribersRegistryTable<>::registerSubscription(control->domain, control->object,
                control->signal, control->pid, control->subscriberObject);
            control->pollerId = detail::LoopPoller::instance().add([state]() { state->control->dispatch(); });
            if (timeoutMs > 0) {
                std::weak_ptr<SubscriptionControl> weak(control);
                SwTimer::singleShot(timeoutMs, [weak]() { if (auto c = weak.lock()) c->stop(); });
            }
        } catch (...) { control->stop(); throw; }
        return Subscription(control);
    }
private:
    static SubscriberCursor* findCursorLocked_(Header* header, uint64_t token) {
        for (auto& cursor : header->cursors)
            if (cursor.active && cursor.token == token) return &cursor;
        return nullptr;
    }
    static void clearDeadCursorsLocked_(Header* header) {
        for (auto& cursor : header->cursors) {
            if (cursor.active && detail::pidStateBestEffort_(cursor.subPid) == detail::PidState::Dead) {
                cursor = SubscriberCursor{};
                --header->cursorCount;
            }
        }
    }
    static SubscriberCursor* createCursorLocked_(Header* header, uint32_t pid,
                                                 bool fireInitial, DeliveryMode mode) {
        clearDeadCursorsLocked_(header);
        for (auto& cursor : header->cursors) {
            if (cursor.active) continue;
            cursor = SubscriberCursor{};
            cursor.active = 1; cursor.subPid = pid;
            cursor.mode = static_cast<uint32_t>(mode);
            cursor.token = ++header->nextToken;
            if (!cursor.token) cursor.token = ++header->nextToken;
            cursor.readSeq = header->seq;
            if (fireInitial && header->seq) {
                const uint64_t backlog = effectiveModeLocked_(header, cursor) == DeliveryMode::LatestOnly ? 1 : header->capacity;
                cursor.readSeq = header->seq > backlog ? header->seq - backlog : 0;
            }
            ++header->cursorCount;
            return &cursor;
        }
        return nullptr;
    }
    static DeliveryMode effectiveModeLocked_(Header* header, const SubscriberCursor& cursor) {
        return cursor.mode == static_cast<uint32_t>(DeliveryMode::FollowPublisher)
            ? static_cast<DeliveryMode>(header->defaultMode) : static_cast<DeliveryMode>(cursor.mode);
    }
    static uint64_t minReadSeqLocked_(Header* header) {
        uint64_t minimum = header->seq;
        for (const auto& cursor : header->cursors) {
            if (cursor.active && effectiveModeLocked_(header, cursor) == DeliveryMode::Replay)
                minimum = std::min(minimum, cursor.readSeq);
        }
        return minimum;
    }
    static bool readLatestBytes_(const std::shared_ptr<DynamicMapping>& map, std::vector<uint8_t>& bytes,
                                 uint64_t& sequence, uint64_t& origin) {
        sequence = 0; origin = 0;
        MappingLock lock(map);
        if (!lock) return false;
        Header* header = map->header();
        if (!header->seq) return false;
        DynamicSlot* slot = map->slotAt(header->seq % header->capacity);
        if (slot->seq != header->seq || slot->size > header->maxPayload) return false;
        bytes.resize(slot->size);
        if (slot->size) std::memcpy(bytes.data(), map->slotData(slot), slot->size);
        sequence = slot->seq; origin = slot->origin;
        return true;
    }
    static bool publishBytes_(const std::shared_ptr<DynamicMapping>& map, uint64_t origin, uint64_t& sequence,
                              const uint8_t* bytes, size_t size,
                              DeliveryMode mode = DeliveryMode::FollowPublisher) {
        sequence = 0;
        if (mode != DeliveryMode::Replay && mode != DeliveryMode::LatestOnly && mode != DeliveryMode::FollowPublisher)
            return false;
        if (size > map->header()->maxPayload || (size && !bytes)) return false;
        std::vector<uint32_t> pids;
        {
            MappingLock lock(map);
            if (!lock) return false;
            Header* header = map->header();
            const uint32_t previousMode = header->defaultMode;
            if (mode != DeliveryMode::FollowPublisher) header->defaultMode = static_cast<uint32_t>(mode);
            // If a state publisher becomes an event publisher, entries already
            // overwritten under LatestOnly cannot be replayed retroactively.
            if (previousMode != header->defaultMode && mode == DeliveryMode::Replay) {
                const uint64_t oldest = header->seq > header->capacity ? header->seq - header->capacity : 0;
                for (auto& cursor : header->cursors)
                    if (cursor.active && cursor.mode == static_cast<uint32_t>(DeliveryMode::FollowPublisher))
                        cursor.readSeq = std::max(cursor.readSeq, oldest);
            }
            uint64_t minimum = minReadSeqLocked_(header);
            if (header->seq - minimum >= header->capacity) {
                clearDeadCursorsLocked_(header);
                minimum = minReadSeqLocked_(header);
                if (header->seq - minimum >= header->capacity) { header->defaultMode = previousMode; return false; }
            }
            if (header->seq == std::numeric_limits<uint64_t>::max()) { header->defaultMode = previousMode; return false; }
            const uint64_t next = header->seq + 1;
            DynamicSlot* slot = map->slotAt(next % header->capacity);
            if (size) std::memcpy(map->slotData(slot), bytes, size);
            slot->size = static_cast<uint32_t>(size); slot->origin = origin; slot->seq = next;
            header->seq = next; sequence = next;
            for (const auto& cursor : header->cursors)
                if (cursor.active) pids.push_back(cursor.subPid);
#ifndef _WIN32
            pthread_cond_broadcast(&header->cv);
#endif
        }
        std::sort(pids.begin(), pids.end());
        pids.erase(std::unique(pids.begin(), pids.end()), pids.end());
        for (auto pid : pids) detail::LoopPoller::notifyProcess(pid);
        return true;
    }
    Registry& reg_;
    SwString signalName_, shmName_;
    std::shared_ptr<DynamicMapping> map_;
};
