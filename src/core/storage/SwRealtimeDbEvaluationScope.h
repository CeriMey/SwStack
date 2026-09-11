#pragma once
#include <cstdint>

// Share one elapsed-time budget across an optional native evaluation and its
// JavaScript fallback. Thread-local state holds a deadline only, never values.
class SwRealtimeDbEvaluationScope {
public:
    explicit SwRealtimeDbEvaluationScope(int budgetMs = 5);
    ~SwRealtimeDbEvaluationScope();
    SwRealtimeDbEvaluationScope(const SwRealtimeDbEvaluationScope&) = delete;
    SwRealtimeDbEvaluationScope& operator=(const SwRealtimeDbEvaluationScope&) = delete;
    void check() const;
    static void checkCurrent();
    static std::uint64_t deadlineNs(int budgetMs);
private:
    std::uint64_t previous_, deadline_;
};
