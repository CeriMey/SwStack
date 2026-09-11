#pragma once
#include <stdint.h>

/* Private, optional instrumentation boundary. Reports contain aggregate times
 * only; neither source text nor application values leave the evaluator. */
enum {
    SW_RTDB_JS_INPUT, SW_RTDB_JS_LOAD, SW_RTDB_JS_EXECUTE,
    SW_RTDB_JS_OUTPUT, SW_RTDB_JS_RESTORE, SW_RTDB_JS_PHASE_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif
int sw_realtime_db_js_profile_enabled(void);
void sw_realtime_db_js_profile_record(const uint64_t* phaseNs, int success,
                                      int bytecodeHit, uint64_t finishedNs);
#ifdef __cplusplus
}
#endif
