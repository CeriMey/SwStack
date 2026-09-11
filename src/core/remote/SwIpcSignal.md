# Shared native signal payloads

`SwIpcSignal` shares an immutable publication with native receivers. Other
processes receive the existing encoded payload through shared memory.

## Ordinary publication

Existing declarations, `publish(...)`, `operator()`, `ipcConnectT` and
`ipcConnectScopedT` remain available.

```cpp
sw::ipc::SwIpcSignal<SwString> changed(registry, "changed", 16, 65536);
auto subscription = changed.connect(&receiver, [](const SwString& text) {
    consume(text);
}, false);
changed.publish(text);
```

When native subscribers exist, ordinary publication makes one owned snapshot.
All local slots accepting `const T&` observe it without per-receiver payload
copies. By-value slots still receive independent copies and may modify them.
A by-value `std::function<void(T)>` or lambda in application code introduces
a copy even when the eventual consumer takes a reference.

## Publication from an existing owner

`Values` is the tuple of the signal's decayed argument types. `SharedValues`
is `std::shared_ptr<const Values>`. `publishShared` retains that owner:

```cpp
using Signal = sw::ipc::SwIpcSignal<SwString>;
Signal changed(registry, "changed", 16, 65536);
Signal::SharedValues values =
    std::make_shared<const Signal::Values>(SwString("a large payload"));
auto subscription = changed.connect(&receiver, [&](const SwString& text) {
    assert(&text == &std::get<0>(*values));
}, false);
bool accepted = changed.publishShared(values);
```

Construct the tuple in place or move arguments into it to avoid copying while
preparing the owner. For multiple arguments, use for example
`SwIpcSignal<uint64_t, SwString>::Values`. Zero-argument signals use an empty
tuple. A null owner is rejected without publishing.

The signal still requires its ordinary codec and serialized payload bound.
Neither a raw address nor the bytes of a `shared_ptr` form a portable wire
representation.

## Lifetime, affinity and cooperative tasks

- On the receiver's thread, with no earlier queued batch, delivery is direct.
- Across threads, only shared ownership is queued; the callback runs on the
  receiver's thread.
- The publication stays owned until the callback returns, including while
  it yields through `SwEventLoop`.
- A borrowed reference or pointer must not escape the callback. Independent
  asynchronous work needs an owning handle, supplied by the application's
  payload or retained separately.
- `publishShared` requires immutable contents throughout the retained lifetime.
  Mutable aliases must not modify or recycle them while the signal, queued
  callbacks or active callbacks own them. A const tuple does not make buffers
  reached through mutable pointers immutable.
- GPU buffers still require synchronization and an owner preventing recycling
  until GPU work completes. Sharing an address does not establish readiness.
- Cancellation prevents a queued callback from starting; it cannot undo a
  running slot. Normal receiver lifetime rules still apply.

Replay preserves order. LatestOnly replaces pending values. Native publication
history is bounded by ring capacity; queued replay receivers retain owners
until delivery. Ownership can outlive the emitter's local handle.

## Wire compatibility

Every accepted publication still writes the ordinary SHM record, even when
all currently known receivers are local. This preserves `readLatest`, late
subscriptions, replay, raw readers and mixed local/interprocess delivery.
Local subscribers without a matching retained native publication decode the
record through the normal wire path.

The native broker key contains a layout version to distinguish the previous
channel layout. Rebuild consumers and plugins using these headers; this is
not a binary hot-reload contract. Wire layout and type identity are unchanged.

## Verification

`tests/runtime_foundation/signal_sharing.cpp` checks addresses and copy counts
through direct, named, scoped and queued connections, cooperative yields,
reentrancy, receiver affinity changes, cancellation, payload rejection, tuple
arguments and retained wire values.

Existing routing, property, module, realtime database notice and dynamic ring
tests cover surrounding IPC behavior. The optional `sw_signal_sharing_bench`
target reports process CPU time, wall time and copy counts for scalar and
64 KiB messages with one and four direct or named receivers.
Compare identical compiler options on the same machine without concurrent
builds; timing is not a portable CTest pass/fail threshold.
