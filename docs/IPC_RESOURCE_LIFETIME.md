# RPC allocation and Linux shared-memory lifetime

RPC queues default to 100 packets of at most 4096 bytes (about 402 KiB per
queue, including its header), instead of 1000 packets. Explicit capacities
remain supported. All communicating executables and modules must use the
same configured capacity; it is part of each request/response channel name.

Constructing a typed RPC client does not allocate wire queues. A call selects
its native endpoint first. Only a call with no native selection initializes
the fixed-packet request and response transport. The custom remote-adapter
overload still supports types that have no fixed-packet codec. Native calls,
including deferred calls, keep their existing lifetime and cancellation rules.

On Linux, fixed RPC mappings and dynamic signal mappings hold a shared kernel
lease for their entire lifetime. The final mapping closure unlinks the named
shared memory. Keeping a subscriber or another process's mapping alive keeps
the channel alive. Data is no longer retained after all participants close.
Fork-inherited descriptors also retain their lease until they are closed.

Lease files live in the user's private `/tmp/sw-ipc-leases-<uid>` directory.
A namespace lock serializes mapping creation with last-close and collection;
publication and reception do not acquire this lock. A new process collects
abandoned managed leases, and reopening an abandoned name starts a fresh
queue rather than reusing stale commands or an abandoned mutex. SIGKILL and
process crashes release kernel leases without needing an exit callback.
Files without lease records are never included in this collection.

The process dispatch rendezvous also holds a lease. Global discovery registry
segments remain shared across process lifetimes; their existing dead-PID
recovery continues to handle registry rows. Response-cache pruning releases
the mapping instead of unconditionally unlinking a name still used by peers.

Deploy this change with the complete application stopped and rebuild all IPC
participants together. The first migration must remove that application's old,
unused mappings: older binaries do not hold the new leases. Participants in
one deployment run under the same user. This Linux lifetime policy does not
change the Windows kernel mapping lifetime or the existing macOS cleanup.

`tests/runtime_foundation` checks native and remote RPCs, errors, cancellation,
timeouts, shared-library boundaries, dynamic queues, last-close removal,
protection of live and forked mappings, and repeated abnormal process exits.
