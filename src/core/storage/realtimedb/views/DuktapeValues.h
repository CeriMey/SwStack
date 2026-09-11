#pragma once

/* Included by DuktapeRuntime.c after Evaluation and rtdb_check. These frames
 * are exclusively C: getters, quota failures and VM errors may longjmp here. */
static SwRtDbValue rtdb_read_input(duk_context* ctx, Evaluation* state) {
    SwRtDbValue value;
    memset(&value, 0, sizeof(value));
    if (!state->bridge->read(state->bridge->context, &value))
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "typed input conversion failed");
    return value;
}

static void rtdb_push_input(duk_context* ctx, Evaluation* state, const SwRtDbValue* value, unsigned depth) {
    duk_idx_t target;
    duk_uarridx_t index = 0;
    if (rtdb_execution_expired(state)) duk_error(ctx, DUK_ERR_RANGE_ERROR, "execution budget exceeded");
    duk_require_stack(ctx, 8);
    switch (value->type) {
    case SW_RTDB_NULL: duk_push_null(ctx); return;
    case SW_RTDB_BOOLEAN: duk_push_boolean(ctx, value->number != 0); return;
    case SW_RTDB_NUMBER:
        if (!isfinite(value->number)) duk_error(ctx, DUK_ERR_TYPE_ERROR, "non-finite input number");
        duk_push_number(ctx, value->number); return;
    case SW_RTDB_STRING: duk_push_lstring(ctx, value->text, value->length); return;
    case SW_RTDB_OBJECT: target = duk_push_object(ctx); break;
    case SW_RTDB_ARRAY: target = duk_push_array(ctx); break;
    default: duk_error(ctx, DUK_ERR_TYPE_ERROR, "invalid typed input token"); return;
    }
    if (depth >= 64) duk_error(ctx, DUK_ERR_RANGE_ERROR, "input nesting exceeds 64 levels");
    for (;;) {
        SwRtDbValue child = rtdb_read_input(ctx, state);
        if (child.type == SW_RTDB_END) break;
        if (value->type == SW_RTDB_OBJECT) {
            if (child.type != SW_RTDB_KEY) duk_error(ctx, DUK_ERR_TYPE_ERROR, "input object needs a key");
            duk_push_lstring(ctx, child.text, child.length);
            child = rtdb_read_input(ctx, state);
            rtdb_push_input(ctx, state, &child, depth + 1);
            /* Define own data properties: __proto__ is data, as with JSON.parse. */
            duk_def_prop(ctx, target, DUK_DEFPROP_HAVE_VALUE | DUK_DEFPROP_SET_WEC);
        } else {
            rtdb_push_input(ctx, state, &child, depth + 1);
            duk_put_prop_index(ctx, target, index++);
        }
    }
}

static void rtdb_emit(duk_context* ctx, Evaluation* state, SwRtDbValueType type, duk_idx_t source) {
    SwRtDbValue value;
    memset(&value, 0, sizeof(value));
    value.type = type;
    if (type == SW_RTDB_STRING || type == SW_RTDB_KEY)
        value.text = duk_get_lstring(ctx, source, &value.length);
    else if (type == SW_RTDB_NUMBER) value.number = duk_get_number(ctx, source);
    else if (type == SW_RTDB_BOOLEAN) value.number = duk_get_boolean(ctx, source);
    if (!state->bridge->write(state->bridge->context, &value))
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "typed output conversion failed");
}

/* Read each getter once into the final C++ tree. There is no temporary JS clone,
 * stringify, parse, or call to user-defined toJSON. Partial C++ output is owned
 * by the caller and discarded if any later field, getter or budget check fails. */
static void rtdb_pull_output(duk_context* ctx, Evaluation* state, duk_idx_t source, unsigned depth) {
    void* identity;
    unsigned ancestor;
    source = duk_normalize_index(ctx, source);
    rtdb_check(ctx, state);
    duk_require_stack(ctx, 8);
    if (duk_is_null(ctx, source)) { rtdb_emit(ctx, state, SW_RTDB_NULL, source); return; }
    if (duk_is_boolean(ctx, source)) { rtdb_emit(ctx, state, SW_RTDB_BOOLEAN, source); return; }
    if (duk_is_string(ctx, source) && !duk_is_symbol(ctx, source)) {
        rtdb_emit(ctx, state, SW_RTDB_STRING, source); return;
    }
    if (duk_is_number(ctx, source)) {
        if (!isfinite(duk_get_number(ctx, source))) duk_error(ctx, DUK_ERR_TYPE_ERROR, "non-finite JSON number");
        rtdb_emit(ctx, state, SW_RTDB_NUMBER, source); return;
    }
    if (!duk_is_object(ctx, source) || duk_is_function(ctx, source) ||
        duk_is_thread(ctx, source) || duk_is_buffer_data(ctx, source))
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "result contains a non-JSON value");
    if (depth >= 64) duk_error(ctx, DUK_ERR_RANGE_ERROR, "JSON nesting exceeds 64 levels");
    identity = duk_get_heapptr(ctx, source);
    for (ancestor = 0; ancestor < depth; ++ancestor)
        if (state->ancestors[ancestor] == identity) duk_error(ctx, DUK_ERR_TYPE_ERROR, "cyclic JSON result");
    state->ancestors[depth] = identity;
    duk_get_prototype(ctx, source);
    if (!duk_is_undefined(ctx, -1) &&
        duk_get_heapptr(ctx, -1) != (duk_is_array(ctx, source) ? state->arrayPrototype : state->objectPrototype))
        duk_error(ctx, DUK_ERR_TYPE_ERROR, "result requires plain JSON objects");
    duk_pop(ctx);
    if (duk_is_array(ctx, source)) {
        const duk_size_t size = duk_get_length(ctx, source);
        duk_uarridx_t index;
        if (size > 100000) duk_error(ctx, DUK_ERR_RANGE_ERROR, "JSON array exceeds 100000 values");
        rtdb_emit(ctx, state, SW_RTDB_ARRAY, source);
        for (index = 0; index < size; ++index) {
            duk_get_prop_index(ctx, source, index);
            rtdb_pull_output(ctx, state, -1, depth + 1);
            duk_pop(ctx);
        }
    } else {
        rtdb_emit(ctx, state, SW_RTDB_OBJECT, source);
        duk_enum(ctx, source, DUK_ENUM_OWN_PROPERTIES_ONLY | DUK_ENUM_INCLUDE_SYMBOLS);
        while (duk_next(ctx, -1, 1)) {
            if (duk_is_symbol(ctx, -2)) duk_error(ctx, DUK_ERR_TYPE_ERROR, "JSON object has a symbol key");
            rtdb_emit(ctx, state, SW_RTDB_KEY, -2);
            rtdb_pull_output(ctx, state, -1, depth + 1);
            duk_pop_2(ctx);
        }
        duk_pop(ctx);
    }
    rtdb_emit(ctx, state, SW_RTDB_END, source);
}
