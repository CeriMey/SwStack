#include <core/storage/SwRealtimeDbEvaluationScope.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace {
thread_local std::uint64_t activeDeadline = 0;
std::uint64_t nowNs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
std::uint64_t SwRealtimeDbEvaluationScope::deadlineNs(int budgetMs) {
    if (budgetMs <= 0) throw std::invalid_argument("evaluation requires a positive budget");
    const auto requested = nowNs() + static_cast<std::uint64_t>(budgetMs) * 1000000;
    return activeDeadline ? std::min(activeDeadline, requested) : requested;
}
SwRealtimeDbEvaluationScope::SwRealtimeDbEvaluationScope(int budgetMs)
    : previous_(activeDeadline), deadline_(deadlineNs(budgetMs)) { activeDeadline = deadline_; }
SwRealtimeDbEvaluationScope::~SwRealtimeDbEvaluationScope() { activeDeadline = previous_; }
void SwRealtimeDbEvaluationScope::check() const {
    if (nowNs() >= deadline_) throw std::runtime_error("view execution budget exceeded");
}
void SwRealtimeDbEvaluationScope::checkCurrent() {
    if (activeDeadline && nowNs() >= activeDeadline) throw std::runtime_error("view execution budget exceeded");
}
