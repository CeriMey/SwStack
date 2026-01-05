#pragma once

#include "SwWidget.h"
#include "media/SwVideoFrame.h"

#include <mutex>

class IpcRingBufferVideoWidget : public SwWidget {
public:
    enum class ScalingMode {
        Fit,
        Stretch,
    };

    explicit IpcRingBufferVideoWidget(SwWidget* parent = nullptr);

    void setScalingMode(ScalingMode mode) { scalingMode_ = mode; }
    ScalingMode scalingMode() const { return scalingMode_; }

    void setFrame(SwVideoFrame frame);

protected:
    void paintEvent(PaintEvent* event) override;

private:
    SwRect computeTargetRect_(const SwRect& bounds, const SwVideoFrame& frame) const;
    bool renderBGRA_(SwPainter* painter, const SwVideoFrame& frame, const SwRect& targetRect);
    void drawPlaceholder_(SwPainter* painter, const SwRect& rect);

    std::mutex frameMutex_;
    SwVideoFrame frame_;
    ScalingMode scalingMode_{ScalingMode::Fit};
};

