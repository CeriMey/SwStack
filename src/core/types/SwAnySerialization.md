# SwAny serialization for typed IPC

`SwIpcSignal`, the shared-memory rings, IPC tuple readers, and the JSON IPC
tools use `SwAny::serializeBinary` and `SwAny::deserializeBinary`. Application
types no longer specialize `sw::ipc::detail::Codec<T>`.

## Register a record once

Register the same type and representation during initialization in each
participating process/module, before creating publishers or readers:

```cpp
SwAny::registerMetaType<Message>();
SwAny::registerStringSerialization<Message>(
    encodeMessage, // const Message& -> SwString
    decodeMessage  // const SwString& -> Message; throw on invalid input
);
```

`registerMetaType` describes storage/copy/destruction; it does not define a
wire representation. `registerStringSerialization` installs both conversion
directions. Existing `registerConversion<T, SwString>` /
`registerConversion<SwString, T>` pairs work too, as do `std::string` pairs.
The typed and boxed conversion paths use the same registered functions.

Declare a payload bound, then emit normally inside a `SwRemoteObject`:

```cpp
SW_IPC_SIGNAL_SIZED(dataReady, 65536, Message);

emit dataReady(message);
using Signal = sw::ipc::SwIpcSignal<Message>;
Signal::SharedValues owner =
    std::make_shared<const Signal::Values>(std::move(message));
emit dataReady(owner);
```

The shared owner is a tuple of the actual signal arguments. IPC serializes
the contained values, never the pointer or shared_ptr control block.
Custom records, including trivially copyable records, require an explicit
size (or a deliberate `IpcIsBounded`/`IpcWireSize` specialization) because
their registered representation may be variable-size.

## Native formats and hot paths

Native arithmetic/enumeration values retain the existing host binary layout.
`SwString` and `SwByteArray` retain the uint32 length prefix and exact bytes;
embedded NUL bytes are preserved. `std::string` and `std::vector<uint8_t>`
also use this bounded length-prefixed format. This is same-platform IPC;
the change does not introduce a cross-endian or compiler-neutral protocol.

Custom records have no automatic raw-memory fallback. Missing registration
returns false instead of copying an address-containing record into SHM.
For an existing binary hot path, register its functions with SwAny:

```cpp
SwAny::registerBinarySerialization<Message>(
    [](SwAny::BinaryWriter& writer, const Message& value) {
        return SwAny::serializeBinary(writer, value.sequence) &&
               SwAny::serializeBinary(writer, value.text);
    },
    [](SwAny::BinaryReader& reader, Message& value) {
        Message next;
        if (!SwAny::deserializeBinary(reader, next.sequence) ||
            !SwAny::deserializeBinary(reader, next.text)) return false;
        value = std::move(next);
        return true;
    });
```

For a custom record, explicit binary registration takes precedence over
SwString conversion, then std::string conversion. Native wire formats
remain fixed even if the application registers display conversions.
`isSerializable()` retains its existing meaning: bidirectional text
conversion support. Binary-only registration does not imply `toString()`.

Serialization invokes the registered encoder on `const T&`, without
constructing a `SwAny` box or copying the source object. Text converters
may still allocate their output. Binary registration preserves paths that
write directly into the bounded IPC buffer; realtime database ChangeNotice
uses it to keep its existing SwString wire identity and native batch owner.

Binary functions without captures use registered function addresses on the
hot path. Functions with captures retain an immutable shared callback owner
for the full call, including during replacement or reentrant registration.

## Failure, lifetime and registration

Encoding/decoding errors and converter exceptions return false; the byte
cursor is restored. A custom binary reader should stage its output before
assignment if it promises unchanged output on failure. IPC tuple decoding
already stages values before delivery. Length-prefixed input is checked
against remaining payload bytes before allocating.

Register the complete representation before using a channel. Concurrent
replacement keeps active callbacks alive, but changing a wire format while
messages are in flight is not a supported protocol migration. Both ends
must agree on the functions and format. Registrations belong to the
participating module; do not unload code still used by a registration.

Native local delivery still retains immutable typed values. Ordinary emit
takes one protective snapshot when native receivers exist; shared emit
retains the supplied owner without that copy. Every accepted emit still
writes the SHM record for external/late readers, even if all current
subscribers are local. A registration is therefore needed for that wire
write as well. Receiver thread affinity and cooperative yielding are unchanged.

## Verification

`tests/runtime_foundation/swany_serialization.cpp` covers registrations,
replacement, concurrency/reentrancy, source copy counts, native byte
compatibility, malformed sizes, exceptions, and embedded NUL values.
`signal_swany.cpp` exercises real SwRemoteObject emits, shared ownership,
named receivers, raw reads, rejection without publication, and a freshly
executed receiving process that registers its own deserializer.

The existing routing, sharing, module, ring, property, RPC and realtime
database notice tests cover the surrounding transport contracts.
