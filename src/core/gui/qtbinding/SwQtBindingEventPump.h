#pragma once

#include "SwGuiApplication.h"
#include "SwWidgetPlatformAdapter.h"

#include <cstdint>
#include <limits>

class SwQtBindingEventPump {
public:
    explicit SwQtBindingEventPump(SwGuiApplication* application = nullptr)
        : application_(application) {
    }

    void setApplication(SwGuiApplication* application) {
        application_ = application;
    }

    SwGuiApplication* application() const {
        return application_;
    }

    int drainPostedWork(int maxIterations = 64, bool flushDamage = true) const {
        SwGuiApplication* app = application_ ? application_ : SwGuiApplication::instance(false);
        if (!app) {
            return -1;
        }

        std::int64_t nextDelayUs = -1;
        for (int iteration = 0; iteration < maxIterations; ++iteration) {
            nextDelayUs = app->processEvent(false);
            if (nextDelayUs != 0) {
                break;
            }
        }

        if (flushDamage) {
            SwWidgetPlatformAdapter::flushDamage();
        }
        if (nextDelayUs < 0) {
            return -1;
        }
        return nextDelayUs > (std::numeric_limits<int>::max)()
                   ? (std::numeric_limits<int>::max)()
                   : static_cast<int>(nextDelayUs);
    }

private:
    SwGuiApplication* application_{nullptr};
};
