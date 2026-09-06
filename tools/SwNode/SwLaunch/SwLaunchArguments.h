#pragma once

#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwString.h"
#include "SwList.h"

// Keep argument boundaries intact: SwProcess receives an argv list, never a
// command assembled for a shell. Validate before modifying the caller's list.
inline bool swLaunchAppendArguments_(const SwJsonObject& spec,
                                     SwStringList& arguments,
                                     SwString& error) {
    error.clear();
    if (!spec.contains("arguments")) return true;
    if (!spec["arguments"].isArray()) {
        error = "arguments must be an array of strings";
        return false;
    }
    const SwJsonArray extra = spec["arguments"].toArray();
    for (size_t i = 0; i < extra.size(); ++i) {
        if (!extra[i].isString()) {
            error = "arguments must contain strings without NUL bytes";
            return false;
        }
        for (const char byte : extra[i].toString()) {
            if (byte == '\0') {
                error = "arguments must contain strings without NUL bytes";
                return false;
            }
        }
    }
    for (size_t i = 0; i < extra.size(); ++i) {
        arguments.append(SwString(extra[i].toString()));
    }
    return true;
}
