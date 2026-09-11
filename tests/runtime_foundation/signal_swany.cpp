#include "SwRemoteObject.h"
#include <filesystem>
#include <iostream>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#endif

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Message {
    int id{0};
    SwString label;
    static int copies, encodes, decodes;
    Message() = default;
    Message(int id, SwString label) : id(id), label(std::move(label)) {}
    Message(const Message& other) : id(other.id), label(other.label) { ++copies; }
    Message& operator=(const Message& other) { id = other.id; label = other.label; ++copies; return *this; }
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;
};
int Message::copies = 0, Message::encodes = 0, Message::decodes = 0;
static void registerMessage() {
    SwAny::registerMetaType<Message>();
    SwAny::registerStringSerialization<Message>(
        [](const Message& value) {
            ++Message::encodes;
            if (value.id < 0) throw std::invalid_argument("negative message");
            return SwString::number(value.id) + "\n" + value.label;
        },
        [](const SwString& text) {
            ++Message::decodes;
            const auto string = text.toStdString();
            const auto split = string.find('\n');
            if (split == std::string::npos) throw std::invalid_argument("missing message separator");
            size_t used = 0; const int id = std::stoi(string.substr(0, split), &used);
            if (used != split || id < 0) throw std::invalid_argument("invalid message id");
            return Message(id, SwString(string.data() + split + 1, string.size() - split - 1));
        });
}
using namespace sw::ipc;
using Signal = SwIpcSignal<Message>;
class Node : public SwRemoteObject {
public:
    Node(const SwString& domain, const SwString& name) : SwRemoteObject(domain, "swany", name) {}
    SW_IPC_SIGNAL_SIZED(data, 4096, Message);
    bool send(const Message& value) { return emit data(value); }
    bool send(const Signal::SharedValues& owner) { return emit data(owner); }
};
static SwString label() { return SwString("camera\0left", 11); }
static void local(const SwString& domain) {
    Node source(domain, "source"), receiver(domain, "receiver");
    std::vector<const Message*> addresses;
    auto received = [&](const Message& value) {
        require(value.id == 42 && value.label == label(), "native content");
        addresses.push_back(&value);
    };
    auto direct = source.data.connect(received, false);
    auto named = receiver.ipcConnectT("swany/source#data", received, false);
    require(named != 0, "named connection");
    Message value(42, label());
    Message::copies = Message::encodes = Message::decodes = 0;
    require(source.send(value) && addresses.size() == 2 && addresses[0] == addresses[1] &&
            addresses[0] != &value && Message::copies == 1 && Message::encodes == 1 && Message::decodes == 0,
            "ordinary emit did not retain one native snapshot");
    auto owner = std::make_shared<const Signal::Values>(Message(42, label()));
    addresses.clear(); Message::copies = 0;
    require(source.send(owner) && addresses.size() == 2 && addresses[0] == &std::get<0>(*owner) &&
            addresses[1] == addresses[0] && Message::copies == 0 && Message::decodes == 0,
            "shared emit copied/decoded the native payload");
    Message latest;
    require(source.data.readLatest(latest) && latest.id == 42 && latest.label == label() && Message::decodes == 1,
            "wire read did not use registered deserializer");
    const auto sequence = source.data.raw().sequence();
    require(!source.send(Message(-1, "invalid")) && source.data.raw().sequence() == sequence &&
            addresses.size() == 2, "failed serialization published/called a receiver");
    require(!source.send(Message(42, SwString(std::string(8192, 'x')))) &&
            source.data.raw().sequence() == sequence, "oversized registered payload published");

    Registry registry(domain, "swany/raw");
    RingQueueDynamic<Message> wire(registry, "malformed", 4, 128);
    uint8_t bytes[64]; SwAny::BinaryWriter writer(bytes, sizeof(bytes));
    require(SwAny::serializeBinary(writer, SwString("not-a-message")), "malformed fixture");
    require(RingQueueDynamic<Message>::publishBytes(wire.shmName(), detail::type_id<Message>(),
            bytes, writer.size()), "malformed fixture publication");
    latest = Message(9, "retained");
    require(!wire.readLatest(latest) && latest.id == 9 && latest.label == "retained", "malformed object accepted");
    receiver.ipcDisconnect(named);
}
#ifndef _WIN32
static void waitPipe(int fd, const char* message) {
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    char byte = 0;
    require(SwEventLoop::waitUntil([&] {
        detail::LoopPoller::instance().dispatch();
        return ::read(fd, &byte, 1) == 1;
    }, 5000) && byte == 'Y', message);
}
static int child(const SwString& domain, int ready, int result) {
    // A fresh executable registers the type independently; no inherited registry.
    registerMessage();
    Node receiver(domain, "external");
    std::vector<int> values;
    const auto token = receiver.ipcConnectT("swany/external_source#data", [&](const Message& value) {
        require(value.label == label(), "external label");
        values.push_back(value.id);
    }, false);
    require(token && ::write(ready, "Y", 1) == 1, "child startup");
    require(SwEventLoop::waitUntil([&] {
        detail::LoopPoller::instance().dispatch(); return values.size() == 2;
    }, 5000), "external timeout");
    require(values == std::vector<int>({10, 20}) && Message::decodes == 2, "external decoder/order");
    require(::write(result, "Y", 1) == 1, "child result");
    receiver.ipcDisconnect(token);
    return 0;
}
static void external(const SwString& domain, const char* executable) {
    Node source(domain, "external_source");
    int ready[2], result[2];
    require(::pipe(ready) == 0 && ::pipe(result) == 0, "pipe");
    const auto readyFd = std::to_string(ready[1]), resultFd = std::to_string(result[1]);
    const auto domainText = domain.toStdString();
    const pid_t pid = ::fork(); require(pid >= 0, "fork");
    if (!pid) {
        ::close(ready[0]); ::close(result[0]);
        ::execl(executable, executable, "--child", domainText.c_str(), readyFd.c_str(), resultFd.c_str(), nullptr);
        ::_exit(127);
    }
    ::close(ready[1]); ::close(result[1]);
    waitPipe(ready[0], "external startup timeout");
    require(source.send(Message(10, label())), "external ordinary emit");
    const auto owner = std::make_shared<const Signal::Values>(Message(20, label()));
    require(source.send(owner), "external shared emit");
    waitPipe(result[0], "external result timeout");
    int status = 0;
    require(SwEventLoop::waitUntil([&] { return ::waitpid(pid, &status, WNOHANG) == pid; }, 5000) &&
            WIFEXITED(status) && WEXITSTATUS(status) == 0, "external child failed");
    ::close(ready[0]); ::close(result[0]);
}
#endif
int main(int argc, char** argv) {
    try {
        SwCoreApplication app(argc, argv);
        const SwString domain = argc > 2 ? SwString(argv[2]) :
            "signal_swany_" + SwString::number(detail::currentPid());
        const auto work = std::filesystem::temp_directory_path() / domain.toStdString();
        SwRemoteObject::ConfigRootScope config(SwString((work / "config").string()));
#ifndef _WIN32
        if (argc == 5 && std::string(argv[1]) == "--child")
            return child(domain, std::stoi(argv[3]), std::stoi(argv[4]));
#endif
        registerMessage();
        local(domain);
#ifndef _WIN32
        external(domain, std::filesystem::absolute(argv[0]).c_str());
#endif
        std::cout << "PASS SwAny remote emit, shared ownership, IPC exec, replay, malformed payloads and limits\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
