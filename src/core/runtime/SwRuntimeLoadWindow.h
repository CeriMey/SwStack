#pragma once
#include <chrono>
#include <cstdint>
#include <deque>

// Internal event-loop accounting. The caller holds its measurement mutex.
// Records arrive in steady-clock order; each record enters and leaves the
// aggregate once, independently of how often telemetry reads the current load.
class SwRuntimeLoadWindow {
public:
    using Clock = std::chrono::steady_clock;
    void record(Clock::time_point timestamp, std::uint64_t busy, std::uint64_t total) {
        records_.push_back({timestamp,busy,total});
        busy_ += busy;
        total_ += total;
        expireBefore(timestamp-std::chrono::seconds(1));
    }
    double loadPercentage(Clock::time_point now) {
        expireBefore(now-std::chrono::seconds(1));
        return total_ ? 100.0*static_cast<double>(busy_)/static_cast<double>(total_) : 0.0;
    }
    std::uint64_t lastTotalMicroseconds() const {
        return records_.empty() ? 0 : records_.back().total;
    }
private:
    struct Record {
        Clock::time_point timestamp;
        std::uint64_t busy,total;
    };
    void expireBefore(Clock::time_point cutoff) {
        // Preserve the existing inclusive one-second boundary. Unsigned sums
        // retain the same modulo arithmetic as summing the live records.
        while(!records_.empty() && records_.front().timestamp<cutoff) {
            busy_ -= records_.front().busy;
            total_ -= records_.front().total;
            records_.pop_front();
        }
    }
    std::deque<Record> records_;
    std::uint64_t busy_{0},total_{0};
};
