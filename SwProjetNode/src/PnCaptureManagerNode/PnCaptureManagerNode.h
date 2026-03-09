#pragma once

#include "SwByteArray.h"
#include "SwMutex.h"
#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"

class SwTimer;
class SwMediaFoundationVideoSource;
class SwVideoPacket;

class PnCaptureManagerNode : public SwRemoteObject {
public:
    PnCaptureManagerNode(const SwString& sysName,
                         const SwString& nameSpace,
                         const SwString& objectName,
                         SwObject* parent = nullptr);
    ~PnCaptureManagerNode() override;

private:
    void retime_();
    void sanitizeGeometry_();
    void restartCamera_();
    void onCameraPacket_(const SwVideoPacket& packet);
    static SwByteArray downscaleBgraToRgb_(const SwByteArray& bgra,
                                           int srcWidth,
                                           int srcHeight,
                                           int dstWidth,
                                           int dstHeight);
    static SwByteArray makeSyntheticRgb_(int seq, int width, int height);
    void publishFrame_();

    SW_REGISTER_SHM_SIGNAL(frame, int, int, int, SwByteArray);

    int periodMs_{100};
    int width_{32};
    int height_{32};
    int deviceIndex_{-1};
    int seq_{0};
    SwTimer* timer_{nullptr};
    SwMediaFoundationVideoSource* cameraSource_{nullptr};

    SwMutex frameMutex_;
    SwByteArray latestFrameRgb_;
    int latestFrameWidth_{0};
    int latestFrameHeight_{0};
    bool hasLatestFrame_{false};
    bool cameraReady_{false};
};
