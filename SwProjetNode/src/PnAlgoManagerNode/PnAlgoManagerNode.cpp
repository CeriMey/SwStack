#include "PnAlgoManagerNode.h"

#include "PnUtils.h"

#include "SwDebug.h"
#include "SwTimer.h"

static int clampThreshold_(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return value;
}

static SwString normalizeMode_(const SwString& rawMode) {
    const SwString mode = rawMode.trimmed().toLower();
    if (mode == "invert") {
        return "invert";
    }
    return "threshold";
}

static SwByteArray runAlgorithm_(const SwByteArray& source,
                                 const SwString& mode,
                                 int threshold) {
    SwByteArray output(source.size(), '\0');

    for (size_t i = 0; i + 2 < source.size(); i += 3) {
        const int r = static_cast<int>(static_cast<unsigned char>(source[i]));
        const int g = static_cast<int>(static_cast<unsigned char>(source[i + 1]));
        const int b = static_cast<int>(static_cast<unsigned char>(source[i + 2]));

        if (mode == "invert") {
            output[i] = static_cast<char>(255 - r);
            output[i + 1] = static_cast<char>(255 - g);
            output[i + 2] = static_cast<char>(255 - b);
            continue;
        }

        const int gray = (r + g + b) / 3;
        const int bw = (gray >= threshold) ? 255 : 0;
        output[i] = static_cast<char>(bw);
        output[i + 1] = static_cast<char>(bw);
        output[i + 2] = static_cast<char>(bw);
    }

    return output;
}

PnAlgoManagerNode::PnAlgoManagerNode(const SwString& sysName,
                                     const SwString& nameSpace,
                                     const SwString& objectName,
                                     SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent) {
    ipcRegisterConfig(SwString, capturePeer_, "capture", SwString("pn/pn/capture"), [this](const SwString&) { reconnect_(); });
    ipcRegisterConfig(SwString, mode_, "mode", SwString("threshold"), [this](const SwString&) { sanitizeConfig_(); });
    ipcRegisterConfig(int, threshold_, "threshold", 120, [this](const int&) { sanitizeConfig_(); });

    sanitizeConfig_();
    reconnect_();
}

PnAlgoManagerNode::~PnAlgoManagerNode() {
    if (captureConn_) {
        delete captureConn_;
        captureConn_ = nullptr;
    }
}

void PnAlgoManagerNode::sanitizeConfig_() {
    mode_ = normalizeMode_(mode_);
    threshold_ = clampThreshold_(threshold_);
}

void PnAlgoManagerNode::reconnect_() {
    if (captureConn_) {
        delete captureConn_;
        captureConn_ = nullptr;
    }
    if (capturePeer_.isEmpty()) return;

    captureConn_ = ipcConnect(capturePeer_, "frame", this,
                              [this](int seq, int width, int height, SwByteArray rgb) {
                                  processFrame_(seq, width, height, rgb);
                              },
                              /*fireInitial=*/false);

    if (!captureConn_) {
        swWarning() << "[PnAlgoManagerNode] ipcConnect failed (capture/frame), retrying...";
        SwTimer::singleShot(500, this, &PnAlgoManagerNode::reconnect_);
        return;
    }

    swDebug() << "[PnAlgoManagerNode] connected to " << capturePeer_ << "#frame";
}

void PnAlgoManagerNode::processFrame_(int seq, int width, int height, const SwByteArray& rgb) {
    sanitizeConfig_();

    if (width <= 0 || height <= 0) {
        return;
    }

    const size_t expectedSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    if (rgb.size() != expectedSize) {
        swWarning() << "[PnAlgoManagerNode] invalid frame size, got=" << static_cast<long long>(rgb.size())
                    << " expected=" << static_cast<long long>(expectedSize);
        return;
    }

    const SwByteArray processed = runAlgorithm_(rgb, mode_, threshold_);
    const bool ok = emit frame(seq, width, height, processed);
    if (!ok) {
        swWarning() << "[PnAlgoManagerNode] publish failed (frame)";
        return;
    }

    if ((seq % 30) == 0) {
        swDebug() << "[PnAlgoManagerNode]" << PnUtils::banner(objectName())
                  << "frame seq=" << seq << " mode=" << mode_ << " threshold=" << threshold_;
    }
}
