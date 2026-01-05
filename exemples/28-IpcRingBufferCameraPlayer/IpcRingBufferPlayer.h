#pragma once

#include "SwIpcNoCopyRingBuffer.h"

#include "IpcRingBufferFrameMeta.h"

#include <atomic>
#include <cstdint>
#include <memory>

class SwLabel;
class SwMainWindow;
class SwTimer;
class IpcRingBufferVideoWidget;

class IpcRingBufferPlayer {
public:
    IpcRingBufferPlayer(int argc, char** argv);
    ~IpcRingBufferPlayer();

    void show();

private:
    using RB = sw::ipc::NoCopyRingBuffer<IpcRingBufferFrameMeta>;

    void parseArgs_(int argc, char** argv);
    void setupUi_();
    void tryConnect_();
    void onFrame_(uint64_t seq, RB::ReadLease lease);
    void updateStats_();

    SwString domain_{"demo"};
    SwString object_{"camera"};
    SwString stream_{"video"};

    std::unique_ptr<sw::ipc::Registry> reg_;
    RB rb_;
    RB::Consumer consumer_;
    sw::ipc::Signal<uint64_t>::Subscription sub_;

    std::unique_ptr<SwMainWindow> window_;
    SwLabel* infoLabel_{nullptr};
    IpcRingBufferVideoWidget* videoWidget_{nullptr};
    std::unique_ptr<SwTimer> statsTimer_;

    std::atomic<int> frames_{0};
    std::atomic<int> width_{0};
    std::atomic<int> height_{0};
    std::atomic<uint64_t> lastSeq_{0};
};
