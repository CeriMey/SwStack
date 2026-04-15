#pragma once

/**
 * @file src/core/runtime/SwFutureWatcher.h
 * @ingroup core_runtime
 * @brief Qt-like watcher for SwFuture state changes.
 */

#include "SwFuture.h"
#include "SwObject.h"
#include "SwPointer.h"
#include "SwThread.h"

template<typename T>
class SwFutureWatcher : public SwObject {
    SW_OBJECT(SwFutureWatcher, SwObject)
    DECLARE_SIGNAL_VOID(started)
    DECLARE_SIGNAL_VOID(finished)
    DECLARE_SIGNAL(failed, const SwString&)
    DECLARE_SIGNAL_VOID(canceled)
    DECLARE_SIGNAL_VOID(suspended)
    DECLARE_SIGNAL_VOID(resumed)
    DECLARE_SIGNAL(resultReadyAt, int)
    DECLARE_SIGNAL(resultsReadyAt, int, int)
    DECLARE_SIGNAL(progressRangeChanged, int, int)
    DECLARE_SIGNAL(progressValueChanged, int)
    DECLARE_SIGNAL(progressTextChanged, const SwString&)
    DECLARE_SIGNAL(progressValueAndTextChanged, int, const SwString&)

public:
    explicit SwFutureWatcher(SwObject* parent = nullptr)
        : SwObject(parent) {
    }

    ~SwFutureWatcher() {
        detach_();
    }

    void setFuture(const SwFuture<T>& future) {
        detach_();
        m_future = future;
        m_observerId = m_future.addObserver_([this](const Notification& notification) {
            dispatchNotification_(notification);
        }, true);
    }

    SwFuture<T> future() const {
        return m_future;
    }

private:
    typedef sw::detail::SwFutureNotification Notification;

    void detach_() {
        if (m_observerId != 0) {
            m_future.removeObserver_(m_observerId);
            m_observerId = 0;
        }
    }

    void dispatchNotification_(const Notification& notification) {
        SwPointer<SwFutureWatcher<T> > self(this);
        std::function<void()> task = [self, notification]() {
            if (!self) {
                return;
            }
            self->handleNotification_(notification);
        };

        SwThread* targetThread = thread();
        if (targetThread && targetThread != SwThread::currentThread()) {
            if (targetThread->postTask(task)) {
                return;
            }
        }

        task();
    }

    void handleNotification_(const Notification& notification) {
        switch (notification.type) {
        case sw::detail::SwFutureNotificationType::Started:
            started();
            break;
        case sw::detail::SwFutureNotificationType::Finished:
            finished();
            break;
        case sw::detail::SwFutureNotificationType::Failed:
            failed(notification.progressText);
            break;
        case sw::detail::SwFutureNotificationType::Canceled:
            canceled();
            break;
        case sw::detail::SwFutureNotificationType::Suspended:
            suspended();
            break;
        case sw::detail::SwFutureNotificationType::Resumed:
            resumed();
            break;
        case sw::detail::SwFutureNotificationType::ResultReadyAt:
            resultReadyAt(notification.firstIndex);
            break;
        case sw::detail::SwFutureNotificationType::ResultsReadyAt:
            resultsReadyAt(notification.firstIndex, notification.lastIndex);
            break;
        case sw::detail::SwFutureNotificationType::ProgressRangeChanged:
            progressRangeChanged(notification.progressMinimum, notification.progressMaximum);
            break;
        case sw::detail::SwFutureNotificationType::ProgressValueChanged:
            progressValueChanged(notification.progressValue);
            break;
        case sw::detail::SwFutureNotificationType::ProgressTextChanged:
            progressTextChanged(notification.progressText);
            progressValueAndTextChanged(notification.progressValue, notification.progressText);
            break;
        }
    }

    SwFuture<T> m_future;
    int m_observerId{0};
};
