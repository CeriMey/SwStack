#pragma once

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 ***************************************************************************************************/

/**
 * @file SwWidgetPerfTrace.h
 * @brief Zero-overhead GUI widget performance tracing, activated by SW_WIDGET_PERF_TRACE=1.
 *
 * Usage
 * -----
 *   Set  SW_WIDGET_PERF_TRACE=1  before launching the process to enable collection.
 *   Call sw::gui::perf::dump()   at any point to print the collected metrics to stdout.
 *   Call sw::gui::perf::reset()  to clear metrics between test phases.
 *
 * When the env var is absent or "0" every macro and function call compiles to nothing.
 *
 * Macros
 * ------
 *   SW_WIDGET_PERF_SCOPE("paintEvent")   — RAII scope timer: records elapsed ms on destruction.
 *   SW_WIDGET_PERF_COUNT("update")       — increments a named counter without timing overhead.
 */

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <cstdio>  // _dupenv_s
#endif

namespace sw {
namespace gui {
namespace perf {

struct Metric {
    unsigned long long count{0};
    double totalMs{0.0};
    double maxMs{0.0};
};

inline bool enabled() {
    static const bool value = []() {
#ifdef _WIN32
        char* raw = nullptr;
        std::size_t rawSize = 0;
        const errno_t status = _dupenv_s(&raw, &rawSize, "SW_WIDGET_PERF_TRACE");
        const bool on = status == 0 && raw && raw[0] != '\0' && std::string(raw) != "0";
        if (raw) {
            std::free(raw);
        }
        return on;
#else
        const char* raw = std::getenv("SW_WIDGET_PERF_TRACE");
        return raw && raw[0] != '\0' && std::string(raw) != "0";
#endif
    }();
    return value;
}

inline std::map<std::string, Metric>& metrics_() {
    static std::map<std::string, Metric> m;
    return m;
}

inline std::mutex& mutex_() {
    static std::mutex m;
    return m;
}

/** Record an elapsed duration for a named metric. */
inline void addSample(const char* name, double elapsedMs) {
    if (!enabled() || !name || !*name) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_());
    Metric& metric = metrics_()[std::string(name)];
    metric.count += 1;
    metric.totalMs += elapsedMs;
    if (elapsedMs > metric.maxMs) {
        metric.maxMs = elapsedMs;
    }
}

/** Increment a named counter without recording time (for high-frequency events). */
inline void addCount(const char* name) {
    if (!enabled() || !name || !*name) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_());
    metrics_()[std::string(name)].count += 1;
}

inline void reset() {
    if (!enabled()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_());
    metrics_().clear();
}

inline Metric metric(const char* name) {
    if (!enabled() || !name || !*name) {
        return {};
    }
    std::lock_guard<std::mutex> lock(mutex_());
    const std::map<std::string, Metric>& allMetrics = metrics_();
    const std::map<std::string, Metric>::const_iterator it = allMetrics.find(std::string(name));
    if (it == allMetrics.end()) {
        return {};
    }
    return it->second;
}

inline void dump(std::ostream& stream = std::cout) {
    if (!enabled()) {
        return;
    }

    struct Row {
        std::string name;
        Metric      metric;
    };

    std::vector<Row> rows;
    {
        std::lock_guard<std::mutex> lock(mutex_());
        for (auto it = metrics_().begin(); it != metrics_().end(); ++it) {
            rows.push_back({it->first, it->second});
        }
    }

    // Sort: highest total time first, then alphabetical.
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.metric.totalMs != b.metric.totalMs) {
            return a.metric.totalMs > b.metric.totalMs;
        }
        return a.name < b.name;
    });

    stream << "[SwWidgetPerf] ---- begin ----\n";
    for (const Row& row : rows) {
        const Metric& m  = row.metric;
        const double  avg = m.count > 0 ? m.totalMs / static_cast<double>(m.count) : 0.0;
        stream << std::fixed << std::setprecision(3)
               << "[SwWidgetPerf]  " << row.name
               << "  count="    << m.count
               << "  total-ms=" << m.totalMs
               << "  avg-ms="   << avg
               << "  max-ms="   << m.maxMs
               << "\n";
    }
    stream << "[SwWidgetPerf] ---- end ----\n";
}

/** RAII scope timer — records name+duration when it goes out of scope. */
class ScopeTimer {
public:
    explicit ScopeTimer(const char* name)
        : m_name(name), m_start(std::chrono::steady_clock::now()) {}

    ~ScopeTimer() {
        if (!enabled()) {
            return;
        }
        const auto  end       = std::chrono::steady_clock::now();
        const double elapsedMs =
            std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                end - m_start).count();
        addSample(m_name, elapsedMs);
    }

private:
    const char*                          m_name;
    std::chrono::steady_clock::time_point m_start;
};

} // namespace perf
} // namespace gui
} // namespace sw

// ── Macros ────────────────────────────────────────────────────────────────────

/** Time the current scope.  @p name must be a string literal. */
#define SW_WIDGET_PERF_SCOPE(name) \
    sw::gui::perf::ScopeTimer swPerfTimer_##__LINE__(name)

/** Increment a named event counter (no time recorded). */
#define SW_WIDGET_PERF_COUNT(name) \
    sw::gui::perf::addCount(name)
