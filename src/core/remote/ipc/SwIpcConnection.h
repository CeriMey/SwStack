#pragma once
#include "../SwSharedMemorySignal.h"

namespace sw { namespace ipc {

// A named connection may precede its publisher. Wait for the declaration so
// that a generic subscriber never creates a channel with guessed dimensions.
template <class... Args>
class NamedSignalConnection {
    typedef typename SwIpcSignal<Args...>::Callback Callback;
    struct State : std::enable_shared_from_this<State> {
        State(const SwString& domain, const SwString& object, const SwString& leaf,
              SwObject* receiver, Callback cb, bool initial)
            : registry(domain, object), name(leaf), context(receiver), life(receiver->lifetimeToken()),
              callback(std::move(cb)), fireInitial(initial),
              eventsRegistry(domain, detail::registryEventsObjectName_()) {}
        Registry registry;
        SwString name;
        SwObject* context;
        std::weak_ptr<void> life;
        Callback callback;
        bool fireInitial;
        bool stopped{false}, attaching{false};
        std::recursive_mutex mutex;
        std::unique_ptr<SwIpcSignal<Args...>> signal;
        typename SwIpcSignal<Args...>::Subscription subscription;
        typename SwIpcSignal<Args...>::DeclarationWatch declaration;
        Registry eventsRegistry;
        std::unique_ptr<SwIpcSignal<uint64_t>> events;
        typename SwIpcSignal<uint64_t>::Subscription changed;

        void attach() {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (stopped || attaching || signal || life.expired()) return;
            uint32_t capacity = 0, bytes = 0;
            DeliveryMode mode = DeliveryMode::Replay;
            const SwString mapping = detail::make_shm_name(registry.domain(), registry.object(), name) + "_r3";
            if (!RingQueueDynamic<Args...>::inspectExisting(mapping, detail::type_id<Args...>(), capacity, bytes, mode)) return;
            attaching = true;
            try {
                signal.reset(new SwIpcSignal<Args...>(registry, name, capacity, bytes));
                subscription = signal->connect(context, callback, fireInitial);
                attaching = false;
                changed.stop();
                declaration = {};
            } catch (...) { signal.reset(); attaching = false; throw; }
        }
        void start() {
            attach();
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if (signal || stopped || life.expired()) return;
            // Nothing existed at connect time. Every retained publication in
            // the newly declared channel belongs to this subscription, even
            // when fireInitial=false was requested before the channel existed.
            fireInitial = true;
            const std::weak_ptr<State> weak = this->shared_from_this();
            declaration = SwIpcSignal<Args...>::watchDeclaration(registry, name, [weak]() {
                if (auto state = weak.lock()) state->attach();
            });
            events.reset(new SwIpcSignal<uint64_t>(eventsRegistry, detail::registryEventsSignalName_(),
                                                 16u, 0u, DeliveryMode::LatestOnly));
            changed = events->connect(context, [weak](uint64_t) {
                if (auto state = weak.lock()) state->attach();
            }, false);
            // Covers registration between the first inspection and subscribing
            // to registry events, without a timer on every named connection.
            attach();
        }
        void stop() {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            stopped = true;
            declaration = {};
            changed.stop(); subscription.stop();
            signal.reset(); events.reset();
        }
    };
public:
    NamedSignalConnection(const SwString& domain, const SwString& object, const SwString& leaf,
                          SwObject* context, Callback callback, bool fireInitial)
        : state_(std::make_shared<State>(domain, object, leaf, context, std::move(callback), fireInitial)) {
        state_->start();
    }
    NamedSignalConnection(const NamedSignalConnection&) = delete;
    NamedSignalConnection& operator=(const NamedSignalConnection&) = delete;
    NamedSignalConnection(NamedSignalConnection&& other) noexcept : state_(std::move(other.state_)) {}
    NamedSignalConnection& operator=(NamedSignalConnection&& other) noexcept {
        if (this != &other) { stop(); state_ = std::move(other.state_); }
        return *this;
    }
    ~NamedSignalConnection() { stop(); }
    void stop() { if (state_) { state_->stop(); state_.reset(); } }
private:
    std::shared_ptr<State> state_;
};

}} // namespace sw::ipc
