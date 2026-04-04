#pragma once

#include "SwFrame.h"
#include "SocketTrafficViewTypes.h"

class SocketTrafficConsumerTimelineWidget : public SwFrame {
    SW_OBJECT(SocketTrafficConsumerTimelineWidget, SwFrame)

public:
    explicit SocketTrafficConsumerTimelineWidget(SwWidget* parent = nullptr);

    virtual SwSize sizeHint() const override;
    virtual SwSize minimumSizeHint() const override;

    void setLaunchTimeNs(long long launchTimeNs);
    void setHistory(const SwList<SocketTrafficDashboardHistoryPoint>* history);
    void setSelectedConsumerLabel(const SwString& label);
    void setXRange(double minimum, double maximum);
    void setYRange(double maximumBytesPerSecond);

protected:
    void paintEvent(PaintEvent* event) override;

private:
    void drawGrid_(SwPainter* painter,
                   const SwRect& plot,
                   int xTicks,
                   int yTicks,
                   const SwColor& gridColor) const;
    void drawAxisLabels_(SwPainter* painter,
                         const SwRect& plot,
                         int xTicks,
                         int yTicks,
                         const SwColor& textColor,
                         const SwFont& font) const;
    void drawLegend_(SwPainter* painter, const SwRect& bounds, const SwFont& font) const;
    void drawSeries_(SwPainter* painter,
                     const SwRect& plot,
                     const SwColor& color,
                     int lineWidth,
                     int seriesKind) const;
    double secondsSinceLaunch_(long long sampleTimeNs) const;
    int xForSeconds_(double seconds, const SwRect& plot) const;
    int yForRate_(double bytesPerSecond, const SwRect& plot) const;
    SwString rateTickText_(double bytesPerSecond) const;

    const SwList<SocketTrafficDashboardHistoryPoint>* history_{nullptr};
    long long launchTimeNs_{0};
    double xMinSeconds_{0.0};
    double xMaxSeconds_{10.0};
    double yMaxBytesPerSecond_{1024.0};
    SwString selectedConsumerLabel_;
};
