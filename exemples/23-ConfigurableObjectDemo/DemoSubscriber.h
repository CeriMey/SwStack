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
    SW_REGISTER_SHM_SIGNAL(ping, int, SwString);
    SW_REGISTER_SHM_SIGNAL(pong, int, SwString);
    SW_REGISTER_SHM_SIGNAL(configAck, uint64_t, SwString);

    int add(int a, int b) const;
    SwString who(const sw::ipc::RpcContext& ctx) const;
    SwString hello(const sw::ipc::RpcContext& ctx, const SwString& name) const;
    void notify(const sw::ipc::RpcContext& ctx, const SwString& msg) const;

    // Extra signals to demonstrate introspection + different argument types.
    SW_REGISTER_SHM_SIGNAL(alarm, bool);
    SW_REGISTER_SHM_SIGNAL(note, SwString);
    SW_REGISTER_SHM_SIGNAL(setPoint, double);
    SW_REGISTER_SHM_SIGNAL(vec3, int, int, int);
    SW_REGISTER_SHM_SIGNAL(telemetry, int, double, SwString);
    SW_REGISTER_SHM_SIGNAL(blob, SwByteArray);
    SW_REGISTER_SHM_SIGNAL(identity, uint64_t, SwString, SwString);
    SW_REGISTER_SHM_SIGNAL(triple, bool, int, SwString);

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
