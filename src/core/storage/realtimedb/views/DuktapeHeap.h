#pragma once

/* Private allocator for a reusable pristine VM. Baseline blocks never move or
 * disappear: reallocating one creates a transient block instead. Restoring all
 * baseline bytes therefore restores every internal VM pointer and descriptor.
 * Transient allocations retain ordinary malloc/free behavior, so a long script
 * can allocate and collect repeatedly within its memory budget.
 *
 * heapLimit bounds all retained VM blocks, including pinned baseline blocks.
 * The immutable baseline image adds at most another heapLimit outside the VM;
 * normal views use a much smaller image containing only sandboxed builtins.
 * No application input, output or compiled view is in that image. */
typedef union SwRtDbAllocation SwRtDbAllocation;
union SwRtDbAllocation {
    struct {
        size_t size;
        SwRtDbAllocation* previous;
        SwRtDbAllocation* next;
        unsigned char* image;
    } block;
    long double alignment;
    void* pointerAlignment;
};

static void* rtdb_allocate(void* userData, duk_size_t size) {
    Evaluation* state = (Evaluation*) userData;
    SwRtDbAllocation* block;
    if (!size || rtdb_execution_expired(state)) return NULL;
    if (size > state->heapLimit || sizeof(*block) > state->heapLimit - size ||
        size + sizeof(*block) > state->heapLimit - state->allocated) {
        state->memoryExceeded = 1;
        return NULL;
    }
    block = (SwRtDbAllocation*) malloc(sizeof(*block) + size);
    if (!block) { state->memoryExceeded = 1; return NULL; }
    block->block.size = size;
    block->block.previous = NULL;
    block->block.next = state->blocks;
    block->block.image = NULL;
    if (state->blocks) state->blocks->block.previous = block;
    state->blocks = block;
    state->allocated += size + sizeof(*block);
    return block + 1;
}

static void rtdb_free(void* userData, void* ptr) {
    Evaluation* state = (Evaluation*) userData;
    SwRtDbAllocation* block;
    if (!ptr) return;
    block = ((SwRtDbAllocation*) ptr) - 1;
    /* Keep baseline addresses and charge their retained storage to the quota. */
    if (block->block.image) return;
    if (block->block.previous) block->block.previous->block.next = block->block.next;
    else state->blocks = block->block.next;
    if (block->block.next) block->block.next->block.previous = block->block.previous;
    state->allocated -= block->block.size + sizeof(*block);
    free(block);
}

static void* rtdb_reallocate(void* userData, void* ptr, duk_size_t size) {
    Evaluation* state = (Evaluation*) userData;
    SwRtDbAllocation* block;
    SwRtDbAllocation* resized;
    size_t previous;
    if (!ptr) return rtdb_allocate(userData, size);
    if (!size) { rtdb_free(userData, ptr); return NULL; }
    if (rtdb_execution_expired(state)) return NULL;
    block = ((SwRtDbAllocation*) ptr) - 1;
    previous = block->block.size;
    if (block->block.image) {
        /* Some VM compaction paths require shrinking realloc to succeed.
         * Retaining the original capacity satisfies that contract for free. */
        if (size <= previous) return ptr;
        void* result = rtdb_allocate(userData, size);
        if (result) memcpy(result, ptr, previous < size ? previous : size);
        return result;
    }
    if (size > state->heapLimit || sizeof(*block) > state->heapLimit - size ||
        size > state->heapLimit - (state->allocated - previous)) {
        state->memoryExceeded = 1;
        return NULL;
    }
    resized = (SwRtDbAllocation*) realloc(block, size + sizeof(*block));
    if (!resized) { state->memoryExceeded = 1; return NULL; }
    if (resized->block.previous) resized->block.previous->block.next = resized;
    else state->blocks = resized;
    if (resized->block.next) resized->block.next->block.previous = resized;
    resized->block.size = size;
    state->allocated = state->allocated - previous + size;
    return resized + 1;
}

static int rtdb_snapshot_heap(Evaluation* state) {
    SwRtDbAllocation* block;
    for (block = state->blocks; block; block = block->block.next) {
        block->block.image = (unsigned char*) malloc(block->block.size);
        if (!block->block.image) return 0;
        memcpy(block->block.image, block + 1, block->block.size);
    }
    return 1;
}

static void rtdb_restore_heap(Evaluation* state) {
    SwRtDbAllocation* block = state->blocks;
    while (block) {
        SwRtDbAllocation* next = block->block.next;
        if (block->block.image) memcpy(block + 1, block->block.image, block->block.size);
        else rtdb_free(state, block + 1);
        block = next;
    }
}

static void rtdb_discard_heap(Evaluation* state) {
    SwRtDbAllocation* block = state->blocks;
    while (block) {
        SwRtDbAllocation* next = block->block.next;
        free(block->block.image);
        free(block);
        block = next;
    }
    state->blocks = NULL;
    state->allocated = 0;
}
