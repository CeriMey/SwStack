#include "SwRemoteObject.h"
#include "SwProxyObject.h"
#include "SwIpcJsonCodec.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class Service : public SwRemoteObject {
    SW_OBJECT(Service, SwRemoteObject)
public:
    Service(const SwString& domain, const SwString& name = "service") : SwRemoteObject(domain, "test", name) {
        ipcRegisterConfig(int, period, "period", 100);
        ipcRegisterConfig(SwStringList, labels, "labels", SwStringList{});
        ipcExposeRpc("echo", this, &Service::echo); // three-argument member macro
        ipcExposeRpc(&Service::failure);
        ipcExposeRpc(&Service::large);
        ipcExposeRpc(&Service::noop);
    }
    int echo(int value) { return value; }
    int failure() { throw std::runtime_error("intentional failure"); }
    SwString large() { return SwString(std::string(8192, 'x')); }
    void noop() {}
    int period{100};
    SwStringList labels;
    SW_IPC_PROPERTY_SIZED(SwString, label, SwString("initial"), 64)
};
SW_PROXY_OBJECT_CLASS_BEGIN(TestProxy)
    SW_PROXY_OBJECT_RPC(int, echo, int)
    SW_PROXY_OBJECT_VOID(noop)
SW_PROXY_OBJECT_CLASS_END()

static void codecChecks() {
    using namespace sw::ipc;
    unsigned char bytes[64]{};
    {
        detail::ScratchBuffer outer(64); outer.data()[0] = 73;
        { detail::ScratchBuffer inner(128); inner.data()[0] = 12; }
        require(outer.data()[0] == 73, "reentrant scratch buffer overwrite");
    }
    SwString error;
    detail::Encoder enc(bytes, sizeof bytes);
    require(jsonwire::encode(enc, "float", SwJsonValue(1.25), error), "float encode");
    require(jsonwire::encode(enc, "uint32_t", SwJsonValue("4294967295"), error), "u32 encode");
    require(jsonwire::encode(enc, "int64_t", SwJsonValue("-9223372036854775808"), error), "i64 encode");
    require(jsonwire::encode(enc, "uint64_t", SwJsonValue("18446744073709551615"), error), "u64 encode");
    detail::Decoder dec(bytes, sizeof bytes); SwJsonValue value;
    require(jsonwire::decode(dec, "float", value, error) && value.toDouble() == 1.25, "float width");
    require(jsonwire::decode(dec, "uint32_t", value, error) && value.toLongLong() == 4294967295LL, "u32 width");
    require(jsonwire::decode(dec, "int64_t", value, error) && value.toString() == "-9223372036854775808", "i64 exact");
    require(jsonwire::decode(dec, "uint64_t", value, error) && value.toString() == "18446744073709551615", "u64 exact");
    for (auto bad : {"-1", "4294967296", "12x", "", "1.5"}) {
        detail::Encoder invalid(bytes, sizeof bytes);
        require(!jsonwire::encode(invalid, "uint32_t", SwJsonValue(bad), error), "invalid integer accepted");
    }
    require(detail::type_name<uint64_t, float>() == "sw::ipc::tuple<uint64_t,float>", "portable registry schema");
}
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using namespace sw::ipc;
    const auto original = fs::current_path();
    const SwString domain = "swfoundation_" + SwString::number(detail::currentPid()) + "_" +
        SwString::number(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto work = fs::temp_directory_path() / domain.toStdString();
    fs::create_directories(work); fs::current_path(work);
    int exitCode = 0;
    try {
        SwCoreApplication app(argc, argv);
        setRpcQueueCapacity(10);
        codecChecks();
        Service server(domain);
        const auto root = work / "config";
        fs::create_directories(root / "user");
        const auto userFile = root / "user" / (domain.toStdString() + "_test_service.json");
        { std::ofstream out(userFile); out << "{\"period\":130}"; }
        server.setConfigRootDirectory(SwString(root.string()));
        require(server.period == 130, "custom root applied");
        Service observer(domain, "observer");
        int observed = 0;
        observer.ipcBindConfigT<int>(observed, "test/service#period");
        require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return observed == 130; }, 500), "initial binding");
        require(server.ipcUpdateConfig<int>("period", 140), "local update published");
        require(SwEventLoop::waitUntil([&] { detail::LoopPoller::instance().dispatch(); return observed == 140; }, 500), "local binding notification");
        server.setConfigValue("period", SwJsonValue(150), false);
        SwJsonArray labels; labels.append(SwJsonValue("camera"));
        server.setConfigValue("labels", SwJsonValue(labels), true);
        require(server.labels.size() == 1 && server.labels[0] == "camera", "list config publication");
        server.setConfigValue("saved", SwJsonValue(7), true);
        require(server.saveUserConfig(), "save persistent config");
        SwJsonDocument doc; SwString parseError;
        std::ifstream input(userFile); std::string text((std::istreambuf_iterator<char>(input)), {});
        require(doc.loadFromJson(text, parseError) && doc.object()["period"].toInt() == 140, "runtime override leaked to disk");
        require(server.period == 150, "runtime precedence");
        server.setConfigValue("period", SwJsonValue(160), true);
        require(server.period == 160, "explicit persistent update replaces runtime key");
        require(server.set_label("ok"), "property valid publication");
        require(!server.set_label(SwString(std::string(128, 'x'))) && server.label() == "ok", "property cache changed on failed publication");
        RpcMethodClient<int, int> first(domain, "test/service", "echo"), second(domain, "test/service", "echo");
        require(first.callResult(42, 500).value == 42, "main-thread synchronous RPC");
        RpcMethodClient<int> failing(domain, "test/service", "failure");
        require(!failing.callResult(500).ok && failing.lastError().contains("intentional failure"), "exception response");
        RpcMethodClient<SwString> large(domain, "test/service", "large");
        require(!large.callResult(500).ok && large.lastError().contains("payload too large"), "oversized response error");
        RpcMethodClient<int> absent(domain, "test/missing", "missing");
        int callbacks = 0; bool destroyedCallback = false;
        auto id = absent.callAsyncResult([&](const RpcResult<int>& r) { require(!r.ok && r.error.contains("cancelled"), "cancel result"); ++callbacks; }, 500);
        require(absent.cancel(id) && !absent.cancel(id), "cancel exactly once");
        absent.callAsyncResult([&](const RpcResult<int>& r) { require(!r.ok && r.error.contains("timeout"), "timeout result"); ++callbacks; }, 40);
        {
            RpcMethodClient<int> destroyed(domain, "test/gone", "missing");
            destroyed.callAsyncResult([&](const RpcResult<int>&) { destroyedCallback = true; }, 30);
        }
        int replies = 0;
        first.callAsyncResult(11, [&](const RpcResult<int>& r) { require(r.ok && r.value == 11, "first shared response"); ++replies; }, 500);
        second.callAsyncResult(22, [&](const RpcResult<int>& r) { require(r.ok && r.value == 22, "second shared response"); ++replies; }, 500);
        bool fiberDone = false;
        SwTimer::singleShot(0, [&] {
            try {
                TestProxy proxy(domain, "test/service");
                require(proxy.echoRpc().callResult(73, 500).value == 73, "fiber sync RPC");
                require(proxy.noopRpc().callResult(500).ok, "void result API");
                fiberDone = true;
            } catch (const std::exception& e) { std::cerr << e.what() << '\n'; exitCode = 1; }
        });
        SwTimer::singleShot(800, [&] { app.quit(); });
        app.exec();
        require(fiberDone && replies == 2 && callbacks == 2 && absent.pendingCount() == 0 && !destroyedCallback, "async lifecycle / event-loop RPC");
        std::cout << "PASS: codec widths/ranges, configuration/binding/runtime persistence, property failure, RPC routing/error/timeout/cancel/destruction/fiber/proxy\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; exitCode = 1; }
    fs::current_path(original); fs::remove_all(work);
    return exitCode;
}
