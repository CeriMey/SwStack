#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private C boundary. Text is length-delimited and borrowed until the callback
 * returns (write) or the next read. No callback may call Duktape or throw: all
 * Duktape longjmps must remain inside the protected C runtime. */
typedef enum {
    SW_RTDB_NULL, SW_RTDB_BOOLEAN, SW_RTDB_NUMBER, SW_RTDB_STRING,
    SW_RTDB_OBJECT, SW_RTDB_ARRAY, SW_RTDB_KEY, SW_RTDB_END
} SwRtDbValueType;

typedef struct {
    SwRtDbValueType type;
    double number;
    const char* text;
    size_t length;
} SwRtDbValue;

typedef struct {
    void* context;
    int (*read)(void* context, SwRtDbValue* value);
    int (*write)(void* context, const SwRtDbValue* value);
    const char* bytecode;
    size_t bytecodeLength;
    int (*compiled)(void* context, const char* bytecode, size_t length);
} SwRtDbValueBridge;

typedef struct SwRtDbJsRuntime SwRtDbJsRuntime;
SwRtDbJsRuntime* sw_realtime_db_create_js_runtime(size_t heapLimit);
void sw_realtime_db_destroy_js_runtime(SwRtDbJsRuntime* runtime);

int sw_realtime_db_evaluate_js(SwRtDbJsRuntime* runtime,
                             const char* function, size_t functionLength,
                             const SwRtDbValueBridge* bridge, uint64_t deadlineNs,
                             char* error, size_t errorCapacity);

#ifdef __cplusplus
}
#endif
