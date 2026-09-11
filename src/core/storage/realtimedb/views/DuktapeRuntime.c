/* Compile the Duktape amalgamation with bounded realtime-view execution. */
#define DUK_COMPILING_DUKTAPE
#include <duktape.h>
#include <stdint.h>
#include "ValueBridge.h"
#include "JsProfile.h"

static int rtdb_execution_expired(void* userData);
#define DUK_USE_INTERRUPT_COUNTER
#define DUK_USE_EXEC_TIMEOUT_CHECK(userData) rtdb_execution_expired(userData)

#undef DUK_USE_COMPILER_RECLIMIT
#define DUK_USE_COMPILER_RECLIMIT 128
#undef DUK_USE_CALLSTACK_LIMIT
#define DUK_USE_CALLSTACK_LIMIT 128
#undef DUK_USE_NATIVE_CALL_RECLIMIT
#define DUK_USE_NATIVE_CALL_RECLIMIT 128
#undef DUK_USE_JSON_DEC_RECLIMIT
#define DUK_USE_JSON_DEC_RECLIMIT 64
#undef DUK_USE_JSON_ENC_RECLIMIT
#define DUK_USE_JSON_ENC_RECLIMIT 64
#undef DUK_USE_REGEXP_COMPILER_RECLIMIT
#define DUK_USE_REGEXP_COMPILER_RECLIMIT 128
/* Allocation-free bytecode must also honor millisecond execution budgets. */
#define DUK_HTHREAD_INTCTR_DEFAULT 4096L
#include <duktape.c>

#include <math.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t sw_realtime_db_monotonic_ns(void);

typedef struct {
    const char* function;
    size_t functionLength;
    const SwRtDbValueBridge* bridge;
    uint64_t deadlineNs;
    size_t heapLimit;
    size_t allocated;
    union SwRtDbAllocation* blocks;
    size_t visited;
    int active;
    int expired;
    int memoryExceeded;
    jmp_buf abortExecution;
    void* objectPrototype;
    void* arrayPrototype;
    void* ancestors[64];
    int profileEnabled;
    int profilePhase;
    uint64_t profileStartedNs;
    uint64_t profileNs[SW_RTDB_JS_PHASE_COUNT];
} Evaluation;

/* State is outside the VM heap and survives both protected errors and the
 * execution-limit longjmp. Disabled profiling performs no clock calls. */
static void rtdb_profile_phase(Evaluation* state, int phase) {
    if (state->profileEnabled) {
        const uint64_t now = sw_realtime_db_monotonic_ns();
        if (state->profilePhase >= 0)
            state->profileNs[state->profilePhase] += now - state->profileStartedNs;
        state->profileStartedNs = now;
        state->profilePhase = phase;
    }
}

static int rtdb_execution_expired(void* userData) {
    Evaluation* state = (Evaluation*) userData;
    if (state->active && (state->expired ||
        sw_realtime_db_monotonic_ns() >= state->deadlineNs)) {
        state->expired = 1;
        /* Returning allocation failure here makes the VM retry emergency GC
         * and compact every object while all allocations continue failing.
         * Abort directly to our outer C frame instead. The complete private
         * heap is restored there, so even interrupted GC needs no VM cleanup.
         * Bridge callbacks never call Duktape: no C++ frame is crossed. */
        longjmp(state->abortExecution, 1);
    }
    return 0;
}

#include "DuktapeHeap.h"

static void rtdb_check(duk_context* ctx, Evaluation* state) {
    if (rtdb_execution_expired(state)) duk_error(ctx, DUK_ERR_RANGE_ERROR, "execution budget exceeded");
    if (++state->visited > 100000) duk_error(ctx, DUK_ERR_RANGE_ERROR, "JSON result exceeds 100000 values");
}

#include "DuktapeValues.h"

static duk_ret_t rtdb_string_operation(duk_context* ctx) {
    const duk_idx_t count = duk_get_top(ctx);
    duk_idx_t index;
    if (count > 0 && !duk_is_string(ctx, 0) && !duk_is_undefined(ctx, 0)) {
        return duk_error(ctx, DUK_ERR_TYPE_ERROR, "JS views require a string separator/search value");
    }
    duk_push_heap_stash(ctx);
    duk_get_prop_index(ctx, -1, (duk_uarridx_t) duk_get_current_magic(ctx));
    duk_remove(ctx, -2);
    duk_push_this(ctx);
    for (index = 0; index < count; ++index) duk_dup(ctx, index);
    duk_call_method(ctx, count);
    return 1;
}

static void rtdb_limit_string_operation(duk_context* ctx, const char* name, duk_int_t id) {
    const duk_idx_t prototype = duk_normalize_index(ctx, -1);
    duk_push_heap_stash(ctx);
    duk_get_prop_string(ctx, prototype, name);
    duk_put_prop_index(ctx, -2, (duk_uarridx_t) id);
    duk_pop(ctx);
    duk_push_c_function(ctx, rtdb_string_operation, DUK_VARARGS);
    duk_set_magic(ctx, -1, id);
    duk_put_prop_string(ctx, prototype, name);
}

static duk_ret_t rtdb_initialize_protected(duk_context* ctx, void* userData) {
    Evaluation* state = (Evaluation*) userData;
    duk_push_object(ctx);
    duk_get_prototype(ctx, -1);
    state->objectPrototype = duk_get_heapptr(ctx, -1);
    duk_pop_2(ctx);
    duk_push_array(ctx);
    duk_get_prototype(ctx, -1);
    state->arrayPrototype = duk_get_heapptr(ctx, -1);
    duk_pop_2(ctx);

    /* All time and external state must come from the tables snapshot. */
    duk_push_global_object(ctx);
    duk_del_prop_string(ctx, -1, "Duktape");
    duk_del_prop_string(ctx, -1, "Date");
    duk_del_prop_string(ctx, -1, "Proxy");
    /* Native RegExp matching/backtracking lacks bytecode interrupts. The
     * generated Duktape builtins require regex support at compile time, so
     * remove all script entry points which can perform matching instead. */
    duk_get_prop_string(ctx, -1, "RegExp");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_del_prop_string(ctx, -1, "exec");
    duk_del_prop_string(ctx, -1, "test");
    duk_del_prop_string(ctx, -1, "constructor");
    duk_pop_2(ctx);
    duk_del_prop_string(ctx, -1, "RegExp");
    duk_get_prop_string(ctx, -1, "String");
    duk_get_prop_string(ctx, -1, "prototype");
    duk_del_prop_string(ctx, -1, "match");
    duk_del_prop_string(ctx, -1, "search");
    rtdb_limit_string_operation(ctx, "replace", 0);
    rtdb_limit_string_operation(ctx, "split", 1);
    duk_pop_2(ctx);
    duk_get_prop_string(ctx, -1, "Math");
    duk_del_prop_string(ctx, -1, "random");
    duk_pop_2(ctx);

    return 0;
}

static duk_ret_t rtdb_evaluate_protected(duk_context* ctx, void* userData) {
    Evaluation* state = (Evaluation*) userData;
    {
        const SwRtDbValue input = rtdb_read_input(ctx, state);
        rtdb_push_input(ctx, state, &input, 0);
    }
    rtdb_profile_phase(state, SW_RTDB_JS_LOAD);
    if (state->bridge->bytecodeLength) {
        void* buffer = duk_push_fixed_buffer(ctx, state->bridge->bytecodeLength);
        memcpy(buffer, state->bridge->bytecode, state->bridge->bytecodeLength);
        duk_load_function(ctx);
    } else {
        duk_eval_lstring(ctx, state->function, state->functionLength);
        /* Only internally compiled bytecode crosses this private boundary.
         * The pristine heap image resets globals after every evaluation. */
        if (state->bridge->compiled) {
            duk_size_t length;
            const char* bytecode;
            duk_dup(ctx, -1);
            duk_dump_function(ctx);
            bytecode = (const char*) duk_get_buffer_data(ctx, -1, &length);
            if (!state->bridge->compiled(state->bridge->context, bytecode, length))
                duk_error(ctx, DUK_ERR_ERROR, "unable to retain compiled view");
            duk_pop(ctx);
        }
    }
    rtdb_profile_phase(state, SW_RTDB_JS_EXECUTE);
    duk_insert(ctx, -2);
    duk_call(ctx, 1);
    rtdb_profile_phase(state, SW_RTDB_JS_OUTPUT);
    if (!duk_is_array(ctx, -1)) duk_error(ctx, DUK_ERR_TYPE_ERROR, "view must return an array of rows");
    state->visited = 0;
    rtdb_pull_output(ctx, state, -1, 0);
    rtdb_check(ctx, state);
    return 1;
}

struct SwRtDbJsRuntime {
    Evaluation state;
    duk_context* ctx;
    size_t baselineAllocated;
};

SwRtDbJsRuntime* sw_realtime_db_create_js_runtime(size_t heapLimit) {
    SwRtDbJsRuntime* runtime = (SwRtDbJsRuntime*) calloc(1, sizeof(*runtime));
    if (!runtime) return NULL;
    runtime->state.heapLimit = heapLimit;
    runtime->ctx = duk_create_heap(rtdb_allocate, rtdb_reallocate, rtdb_free, &runtime->state, NULL);
    if (!runtime->ctx || duk_safe_call(runtime->ctx, rtdb_initialize_protected,
                                     &runtime->state, 0, 0) != DUK_EXEC_SUCCESS) {
        sw_realtime_db_destroy_js_runtime(runtime);
        return NULL;
    }
    /* Snapshot outside any protected call: the VM must hold no pointer into
     * that call's expired C stack. All heap-owned allocation addresses remain
     * stable until this runtime is destroyed. */
    if (!rtdb_snapshot_heap(&runtime->state)) {
        sw_realtime_db_destroy_js_runtime(runtime);
        return NULL;
    }
    runtime->baselineAllocated = runtime->state.allocated;
    return runtime;
}

void sw_realtime_db_destroy_js_runtime(SwRtDbJsRuntime* runtime) {
    if (!runtime) return;
    /* Duktape finalizers cannot be installed in this sandbox. Every allocation
     * belongs to the private allocator, including cycles and failed VMs. */
    rtdb_discard_heap(&runtime->state);
    free(runtime);
}

int sw_realtime_db_evaluate_js(
    SwRtDbJsRuntime* runtime, const char* function, size_t functionLength,
    const SwRtDbValueBridge* bridge, uint64_t deadlineNs, char* error, size_t errorCapacity) {
    Evaluation* state = &runtime->state;
    duk_context* ctx = runtime->ctx;
    volatile int success = 0;
    state->function = function;
    state->functionLength = functionLength;
    state->bridge = bridge;
    state->deadlineNs = deadlineNs;
    state->profileEnabled = sw_realtime_db_js_profile_enabled();
    if (state->profileEnabled) {
        memset(state->profileNs, 0, sizeof(state->profileNs));
        state->profilePhase = -1;
        rtdb_profile_phase(state, SW_RTDB_JS_INPUT);
    }
    if (setjmp(state->abortExecution)) {
        snprintf(error, errorCapacity, "execution budget exceeded");
    } else {
        state->active = 1;
        if (duk_safe_call(ctx, rtdb_evaluate_protected, state, 0, 1) == DUK_EXEC_SUCCESS) {
            success = 1;
        } else if (state->memoryExceeded) {
            snprintf(error, errorCapacity, "memory budget exceeded");
        } else {
            snprintf(error, errorCapacity, "%s", duk_safe_to_string(ctx, -1));
        }
    }
    state->active = 0;
    rtdb_profile_phase(state, SW_RTDB_JS_RESTORE);
    /* Never run script or Duktape cleanup after failure. Restore all VM bytes,
     * including GC, string intern tables, stacks, globals and prototypes, then
     * drop each transient allocation even if evaluation exited by longjmp. */
    rtdb_restore_heap(state);
    assert(state->allocated == runtime->baselineAllocated);
    state->visited = 0;
    state->expired = 0;
    state->memoryExceeded = 0;
    state->function = NULL;
    state->bridge = NULL;
    memset(state->ancestors, 0, sizeof(state->ancestors));
    rtdb_profile_phase(state, -1);
    if (state->profileEnabled)
        sw_realtime_db_js_profile_record(state->profileNs, success,
                                         bridge->bytecodeLength != 0, state->profileStartedNs);
    return success;
}
