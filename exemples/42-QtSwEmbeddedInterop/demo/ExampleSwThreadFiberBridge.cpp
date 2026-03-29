#include "ExampleSwThreadFiberBridge.h"

#include <atomic>
#include <utility>

#include "SwCoreApplication.h"
#include "SwObject.h"
#include "SwThread.h"
#include "SwTimer.h"

namespace {

class SwThreadBackedFiberWorker final : public SwObject {
    SW_OBJECT(SwThreadBackedFiberWorker, SwObject)

public:
    explicit SwThreadBackedFiberWorker(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    void initializeRuntime() {
        ensureInitialized_();
        publishStatus_(SwString("SwThread ready | heartbeat active"));
    }

    void shutdownRuntime() {
        if (shuttingDown_) {
            return;
        }

        shuttingDown_ = true;
        if (heartbeatTimer_) {
            heartbeatTimer_->stop();
        }
    }

    void requestFiberRoundTrip(const SwString& origin, const SwString& payload) {
        if (shuttingDown_) {
            return;
        }

        ensureInitialized_();

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            publishStatus_(SwString("%1 worker runtime unavailable").arg(origin));
            return;
        }

        const int jobIndex = nextJobIndex_++;
        const int waitId = 400000 + jobIndex;
        const SwString safePayload = payload.isEmpty() ? SwString("no payload") : payload;

        publishStatus_(SwString("%1 queued fiber #%2").arg(origin).arg(jobIndex));

        app->postEventOnLane([this, origin, safePayload, jobIndex, waitId]() {
            if (shuttingDown_) {
                return;
            }

            publishStatus_(SwString("%1 fiber #%2 begin").arg(origin).arg(jobIndex));

            SwCoreApplication* workerApp = SwCoreApplication::instance(false);
            if (workerApp) {
                workerApp->addTimer([waitId]() {
                    SwCoreApplication::unYieldFiberHighPriority(waitId);
                },
                                    160 * 1000,
                                    true,
                                    SwFiberLane::Control);
            }

            SwCoreApplication::yieldFiber(waitId);

            if (shuttingDown_) {
                return;
            }

            ++completedFiberCount_;
            publishStatus_(SwString("%1 fiber #%2 resumed | msg=%3 | done=%4 | beat=%5")
                               .arg(origin)
                               .arg(jobIndex)
                               .arg(safePayload)
                               .arg(completedFiberCount_)
                               .arg(heartbeatCount_));
        }, SwFiberLane::Background);
    }

signals:
    DECLARE_SIGNAL(runtimeStatusChanged, const SwString&);

private:
    void ensureInitialized_() {
        if (initialized_) {
            return;
        }

        initialized_ = true;
        heartbeatTimer_ = new SwTimer(700, this);
        SwObject::connect(heartbeatTimer_, &SwTimer::timeout, this, [this]() {
            if (shuttingDown_) {
                return;
            }

            ++heartbeatCount_;
            if ((heartbeatCount_ % 4) == 0) {
                publishStatus_(SwString("SwThread heartbeat #%1 | fibers=%2")
                                   .arg(heartbeatCount_)
                                   .arg(completedFiberCount_));
            }
        });
        heartbeatTimer_->start();
    }

    void publishStatus_(const SwString& text) {
        runtimeStatusChanged(text);
    }

    SwTimer* heartbeatTimer_{nullptr};
    bool initialized_{false};
    bool shuttingDown_{false};
    int heartbeatCount_{0};
    int completedFiberCount_{0};
    int nextJobIndex_{1};
};

} // namespace

class ExampleSwThreadFiberBridge::Impl {
public:
    explicit Impl(StatusSink statusSink)
        : statusSink_(std::move(statusSink))
        , runtimeThread_("QtSwFiberWorker") {
        acceptStatus_.store(true);
    }

    ~Impl() {
        shutdown();
    }

    bool start() {
        if (worker_ && runtimeThread_.isRunning()) {
            return true;
        }

        if (worker_) {
            delete worker_;
            worker_ = nullptr;
        }

        acceptStatus_.store(true);
        worker_ = new SwThreadBackedFiberWorker();
        SwObject::connect(worker_, &SwThreadBackedFiberWorker::runtimeStatusChanged, worker_, [this](const SwString& text) {
            if (acceptStatus_.load() && statusSink_) {
                statusSink_(text);
            }
        });

        if (!runtimeThread_.start()) {
            delete worker_;
            worker_ = nullptr;
            return false;
        }

        worker_->moveToThread(&runtimeThread_);
        runtimeThread_.postTask([worker = worker_]() {
            if (SwObject::isLive(worker)) {
                worker->initializeRuntime();
            }
        });
        started_ = true;
        return true;
    }

    void requestFiberRoundTrip(const SwString& origin, const SwString& payload) {
        if (!started_ || !worker_ || !runtimeThread_.isRunning()) {
            return;
        }

        runtimeThread_.postTask([worker = worker_, origin, payload]() {
            if (SwObject::isLive(worker)) {
                worker->requestFiberRoundTrip(origin, payload);
            }
        });
    }

    void shutdown() {
        if (!worker_) {
            return;
        }

        SwThreadBackedFiberWorker* worker = worker_;
        worker_ = nullptr;
        started_ = false;
        acceptStatus_.store(false);

        if (runtimeThread_.isRunning()) {
            runtimeThread_.postTask([worker]() {
                if (SwObject::isLive(worker)) {
                    worker->shutdownRuntime();
                    delete worker;
                }
            });
            runtimeThread_.quit();
            runtimeThread_.wait();
        } else {
            delete worker;
        }
    }

    bool isRunning() const {
        return runtimeThread_.isRunning();
    }

private:
    StatusSink statusSink_;
    std::atomic_bool acceptStatus_{false};
    SwThread runtimeThread_;
    SwThreadBackedFiberWorker* worker_{nullptr};
    bool started_{false};
};

ExampleSwThreadFiberBridge::ExampleSwThreadFiberBridge(StatusSink statusSink)
    : impl_(new Impl(std::move(statusSink))) {
}

ExampleSwThreadFiberBridge::~ExampleSwThreadFiberBridge() = default;

bool ExampleSwThreadFiberBridge::start() {
    return impl_->start();
}

void ExampleSwThreadFiberBridge::requestFiberRoundTrip(const SwString& origin, const SwString& payload) {
    impl_->requestFiberRoundTrip(origin, payload);
}

void ExampleSwThreadFiberBridge::shutdown() {
    impl_->shutdown();
}

bool ExampleSwThreadFiberBridge::isRunning() const {
    return impl_->isRunning();
}
