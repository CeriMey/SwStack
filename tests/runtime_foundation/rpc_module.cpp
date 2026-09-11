#include "SwRemoteObject.h"
#include "SwProxyObject.h"
#include "SwThread.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace sw::ipc;
using Callback = void (*)(void*, bool, int, const char*);
#ifdef SW_RPC_MODULE_PLUGIN
#if defined(_WIN32)
#define RPC_EXPORT extern "C" __declspec(dllexport)
#else
#define RPC_EXPORT extern "C" __attribute__((visibility("default")))
#endif
SW_PROXY_OBJECT_CLASS_BEGIN(ModuleProxy)
    SW_PROXY_OBJECT_RPC(int, echo, int)
SW_PROXY_OBJECT_CLASS_END()
RPC_EXPORT bool rpc_module_has_runtime() { return SwCoreApplication::instance(false) != nullptr; }
RPC_EXPORT void* rpc_module_create(const char* domain, const char* object) { return new ModuleProxy(domain, object, "module"); }
RPC_EXPORT bool rpc_module_direct(void* pointer, int value) {
    RpcResult<int> result;
    return static_cast<ModuleProxy*>(pointer)->echoRpc().tryCallDirect(result, value) && result.ok && result.value == value;
}
RPC_EXPORT uint64_t rpc_module_call(void* pointer, int value, Callback callback, void* data, int timeout) {
    return static_cast<ModuleProxy*>(pointer)->echoRpc().callAsyncResult(value,
        [callback, data](const RpcResult<int>& result) {
            callback(data, result.ok, result.value, result.error.c_str());
        }, timeout);
}
RPC_EXPORT bool rpc_module_cancel(void* pointer, uint64_t id) { return static_cast<ModuleProxy*>(pointer)->echoRpc().cancel(id); }
RPC_EXPORT void rpc_module_destroy(void* pointer) { delete static_cast<ModuleProxy*>(pointer); }
using Endpoint = NativeRpcEndpoint<int, int>;
RPC_EXPORT void* rpc_module_expose(const char* domain, SwObject* owner) {
    return new Endpoint::Registration(Endpoint::expose(domain, "rpc/hidden", "double", owner,
        [](RpcContext, int value) { return value * 2; }));
}
RPC_EXPORT void rpc_module_withdraw(void* pointer) { delete static_cast<Endpoint::Registration*>(pointer); }
#else
#ifndef _WIN32
#include <dlfcn.h>
#endif
static void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class Fn> static void waitFor(Fn fn, const char* text) {
    require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return fn(); }, 2000), text);
}
class Service : public SwRemoteObject {
public:
    Service(const SwString& domain) : SwRemoteObject(domain, "rpc", "service") {
        token = ipcExposeRpcT("echo", [this](RpcContext context, int value) {
            require(context.clientInfo == "module", "module client metadata missing"); ++calls; return value;
        }, false);
    }
    int calls{0}; size_t token;
};
struct Answer {
    int count{0}, value{0}; bool ok{false}, correctThread{true}; SwString error;
    std::thread::id caller{std::this_thread::get_id()};
    static void receive(void* data, bool ok, int value, const char* error) {
        auto& answer = *static_cast<Answer*>(data);
        ++answer.count; answer.ok = ok; answer.value = value; answer.error = error;
        answer.correctThread = answer.correctThread && answer.caller == std::this_thread::get_id();
    }
};
int main(int argc, char** argv) {
    try {
        require(argc == 2, "module path missing");
        SwCoreApplication app(argc, argv);
        setRpcQueueCapacity(10);
        const SwString domain = "rpcmodule_" + SwString::number(detail::currentPid());
        const auto work = std::filesystem::temp_directory_path() / domain.toStdString();
        std::filesystem::create_directories(work);
        SwRemoteObject::ConfigRootScope config(SwString((work / "config").string()));
        Service service(domain);
#ifdef _WIN32
        const auto module = ::LoadLibraryA(argv[1]);
        require(module != nullptr, "LoadLibrary failed");
        auto symbol = [module](const char* name) { return reinterpret_cast<void*>(::GetProcAddress(module, name)); };
#else
        void* module = ::dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        require(module != nullptr, ::dlerror());
        auto symbol = [module](const char* name) { return ::dlsym(module, name); };
#endif
        auto hasRuntime = reinterpret_cast<bool (*)()>(symbol("rpc_module_has_runtime"));
        auto create = reinterpret_cast<void* (*)(const char*, const char*)>(symbol("rpc_module_create"));
        auto direct = reinterpret_cast<bool (*)(void*, int)>(symbol("rpc_module_direct"));
        auto call = reinterpret_cast<uint64_t (*)(void*, int, Callback, void*, int)>(symbol("rpc_module_call"));
        auto cancel = reinterpret_cast<bool (*)(void*, uint64_t)>(symbol("rpc_module_cancel"));
        auto destroy = reinterpret_cast<void (*)(void*)>(symbol("rpc_module_destroy"));
        auto expose = reinterpret_cast<void* (*)(const char*, SwObject*)>(symbol("rpc_module_expose"));
        auto withdraw = reinterpret_cast<void (*)(void*)>(symbol("rpc_module_withdraw"));
        require(hasRuntime && create && direct && call && cancel && destroy && expose && withdraw, "module exports missing");
        require(!hasRuntime(), "test module unexpectedly shares caller TLS");
        void* client = create(domain.c_str(), "rpc/service");
        require(!hasRuntime() && direct(client, 7) && service.calls == 1, "hidden proxy direct routing failed");
        Answer success;
        call(client, 11, &Answer::receive, &success, 500);
        require(success.count == 0 && service.calls == 1, "hidden async call ran inline");
        waitFor([&] { return success.count; }, "hidden async callback missing");
        require(success.ok && success.value == 11 && success.correctThread && service.calls == 2,
                "hidden async proxy did not use host runtime");
        Answer cancelled;
        const auto id = call(client, 13, &Answer::receive, &cancelled, 500);
        require(cancel(client, id) && cancelled.count == 1 && cancelled.error.contains("cancelled"), "hidden cancellation failed");
        Answer timeout;
        void* missing = create(domain.c_str(), "rpc/missing");
        call(missing, 19, &Answer::receive, &timeout, 20);
        waitFor([&] { return timeout.count; }, "hidden timer did not use caller runtime");
        require(!timeout.ok && timeout.error.contains("timeout") && timeout.correctThread,
                "hidden timeout callback wrong");
        destroy(missing);
        require(service.calls == 2, "hidden cancelled call executed");

        SwObject owner;
        void* registration = expose(domain.c_str(), &owner);
        RpcMethodClient<int, int> host(domain, "rpc/hidden", "double", "", {});
        require(host.callResult(21, 500).value == 42, "host could not invoke hidden native endpoint");
        SwThread worker; require(worker.start(), "worker did not start"); owner.moveToThread(&worker);
        bool returned = false;
        const auto caller = std::this_thread::get_id();
        host.callAsyncResult(31, [&](const RpcResult<int>& result) {
            require(result.ok && result.value == 62 && std::this_thread::get_id() == caller,
                    "hidden endpoint callback lost caller affinity"); returned = true;
        }, 500);
        waitFor([&] { return returned; }, "hidden endpoint queued response missing");
        withdraw(registration); worker.quit(); worker.wait();
        require(!host.canCallDirect(), "hidden endpoint remained registered after withdrawal");

        Answer destroyed;
        call(client, 23, &Answer::receive, &destroyed, 500);
        destroy(client);
#ifdef _WIN32
        ::FreeLibrary(module);
#else
        ::dlclose(module);
#endif
        SwEventLoop::waitUntil([] { return false; }, 40);
        require(destroyed.count == 0 && service.calls == 2, "destroyed hidden client still executed or completed");
        require(!hasRuntime(), "hidden module created a second caller runtime");
        std::cout << "PASS hidden-module RPC direct/queued, proxy callbacks, caller timers and teardown\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
#endif
