#pragma once

#include "SwFrame.h"
#include "RuntimeProfilerViewTypes.h"

class RuntimeProfilerStallTimelineWidget : public SwFrame {
    SW_OBJECT(RuntimeProfilerStallTimelineWidget, SwFrame)

public:
    explicit RuntimeProfilerStallTimelineWidget(SwWidget* parent = nullptr);

    virtual SwSize sizeHint() const override;
    virtual SwSize minimumSizeHint() const override;

    void setLaunchTimeNs(long long launchTimeNs);
    void setStallThresholdMs(double thresholdMs);
    void setXRange(double minimum, double maximum);
    void setYRange(double maximum);
    void setStalls(const SwList<RuntimeProfilerDashboardStallEntry>* stalls);
    void setLoadSamples(const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples);
    void setLoadRange(double maximum);

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
    void drawOccupancyAxisLabels_(SwPainter* painter,
                                  const SwRect& plot,
                                  int yTicks,
                                  const SwColor& textColor,
                                  const SwFont& font) const;
    void drawLoadSeries_(SwPainter* painter,
                         const SwRect& plot,
                         const SwColor& color) const;
    double secondsSinceLaunch_(long long sampleTimeNs) const;
    int xForSeconds_(double seconds, const SwRect& plot) const;
    int yForDuration_(double durationMs, const SwRect& plot) const;
    int yForOccupancy_(double occupancy, const SwRect& plot) const;

    const SwList<RuntimeProfilerDashboardStallEntry>* stalls_{nullptr};
    const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples_{nullptr};
    long long launchTimeNs_{0};
    double xMinSeconds_{0.0};
    double xMaxSeconds_{10.0};
    double yMaxMs_{16.0};
    double stallThresholdMs_{10.0};
    double loadMaxPercent_{25.0};
};
