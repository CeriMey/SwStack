#include "SwCoreApplication.h"
#include "SwObject.h"
#include "SwJsonObject.h"
#include "SwTimer.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct CopyOnly {
    int value;
    explicit CopyOnly(int input = 0) : value(input) {}
    CopyOnly(const CopyOnly&) = default;
    CopyOnly(CopyOnly&&) = delete;
};

class Source : public SwObject {
public:
    DECLARE_SIGNAL(payload, const SwJsonObject&);
    DECLARE_SIGNAL(number, int);
    DECLARE_SIGNAL(copy, CopyOnly);
    DECLARE_SIGNAL(pointer, const char*);
    DECLARE_SIGNAL_VOID(empty);
    void literal() { emitSignal("literal", "literal value"); }
    void namedSnapshot(const SwJsonObject& value) { emitSignal("snapshot", value); }
};

class Receiver : public SwObject {
public:
    int count{0}, value{0};
    const CopyOnly* address{nullptr};
    void copied(CopyOnly input) { ++count; value = input.value; }
    void reference(const CopyOnly& input) { address = &input; }
    void mutableReference(int& input) { ++input; }
};

SwJsonObject object(int number) {
    SwJsonObject nested, result;
    nested["value"] = number; result["nested"] = nested; return result;
}
int value(const SwJsonObject& object) {
    return object["nested"].toObject()["value"].toInt();
}
void modify(const SwJsonObject& object, int number) {
    // Exercise the public borrowed-container API, not only detached toObject().
    (*object["nested"].toObjectPtr())["value"] = number;
}

void directDelivery() {
    Source source;
    SwJsonObject input = object(7);
    std::vector<std::string> calls;
    SwObject::connect(&source, "payload", [&](const SwJsonObject& received) {
        require(value(received) == 7, "named receiver lost original payload");
        calls.push_back("named first"); modify(received, 90);
    }, DirectConnection);
    SwObject::connect(&source, "payload", [&](const SwJsonObject& received) {
        require(value(received) == 7, "named receivers share mutable payload");
        calls.push_back("named second");
    }, DirectConnection);
    Receiver receiver;
    SwObject::connect(&source, &Source::payload, &receiver, [&](const SwJsonObject& received) {
        require(value(received) == 7 && receiver.sender() == &source,
                "typed receiver lost payload or sender");
        calls.push_back("typed first"); modify(received, 91);
    }, AutoConnection);
    SwObject::connect(&source, &Source::payload, [&](const SwJsonObject& received) {
        require(value(received) == 7, "typed receivers share mutable payload");
        calls.push_back("typed second");
    }, DirectConnection);
    source.payload(input);
    require(value(input) == 7, "receiver mutation changed emitting argument");
    require(calls == std::vector<std::string>({"named first", "named second", "typed first", "typed second"}),
            "named/typed receiver order changed");

    // Modifying the external source during the first direct callback must not
    // change the remaining receivers of that emission's owned snapshot.
    int afterMutation = 0;
    SwObject::connect(&source, "snapshot", [&](const SwJsonObject&) { modify(input, 33); }, DirectConnection);
    SwObject::connect(&source, "snapshot", [&](const SwJsonObject& received) {
        afterMutation = value(received);
    }, DirectConnection);
    source.namedSnapshot(input);
    require(value(input) == 33 && afterMutation == 7, "direct dispatch borrows mutable emitting storage");

    CopyOnly copy(41);
    SwObject::connect(&source, "copy", &receiver, &Receiver::copied, DirectConnection);
    source.copy(copy);
    require(receiver.count == 1 && receiver.value == 41, "copy-only member slot failed");
    SlotMember<Receiver, const CopyOnly&> byReference(&receiver, &Receiver::reference);
    byReference.invokeRaw(copy);
    require(receiver.address == &copy, "reference slot copied its referent");
    int mutableValue = 12;
    SlotMember<Receiver, int&> mutableReference(&receiver, &Receiver::mutableReference);
    mutableReference.invokeRaw(mutableValue);
    require(mutableValue == 13, "mutable-reference slot lost its reference");

    std::string named, typed, literal;
    SwObject::connect(&source, "pointer", [&](const char* received) { named = received; }, DirectConnection);
    SwObject::connect(&source, &Source::pointer, [&](const char* received) { typed = received; }, DirectConnection);
    SwObject::connect(&source, "literal", [&](const char* received) { literal = received; }, DirectConnection);
    source.pointer("pointer value"); source.literal();
    require(named == "pointer value" && typed == named && literal == "literal value",
            "C-string or array-to-pointer deduction changed");
    int noArguments = 0;
    SwObject::connect(&source, "empty", [&] { ++noArguments; }, DirectConnection);
    SwObject::connect(&source, &Source::empty, [&] { ++noArguments; }, AutoConnection);
    source.empty(); require(noArguments == 2, "zero-argument dispatch failed");
}

void reentrantDelivery() {
    Source source;
    Receiver first, second;
    std::vector<int> seen;
    SwObject::connect(&source, &Source::number, &first, [&](int value) {
        seen.push_back(value);
        if (value == 1) {
            SwObject::disconnect(&source, &second);
            source.number(2);
        }
    }, DirectConnection);
    SwObject::connect(&source, &Source::number, &second, [&](int value) { seen.push_back(10 + value); }, DirectConnection);
    source.number(1);
    source.number(3);
    require(seen == std::vector<int>({1, 2, 11, 3}),
            "disconnect/reentry changed the current connection snapshot");

    Source destruction;
    Receiver* self = new Receiver;
    Receiver* later = new Receiver;
    int survivors = 0, deadCalls = 0;
    SwObject::connect(&destruction, &Source::number, self, [&](int) {
        delete self; self = nullptr;
        delete later; later = nullptr;
    }, DirectConnection);
    SwObject::connect(&destruction, &Source::number, later, [&](int) { ++deadCalls; }, DirectConnection);
    SwObject::connect(&destruction, &Source::number, [&](int) { ++survivors; }, DirectConnection);
    destruction.number(1); destruction.number(2);
    require(!self && !later && deadCalls == 0 && survivors == 2,
            "self-deletion or deletion of a later receiver is unsafe");
}
}

int main(int argc, char** argv) {
    SwCoreApplication app(argc, argv);
    try {
        directDelivery(); reentrantDelivery();

        Source source;
        Receiver retained;
        Receiver* deleted = new Receiver;
        SwJsonObject input = object(8);
        int delivered = 0, deadCalls = 0;
        bool valuesOkay = true, senderOkay = true, timeout = false;
        auto arrived = [&] { if (++delivered == 4) app.quit(); };
        SwObject::connect(&source, &Source::payload, &retained, [&](const SwJsonObject& received) {
            valuesOkay = valuesOkay && value(received) == 8;
            senderOkay = senderOkay && retained.sender() == &source;
            modify(received, 99); arrived();
        }, QueuedConnection);
        SwObject::connect(&source, &Source::payload, [&](const SwJsonObject& received) {
            valuesOkay = valuesOkay && value(received) == 8; arrived();
        }, QueuedConnection);
        SwObject::connect(&source, &Source::payload, deleted, [&](const SwJsonObject&) { ++deadCalls; }, QueuedConnection);
        source.payload(input);
        modify(input, 123);
        // A previously queued task owns its slot; disconnect does not revoke it.
        SwObject::disconnect(&source, &retained);
        delete deleted; deleted = nullptr;

        Source* deletedSender = new Source;
        Receiver senderObserver;
        SwObject::connect(deletedSender, &Source::number, &senderObserver, [&](int number) {
            valuesOkay = valuesOkay && number == 17;
            senderOkay = senderOkay && senderObserver.sender() == nullptr; arrived();
        }, QueuedConnection);
        deletedSender->number(17); delete deletedSender; deletedSender = nullptr;

        Receiver copied;
        CopyOnly only(61);
        SwObject::connect(&source, "copy", &copied, &Receiver::copied, QueuedConnection);
        source.copy(only);
        // This queued event runs after the copy-only event without sleeping.
        SwObject::connect(&source, &Source::empty, [&] {
            valuesOkay = valuesOkay && copied.count == 1 && copied.value == 61;
            arrived();
        }, QueuedConnection);
        source.empty();
        SwTimer::singleShot(2000, [&] { timeout = true; app.quit(); });
        app.exec();
        require(!timeout && delivered == 4 && !deadCalls && valuesOkay && senderOkay,
                "queued snapshot/copy-only/lifetime/disconnect delivery failed");
        std::cout << "PASS: signal value isolation, order, copy-only/ref/char arguments, reentry and queued lifetime\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
