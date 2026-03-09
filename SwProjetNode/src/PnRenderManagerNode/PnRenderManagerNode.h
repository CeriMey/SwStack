#pragma once

#include "SwByteArray.h"
#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"

class PnRenderManagerNode : public SwRemoteObject {
public:
    PnRenderManagerNode(const SwString& sysName,
                        const SwString& nameSpace,
                        const SwString& objectName,
                        SwObject* parent = nullptr);

    ~PnRenderManagerNode() override;

private:
    void reconnectCapture_();
    void reconnectAlgo_();
    void sanitizeConfig_();
    void onCaptureFrame_(int seq, int width, int height, const SwByteArray& rgb);
    void onAlgoFrame_(int seq, int width, int height, const SwByteArray& rgb);
    void publishRender_(int seq, int width, int height, const SwByteArray& captureRgb);

    SW_REGISTER_SHM_SIGNAL(frame, int, int, int, SwByteArray);

    SwString capturePeer_{SwString("pn/pn/capture")};
    SwString algoPeer_{SwString("pn/pn/algoManager")};
    int blendPercent_{65};

    SwObject* captureConn_{nullptr};
    SwObject* algoConn_{nullptr};

    int lastCaptureSeq_{-1};
    int lastCaptureWidth_{0};
    int lastCaptureHeight_{0};
    SwByteArray lastCaptureFrame_;

    int lastAlgoSeq_{-1};
    int lastAlgoWidth_{0};
    int lastAlgoHeight_{0};
    SwByteArray lastAlgoFrame_;
};
