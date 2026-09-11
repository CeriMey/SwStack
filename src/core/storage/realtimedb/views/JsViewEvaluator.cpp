#include <core/storage/SwRealtimeDbJsEvaluator.h>
#include <core/storage/SwRealtimeDbEvaluationScope.h>

#include "JsValueBridge.hpp"
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

extern "C" std::uint64_t sw_realtime_db_monotonic_ns() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

SwRealtimeDbJsEvaluator::SwRealtimeDbJsEvaluator(int budgetMs, std::size_t heapLimitBytes)
    : programs_(std::make_shared<swRealtimeDbDetail::JsPrograms>()), budgetMs_(budgetMs), heapLimitBytes_(heapLimitBytes) {
    if (budgetMs <= 0 || heapLimitBytes < 256 * 1024) {
        throw std::invalid_argument("JS views require a positive budget and at least 256 KiB of heap");
    }
}

SwJsonArray SwRealtimeDbJsEvaluator::evaluate(const SwString& script, const SwJsonObject& tables) const {
    const auto deadline = SwRealtimeDbEvaluationScope::deadlineNs(budgetMs_);
    const auto checkBudget = [deadline] {
        if (sw_realtime_db_monotonic_ns() >= deadline)
            throw std::runtime_error("JS view: execution budget exceeded");
    };
    if (script.size() > 64 * 1024) {
        throw std::runtime_error("JS view source exceeds 64 KiB");
    }
    const SwString function = SwString("(function(tables){'use strict';\n") + script + "\n})";
    checkBudget();
    auto rows = swRealtimeDbDetail::evaluateTypedValues(function, tables, deadline, heapLimitBytes_, *programs_);
    // The same deadline covers typed conversion, script execution and cleanup.
    checkBudget();
    return rows;
}

std::size_t SwRealtimeDbJsEvaluator::compiledProgramCount() const {
    std::lock_guard<std::mutex> lock(programs_->mutex);
    return programs_->entries.size();
}

SwRealtimeDbJsEvaluator::Program SwRealtimeDbJsEvaluator::prepare(const SwString& script) const {
    if (script.size() > 64 * 1024) throw std::runtime_error("JS view source exceeds 64 KiB");
    auto program = std::make_shared<swRealtimeDbDetail::JsProgram>();
    program->function = SwString("(function(tables){'use strict';\n") + script + "\n})";
    return program;
}

SwJsonArray SwRealtimeDbJsEvaluator::evaluate(const Program& program, const SwJsonObject& tables) const {
    if (!program) throw std::invalid_argument("missing JS view program");
    const auto deadline = SwRealtimeDbEvaluationScope::deadlineNs(budgetMs_);
    std::lock_guard<std::mutex> lock(program->mutex);
    auto rows = swRealtimeDbDetail::evaluateTypedValues(program->function, tables, deadline,
                                                       heapLimitBytes_, *programs_, &program->bytecode);
    if (sw_realtime_db_monotonic_ns() >= deadline) throw std::runtime_error("JS view: execution budget exceeded");
    return rows;
}
