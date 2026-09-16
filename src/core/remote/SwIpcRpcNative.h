#pragma once

#include "SwIpcRpcRouter.h"
#include <chrono>
#include <tuple>
#include <utility>

namespace sw { namespace ipc {

struct RpcContext {
    uint32_t clientPid{0};
    SwString clientInfo;
};

namespace detail {
template<class Ret> struct NativeRpcInvoke {
    template<class Fn, class Tuple, size_t... I>
    static RpcResult<Ret> call(const Fn& fn, const RpcContext& context, Tuple&& args,
                               detail::index_sequence<I...>) {
        RpcResult<Ret> result;
        result.value = fn(context, std::get<I>(std::forward<Tuple>(args))...);
        result.ok = true;
        return result;
    }
};
template<> struct NativeRpcInvoke<void> {
    template<class Fn, class Tuple, size_t... I>
    static RpcResult<void> call(const Fn& fn, const RpcContext& context, Tuple&& args,
                                detail::index_sequence<I...>) {
        fn(context, std::get<I>(std::forward<Tuple>(args))...);
        RpcResult<void> result; result.ok = true;
        return result;
    }
};
}

// A typed endpoint shares the signal broker and its module/thread lifetime
// rules. It never instantiates an IPC codec: a client may supply a separate
// remote adapter for types or transfers which do not fit a fixed RPC packet.
template<class Ret, class... Args>
class NativeRpcEndpoint {
public:
    using Result = RpcResult<Ret>;
    using Handler = std::function<Ret(RpcContext, Args...)>;
    using Completion = std::function<void(const Result&)>;
    using Tuple = std::tuple<typename std::decay<Args>::type...>;
    struct Entry {
        std::atomic_bool active{true};
        SwObject* context{nullptr};
        std::weak_ptr<void> lifetime;
        std::thread::id originalThread;
        Handler handler;
    };
    using Selection = std::shared_ptr<Entry>;
    struct Channel {
        std::mutex mutex;
        std::weak_ptr<Entry> endpoint;
    };
    class Registration {
    public:
        Registration() = default;
        Registration(Registration&& other) noexcept
            : channel_(std::move(other.channel_)), entry_(std::move(other.entry_)) {}
        Registration& operator=(Registration&& other) noexcept {
            if (this != &other) { stop(); channel_ = std::move(other.channel_); entry_ = std::move(other.entry_); }
            return *this;
        }
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        ~Registration() { stop(); }
        void stop() {
            if (entry_) entry_->active.store(false, std::memory_order_release);
            entry_.reset(); channel_.reset();
        }
    private:
        friend class NativeRpcEndpoint;
        Registration(std::shared_ptr<Channel> channel, Selection entry)
            : channel_(std::move(channel)), entry_(std::move(entry)) {}
        std::shared_ptr<Channel> channel_;
        Selection entry_;
    };

    static std::shared_ptr<Channel> channel(const SwString& domain, const SwString& object,
                                            const SwString& method) {
        return detail::nativeSignalChannel<Channel>(
            "rpc:" + detail::make_shm_name(domain, object, method).toStdString());
    }
    static Registration expose(const SwString& domain, const SwString& object, const SwString& method,
                               SwObject* context, Handler handler) {
        if (!handler) throw std::invalid_argument("rpc: empty native handler");
        detail::registerNativeSignalThread();
        auto shared = channel(domain, object, method);
        auto entry = std::make_shared<Entry>();
        entry->context = context;
        if (context) entry->lifetime = context->lifetimeToken();
        entry->originalThread = std::this_thread::get_id();
        entry->handler = std::move(handler);
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (valid(shared->endpoint.lock())) throw std::runtime_error("rpc: native method already exposed");
            shared->endpoint = entry;
        }
        return Registration(std::move(shared), std::move(entry));
    }
    static Selection select(const std::shared_ptr<Channel>& shared) {
        std::lock_guard<std::mutex> lock(shared->mutex);
        auto entry = shared->endpoint.lock();
        return valid(entry) ? entry : Selection();
    }
    static bool valid(const Selection& entry) {
        return entry && entry->active.load(std::memory_order_acquire) &&
               (!entry->context || !entry->lifetime.expired());
    }
    static std::thread::id targetThread(const Selection& entry) {
        if (!entry->context) return entry->originalThread;
        return entry->context->affinityThreadId();
    }
    static bool isDirect(const Selection& entry) {
        return valid(entry) && targetThread(entry) == std::this_thread::get_id();
    }
    template<class Arguments>
    static Result invoke(const Selection& entry, const RpcContext& context, Arguments&& args) {
        Result result;
        if (!valid(entry)) { result.error = "rpc: native endpoint stopped"; return result; }
        try { return detail::NativeRpcInvoke<Ret>::call(entry->handler, context, std::forward<Arguments>(args),
                                                       typename detail::make_index_sequence<sizeof...(Args)>::type{}); }
        catch (const std::exception& error) { result.error = SwString("rpc: handler exception: ") + SwString(error.what()).left(512); }
        catch (...) { result.error = "rpc: unknown handler exception"; }
        return result;
    }
    // Selection pins a particular registration generation. Withdrawal/restart
    // cannot cause an already submitted mutation to execute on a new service.
    static void dispatch(const Selection& entry, RpcContext context, std::shared_ptr<Tuple> args,
                         Completion complete, std::function<bool()> current) {
        if (!current()) return;
        if (!valid(entry)) {
            Result result; result.error = "rpc: native endpoint stopped";
            complete(result); return;
        }
        // A selected invocation executes once. Its queued arguments already own
        // the caller snapshot and can be consumed by the by-value handler.
        if (isDirect(entry)) { complete(invoke(entry, context, std::move(*args))); return; }
        auto task = [entry, context, args, complete, current] {
            dispatch(entry, context, args, complete, current);
        };
        const bool posted = entry->context ? entry->context->postToAffinity(std::move(task))
            : detail::postNativeSignalThread(entry->originalThread, std::move(task));
        if (!posted) {
            Result result; result.error = "rpc: native endpoint thread unavailable";
            complete(result);
        }
    }
};

// One subscription token controls both the native registration and the wire
// listener. Destruction and explicit disconnect invalidate queued native work.
template<class Registration, class Subscription>
struct RpcExposure {
    Registration native;
    Subscription wire;
    RpcExposure(Registration registration, Subscription subscription)
        : native(std::move(registration)), wire(std::move(subscription)) {}
    RpcExposure(RpcExposure&&) = default;
    RpcExposure& operator=(RpcExposure&&) = default;
    RpcExposure(const RpcExposure&) = delete;
    void stop() { native.stop(); wire.stop(); }
};

}}
