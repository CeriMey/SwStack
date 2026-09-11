#include "SwRemoteObject.h"
#include "SwProxyObject.h"
#include "SwThread.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace sw::ipc;
static void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class Fn> static void waitFor(Fn fn, const char* text, int timeout = 3000) {
    require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return fn(); }, timeout), text);
}
struct Owned {
    std::shared_ptr<std::vector<int>> values;
}; // Intentionally no wire Codec specialization.
using TypedClient = RpcMethodClient<Owned, Owned>;
using TypedEndpoint = NativeRpcEndpoint<Owned, Owned>;

class Service : public SwRemoteObject {
    SW_OBJECT(Service, SwRemoteObject)
public:
    Service(const SwString& domain) : SwRemoteObject(domain, "rpc", "service") {
        echoToken = ipcExposeRpcT("echo", this, &Service::echo, false);
        ipcExposeRpcT("large", [] { return SwString(std::string(9000, 'x')); }, false);
        ipcExposeRpcT("context", [](RpcContext context, int value) {
            return context.clientPid != 0 && context.clientInfo == "actor" ? value : -1;
        }, false);
        ipcExposeRpcT("noop", [this] { ++voidCalls; }, false);
    }
    SwString echo(SwString text) { ++calls; return text; }
    int calls{0}, voidCalls{0};
    size_t echoToken;
};
SW_PROXY_OBJECT_CLASS_BEGIN(ServiceProxy)
    SW_PROXY_OBJECT_RPC(SwString, echo, SwString)
    SW_PROXY_OBJECT_RPC(SwString, large)
    SW_PROXY_OBJECT_RPC(int, context, int)
    SW_PROXY_OBJECT_VOID(noop)
SW_PROXY_OBJECT_CLASS_END()

static void direct(const SwString& domain, Service& service) {
    ServiceProxy proxy(domain, "rpc/service", "actor");
    require(proxy.echoRpc().canCallDirect(), "generated proxy did not discover native endpoint");
    RpcResult<SwString> direct;
    require(proxy.echoRpc().tryCallDirect(direct, "direct") && direct.ok && direct.value == "direct" && service.calls == 1,
            "tryCallDirect failed to run inline");
    require(proxy.largeRpc().callResult(500).value.size() == 9000, "native result used fixed wire payload limit");
    require(proxy.contextRpc().callResult(73, 500).value == 73, "native RpcContext was lost");
    require(proxy.noopRpc().callResult(500).ok && service.voidCalls == 1, "native void result failed");
    bool completed = false;
    proxy.echoRpc().callAsyncResult("async", [&](const RpcResult<SwString>& result) {
        require(result.ok && result.value == "async", "native async result failed"); completed = true;
    }, 500);
    require(!completed && service.calls == 1, "async handler or callback ran inline");
    waitFor([&] { return completed; }, "native async completion missing");
    bool cancelled = false;
    const auto id = proxy.echoRpc().callAsyncResult("cancelled", [&](const RpcResult<SwString>& result) {
        require(!result.ok && result.error.contains("cancelled"), "cancel did not report cancellation"); cancelled = true;
    }, 500);
    require(proxy.echoRpc().cancel(id) && cancelled, "async call was not cancellable before execution");
    SwEventLoop::waitUntil([] { return false; }, 20);
    require(service.calls == 2, "cancelled same-thread handler executed");
    const auto caller = std::this_thread::get_id();
    bool foreignComplete = false;
    const auto foreignId = proxy.echoRpc().callAsyncResult("foreign cancellation", [&](const RpcResult<SwString>& result) {
        require(std::this_thread::get_id() == caller && !result.ok && result.error.contains("cancelled"),
                "cancellation callback ran outside caller affinity");
        foreignComplete = true;
    }, 500);
    std::atomic_bool accepted{false};
    std::thread canceller([&] { accepted = proxy.echoRpc().cancel(foreignId); });
    canceller.join();
    require(accepted && !foreignComplete, "foreign cancellation ran callback inline");
    waitFor([&] { return foreignComplete; }, "foreign cancellation callback missing");
    require(service.calls == 2, "foreign-cancelled call executed before cancellation delivery");

    bool destroyedCompletion = false;
    {
        ServiceProxy transient(domain, "rpc/service");
        transient.echoRpc().callAsyncResult("destroyed", [&](const RpcResult<SwString>&) { destroyedCompletion = true; });
    }
    SwEventLoop::waitUntil([] { return false; }, 20);
    require(!destroyedCompletion && service.calls == 2, "destroyed client executed deferred native handler");
}

static void typed(const SwString& domain) {
    SwObject context;
    Owned input{std::make_shared<std::vector<int>>(std::initializer_list<int>{1, 2, 3})};
    int calls = 0, fallback = 0;
    auto registration = TypedEndpoint::expose(domain, "typed", "query", &context,
        [&](RpcContext rpc, Owned value) { require(rpc.clientInfo == "typed", "typed context lost"); ++calls; return value; });
    TypedClient client(domain, "typed", "query", "typed",
        [&](const Owned&, TypedClient::Completion done, int) {
            ++fallback; RpcResult<Owned> result; result.error = "unexpected remote fallback"; done(result);
            return TypedClient::RemoteCancel{};
        });
    auto result = client.callResult(input, 500);
    require(result.ok && result.value.values == input.values && calls == 1 && fallback == 0,
            "typed native call did not preserve shared ownership");
    bool oldCompleted = false;
    client.callAsyncResult(input, [&](const RpcResult<Owned>& result) {
        require(!result.ok && result.error.contains("stopped"), "withdrawn generation rerouted to replacement"); oldCompleted = true;
    }, 500);
    registration.stop();
    auto replacement = TypedEndpoint::expose(domain, "typed", "query", &context,
        [&](RpcContext, Owned value) { ++calls; return value; });
    waitFor([&] { return oldCompleted; }, "withdrawn generation never completed");
    require(calls == 1 && fallback == 0, "withdrawn call executed on replacement or remote adapter");
    require(client.callResult(input, 500).ok && calls == 2, "subsequent call did not discover replacement");
    std::vector<TypedClient::PreparedCall> admitted;
    for (int i = 0; i < 3; ++i) admitted.push_back(client.prepare(input));
    replacement.stop();
    auto restarted = TypedEndpoint::expose(domain, "typed", "query", &context,
        [&](RpcContext, Owned value) { ++calls; return value; });
    int rejected = 0;
    for (auto& prepared : admitted) client.callAsyncResult(std::move(prepared), [&](const RpcResult<Owned>& result) {
        require(!result.ok && result.error.contains("stopped"), "prepared mutation migrated to replacement endpoint");
        ++rejected;
    }, 500);
    waitFor([&] { return rejected == 3; }, "prepared calls did not complete after restart");
    require(calls == 2 && fallback == 0, "admitted calls executed on replacement or remote adapter");

    auto lostContext = std::make_unique<SwObject>();
    auto lifetime = TypedEndpoint::expose(domain, "typed", "lifetime", lostContext.get(),
        [&](RpcContext, Owned value) { ++calls; return value; });
    TypedClient lifeClient(domain, "typed", "lifetime", "", TypedClient::RemoteInvoker{});
    bool lostComplete = false;
    lifeClient.callAsyncResult(input, [&](const RpcResult<Owned>& result) {
        require(!result.ok && result.error.contains("stopped"), "destroyed endpoint was invoked"); lostComplete = true;
    }, 500);
    lostContext.reset();
    waitFor([&] { return lostComplete; }, "destroyed endpoint completion missing");
    require(calls == 2, "destroyed endpoint accessed handler");
}

static void queued(const SwString& domain) {
    SwThread worker;
    SwObject target;
    std::atomic<int> executions{0};
    const auto caller = std::this_thread::get_id();
    auto registration = TypedEndpoint::expose(domain, "worker", "query", &target,
        [&](RpcContext, Owned input) {
            require(std::this_thread::get_id() == worker.threadId(), "handler ran outside service affinity");
            ++executions; return input;
        });
    require(worker.start(), "worker failed to start");
    target.moveToThread(&worker);
    TypedClient client(domain, "worker", "query", "", TypedClient::RemoteInvoker{});
    require(!client.canCallDirect(), "cross-thread endpoint advertised direct call");
    Owned input{std::make_shared<std::vector<int>>(1, 42)};
    RpcResult<Owned> untouched;
    require(!client.tryCallDirect(untouched, input) && executions == 0, "direct-only read queued work");
    auto result = client.callResult(input, 1000);
    require(result.ok && result.value.values == input.values && executions == 1, "queued synchronous native result failed");
    bool completed = false;
    client.callAsyncResult(input, [&](const RpcResult<Owned>& reply) {
        require(std::this_thread::get_id() == caller && reply.ok && reply.value.values == input.values,
                "queued response lost caller affinity or payload ownership"); completed = true;
    });
    waitFor([&] { return completed; }, "queued async result missing");

    std::mutex mutex; std::condition_variable cv;
    bool release = false;
    std::atomic_bool blocked{false}, drained{false};
    require(worker.postTask([&] {
        blocked = true; std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [&] { return release; });
    }), "worker blocker rejected");
    waitFor([&] { return blocked.load(); }, "worker did not block");
    int cancelled = 0;
    auto id = client.callAsyncResult(input, [&](const RpcResult<Owned>& reply) {
        require(!reply.ok && reply.error.contains("cancelled"), "queued cancellation result wrong"); ++cancelled;
    });
    bool submitted = false;
    detail::postNativeSignalThread(caller, [&] { submitted = true; });
    waitFor([&] { return submitted; }, "queued call was not submitted");
    require(client.cancel(id) && cancelled == 1, "in-flight queued call did not cancel");
    bool expired = false;
    client.callAsyncResult(input, [&](const RpcResult<Owned>& reply) {
        require(!reply.ok && reply.error.contains("timeout"), "queued expiration result wrong"); expired = true;
    }, 20);
    waitFor([&] { return expired; }, "queued call did not expire");
    { std::lock_guard<std::mutex> lock(mutex); release = true; }
    cv.notify_all();
    worker.postTask([&] { drained = true; });
    waitFor([&] { return drained.load(); }, "worker did not drain cancelled tasks");
    require(executions == 2, "cancelled or expired queued mutation executed");
    registration.stop();
    worker.quit(); worker.wait();
}

static void adapter(const SwString& domain) {
    Owned input{std::make_shared<std::vector<int>>(3, 7)};
    int invoked = 0, cancelled = 0;
    TypedClient::Completion finish;
    TypedClient client(domain, "adapter", "query", "",
        [&](const Owned& value, TypedClient::Completion done, int timeout) {
            require(value.values == input.values && timeout > 0, "remote adapter arguments/deadline wrong");
            ++invoked; finish = std::move(done);
            return [&] { ++cancelled; finish = {}; };
        });
    bool done = false;
    const auto id = client.callAsyncResult(input, [&](const RpcResult<Owned>& reply) {
        require(!reply.ok && reply.error.contains("cancelled"), "adapter cancellation wrong"); done = true;
    }, 500);
    require(invoked == 0, "remote adapter executed inline for async call");
    waitFor([&] { return invoked == 1; }, "remote adapter was not invoked");
    require(client.cancel(id) && done && cancelled == 1, "remote adapter cancel handle not invoked");
    bool success = false;
    client.callAsyncResult(input, [&](const RpcResult<Owned>& reply) {
        require(reply.ok && reply.value.values == input.values, "remote adapter result failed"); success = true;
    }, 500);
    waitFor([&] { return invoked == 2; }, "second remote invocation missing");
    RpcResult<Owned> result; result.ok = true; result.value = input;
    auto callback = std::move(finish); callback(result);
    require(success && cancelled == 1, "successful adapter was cancelled");
}

static void completionDeadline(const SwString& domain) {
    SwObject context;
    int executions = 0;
    auto slow = NativeRpcEndpoint<int, int>::expose(domain, "deadline", "slow", &context,
        [&](RpcContext, int value) {
            ++executions;
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
            return value;
        });
    RpcMethodClient<int, int> client(domain, "deadline", "slow", "", {});
    bool completed = false;
    client.callAsyncResult(42, [&](const RpcResult<int>& result) {
        require(!result.ok && result.error.contains("timeout"), "blocking same-thread handler completed after deadline");
        completed = true;
    }, 5);
    require(!completed && executions == 0, "slow async handler started inline");
    waitFor([&] { return completed; }, "slow handler completion missing");
    require(executions == 1, "slow mutation was executed more than once");
    auto synchronous = client.callResult(43, 5);
    require(!synchronous.ok && synchronous.error.contains("timeout") && executions == 2,
            "blocking synchronous handler ignored deadline");

    RpcMethodClient<int, int>::Completion reply;
    RpcMethodClient<int, int> remote(domain, "deadline", "remote", "",
        [&](const int&, RpcMethodClient<int, int>::Completion done, int) {
            reply = std::move(done); return RpcMethodClient<int, int>::RemoteCancel{};
        });
    bool late = false;
    remote.callAsyncResult(44, [&](const RpcResult<int>& result) {
        require(!result.ok && result.error.contains("timeout"), "queued response admitted after caller deadline");
        late = true;
    }, 30);
    waitFor([&] { return static_cast<bool>(reply); }, "remote request was not admitted");
    // The response is posted before its deadline, but the caller cannot read it
    // until afterwards. Checking only at the producing thread would accept it.
    std::thread producer([&] { RpcResult<int> result; result.ok = true; result.value = 44; reply(result); });
    producer.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    waitFor([&] { return late; }, "delayed response completion missing");
}

#ifndef _WIN32
static int child(const char* domain) {
    ServiceProxy proxy(domain, "rpc/service", "actor");
    require(!proxy.echoRpc().canCallDirect(), "child used parent's native registry");
    auto echo = proxy.echoRpc().callResult("external", 2000);
    require(echo.ok && echo.value == "external", "external fixed-RPC exchange failed");
    require(proxy.contextRpc().callResult(89, 2000).value == 89, "remote context lost");
    require(proxy.noopRpc().callResult(2000).ok, "remote void response failed");
    auto large = proxy.largeRpc().callResult(2000);
    require(!large.ok && large.error.contains("payload too large"), "remote oversized result did not report wire error");
    return 0;
}
static void external(const SwString& domain, const char* executable, Service& service) {
    const auto pid = ::fork();
    require(pid >= 0, "fork failed");
    if (!pid) {
        ::execl(executable, executable, "--child", domain.toStdString().c_str(), nullptr);
        ::_exit(127);
    }
    int status = 0;
    waitFor([&] { return ::waitpid(pid, &status, WNOHANG) == pid; }, "external RPC child did not complete", 8000);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && service.calls == 3,
            "external RPC failed or native requests also entered IPC");
    require(service.ipcDisconnect(service.echoToken), "ordinary exposure disconnect failed");
    ServiceProxy proxy(domain, "rpc/service");
    require(!proxy.echoRpc().canCallDirect(), "ipcDisconnect retained native endpoint");
}
#endif

int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        setRpcQueueCapacity(10);
#ifndef _WIN32
        if (argc == 3 && std::string(argv[1]) == "--child") return child(argv[2]);
#endif
        const SwString domain = "rpcroute_" + SwString::number(detail::currentPid());
        const auto work = std::filesystem::temp_directory_path() / domain.toStdString();
        std::filesystem::create_directories(work);
        SwRemoteObject::ConfigRootScope config(SwString((work / "config").string()));
        Service service(domain);
        direct(domain, service); typed(domain); queued(domain); adapter(domain); completionDeadline(domain);
#ifndef _WIN32
        external(domain, std::filesystem::absolute(argv[0]).c_str(), service);
#endif
        std::cout << "PASS common RPC direct/queued/IPC, typed adapter, lifetime, cancellation and proxy routing\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
