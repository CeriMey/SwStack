#pragma once

/**
 * @file src/core/runtime/SwRuntimeApplicationMonitor.h
 * @ingroup core_runtime
 * @brief Process-local runtime observability service for all SwCoreApplication instances.
 */

#include "SwCoreApplication.h"
#include "SwRuntimeProfiler.h"
#include "SwRuntimeTelemetry.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

struct SwRuntimeApplicationTimingBatch {
    long long sampleTimeNs;
    SwRuntimeApplicationDescriptor application;
    SwList<SwRuntimeTimingRecord> records;
    SwRuntimeCountersSnapshot counters;

    SwRuntimeApplicationTimingBatch()
        : sampleTimeNs(0) {}
};

struct SwRuntimeApplicationStallEvent {
    long long sampleTimeNs;
    SwRuntimeApplicationDescriptor application;
    SwRuntimeStallReport stall;

    SwRuntimeApplicationStallEvent()
        : sampleTimeNs(0) {}
};

struct SwRuntimeApplicationMonitorConfig {
    bool captureIterationSamples;
    bool captureTimingBatches;
    bool captureStalls;
    bool profilerCaptureEnabledOnStart;
    SwRuntimeProfilerConfig profilerConfig;

    SwRuntimeApplicationMonitorConfig()
        : captureIterationSamples(true),
          captureTimingBatches(true),
          captureStalls(true),
          profilerCaptureEnabledOnStart(false),
          profilerConfig() {}
};

class SwRuntimeApplicationMonitorSink {
public:
    virtual ~SwRuntimeApplicationMonitorSink() {}

    virtual void onRuntimeApplicationAttached(const SwRuntimeApplicationDescriptor& descriptor) {
        SW_UNUSED(descriptor);
    }

    virtual void onRuntimeApplicationDetached(const SwRuntimeApplicationDescriptor& descriptor) {
        SW_UNUSED(descriptor);
    }

    virtual void onRuntimeApplicationIterationSample(const SwRuntimeApplicationIterationSample& sample) {
        SW_UNUSED(sample);
    }

    virtual void onRuntimeApplicationTimingBatch(const SwRuntimeApplicationTimingBatch& batch) {
        SW_UNUSED(batch);
    }

    virtual void onRuntimeApplicationStall(const SwRuntimeApplicationStallEvent& event) {
        SW_UNUSED(event);
    }
};

class SwRuntimeApplicationMonitor : private SwCoreApplicationRegistryObserver {
private:
    struct ApplicationState_;

public:
    explicit SwRuntimeApplicationMonitor(
        SwRuntimeApplicationMonitorSink* sink,
        const SwRuntimeApplicationMonitorConfig& config = SwRuntimeApplicationMonitorConfig())
        : sink_(sink),
          config_(config) {}

    ~SwRuntimeApplicationMonitor() {
        stop();
    }

    bool start() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (running_) {
                return true;
            }
            running_ = true;
        }

        SwCoreApplication::attachRegistryObserver(this);

        const SwList<SwCoreApplication*> apps = SwCoreApplication::instancesSnapshot();
        for (size_t i = 0; i < apps.size(); ++i) {
            if (apps[i]) {
                attachOrRefreshApplication_(apps[i], apps[i]->runtimeApplicationDescriptor());
            }
        }
        return true;
    }

    void stop() {
        std::vector<std::shared_ptr<ApplicationState_>> states;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }
            running_ = false;
            for (std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::const_iterator it = applications_.begin();
                 it != applications_.end();
                 ++it) {
                states.push_back(it->second);
            }
            applications_.clear();
        }

        SwCoreApplication::detachRegistryObserver(this);

        for (std::size_t i = 0; i < states.size(); ++i) {
            detachStateFromApplication_(states[i]);
            notifyDetached_(states[i] ? states[i]->descriptor : SwRuntimeApplicationDescriptor());
        }
    }

    bool running() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return running_;
    }

    SwList<SwRuntimeApplicationDescriptor> applicationsSnapshot() const {
        SwList<SwRuntimeApplicationDescriptor> descriptors;
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::const_iterator it = applications_.begin();
             it != applications_.end();
             ++it) {
            if (it->second) {
                descriptors.append(it->second->descriptor);
            }
        }
        return descriptors;
    }

    bool setProfilerCaptureEnabledForApplication(unsigned long long applicationId, bool enabled) {
        const std::shared_ptr<ApplicationState_> state = stateForApplication_(applicationId);
        if (!state || !state->profilerSession) {
            return false;
        }
        state->profilerSession->setEnabled(enabled);
        return true;
    }

    bool setProfilerStallThresholdUsForApplication(unsigned long long applicationId, long long thresholdUs) {
        const std::shared_ptr<ApplicationState_> state = stateForApplication_(applicationId);
        if (!state || !state->profilerSession) {
            return false;
        }
        state->profilerSession->setStallThresholdUs(thresholdUs);
        return true;
    }

private:
    class ApplicationTelemetrySession_ : public SwRuntimeTelemetrySession {
    public:
        ApplicationTelemetrySession_(SwRuntimeApplicationMonitor* monitor,
                                     unsigned long long applicationId)
            : monitor_(monitor),
              applicationId_(applicationId) {}

        void onAttached(SwCoreApplication* app) override {
            app_ = app;
        }

        void onDetached() override {
            app_ = nullptr;
        }

        void updateIterationSnapshot(const SwRuntimeIterationSnapshot& snapshot) override {
            if (monitor_) {
                monitor_->publishIterationSample_(applicationId_, app_, snapshot);
            }
        }

    private:
        SwRuntimeApplicationMonitor* monitor_{nullptr};
        unsigned long long applicationId_{0};
        SwCoreApplication* app_{nullptr};
    };

    class ApplicationProfilerSink_ : public SwRuntimeProfilerSink {
    public:
        ApplicationProfilerSink_(SwRuntimeApplicationMonitor* monitor,
                                 unsigned long long applicationId)
            : monitor_(monitor),
              applicationId_(applicationId) {}

        void setApplication(SwCoreApplication* app) {
            app_ = app;
        }

        void onRuntimeBatch(const SwList<SwRuntimeTimingRecord>& records,
                            const SwRuntimeCountersSnapshot& counters) override {
            if (monitor_) {
                monitor_->publishTimingBatch_(applicationId_, app_, records, counters);
            }
        }

        void onStall(const SwRuntimeStallReport& report) override {
            if (monitor_) {
                monitor_->publishStall_(applicationId_, app_, report);
            }
        }

    private:
        SwRuntimeApplicationMonitor* monitor_{nullptr};
        unsigned long long applicationId_{0};
        SwCoreApplication* app_{nullptr};
    };

    struct ApplicationState_ {
        SwCoreApplication* app{nullptr};
        SwRuntimeApplicationDescriptor descriptor{};
        std::shared_ptr<ApplicationTelemetrySession_> telemetrySession{};
        std::shared_ptr<ApplicationProfilerSink_> profilerSink{};
        std::shared_ptr<SwRuntimeProfilerSession> profilerSession{};
    };

    static long long nowNs_() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void onRuntimeApplicationRegistered(SwCoreApplication* app,
                                        const SwRuntimeApplicationDescriptor& descriptor) override {
        attachOrRefreshApplication_(app, descriptor);
    }

    void onRuntimeApplicationUnregistered(const SwRuntimeApplicationDescriptor& descriptor) override {
        std::shared_ptr<ApplicationState_> removedState;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::iterator it =
                applications_.find(descriptor.applicationId);
            if (it == applications_.end()) {
                return;
            }
            removedState = it->second;
            applications_.erase(it);
        }

        notifyDetached_(removedState ? removedState->descriptor : descriptor);
    }

    std::shared_ptr<ApplicationState_> stateForApplication_(unsigned long long applicationId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::const_iterator it =
            applications_.find(applicationId);
        return it == applications_.end() ? std::shared_ptr<ApplicationState_>() : it->second;
    }

    void attachOrRefreshApplication_(SwCoreApplication* app,
                                     const SwRuntimeApplicationDescriptor& descriptor) {
        if (!app) {
            return;
        }

        std::shared_ptr<ApplicationState_> state;
        bool isNewState = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }

            std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::iterator it =
                applications_.find(descriptor.applicationId);
            if (it == applications_.end()) {
                state.reset(new ApplicationState_());
                state->app = app;
                state->descriptor = descriptor;
                applications_[descriptor.applicationId] = state;
                isNewState = true;
            } else {
                state = it->second;
                if (state) {
                    state->app = app;
                    state->descriptor = descriptor;
                }
            }
        }

        if (!state) {
            return;
        }

        if (!isNewState) {
            if (state->profilerSink) {
                state->profilerSink->setApplication(app);
            }
            return;
        }

        bool attachOk = true;
        if (config_.captureIterationSamples) {
            state->telemetrySession.reset(new ApplicationTelemetrySession_(this, descriptor.applicationId));
            attachOk = app->attachTelemetrySession(state->telemetrySession);
        }

        if (attachOk && (config_.captureTimingBatches || config_.captureStalls)) {
            state->profilerSink.reset(new ApplicationProfilerSink_(this, descriptor.applicationId));
            state->profilerSink->setApplication(app);
            state->profilerSession.reset(new SwRuntimeProfilerSession(state->profilerSink.get(),
                                                                      config_.profilerConfig));
            state->profilerSession->setEnabled(config_.profilerCaptureEnabledOnStart);
            attachOk = app->attachProfilerSession(state->profilerSession);
        }

        if (!attachOk) {
            detachStateFromApplication_(state);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                applications_.erase(descriptor.applicationId);
            }
            return;
        }

        notifyAttached_(descriptor);
    }

    void detachStateFromApplication_(const std::shared_ptr<ApplicationState_>& state) {
        if (!state || !state->app) {
            return;
        }

        if (state->telemetrySession) {
            state->app->detachTelemetrySession(state->telemetrySession.get());
        }
        if (state->profilerSession) {
            state->app->detachProfilerSession(state->profilerSession.get());
        }
    }

    void notifyAttached_(const SwRuntimeApplicationDescriptor& descriptor) {
        SwRuntimeApplicationMonitorSink* sink = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sink = running_ ? sink_ : nullptr;
        }
        if (sink) {
            sink->onRuntimeApplicationAttached(descriptor);
        }
    }

    void notifyDetached_(const SwRuntimeApplicationDescriptor& descriptor) {
        SwRuntimeApplicationMonitorSink* sink = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sink = sink_;
        }
        if (sink) {
            sink->onRuntimeApplicationDetached(descriptor);
        }
    }

    void publishIterationSample_(unsigned long long applicationId,
                                 SwCoreApplication* app,
                                 const SwRuntimeIterationSnapshot& snapshot) {
        SwRuntimeApplicationMonitorSink* sink = nullptr;
        SwRuntimeApplicationDescriptor descriptor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }

            std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::iterator it =
                applications_.find(applicationId);
            if (it == applications_.end()) {
                return;
            }

            descriptor = app ? app->runtimeApplicationDescriptor() : it->second->descriptor;
            it->second->descriptor = descriptor;
            sink = sink_;
        }

        if (!sink) {
            return;
        }

        SwRuntimeApplicationIterationSample sample;
        sample.sampleTimeNs = nowNs_();
        sample.application = descriptor;
        sample.snapshot = snapshot;
        sink->onRuntimeApplicationIterationSample(sample);
    }

    void publishTimingBatch_(unsigned long long applicationId,
                             SwCoreApplication* app,
                             const SwList<SwRuntimeTimingRecord>& records,
                             const SwRuntimeCountersSnapshot& counters) {
        if (!config_.captureTimingBatches) {
            return;
        }

        SwRuntimeApplicationMonitorSink* sink = nullptr;
        SwRuntimeApplicationDescriptor descriptor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }

            std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::iterator it =
                applications_.find(applicationId);
            if (it == applications_.end()) {
                return;
            }

            descriptor = app ? app->runtimeApplicationDescriptor() : it->second->descriptor;
            it->second->descriptor = descriptor;
            sink = sink_;
        }

        if (!sink) {
            return;
        }

        SwRuntimeApplicationTimingBatch batch;
        batch.sampleTimeNs = nowNs_();
        batch.application = descriptor;
        batch.records = records;
        batch.counters = counters;
        sink->onRuntimeApplicationTimingBatch(batch);
    }

    void publishStall_(unsigned long long applicationId,
                       SwCoreApplication* app,
                       const SwRuntimeStallReport& stall) {
        if (!config_.captureStalls) {
            return;
        }

        SwRuntimeApplicationMonitorSink* sink = nullptr;
        SwRuntimeApplicationDescriptor descriptor;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return;
            }

            std::map<unsigned long long, std::shared_ptr<ApplicationState_>>::iterator it =
                applications_.find(applicationId);
            if (it == applications_.end()) {
                return;
            }

            descriptor = app ? app->runtimeApplicationDescriptor() : it->second->descriptor;
            it->second->descriptor = descriptor;
            sink = sink_;
        }

        if (!sink) {
            return;
        }

        SwRuntimeApplicationStallEvent event;
        event.sampleTimeNs = nowNs_();
        event.application = descriptor;
        event.stall = stall;
        sink->onRuntimeApplicationStall(event);
    }

    SwRuntimeApplicationMonitorSink* sink_{nullptr};
    SwRuntimeApplicationMonitorConfig config_{};
    mutable std::mutex mutex_;
    bool running_{false};
    std::map<unsigned long long, std::shared_ptr<ApplicationState_>> applications_{};
};
