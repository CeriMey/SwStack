#pragma once

#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"

class DemoSubscriber : public SwRemoteObject {
 public:
    DemoSubscriber(const SwString& sysName,
                   const SwString& nameSpace,
                   const SwString& objectName,
                   SwObject* parent = nullptr);

    void start();

    int exposure() const { return exposure_; }
    const SwString& modeName() const { return modeName_; }
    const SwString& modeProfile() const { return modeProfile_; }

 private:
    SW_IPC_SIGNAL_SIZED(ping, 4096, int, SwString);
    SW_IPC_SIGNAL_SIZED(pong, 4096, int, SwString);
    SW_IPC_SIGNAL_SIZED(configAck, 4096, uint64_t, SwString);

    int add(int a, int b) const;
    SwString who(const sw::ipc::RpcContext& ctx) const;
    SwString hello(const sw::ipc::RpcContext& ctx, const SwString& name) const;
    void notify(const sw::ipc::RpcContext& ctx, const SwString& msg) const;

    // Extra signals to demonstrate introspection + different argument types.
    SW_IPC_SIGNAL(alarm, bool);
    SW_IPC_SIGNAL_SIZED(note, 4096, SwString);
    SW_IPC_LATCH(setPoint, double);
    SW_IPC_LATCH(vec3, int, int, int);
    SW_IPC_LATCH_SIZED(telemetry, 4096, int, double, SwString);
    SW_IPC_SIGNAL_SIZED(blob, 4096, SwByteArray);
    SW_IPC_LATCH_SIZED(identity, 4096, uint64_t, SwString, SwString);
    SW_IPC_SIGNAL_SIZED(triple, 4096, bool, int, SwString);

    int exposure_{0};
    int brightness_{10};
    bool enabled_{true};
    double gain_{1.0};
    int imageWidth_{1920};
    int imageHeight_{1080};
    SwString modeName_{"auto"};
    SwString modeProfile_{"standard"};

    // Complex Sw* container configs
    SwStringList tags_{};
    SwList<int> histogram_{};
    SwAnyList anyList_{};
    SwMap<SwString, int> thresholds_{};
    SwMap<SwString, SwAny> anyMap_{};
};
