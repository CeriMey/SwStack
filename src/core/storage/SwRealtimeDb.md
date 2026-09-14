# SwRealtimeDb

`SwRealtimeDb` is a generic database service for shared application state. Its
rows are stored by `SwTableDb`, backed by `SwEmbeddedDb`. It adds actor ownership,
typed table contracts, bounded retention, reactive JavaScript views, a catalog,
leases and subscriptions. It contains no application-specific tables or devices.

## Layers and build

| API | Responsibility |
| --- | --- |
| `SwEmbeddedDb` | Binary records, indexes, snapshots and persistent or memory-only storage |
| `SwTableDb` | Schemas, rows and atomic row batches; `SwTableRowMode::Exact` preserves supplied fields and values |
| `SwRealtimeDb` | Database contracts, actor sessions, retention, view graph and revision journal |
| `SwRealtimeDbJsEvaluator` | Isolated, bounded ES5 transformations |
| `SwRealtimeDbNode` | Native SwStack RPC service and shared-memory change signal |
| `SwRealtimeDbClient` | Asynchronous requests and response paging |
| `SwRealtimeDbComponent` | Actor declarations, reconnect, leases and subscriptions |

Build the independent example/test project, without a host application:

```sh
cmake -S exemples/58-RealtimeDbSelfTest -B /tmp/sw-realtimedb-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/sw-realtimedb-tests -j 2
ctest --test-dir /tmp/sw-realtimedb-tests --output-on-failure
```

The standalone latency benchmark measures the complete evaluator with 100 input
and output rows, changing values on every iteration and a 5 ms budget:

```sh
/tmp/sw-realtimedb-tests/SwRealtimeDbBenchmark --iterations 1000
```

Its JSON report includes latency percentiles, maximum, failures and validated
outputs. It deliberately runs outside CTest: scheduling pauses on a shared host
must remain visible rather than becoming a flaky functional test.

An embedding project can add `src/core/storage/realtimedb` as a CMake subdirectory
and link its store, client or service targets. This module requires C++17; the
underlying embedded/table database headers retain their existing language support.
Duktape is bundled under `src/core/third_party/duktape`, including its license.

## Common transport

The client uses the common SwStack RPC router. In-process requests pass owned
typed values to a `NativeRpcEndpoint`, executing on the service's thread and
returning on the client's thread. Interprocess requests use the existing JSON
RPC upload and response-paging adapter. All routes execute the same virtual
`SwRealtimeDbNode::executeRequest` domain operation; the RTDB has no separate
local-endpoint registry.

`request(request, callback)` remains asynchronous, including in one thread,
and cancellation prevents a queued native operation from starting. The client
and its callbacks belong to one SwStack event-loop thread. `canRequestDirect()`
reports immediate capability on that thread; `requestImmediate(request)` returns
an optional reply and never queues or falls back to blocking IPC.

Notifications use one common `SwIpcSignal<ChangeNotice>` in `LatestOnly` mode.
Native receivers share an immutable indexed `ChangeBatch`, including complete
retained journal metadata; public callbacks receive detached JSON. The wire
representation remains the compact `SwString` wakeup summary for external tools
and processes. A slow receiver may coalesce notices; RTDB revision cursors and
journal replay still detect gaps and perform resynchronization.

## Bidirectional views

A view may declare `decode` (table rows to view rows) and `encode` (written
view rows to target rows). `script` remains the equivalent of `decode` for
existing read-only views. Supplying both requires identical read scripts.
The native node retains separate prepared program handles for both directions;
bytecode compiles on first use and remains pinned in RAM. Values cross the
existing typed C++/JavaScript boundary without a JSON-text bridge.

```json
{
  "table": "gcs.Pose.command",
  "key": "id",
  "columns": {"id": "string", "degrees": "number"},
  "max_rows": 1,
  "dependencies": ["gimbal.Pose.command"],
  "write_target": "gimbal.Pose.command",
  "encode": "return tables.rows.map(function(r){return {id:r.id,units:r.degrees*100};});",
  "decode": "return tables['gimbal.Pose.command'].map(function(r){return {id:r.id,degrees:r.units/100};});"
}
```

The target is a stored table declared by its owner, with a compatible target
schema (`id`, `units` here), `max_rows: 1`, and the permitted actors in `writers`.
The writer sends `[{"id":"current","degrees":12.5}]` through normal `writeRows`
on its view. Input rows are checked against the view schema, encoded, and checked
against the target schema before storage. The target writer policy is checked
using the original actor, including in `mutate_many`; a view grants no extra
target permission. A writable view may also declare its own `writers` list.

`write_mode: "replace"` is the default and upserts complete encoded rows by key.
`write_mode: "merge"` applies encoded rows as sparse target patches through the
existing atomic merge operation. The encoder also receives the current target snapshot in `tables.current`,
from the same serialized mutation, so a packed-field update can preserve its
other bits without a separate read. This snapshot does not run `decode`.
The encoder receives complete input rows in
`tables.rows`; write-side `merge`, increments, erase and descriptor-bearing first
writes are not accepted on views. A view without `encode` stays read-only.
Each writable view names one stored target which must also be a dependency.
Other dependencies may participate in `decode`; they are not implicitly written.

Views can be registered before their target exists. Reads report the missing
dependency; writes fail without changing data until the target owner publishes
its initial snapshot. A write returns a compact target receipt and the `view`
name. It does not force a lazy decode just to construct a response. Encoding
errors and invalid output reject the write before target mutation. Read decoding
errors retain the existing invalid-view behavior.

Use the same key `current` to replace a command instead of appending identities.
Equal encoded values still produce target `write` events but no `change` event.
Depth one bounds stored values; a consumer implementing latest-value semantics
should coalesce invalidations and read the current row, rather than replaying
historical values from a local queue. The database does not execute hardware
commands or decide which application actions may be superseded.

Introspection exposes `writable`, `write_target`, `write_mode` and
`encode_evaluations`; `include_scripts: true` includes both codecs. Descriptors
and codecs are also retained by persistent storage.

## Persistence

Persistence is enabled by default. The default generic database directory is
`rtdb`; use a distinct `storage.dbPath` for each persistent database instance.

```cpp
#include <core/storage/SwRealtimeDb.h>
#include <core/storage/SwRealtimeDbJsEvaluator.h>

SwRealtimeDbOptions options;
options.storage.persistent = false; // No database files, WAL or disk workers.
options.defaultMaxRows = 100;
options.defaultOverflowPolicy = "evict_oldest_write";
SwRealtimeDbJsEvaluator js;
SwRealtimeDb database([&](const SwString& script, const SwJsonObject& tables) {
    return js.evaluate(script, tables);
}, options);
```

The same `SwEmbeddedDbOptions::persistent` option can be used directly with
`SwEmbeddedDb` or `SwTableDb::open()`. `SwEmbeddedDb::isPersistent()` reports the
mode. Disk remains the default; `lazyWrite` controls disk durability timing and
is distinct from memory-only storage.

Memory databases are independent, process-local instances. Closing one discards
its records; live snapshots may retain their immutable prior state until released.
There is no disk path to reopen in memory mode and no read-only shared instance.
The realtime node's transport/configuration may still use SwStack runtime files;
`persistent=false` concerns database storage.

Persistent realtime databases retain descriptors and rows. On restart they have
a new epoch, no sessions/subscriptions, and invalid retained rows. Owners must
register again and publish a fresh snapshot. Neither persisted rows nor a renewed
lease prove that a hardware measurement is fresh. Pending commands require their
own explicit lifecycle; this API is not a durable task execution engine.

## Tables and first writes

Tables are owned by the actor that declares them. Actor identities may contain
namespace segments, such as `devices/gimbal`. Tables and columns use letters,
digits, `_`, `.` and `-`; `__sw_rtdb` is reserved for storage metadata.

```json
{
  "table": "sensor.samples",
  "schema_version": 1,
  "key": "id",
  "columns": {"id": "string", "temperature": "number"},
  "max_rows": 100,
  "overflow_policy": "evict_oldest_write"
}
```

Supported column types are `string`, `bool`, `number`, `integer`, `object` and
`array`. Every published row must contain exactly the declared columns. Keys are
nonempty strings up to 128 bytes. Validation rejects duplicate keys within a
batch, missing fields, incorrect types and non-finite numbers before publication.
The underlying `SwTableDb` exact mode preserves rows without generated timestamps,
IDs or coercion. Numeric JSON serialization preserves double precision and int64.

`declareTable(definition)` creates an empty, initially invalid table. A client
may also call `writeRows(definition, rows, callback)` once connected: the first
successful write creates and populates the missing table. Its descriptor is
remembered for reconnection. Existing tables must match the descriptor and owner;
schemas are never inferred from a possibly incomplete sample or silently changed.
`writeRows(tableName, rows, callback)` uses an already declared table.

An optional `writers` array authorizes other actors to update a producer's table.
Views are read-only. In-memory validation and a replacement/eviction batch are
atomic to readers. With disk storage, a new descriptor and its first data batch
are separate backend writes: a process crash between them is not a multitable
transaction. Subsequent writes atomically replace the table rows and their write
ordering metadata.

## Retention

The generic defaults are 1,024 rows and `reject`. Applications may override the
defaults through `SwRealtimeDbOptions`, or each table through `max_rows` and
`overflow_policy`:

- `reject`: an overflowing batch fails without modifying existing rows.
- `evict_oldest_write`: retain the most recently written keys, removing the
  oldest writes until the table is within its configured bound.

A same-value rewrite refreshes recency; reading does not. Input array order
defines recency within a batch. Eviction, insertion and updates are committed in
one row batch. The response reports `evicted_keys`; change events report the count
and, for small sets, the evicted keys. Views are recomputed from retained rows.
This is a bound on distinct row keys, not a per-key history. An explicit `reject`
policy is appropriate when dropping a pending request is unacceptable.

The database also bounds logical data/descriptors to 8 MiB, tables/views to 1,024,
columns to 64, and incoming row batches to 1,024. Session and subscription limits
are independent of row retention. Released rows do not leave an unbounded history
or tombstones in the memory backend.

## Views before their sources

A view may explicitly declare `allow_invalid_sources: true` to evaluate the
retained rows of an invalid **base table**, for example after its producer's
lease expires. The default is `false`. This does not change source validity,
rows or timestamps; the transform must decide how to label or use retained data.
Missing dependencies, invalid upstream views, and expiration of the view owner
still prevent evaluation. Unchanged retained inputs reuse the ordinary view
cache. This option is persisted and included in introspection.

A view descriptor adds `dependencies` and `script`. Its ES5 function body receives
`tables`, mapping each dependency name to its current rows, and returns rows
conforming to the view's schema.

```json
{
  "table": "dashboard.temperature",
  "schema_version": 1,
  "key": "id",
  "columns": {"id": "string", "fahrenheit": "number"},
  "dependencies": ["sensor.samples"],
  "script": "return tables['sensor.samples'].map(function(r) { return {id:r.id, fahrenheit:r.temperature*1.8+32}; });"
}
```

The view can be created while `sensor.samples` does not exist. It appears in the
catalog with `valid=false` and a diagnostic naming the missing dependency. Merely
creating an uninitialized source does not validate the view. A successful source
publication marks dependent views dirty; by default all dependencies and the view owner must be
valid/online (see `allow_invalid_sources` for retained base-table inputs). Reading a dirty view materializes it and its dependencies once.
An active `write` or `change` subscription instead materializes the subscribed
view and its ancestors on every source write, preserving notification semantics.
Without readers or subscribers, source writes do not execute those scripts.
Registration still computes its initial response once when dependencies are ready.
`changes` reads the existing journal; it does not activate a view or reconstruct
intermediate evaluations skipped while the view was unobserved. Persisted view
rows likewise represent the last materialization, not later unread source updates.
Introspection exposes `dirty`, with `valid=false` and an awaiting-evaluation or
dependency diagnostic until materialization. Script errors, invalid output and dependency/owner loss invalidate
the view while retaining its last successful rows. Cycles are rejected.

Each thread reuses an initialized interpreter and restores its pristine memory
image after every calculation. Global variables, prototypes and application
values cannot survive that restoration, including after execution errors.
The default live-heap limit is 4 MiB; the private pristine image consumes at most
another heap limit and contains only initialized builtins, never application data.
The cooperative 5 ms elapsed-time budget includes typed value conversion,
compilation, execution and restoration. The interpreter and its value adapter are optimized even in Debug
builds (GNU/Clang `-O2`, with Debug symbols preserved); other targets keep their
normal build settings. `SwRealtimeDbNode` takes these
limits from `SwRealtimeDbOptions::viewBudgetMs` and `viewHeapLimitBytes`; a custom
transform supplied directly to the store manages its own execution limits.
Inputs are borrowed read-only while constructing independent JavaScript values.
Validated results are built directly as C++ values; no JSON text or temporary
JavaScript result clone is involved. The 1 MiB input/output bound charges the
equivalent compact SwJson size, including escaped strings, without serializing
the document. Table descriptors, the script interface and RPC remain unchanged.
JavaScript numbers retain their IEEE-754 double semantics; integral results are
stored as int64 when representable, otherwise as doubles. Finite extremes and
subnormal doubles no longer depend on a text parser's integer/underflow limits.
All VM operations remain inside a protected C call; C++ conversion callbacks
catch their exceptions before returning. Partial output is discarded on failure.
`Date`, random values, native RegExp matching and external
I/O are unavailable. Supply time and other external inputs through source tables.
Views run synchronously; budgets apply per view, not per whole publication. The
service does not promise a hard realtime deadline or execute triggers/timers.

## Client subscriptions and introspection

`introspect` accepts optional `namespace`, `schema` and `table` filters. Scripts
are omitted by default; set `include_scripts: true` to inspect the selected view
programs. Listing the catalog therefore does not download every view script.

For bounded catalog discovery, use `summary: true`, `include_runtime: false`,
`offset` (0–100000) and `limit` (0–1024). Filtering precedes pagination in
lexicographic table-name order. `total` counts all matching tables; `next_offset`
is present only for a nonempty page with more matches. A zero limit returns only
the count and envelope. Each page is a current snapshot, not a retained cursor:
restart discovery if tables are added/removed while traversing it.
Summary descriptors retain identity, owner, kind, row count, validity/error,
revision and timestamps (and `writable` for views), but omit columns, metadata,
dependencies, write rules and scripts. Request the exact `table` without
`summary` for its full descriptor. `include_runtime: false` skips global actors,
subscriptions and storage diagnostics. Defaults preserve full introspection.

For runtime diagnosis, setting `SW_RTDB_PROFILE=1` emits aggregate query, store,
JavaScript, notification and response-encoding times once per second on the
`sw.core.storage.realtimedb.profile` debug topic. The stage durations overlap;
they should not be added together. Profiling is disabled when the variable is unset.

`read` can select explicit string `keys`, sort by a numeric or string `order_by`
column (ascending, nulls first, ties by key), and cap the result using `limit`
(0–1024). A limit of zero reads validity and revisions without transferring rows.
These options let command actors fetch one pending request and its matching
result even when their queues contain many entries.

Use the component on one `SwCoreApplication` runtime thread. It derives from
`SwRemoteObject`; concrete actors retain their own acquisition and I/O. The
default endpoint is `rtdb`, with an overload accepting another endpoint.

| Component method | Delivery |
| --- | --- |
| `subscribe(table, "write", callback)` | Initial `snapshot`, then each retained write notification, including equal writes |
| `subscribe(table, "change", callback)` | Initial `snapshot`, then changes to data or validity |
| `subscribeTables(callback)` | Initial `catalog_snapshot`, then `created` events for new tables and views |
| `unsubscribe(id)` | Stops either kind of subscription |
| `readTable(table, callback)` | Current rows, validity, error, epoch and revisions |
| `database().request({"op":"introspect"}, callback)` | Descriptors, owners, retention policy, validity/errors, sessions and subscriptions |

Re-registering an existing table and rewriting a value do not create new catalog
entries. Catalog observation is opt-in. Views with missing dependencies still
generate creation events, since their contracts already exist.

The revision journal contains metadata, not historical row payloads. After a
journal overrun or epoch change, callbacks receive `resync`, followed by a fresh
table/catalog snapshot. The global journal is bounded to 256 events/256 KiB.
An event followed by `readTable()` returns the latest state; intermediate values
may already have been overwritten. Keep separate immutable rows for occurrences
that must all be processed.

Reads are explicit: writing a source does not send its rows to every actor.
Component callbacks include the database `epoch` on snapshots, journal events
and resyncs, so consumers can compare revisions within the same database lifetime.
Subscriptions use common native/SHM notices and filtered journal reads. Complete
native batches are consumed directly; missing coverage requests the journal,
with immediate draining when another notification arrives during an RPC. A sequence gap requests the
journal again; the lease heartbeat also repairs missed wakeups. The maintenance
timer handles retries and leases, not periodic subscription data reads.

Client requests accept `priority: "high"` (default `"normal"`). This local queue
hint is removed before transmission. Mutations and notification journal reads
use high priority; bounded fairness still admits background reads. It does not
preempt an already running RPC or JavaScript evaluation.

The dependency graph is indexed at registration/replacement. Each write visits
only its affected views, in dependency order. `SwRealtimeDbOptions::viewWorkerCount`
defaults to zero; setting it to 1..8 opts a **thread-safe** transform into bounded
parallel evaluation of independent views. Inputs are immutable snapshots, each
built-in JS worker keeps its own reusable interpreter, and storage commits and notifications
remain on the owning thread. Dependent views see their parents' committed output.
An evaluation level containing one view runs directly on the owning thread.
VisionMAX also defaults to serial evaluation: on its 50 Hz actor workload this
used less CPU than four workers. `--view_workers 1..8` enables the worker pool.

An authorized external writer can write only after the owner has initialized a
valid table. After an owner restart or lease expiry, the owner must publish its
fresh snapshot first. The check is atomic with the foreign write.

Valid-table writes use `SwTableDb::applyRows(schema, upserts, eraseKeys)` to commit
only affected rows, including retention evictions, in one atomic storage batch.
This exact-row API validates the whole batch before mutation, maintains indexes,
and rejects duplicate or overlapping upsert/erase keys. Initial publication and
owner recovery still replace the full snapshot; no second row-value cache is kept.

A scalar column descriptor may request a native index with
`{"type":"integer","indexed":true}` (also accepted for string and number).
Bounded reads ordered by a non-null integer/string index use `SwTableDb::queryRows`
to fetch the selected rows directly. The existing comparison remains in use for
nullable columns, numbers, key filters and limits above 250, preserving ordering
and tie semantics. An unqualified bounded read can use the primary-key index.

`changes` accepts `summary:true` to return distinct affected `tables` and
`catalog_changed` instead of copying the individual `events`. Epoch, revision,
selection filters and resync detection are unchanged. The service builds a
compact summary for the wire representation of its change signal while retaining
the individual journal events in the shared native batch.

The interprocess API exposes `query(SwString)`, `readResult(SwString,int)` and the
`changed` SHM signal. Across processes, request/reply data crosses SwStack IPC and
paging handles large JSON messages. In-process RTDB clients use the typed common
RPC route described above. Database rows themselves remain private to the database
process in memory mode. There is no application-specific socket protocol.


Qualified names `namespace.schema.name` expose their three parts on reads and
introspection (legacy unqualified names remain valid). Columns accept either a
built-in type string or an object with `type`, `nullable` and descriptive metadata.
Column labels may include signs and spaces; reserved prototype/storage names and
control characters remain prohibited. Table `metadata` persists with its descriptor.

`read_many` takes an explicit `tables` array of at most 64 distinct names. It returns
`tables: { qualifiedName: snapshot }` at one cursor, including an invalid snapshot
for each missing name. The combined result is limited to 1 MiB. `changes` accepts an
optional explicit `tables` filter for batched subscription delivery; it returns
journal metadata, never copies of all message values. The common component batches
active watchers, caps concurrent initial subscriptions and preserves write/change,
resync, cancellation and callback lifetime semantics.

`write` and `erase` accept the boolean `include_rows` (default `true`). Setting it
to `false` omits `rows` from the acknowledgement while retaining validity,
revision/cursor, `row_count` and eviction metadata. It avoids downloading an entire
queue for each single-row mutation. The option is validated before any mutation.
`SwRealtimeDbComponent::setIncludeWriteRows(false)` selects this response policy
for subsequent writes and erases; explicit reads keep their normal behavior.

`mutate_many` accepts an `operations` array of 1 to 64 ordinary `write` or `erase`
objects, with the `session` on the outer request. It executes them in order using
the same ownership, schema, creation, retention and view rules as individual
mutations. Every write retains its journal event, including equal rewrites and
intermediate updates of observed views. The existing 1 MiB request bound applies.
Replies are always compact, regardless of `include_rows`:

```json
{"complete": false, "applied": 1, "failed_index": 1,
 "results": [{"index": 0, "outcome": "applied", "revision": "4", "cursor": "5",
              "row_count": 1, "evicted_count": 0},
             {"index": 1, "outcome": "failed", "error": "actor is not an authorized writer"},
             {"index": 2, "outcome": "not_run"}],
 "epoch": "...", "cursor": "5", "error": "actor is not an authorized writer"}
```

This is ordered execution, not a transaction across tables. Execution stops on
the first error; previous commits remain. A failure before storage is `failed`.
If storage started but its complete publication could not be confirmed, the
outcome is `unknown`; a confirmed commit remains `applied` even if subsequent view
processing fails. Results contain no row payloads or key lists, and error text is
bounded to 1024 bytes. A transport failure without results cannot establish which
operations executed, so callers must not assume rollback or blindly retry a batch.

`SwRealtimeDbComponent::mutateMany(operations, callback)` supplies its session and
converts a partial result to `reply.ok == false` while preserving `reply.data`.
It remembers auto-created table definitions only for confirmed `applied` entries,
including those preceding an error, and replays them after a service restart.
Local descriptor conflicts are rejected before dispatch. Pending completions are
suppressed when the component is destroyed. Callers retain application ordering
barriers: for example, batch context writes, check cancellation, then separately
admit a command that uses those contexts.

`write` also accepts `merge: true` for sparse row updates. Every row supplies its
primary key; omitted fields remain in RtDb. Objects merge recursively, arrays and
scalars replace their previous values, and `null` remains an explicit value.
For a missing row, omitted nullable columns become `null`; required nonnullable
columns must still be supplied. The ordinary first-write `definition` can create
the table. Normal writes retain their complete-row semantics.

An optional `increments` array applies after the merge, in the same commit:

```json
{"op":"write","session":"...","table":"gimbal.DAYCAM.STATUS","merge":true,
 "rows":[{"__key":"current","FOCUSMODE":2,
          "__meta":{"received_ms":1234,"field_received_ms":{"FOCUSMODE":1234}}}],
 "increments":[{"key":"current","path":["__meta","sequence"],"amount":1}],
 "include_rows":false}
```

There are at most 64 increments per write. Each path contains 1–32 literal field
names, so a field name containing a dot stays one segment. A missing numeric
target starts at zero; nonnumeric targets, overlapping paths, primary-key changes,
integer overflow and nonfinite results are rejected. Each increment key must be
one of the patched rows. Complete schema, depth and capacity validation precedes
storage; an invalid patch cannot partly update another row, an index or the journal.
Identical patches still produce write events and advance retention recency.

Merge reads only the affected keys inside the serialized database operation, then
uses the same atomic `SwTableDb` row batch, indexes, byte accounting and view
notifications as a complete write. No last-value copy is retained by the client.
On an invalid table, only its registered owner can merge retained values into a
fresh publication; omitted stale rows are replaced as with ordinary owner recovery.
`SwRealtimeDbComponent::patchRows(table, rows, increments, callback)` uses this
native RPC and the component's acknowledgement policy. Merging writes also work
inside `mutate_many`. Incrementing writes are not idempotent: a transport error
with unknown outcome must not be treated as confirmation that nothing committed.

## JavaScript view programs and result caches

The RtDb node attaches a prepared JavaScript program handle to each view. Its
first evaluation compiles bytecode in the runtime and pins it in RAM; later
evaluations use that handle directly. Changing the script replaces the handle.
The VM resets application state after each call; only immutable bytecode is
shared. C++ values cross the typed bridge without JSON text serialization.
JSON calculation plans (`projection`) are rejected.

A separate cache holds the last materialized result. Unobserved views are only
recomputed on demand. Equal source writes still emit ordinary write events but
do not invalidate views. On a changed source, subscribed views and their needed
dependencies are refreshed; unused branches stay dirty until read. A dependency
whose resulting values remain equal does not invalidate the next computation.
Schema/script replacement and validity changes still invalidate results.

`SwRealtimeDbJsEvaluator::prepare` returns the owning program handle;
`evaluate(handle, tables)` reuses its bytecode with new input values. The generic
store's optional `PrepareView` callback attaches the corresponding callable to
each table descriptor. Program handles and result rows are never persisted as
pointers; reopening a persistent catalog prepares fresh handles from its scripts.

### Direct requests and value subscriptions

`SwRealtimeDbClient::requestPreferDirect(request, completion, timeoutMs)` executes
on the current owning event loop when native RPC permits it and the client has
no earlier pending request. The completion may run before this method returns.
Other threads/processes and bounded recursive completions use the existing async
transport. `request()` remains asynchronous. The explicit `requestImmediate()`
API keeps its existing immediate semantics (used for out-of-band status reads);
use `requestPreferDirect()` when the operation must stay behind admitted work.
Component mutations and explicit table reads use the preferred direct path.

`SwRealtimeDbComponent::subscribeValues(table, mode, callback, dependencies)` adds
an opt-in latest-value subscription. Initial snapshots and notifications include
`rows`, `valid`, `epoch`, `revision` and selected table snapshots in `dependencies`.
Use `write` for every publication containing a write, including an equal rewrite;
use `change` for publications containing a value change. Multiple writes pending
before delivery coalesce to their latest table snapshot; metadata-only `subscribe`
continues to expose individual retained journal events. Values are detached for
each public callback. Missing tables initially report an invalid snapshot and
become valid when created. Dependencies (at most 63) are read when the target is
published, without continuously evaluating unused dependency views.

The low-level `subscribe` request accepts `include_rows: true` and `dependencies`.
Its response includes `values: {tables, epoch, cursor}`. `subscription_snapshot`
with a session and subscription token retrieves these values for recovery. A
`changes` request with `include_values: true` adds bounded `snapshots`; optional
`value_subscriptions` restricts the selection to specified tokens. Notifications
are published after the query ends. Native local delivery shares an immutable
batch, while SHM carries up to 64 KiB, with existing RPC recovery for larger batches.
The database retains authoritative values; the journal retains only metadata.
