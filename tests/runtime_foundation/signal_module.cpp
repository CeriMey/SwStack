#include "SwSharedMemorySignal.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#ifndef _WIN32
#include <dlfcn.h>
#endif

struct ModulePacket { int value; };
static int decodedPackets = 0;
namespace sw { namespace ipc { namespace detail {
template <> struct Codec<ModulePacket> {
    static bool write(Encoder& encoder, const ModulePacket& value) { return encoder.writePOD(value.value); }
    static bool read(Decoder& decoder, ModulePacket& value) {
        ++decodedPackets;
        return decoder.readPOD(value.value);
    }
};
}}}

#ifdef SW_SIGNAL_MODULE_PLUGIN
static std::unique_ptr<sw::ipc::Registry> moduleRegistry;
static std::unique_ptr<sw::ipc::SwIpcSignal<ModulePacket>> moduleSignal;
static sw::ipc::SwIpcSignal<ModulePacket>::Subscription moduleSubscription;
static int moduleReceived = 0;
static bool moduleWrongThread = false;
#if defined(_WIN32)
#define MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif
MODULE_EXPORT bool moduleStart(const char* domain) {
    try {
        moduleRegistry.reset(new sw::ipc::Registry(domain, "module"));
        moduleSignal.reset(new sw::ipc::SwIpcSignal<ModulePacket>(*moduleRegistry, "packet", 16, 32));
        return true;
    } catch (...) { return false; }
}
MODULE_EXPORT bool modulePublish(int value) { return moduleSignal && moduleSignal->publish(ModulePacket{value}); }
MODULE_EXPORT bool moduleSubscribe(SwObject* context) {
    if (!moduleSignal) return false;
    moduleSubscription.stop();
    const std::thread::id expected = std::this_thread::get_id();
    auto callback = [expected](ModulePacket packet) {
        moduleWrongThread = moduleWrongThread || std::this_thread::get_id() != expected;
        moduleReceived = packet.value;
    };
    if (context) moduleSubscription = moduleSignal->connect(context, callback, false);
    else moduleSubscription = moduleSignal->connect(callback, false);
    return true;
}
MODULE_EXPORT int moduleValue() { return moduleReceived; }
MODULE_EXPORT int moduleDecodeCount() { return decodedPackets; }
MODULE_EXPORT bool moduleThreadMismatch() { return moduleWrongThread; }
MODULE_EXPORT void moduleStop() {
    moduleSubscription.stop(); moduleSignal.reset(); moduleRegistry.reset();
}
#else
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("usage: signal_module <module path> [publisher-first]");
        const bool publisherFirst = argc == 3;
        SwCoreApplication app(argc, argv);
        const SwString domain = "signal_module_" + SwString::number(sw::ipc::detail::currentPid()) + "_" +
            SwString::number(std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
        HMODULE module = ::LoadLibraryA(argv[1]);
        if (!module) throw std::runtime_error("LoadLibrary failed");
        auto start = reinterpret_cast<bool (*)(const char*)>(::GetProcAddress(module, "moduleStart"));
        auto publish = reinterpret_cast<bool (*)(int)>(::GetProcAddress(module, "modulePublish"));
        auto stop = reinterpret_cast<void (*)()>(::GetProcAddress(module, "moduleStop"));
        auto subscribe = reinterpret_cast<bool (*)(SwObject*)>(::GetProcAddress(module, "moduleSubscribe"));
        auto value = reinterpret_cast<int (*)()>(::GetProcAddress(module, "moduleValue"));
        auto decodeCount = reinterpret_cast<int (*)()>(::GetProcAddress(module, "moduleDecodeCount"));
        auto threadMismatch = reinterpret_cast<bool (*)()>(::GetProcAddress(module, "moduleThreadMismatch"));
#else
        void* module = ::dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!module) throw std::runtime_error(::dlerror());
        auto start = reinterpret_cast<bool (*)(const char*)>(::dlsym(module, "moduleStart"));
        auto publish = reinterpret_cast<bool (*)(int)>(::dlsym(module, "modulePublish"));
        auto stop = reinterpret_cast<void (*)()>(::dlsym(module, "moduleStop"));
        auto subscribe = reinterpret_cast<bool (*)(SwObject*)>(::dlsym(module, "moduleSubscribe"));
        auto value = reinterpret_cast<int (*)()>(::dlsym(module, "moduleValue"));
        auto decodeCount = reinterpret_cast<int (*)()>(::dlsym(module, "moduleDecodeCount"));
        auto threadMismatch = reinterpret_cast<bool (*)()>(::dlsym(module, "moduleThreadMismatch"));
#endif
        if (!start || !publish || !stop || !subscribe || !value || !decodeCount || !threadMismatch)
            throw std::runtime_error("module exports missing");
        if (publisherFirst && !start(domain.toStdString().c_str())) throw std::runtime_error("early publisher failed");
        sw::ipc::Registry registry(domain, "module");
        sw::ipc::SwIpcSignal<ModulePacket> signal(registry, "packet", 16, 32);
        int received = 0;
        auto subscription = signal.connect([&](ModulePacket packet) { received = packet.value; }, false);
        if ((!publisherFirst && !start(domain.toStdString().c_str())) || !publish(42))
            throw std::runtime_error("module initialization/publication failed");
        const bool direct = received == 42 && decodedPackets == 0;
        if (!direct) sw::ipc::detail::LoopPollerDispatchRegistry::dispatchAll();
        std::cout << "direct=" << direct << " received=" << received << " decoded=" << decodedPackets << '\n';
        if (!direct) throw std::runtime_error("same-thread module publication took the IPC path");
        if (!subscribe(nullptr) || !signal.publish(ModulePacket{43}) || value() != 43 || decodeCount())
            throw std::runtime_error("same-thread hidden module receiver lost native delivery");
        auto fromWorker = [&](int next) {
            bool published = false;
            std::thread worker([&] {
                sw::ipc::Registry workerRegistry(domain, "module");
                sw::ipc::SwIpcSignal<ModulePacket> workerSignal(workerRegistry, "packet", 16, 32);
                published = workerSignal.publish(ModulePacket{next});
            });
            worker.join();
            if (!published) throw std::runtime_error("worker native publication failed");
        };
        fromWorker(44);
        if (value() != 43 || received != 43)
            throw std::runtime_error("queued module callback executed on emitter thread");
        if (!SwEventLoop::waitUntil([&] { return value() == 44 && received == 44; }, 1000))
            throw std::runtime_error("queued hidden module callback was not delivered");
        std::unique_ptr<SwObject> context(new SwObject);
        if (!subscribe(context.get())) throw std::runtime_error("module context subscription failed");
        fromWorker(45);
        if (!SwEventLoop::waitUntil([&] { return value() == 45 && received == 45; }, 1000))
            throw std::runtime_error("foreign context did not queue to its owner runtime");
        fromWorker(46);
        context.reset();
        if (!SwEventLoop::waitUntil([&] { return received == 46; }, 1000) || value() != 45)
            throw std::runtime_error("destroyed foreign context received a pending callback");
        if (decodeCount() || decodedPackets || threadMismatch())
            throw std::runtime_error("module receiver decoded SHM or ran on the wrong thread");
        stop();
#ifdef _WIN32
        ::FreeLibrary(module);
#else
        ::dlclose(module);
#endif
        if (!signal.publish(ModulePacket{47}) || received != 47)
            throw std::runtime_error("channel failed after module unload");
        std::cout << "signal_module: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "signal_module: " << error.what() << '\n';
        return 1;
    }
}
#endif
