#pragma once

#include "SwByteArray.h"
#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"

class PnAlgoManagerNode : public SwRemoteObject {
public:
    PnAlgoManagerNode(const SwString& sysName,
                      const SwString& nameSpace,
                      const SwString& objectName,
                      SwObject* parent = nullptr);

    ~PnAlgoManagerNode() override;

private:
    void reconnect_();
    void sanitizeConfig_();
    void processFrame_(int seq, int width, int height, const SwByteArray& rgb);

    SW_REGISTER_SHM_SIGNAL(frame, int, int, int, SwByteArray);

    SwString capturePeer_{SwString("pn/pn/capture")};
    SwString mode_{SwString("threshold")};
    int threshold_{120};
    SwObject* captureConn_{nullptr};
};
