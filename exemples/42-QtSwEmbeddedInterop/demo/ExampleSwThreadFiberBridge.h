#pragma once

#include <functional>
#include <memory>

#include "SwString.h"

class ExampleSwThreadFiberBridge final {
public:
    using StatusSink = std::function<void(const SwString&)>;

    explicit ExampleSwThreadFiberBridge(StatusSink statusSink);
    ~ExampleSwThreadFiberBridge();

    bool start();
    void requestFiberRoundTrip(const SwString& origin, const SwString& payload);
    void shutdown();
    bool isRunning() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
