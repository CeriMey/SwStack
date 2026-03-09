#pragma once

#include <utility>

class SwApiSubscriptionHandle {
public:
    SwApiSubscriptionHandle() = default;
    SwApiSubscriptionHandle(const SwApiSubscriptionHandle&) = delete;
    SwApiSubscriptionHandle& operator=(const SwApiSubscriptionHandle&) = delete;

    ~SwApiSubscriptionHandle() { reset(); }

    bool isNull() const { return ptr_ == nullptr; }

    void reset() {
        if (!ptr_) return;
        if (stopFn_) stopFn_(ptr_);
        if (destroyFn_) destroyFn_(ptr_);
        ptr_ = nullptr;
        stopFn_ = nullptr;
        destroyFn_ = nullptr;
    }

    template <typename SubscriptionT>
    void emplace(SubscriptionT&& sub) {
        reset();
        SubscriptionT* p = new SubscriptionT(std::move(sub));
        ptr_ = p;
        stopFn_ = &stop_<SubscriptionT>;
        destroyFn_ = &destroy_<SubscriptionT>;
    }

private:
    template <typename SubscriptionT>
    static void stop_(void* p) {
        if (!p) return;
        static_cast<SubscriptionT*>(p)->stop();
    }

    template <typename SubscriptionT>
    static void destroy_(void* p) {
        delete static_cast<SubscriptionT*>(p);
    }

    void* ptr_{nullptr};
    void (*stopFn_)(void*){nullptr};
    void (*destroyFn_)(void*){nullptr};
};

