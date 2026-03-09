#include "PnRenderManagerNode.h"

#include "PnUtils.h"

#include "SwDebug.h"
#include "SwTimer.h"

static int clampPercent_(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

static SwByteArray blendFrames_(const SwByteArray& capture,
                                const SwByteArray& algo,
                                int blendPercent) {
    SwByteArray output(capture.size(), '\0');
    const int algoWeight = clampPercent_(blendPercent);
    const int captureWeight = 100 - algoWeight;

    for (size_t i = 0; i < capture.size(); ++i) {
        const int c = static_cast<int>(static_cast<unsigned char>(capture[i]));
        const int a = static_cast<int>(static_cast<unsigned char>(algo[i]));
        const int mixed = (captureWeight * c + algoWeight * a) / 100;
        output[i] = static_cast<char>(mixed);
    }

    return output;
}

PnRenderManagerNode::PnRenderManagerNode(const SwString& sysName,
                                         const SwString& nameSpace,
                                         const SwString& objectName,
                                         SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent) {
    ipcRegisterConfig(SwString, capturePeer_, "capture", SwString("pn/pn/capture"),
                      [this](const SwString&) { reconnectCapture_(); });
    ipcRegisterConfig(SwString, algoPeer_, "algo", SwString("pn/pn/algoManager"),
                      [this](const SwString&) { reconnectAlgo_(); });
    ipcRegisterConfig(int, blendPercent_, "blend_percent", 65,
                      [this](const int&) { sanitizeConfig_(); });

    sanitizeConfig_();
    reconnectCapture_();
    reconnectAlgo_();
}

PnRenderManagerNode::~PnRenderManagerNode() {
    if (captureConn_) {
        delete captureConn_;
        captureConn_ = nullptr;
    }
    if (algoConn_) {
        delete algoConn_;
        algoConn_ = nullptr;
    }
}

void PnRenderManagerNode::sanitizeConfig_() {
    blendPercent_ = clampPercent_(blendPercent_);
}

void PnRenderManagerNode::reconnectCapture_() {
    if (captureConn_) {
        delete captureConn_;
        captureConn_ = nullptr;
    }
    if (capturePeer_.isEmpty()) return;

    captureConn_ = ipcConnect(capturePeer_, "frame", this,
                              [this](int seq, int width, int height, SwByteArray rgb) {
                                  onCaptureFrame_(seq, width, height, rgb);
                              },
                              /*fireInitial=*/false);

    if (!captureConn_) {
        swWarning() << "[PnRenderManagerNode] ipcConnect failed (capture/frame), retrying...";
        SwTimer::singleShot(500, this, &PnRenderManagerNode::reconnectCapture_);
        return;
    }

    swDebug() << "[PnRenderManagerNode] connected to " << capturePeer_ << "#frame";
}

void PnRenderManagerNode::reconnectAlgo_() {
    if (algoConn_) {
        delete algoConn_;
        algoConn_ = nullptr;
    }
    if (algoPeer_.isEmpty()) return;

    algoConn_ = ipcConnect(algoPeer_, "frame", this,
                           [this](int seq, int width, int height, SwByteArray rgb) {
                               onAlgoFrame_(seq, width, height, rgb);
                           },
                           /*fireInitial=*/false);

    if (!algoConn_) {
        swWarning() << "[PnRenderManagerNode] ipcConnect failed (algo/frame), retrying...";
        SwTimer::singleShot(500, this, &PnRenderManagerNode::reconnectAlgo_);
        return;
    }

    swDebug() << "[PnRenderManagerNode] connected to " << algoPeer_ << "#frame";
}

void PnRenderManagerNode::onCaptureFrame_(int seq, int width, int height, const SwByteArray& rgb) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    if (rgb.size() != expectedSize) {
        swWarning() << "[PnRenderManagerNode] invalid capture frame size";
        return;
    }

    lastCaptureSeq_ = seq;
    lastCaptureWidth_ = width;
    lastCaptureHeight_ = height;
    lastCaptureFrame_ = rgb;

    publishRender_(seq, width, height, rgb);
}

void PnRenderManagerNode::onAlgoFrame_(int seq, int width, int height, const SwByteArray& rgb) {
    if (width <= 0 || height <= 0) {
        return;
    }
    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    if (rgb.size() != expectedSize) {
        swWarning() << "[PnRenderManagerNode] invalid algo frame size";
        return;
    }

    lastAlgoSeq_ = seq;
    lastAlgoWidth_ = width;
    lastAlgoHeight_ = height;
    lastAlgoFrame_ = rgb;

    if (lastCaptureSeq_ == seq &&
        lastCaptureWidth_ == width &&
        lastCaptureHeight_ == height &&
        !lastCaptureFrame_.isEmpty()) {
        publishRender_(seq, width, height, lastCaptureFrame_);
    }
}

void PnRenderManagerNode::publishRender_(int seq, int width, int height, const SwByteArray& captureRgb) {
    sanitizeConfig_();

    SwByteArray out = captureRgb;
    if (lastAlgoSeq_ == seq &&
        lastAlgoWidth_ == width &&
        lastAlgoHeight_ == height &&
        lastAlgoFrame_.size() == captureRgb.size()) {
        out = blendFrames_(captureRgb, lastAlgoFrame_, blendPercent_);
    }

    const bool ok = emit frame(seq, width, height, out);
    if (!ok) {
        swWarning() << "[PnRenderManagerNode] publish failed (frame)";
        return;
    }

    if ((seq % 30) == 0) {
        swDebug() << "[PnRenderManagerNode]" << PnUtils::banner(objectName())
                  << "frame seq=" << seq << " blend=" << blendPercent_;
    }
}
