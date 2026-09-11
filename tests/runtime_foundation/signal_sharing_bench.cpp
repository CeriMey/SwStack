#include "SwRemoteObject.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <vector>

struct BenchPayload {
    std::vector<unsigned char> bytes;
    static size_t copies;
    BenchPayload() = default;
    explicit BenchPayload(size_t size) : bytes(size, 7) {}
    BenchPayload(const BenchPayload& other) : bytes(other.bytes) { ++copies; }
    BenchPayload& operator=(const BenchPayload& other) { bytes = other.bytes; ++copies; return *this; }
    BenchPayload(BenchPayload&&) = default;
    BenchPayload& operator=(BenchPayload&&) = default;
};
size_t BenchPayload::copies = 0;
static size_t encoded = 0, decoded = 0;
static const bool serializationRegistered = [] {
    SwAny::registerBinarySerialization<BenchPayload>(
    [](SwAny::BinaryWriter& encoder, const BenchPayload& value) {
        ++encoded;
        const uint32_t size = static_cast<uint32_t>(value.bytes.size());
        return encoder.writePOD(size) && encoder.writeBytes(value.bytes.data(), size);
    },
    [](SwAny::BinaryReader& decoder, BenchPayload& value) {
        ++decoded;
        uint32_t size = 0;
        if (!decoder.readPOD(size) || size > decoder.cap - decoder.pos) return false;
        value.bytes.resize(size);
        return decoder.readBytes(value.bytes.data(), size);
    });
    return true;
}();
using namespace sw::ipc;
static double cpuNs() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        throw std::runtime_error("cannot read process CPU time");
    const uint64_t kernelTicks = (uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
    const uint64_t userTicks = (uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
    return (kernelTicks + userTicks) * 100.0;
#else
    timespec time{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time);
    return time.tv_sec * 1e9 + time.tv_nsec;
#endif
}
template <class T>
static void measure(const SwString& domain, const char* label, const T& value, unsigned bytes,
                    unsigned receivers, bool named, int iterations, bool& comma) {
    Registry registry(domain, "bench/source");
    const SwString leaf = SwString(label) + SwString::number(receivers) + (named ? "_named" : "_direct");
    SwIpcSignal<T> signal(registry, leaf, 16, bytes + 64, DeliveryMode::Replay);
    SwRemoteObject node(domain, "bench", "receiver");
    size_t calls = 0;
    std::vector<typename SwIpcSignal<T>::Subscription> subscriptions;
    std::vector<size_t> tokens;
    for (unsigned i = 0; i < receivers; ++i) {
        auto callback = [&](const T&) { ++calls; };
        if (named) tokens.push_back(node.ipcConnectT("bench/source#" + leaf, callback, false));
        else subscriptions.push_back(signal.connect(callback, false));
    }
    for (int i = 0; i < 100; ++i) if (!signal.publish(value)) throw std::runtime_error("warmup failed");
    BenchPayload::copies = encoded = decoded = 0;
    calls = 0;
    const auto begin = std::chrono::steady_clock::now();
    const auto cpuBegin = cpuNs();
    for (int i = 0; i < iterations; ++i)
        if (!signal.publish(value)) throw std::runtime_error("publication failed");
    const double cpu = (cpuNs() - cpuBegin) / iterations;
    const double wall = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - begin).count() / iterations;
    if (calls != receivers * static_cast<size_t>(iterations) || decoded)
        throw std::runtime_error("native delivery count/decoder mismatch");
    if (comma) std::cout << ",\n";
    comma = true;
    std::cout << "{\"case\":\"" << label << "\",\"named\":" << (named ? "true" : "false")
              << ",\"receivers\":" << receivers << ",\"iterations\":" << iterations
              << ",\"wall_ns\":" << wall << ",\"cpu_ns\":" << cpu
              << ",\"copies_per_publish\":" << double(BenchPayload::copies) / iterations
              << ",\"encodes_per_publish\":" << double(encoded) / iterations
              << ",\"decodes\":" << decoded << "}";
    for (size_t token : tokens) node.ipcDisconnect(token);
}
int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        const SwString domain = "signal_bench_" + SwString::number(detail::currentPid());
        const auto configPath = std::filesystem::temp_directory_path() / domain.toStdString();
        SwRemoteObject::ConfigRootScope config(SwString(configPath.string()));
        const BenchPayload large(65536);
        const uint64_t small = 42;
        std::cout << "[\n";
        bool comma = false;
        for (bool named : {false, true})
            for (unsigned count : {1u, 4u}) {
                measure(domain, "scalar", small, 8, count, named, 10000, comma);
                measure(domain, "64KiB", large, 65536, count, named, 1000, comma);
            }
        std::cout << "\n]\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
