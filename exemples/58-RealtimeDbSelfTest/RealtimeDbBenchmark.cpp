#include "SwRealtimeDbJsEvaluator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int rowCount = 100;
constexpr int budgetMs = 5;
using Clock = std::chrono::steady_clock;

class CpuClock {
public:
    CpuClock() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
        timespec sample{};
        threadClock_ = ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sample) == 0;
#endif
    }

    double milliseconds() const {
#if defined(CLOCK_THREAD_CPUTIME_ID)
        if (threadClock_) {
            timespec sample{};
            if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sample) != 0)
                throw std::runtime_error("cannot read thread CPU clock");
            return sample.tv_sec * 1000.0 + sample.tv_nsec / 1000000.0;
        }
#endif
        const auto sample = std::clock();
        if (sample == static_cast<std::clock_t>(-1))
            throw std::runtime_error("cannot read process CPU clock");
        return static_cast<double>(sample) * 1000.0 / CLOCKS_PER_SEC;
    }

    const char* name() const { return threadClock_ ? "thread_cpu_time" : "process_std_clock"; }

private:
    bool threadClock_ = false;
};

int parseIterations(int argc, char** argv) {
    if (argc == 1) return 1000;
    if (argc != 3 || std::string(argv[1]) != "--iterations")
        throw std::invalid_argument("usage: SwRealtimeDbBenchmark [--iterations 1..1000000]");
    const std::string input = argv[2];
    if (input.empty() || input.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("iterations must be an integer between 1 and 1000000");
    const auto value = std::stoull(input);
    if (!value || value > 1000000)
        throw std::invalid_argument("iterations must be between 1 and 1000000");
    return static_cast<int>(value);
}

SwJsonObject inputs(int iteration) {
    SwJsonArray rows;
    for (int i = 0; i < rowCount; ++i) {
        SwJsonObject row;
        row["id"] = "row_" + SwString::number(i);
        row["angle"] = i + 0.5 + iteration * 0.125;
        rows.append(row);
    }
    SwJsonObject tables;
    tables["gimbal.recent"] = rows;
    return tables;
}

bool validOutput(const SwJsonArray& rows, int iteration) {
    if (rows.size() != rowCount) return false;
    for (int i = 0; i < rowCount; ++i) {
        if (!rows[i].isObject()) return false;
        const auto row = rows[i].toObject();
        if (row.size() != 2 || row["id"].toString() != "row_" + SwString::number(i) ||
            !row["double_angle"].isDouble() ||
            row["double_angle"].toDouble() != (i + 0.5 + iteration * 0.125) * 2)
            return false;
    }
    return true;
}

SwJsonObject statistics(std::vector<double> samples) {
    SwJsonObject result;
    result["samples"] = static_cast<int>(samples.size());
    if (samples.empty()) {
        result["p50_ms"] = SwJsonValue();
        result["p95_ms"] = SwJsonValue();
        result["max_ms"] = SwJsonValue();
        return result;
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double quantile) {
        return samples[static_cast<std::size_t>(std::ceil(quantile * samples.size())) - 1];
    };
    result["p50_ms"] = percentile(0.50);
    result["p95_ms"] = percentile(0.95);
    result["max_ms"] = samples.back();
    return result;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: SwRealtimeDbBenchmark [--iterations 1..1000000]\n"
                     "Measures complete evaluate() calls with a 5 ms budget and 100 changing rows.\n"
                     "Includes the first call, with no warmup. JSON timings include failed calls;\n"
                     "successful_timings reports validated results separately. Timing overruns\n"
                     "are reported, not used as an automated test assertion. CPU timing and\n"
                     "the first eight overruns help distinguish CPU work from preemption.\n";
        return 0;
    }
    try {
        const int iterations = parseIterations(argc, argv);
        const SwRealtimeDbJsEvaluator evaluator(budgetMs);
        const SwString script =
            "return tables['gimbal.recent'].map(function(row) {"
            "return {id:row.id,double_angle:row.angle*2};});";
        std::vector<double> durations;
        std::vector<double> cpuDurations;
        std::vector<double> successfulDurations;
        durations.reserve(iterations);
        cpuDurations.reserve(iterations);
        successfulDurations.reserve(iterations);
        const CpuClock cpuClock;
        SwJsonArray overBudgetDetails;
        int executionErrors = 0;
        int outputErrors = 0;
        int overBudget = 0;
        SwString firstError;
        for (int iteration = 0; iteration < iterations; ++iteration) {
            // Fixture construction and result validation are outside the timer.
            // Input values change each time so a result cache cannot pass this workload.
            const auto tables = inputs(iteration);
            SwJsonArray rows;
            // Both CPU-clock reads are outside the elapsed-time interval.
            const double cpuStart = cpuClock.milliseconds();
            const auto start = Clock::now();
            Clock::time_point end;
            double cpuEnd;
            bool success = false;
            SwString iterationError;
            try {
                rows = evaluator.evaluate(script, tables);
                end = Clock::now();
                cpuEnd = cpuClock.milliseconds();
                success = true;
            } catch (const std::exception& error) {
                end = Clock::now();
                cpuEnd = cpuClock.milliseconds();
                ++executionErrors;
                iterationError = error.what();
            }
            const double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
            const double cpuMs = cpuEnd - cpuStart;
            durations.push_back(elapsedMs);
            cpuDurations.push_back(cpuMs);
            if (elapsedMs > budgetMs) ++overBudget;
            if (success && !validOutput(rows, iteration)) {
                success = false;
                ++outputErrors;
                iterationError = "incorrect transformed output";
            }
            if (firstError.isEmpty() && !iterationError.isEmpty()) firstError = iterationError;
            if (success) successfulDurations.push_back(elapsedMs);
            if (elapsedMs > budgetMs && overBudgetDetails.size() < 8) {
                SwJsonObject detail;
                detail["iteration"] = iteration + 1;
                detail["elapsed_ms"] = elapsedMs;
                detail["cpu_ms"] = cpuMs;
                detail["error"] = iterationError;
                overBudgetDetails.append(detail);
            }
        }
        auto report = statistics(durations);
        report["benchmark"] = "js_evaluate_100_rows";
        report["build_type"] = SW_RTDB_BENCHMARK_BUILD_TYPE;
        report["iterations"] = iterations;
        report["input_rows"] = rowCount;
        report["expected_output_rows"] = rowCount;
        report["input_changes_each_iteration"] = true;
        report["warmup_iterations"] = 0;
        report["budget_ms"] = budgetMs;
        report["timing_scope"] = "complete evaluate() call, steady_clock elapsed time";
        report["over_budget"] = overBudget;
        report["over_budget_details"] = overBudgetDetails;
        report["over_budget_details_limit"] = 8;
        report["cpu_clock"] = cpuClock.name();
        report["cpu_timings"] = statistics(std::move(cpuDurations));
        report["errors"] = executionErrors + outputErrors;
        report["execution_errors"] = executionErrors;
        report["output_errors"] = outputErrors;
        report["first_error"] = firstError;
        report["successful_timings"] = statistics(std::move(successfulDurations));
        std::cout << report.toJsonString().toStdString() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
