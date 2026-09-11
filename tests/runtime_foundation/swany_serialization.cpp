#include "SwAny.h"
#include <array>
#include <atomic>
#include <iostream>
#include <thread>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Record {
    SwString text;
    static int copies;
    Record() = default;
    explicit Record(SwString value) : text(std::move(value)) {}
    Record(const Record& other) : text(other.text) { ++copies; }
    Record& operator=(const Record& other) { text = other.text; ++copies; return *this; }
    Record(Record&&) = default;
    Record& operator=(Record&&) = default;
};
int Record::copies = 0;
struct StdRecord { std::string text; };
struct PodRecord { int value{0}; };
struct BinaryRecord { std::string text; };
using Writer = SwAny::BinaryWriter;
using Reader = SwAny::BinaryReader;

static void registeredTypes() {
    std::array<uint8_t, 256> bytes{};
    Writer output(bytes.data(), bytes.size());
    Record value(SwString("camera\0left", 11)), result;
    SwAny::registerMetaType<Record>();
    require(!SwAny::isSerializable<Record>(), "metatype must not imply a serializer");
    require(!SwAny::serializeBinary(output, value) && output.pos == 0, "unregistered record accepted");
    SwAny::registerStringSerialization<Record>(
        [](const Record& item) { return item.text; },
        [](const SwString& text) { return Record(text); });
    require(SwAny::isSerializable<Record>(), "registered round trip not recognized");
    Record::copies = 0;
    require(SwAny::serializeBinary(output, value) && Record::copies == 0, "encoding copied the source");
    Reader input(bytes.data(), output.size());
    require(SwAny::deserializeBinary(input, result) && result.text == value.text &&
            input.pos == input.cap && Record::copies == 0, "registered text round trip");
    auto boxed = SwAny::from(value);
    Record::copies = 0;
    require(boxed.toString() == value.text && Record::copies == 0, "erased conversion copied source");
    require(SwAny(value.text).convert<Record>().get<Record>().text == value.text, "SwAny conversion changed");

    // Registrations made after a failed attempt and replacements are visible.
    SwAny::registerConversion<Record, SwString>([](const Record&) { return SwString("updated"); });
    output.pos = 0;
    require(SwAny::serializeBinary(output, value), "replacement encode");
    Reader updated(bytes.data(), output.size());
    require(SwAny::deserializeBinary(updated, result) && result.text == "updated", "stale converter");

    SwAny::registerConversion<StdRecord, std::string>([](const StdRecord& item) { return item.text; });
    SwAny::registerConversion<std::string, StdRecord>([](const std::string& text) { return StdRecord{text}; });
    output.pos = 0;
    require(SwAny::serializeBinary(output, StdRecord{"std"}), "std::string converter encode");
    Reader stdInput(bytes.data(), output.size());
    StdRecord stdResult;
    require(SwAny::deserializeBinary(stdInput, stdResult) && stdResult.text == "std", "std::string converter decode");

    PodRecord pod{42}, podResult;
    output.pos = 0;
    require(!SwAny::serializeBinary(output, pod), "record silently copied as raw memory");
    SwAny::registerStringSerialization<PodRecord>(
        [](const PodRecord& item) { return SwString::number(item.value); },
        [](const SwString& text) {
            bool ok = false; const int number = text.toInt(&ok);
            if (!ok) throw std::invalid_argument("invalid record");
            return PodRecord{number};
        });
    require(SwAny::serializeBinary(output, pod) && output.size() == 6, "POD registration ignored");
    Reader podInput(bytes.data(), output.size());
    require(SwAny::deserializeBinary(podInput, podResult) && podResult.value == 42, "POD text decode");
}
static void boundsAndErrors() {
    uint8_t bytes[64]{};
    Writer output(bytes, sizeof(bytes));
    const uint32_t impossible = UINT32_MAX;
    require(output.writePOD(impossible), "length fixture");
    SwString kept("unchanged");
    Reader malformed(bytes, output.size());
    require(!SwAny::deserializeBinary(malformed, kept) && malformed.pos == 0 && kept == "unchanged",
            "invalid length was allocated/accepted");
    Writer small(bytes, 5);
    require(!SwAny::serializeBinary(small, SwString("xx")) && small.pos == 0, "oversized field accepted");
    Writer overflow(bytes, sizeof(bytes));
    require(!overflow.writeBytes(bytes, SIZE_MAX) && overflow.pos == 0, "write length overflow");
    Reader overflowRead(bytes, sizeof(bytes));
    require(!overflowRead.readBytes(bytes, SIZE_MAX) && overflowRead.pos == 0, "read length overflow");
    Writer nullWriter(nullptr, 1);
    Reader nullReader(nullptr, 1);
    uint8_t byte = 0;
    require(!nullWriter.writeBytes(&byte, 1) && !nullReader.readBytes(&byte, 1), "null storage accepted");
    int* pointer = nullptr;
    require(!SwAny::serializeBinary(output, pointer), "pointer serialized across processes");

    SwAny::registerConversion<Record, SwString>([](const Record&) -> SwString { throw std::runtime_error("encode"); });
    output.pos = 0;
    require(!SwAny::serializeBinary(output, Record{}) && output.pos == 0, "encoder exception escaped");
    SwAny::registerConversion<SwString, Record>([](const SwString&) -> Record { throw std::runtime_error("decode"); });
    require(SwAny::serializeBinary(output, SwString("bad")), "bad text fixture");
    Reader badText(bytes, output.size()); Record retained(SwString("kept"));
    require(!SwAny::deserializeBinary(badText, retained) && badText.pos == 0 && retained.text == "kept",
            "decoder exception escaped or changed output");
    output.pos = 0;
    require(SwAny::serializeBinary(output, SwString()), "empty text rejected");
    Reader empty(bytes, output.size());
    require(SwAny::deserializeBinary(empty, kept) && kept.isEmpty(), "empty text round trip");
}
static void nativeWire() {
    uint8_t bytes[128]{}, expected[128]{};
    Writer output(bytes, sizeof(bytes));
    const uint64_t number = UINT64_MAX;
    const double precise = -0.0;
    const SwString text("a\0b", 3);
    const SwByteArray blob("\0\xff", 2);
    require(SwAny::serializeBinary(output, number) && SwAny::serializeBinary(output, precise) &&
            SwAny::serializeBinary(output, text) && SwAny::serializeBinary(output, blob), "native encoding");
    size_t pos = 0;
    auto append = [&](const void* source, size_t size) { std::memcpy(expected + pos, source, size); pos += size; };
    const uint32_t textLength = 3, blobLength = 2;
    append(&number, sizeof(number)); append(&precise, sizeof(precise));
    append(&textLength, 4); append("a\0b", 3); append(&blobLength, 4); append("\0\xff", 2);
    require(output.size() == pos && std::memcmp(bytes, expected, pos) == 0, "legacy wire bytes changed");
    Reader input(bytes, output.size());
    uint64_t resultNumber{}; double resultPrecise{}; SwString resultText; SwByteArray resultBlob;
    require(SwAny::deserializeBinary(input, resultNumber) && SwAny::deserializeBinary(input, resultPrecise) &&
            SwAny::deserializeBinary(input, resultText) && SwAny::deserializeBinary(input, resultBlob),
            "native decoding");
    require(resultNumber == number && std::memcmp(&precise, &resultPrecise, sizeof(double)) == 0 &&
            resultText == text && resultBlob == blob, "native values changed");
}
static void binaryAndConcurrency() {
    auto encode = [](Writer& output, const BinaryRecord& item) {
        return SwAny::serializeBinary(output, item.text);
    };
    auto decode = [](Reader& input, BinaryRecord& item) {
        return SwAny::deserializeBinary(input, item.text);
    };
    SwAny::registerBinarySerialization<BinaryRecord>(encode, decode);
    std::atomic<bool> failed{false};
    std::thread replace([&] {
        for (int i = 0; i < 1000; ++i) {
            if (i % 2 == 0) SwAny::registerBinarySerialization<BinaryRecord>(encode, decode);
            else {
                auto owner = std::make_shared<int>(i);
                SwAny::registerBinarySerialization<BinaryRecord>(
                    [encode, owner](Writer& output, const BinaryRecord& item) {
                        return *owner >= 0 && encode(output, item);
                    },
                    [decode, owner](Reader& input, BinaryRecord& item) {
                        return *owner >= 0 && decode(input, item);
                    });
            }
        }
    });
    for (int i = 0; i < 1000; ++i) {
        uint8_t bytes[32]{};
        Writer output(bytes, sizeof(bytes)); BinaryRecord value{"binary"}, result;
        if (!SwAny::serializeBinary(output, value)) failed = true;
        Reader input(bytes, output.size());
        if (!SwAny::deserializeBinary(input, result) || result.text != "binary") failed = true;
    }
    replace.join();
    require(!failed, "concurrent registration lifetime failed");
    // Reentrant replacement must not destroy the callback while it is executing.
    SwAny::registerBinarySerialization<BinaryRecord>(
        [encode, decode](Writer& output, const BinaryRecord& item) {
            SwAny::registerBinarySerialization<BinaryRecord>(encode, decode);
            return encode(output, item);
        }, decode);
    uint8_t bytes[32]{}; Writer output(bytes, sizeof(bytes));
    require(SwAny::serializeBinary(output, BinaryRecord{"reentrant"}), "reentrant registration failed");

    auto owner = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = owner;
    SwAny::registerBinarySerialization<BinaryRecord>(
        [owner, encode, decode, &lifetime](Writer& writer, const BinaryRecord& item) {
            SwAny::registerBinarySerialization<BinaryRecord>(encode, decode);
            require(!lifetime.expired() && *owner == 42, "active captured callback was released");
            return SwAny::serializeBinary(writer, item);
        }, decode);
    owner.reset();
    output.pos = 0;
    require(SwAny::serializeBinary(output, BinaryRecord{"nested"}), "nested replacement failed");
    require(lifetime.expired(), "replaced captured owner leaked into direct registration");
}
int main() {
    try {
        registeredTypes(); boundsAndErrors(); nativeWire(); binaryAndConcurrency();
        std::cout << "PASS SwAny registrations, no source copies, native wire, bounds, exceptions and concurrency\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
