# Runtime foundation regression test

Standalone C++17 test, no ROS/Qt or application hardware required.

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

This is a functional regression test, not a concurrency stress test, a latency
benchmark or certification of crash recovery. Callbacks must obey application
thread/lifetime rules. A local timeout/cancel does not cancel server execution.
