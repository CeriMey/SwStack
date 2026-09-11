# Runtime foundation regression tests

Standalone C++17 tests, no ROS/Qt or application hardware required.

```sh
cmake -S tests/runtime_foundation -B /tmp/swstack-foundation-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/swstack-foundation-tests -j2
ctest --test-dir /tmp/swstack-foundation-tests --output-on-failure
```

Uses a PID-specific IPC domain and temporary configuration directory. Covers
RPC response multiplexing, event-loop/fiber synchronous calls, remote exceptions,
oversized replies, async timeout/cancellation/destruction, generated proxy accessors,
JSON scalar widths and overflow rejection, configuration binding and runtime-layer
persistence, list configuration serialization and failed property publication.

The `native_types` executable also checks complete unsigned string conversion
(including overflow, bases and embedded NULs), `SwSet` uniqueness/hash collisions,
direct `SwFileInfo(SwString)` construction and malformed RPC response PIDs.

The unified signal tests cover:

- `ring_dynamic`: independent subscription cursors, Replay backpressure,
  LatestOnly replacement, wire validation, raw access and separate processes.
- `signal_routing`: direct and queued delivery without local SHM decoding,
  reentrant emissions, affinity changes, cancellation, deferred named connections
  and simultaneous local/remote subscribers. It also checks that unavailable
  receiver threads cannot turn a committed publication into a retryable failure,
  and that named connections reject existing signals with incompatible types.
- `signal_property`: cache visibility before callbacks, reentrant updates,
  concurrent writers and rejected oversized publications.
- `signal_module` and `signal_module_publisher_first`: signals in both directions
  across a dynamically loaded module with hidden symbols, including queued
  callbacks and foreign object contexts. Both module construction orders run.
- `rpc_routing`: direct/queued/IPC requests, generated proxies, owned native
  payloads, deadlines, cancellation before execution and prepared requests
  retaining their service generation across replacement.
- `rpc_module`: hidden-module generated proxies, caller-runtime posting/timers,
  queued handlers, cancellation and module teardown.
- `realtimedb_notice`: shared immutable change batches, mutation isolation,
  queued affinity and the unchanged compact SwString wire format in a child process.
- `registry_heartbeat` (Linux): an external observer sees expired presence as
  offline, then sees it return when the owning runtime resumes. A reader cannot
  take ownership from a live publisher during the gap; subscriptions retain
  their reference counts and wakeup routing. Exited publishers are reclaimed
  even when the registry was full. The test ages only its private registry's
  timestamps, so it exercises expiry without pausing production processes.
- `registry_crash` (Linux): kills the holder of private subscriber and signal
  registry mutexes, checks recovery and subsequent IPC, bounds interrupted
  counters, exercises the app registry mutex helper with an isolated mapping,
  and verifies simultaneous creators do not reset each other's published state.

The module code stays loaded until process exit to keep shared callbacks and
control-block destructors valid after plugin instances are destroyed. Replacing
the plugin binary requires a process restart.

This is a functional regression test, not a concurrency stress test, a latency
benchmark or certification of crash recovery. Callbacks must obey application
thread/lifetime rules. Cancellation can prevent a queued native handler from
starting; it cannot undo an operation that has already begun executing.
