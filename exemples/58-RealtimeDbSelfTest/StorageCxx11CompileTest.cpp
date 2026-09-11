#include "SwEmbeddedDb.h"
#include "SwTableDb.h"

// MSVC's oldest selectable language mode is C++14; GCC/Clang must actually
// exercise the C++11 branch instead of inheriting RtDb's newer requirement.
#if !defined(_MSC_VER)
static_assert(__cplusplus == 201103L, "storage compatibility target must compile as C++11");
#endif

// Compile the actual memory/typed APIs, not just forward declarations. This
// object target runs no filesystem/IPC work and needs no test-time process.
void swStorageCxx11CompileSmoke() {
    SwEmbeddedDbOptions options;
    options.persistent = false;
    SwEmbeddedDb embedded;
    embedded.open(options);
    SwJsonObject value;
    value["id"] = "one";
    value["number"] = 0.1;
    SwDbWriteBatch batch;
    batch.putJson(SwByteArray("one"), value);
    embedded.write(batch);
    SwDbJsonRecord record;
    embedded.getJsonRecord(SwByteArray("one"), &record);
    (void)record.detach();
    (void)record.byteSize();
    (void)record.equals(value);
    SwTableDb table;
    table.open(options);
    SwTableSchema schema;
    SwTableDbPreparedSchema prepared;
    SwTableDb::prepareSchema(schema, &prepared);
    SwJsonArray rows;
    rows.append(value);
    table.upsertRows(prepared, rows);
    table.getRowRecord(prepared, "one", &record);
}
