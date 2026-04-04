#include "RuntimeProfilerStallTimelineWidget.h"

#include <algorithm>
#include <cmath>

namespace {

struct RuntimeProfilerLoadSeriesDescriptor_ {
    unsigned long long applicationId{0};
    unsigned long long threadId{0};
    SwString label;
};

static bool runtimeProfilerMatchesLoadSeries_(
    const RuntimeProfilerDashboardLoadSample& sample,
    const RuntimeProfilerLoadSeriesDescriptor_& series) {
    return sample.applicationId == series.applicationId &&
           sample.threadId == series.threadId;
}

static SwColor runtimeProfilerLoadSeriesColor_(int index) {
    static const SwColor palette[] = {
        SwColor{78, 201, 176},
        SwColor{97, 218, 251},
        SwColor{255, 203, 107},
        SwColor{206, 145, 120},
        SwColor{197, 134, 192},
        SwColor{220, 220, 170}
    };
    const int paletteCount = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if (paletteCount <= 0) {
        return SwColor{78, 201, 176};
    }
    const int safeIndex = index < 0 ? 0 : (index % paletteCount);
    return palette[safeIndex];
}

} // namespace

RuntimeProfilerStallTimelineWidget::RuntimeProfilerStallTimelineWidget(SwWidget* parent)
    : SwFrame(parent) {}

SwSize RuntimeProfilerStallTimelineWidget::sizeHint() const {
    return SwSize{320, 200};
}

SwSize RuntimeProfilerStallTimelineWidget::minimumSizeHint() const {
    return SwSize{160, 200};
}

void RuntimeProfilerStallTimelineWidget::setLaunchTimeNs(long long launchTimeNs) {
    launchTimeNs_ = launchTimeNs;
    update();
}

void RuntimeProfilerStallTimelineWidget::setStallThresholdMs(double thresholdMs) {
    stallThresholdMs_ = std::max(0.0, thresholdMs);
    update();
}

void RuntimeProfilerStallTimelineWidget::setXRange(double minimum, double maximum) {
    xMinSeconds_ = minimum;
    xMaxSeconds_ = std::max(minimum, maximum);
    update();
}

void RuntimeProfilerStallTimelineWidget::setYRange(double maximum) {
    yMaxMs_ = std::max(1.0, maximum);
    update();
}

void RuntimeProfilerStallTimelineWidget::setStalls(const SwList<RuntimeProfilerDashboardStallEntry>* stalls) {
    stalls_ = stalls;
    update();
}

void RuntimeProfilerStallTimelineWidget::setLoadSamples(const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples) {
    loadSamples_ = loadSamples;
    update();
}

void RuntimeProfilerStallTimelineWidget::setLoadRange(double maximum) {
    loadMaxPercent_ = std::max(1.0, maximum);
    update();
}

void RuntimeProfilerStallTimelineWidget::paintEvent(PaintEvent* event) {
    SwPainter* painter = event ? event->painter() : nullptr;
    if (!painter) {
        return;
    }

    const SwRect bounds = rect();
    if (bounds.width <= 2 || bounds.height <= 2) {
        return;
    }

    const SwColor background{37, 37, 38};
    const SwColor plotColor{30, 30, 30};
    const SwColor gridColor{51, 51, 55};
    const SwColor axisColor{111, 111, 111};
    const SwColor textColor{204, 204, 204};
    const SwColor borderColor{62, 62, 66};
    const SwColor stallFill{55, 148, 255};
    const SwColor stallBorder{30, 104, 196};
    const SwColor thresholdFill{209, 105, 105};
    const SwColor thresholdBorder{181, 82, 82};

    painter->fillRect(bounds, background, borderColor, 1);

    const int leftMargin = 38;
    const int rightMargin = 36;
    const int topMargin = 8;
    const int bottomMargin = 24;
    const SwRect plot{bounds.x + leftMargin,
                      bounds.y + topMargin,
                      std::max(1, bounds.width - leftMargin - rightMargin),
                      std::max(1, bounds.height - topMargin - bottomMargin)};

    painter->fillRect(plot, plotColor, borderColor, 1);

    SwFont axisFont = getFont();
    axisFont.setPointSize(std::max(7, axisFont.getPointSize() - 2));

    const int xTicks = 5;
    const int yTicks = 4;
    drawGrid_(painter, plot, xTicks, yTicks, gridColor);
    drawAxisLabels_(painter, plot, xTicks, yTicks, textColor, axisFont);
    drawOccupancyAxisLabels_(painter, plot, yTicks, SwColor{78, 201, 176}, axisFont);

    const int zeroY = plot.y + plot.height;
    painter->drawLine(plot.x, zeroY, plot.x + plot.width, zeroY, axisColor, 1);
    painter->drawLine(plot.x, plot.y, plot.x, plot.y + plot.height, axisColor, 1);

    const int thresholdY = yForDuration_(stallThresholdMs_, plot);
    painter->drawLine(plot.x, thresholdY, plot.x + plot.width, thresholdY, thresholdFill, 2);
    painter->drawText(SwRect{plot.x + 6, thresholdY - 12, 84, 12},
                      SwString("Threshold"),
                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                      thresholdFill,
                      axisFont);

    painter->pushClipRect(plot);
    drawLoadSeries_(painter, plot);
    if (stalls_ && !stalls_->isEmpty()) {
        for (size_t i = 0; i < stalls_->size(); ++i) {
            const RuntimeProfilerDashboardStallEntry& entry = (*stalls_)[i];
            const double durationMs = std::max(0.0, static_cast<double>(entry.elapsedUs) / 1000.0);
            const double endSeconds = secondsSinceLaunch_(entry.sampleTimeNs);
            const double startSeconds = endSeconds - (durationMs / 1000.0);
            if (endSeconds < xMinSeconds_ || startSeconds > xMaxSeconds_) {
                continue;
            }

            const double visibleStart = std::max(xMinSeconds_, startSeconds);
            const double visibleEnd = std::min(xMaxSeconds_, endSeconds);
            int left = xForSeconds_(visibleStart, plot);
            int right = xForSeconds_(visibleEnd, plot);
            if (right <= left) {
                right = left + 2;
            }

            const int top = yForDuration_(durationMs, plot);
            const int height = std::max(2, zeroY - top);
            const SwRect stallRect{left,
                                   top,
                                   std::max(2, right - left),
                                   height};
            const bool overThreshold = durationMs >= stallThresholdMs_;
            painter->fillRect(stallRect,
                              overThreshold ? thresholdFill : stallFill,
                              overThreshold ? thresholdBorder : stallBorder,
                              1);
        }
    } else if (!loadSamples_ || loadSamples_->size() < 2) {
        painter->drawText(plot,
                          "No stalls captured in the current window.",
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          SwColor{128, 128, 128},
                          axisFont);
    }
    painter->popClipRect();
}

void RuntimeProfilerStallTimelineWidget::drawGrid_(SwPainter* painter,
                                                   const SwRect& plot,
                                                   int xTicks,
                                                   int yTicks,
                                                   const SwColor& gridColor) const {
    if (!painter || plot.width <= 0 || plot.height <= 0) {
        return;
    }

    for (int i = 0; i < xTicks; ++i) {
        const double t = (xTicks > 1) ? static_cast<double>(i) / static_cast<double>(xTicks - 1) : 0.0;
        const int x = plot.x + static_cast<int>(std::round(t * plot.width));
        painter->drawLine(x, plot.y, x, plot.y + plot.height, gridColor, 1);
    }

    for (int i = 0; i < yTicks; ++i) {
        const double t = (yTicks > 1) ? static_cast<double>(i) / static_cast<double>(yTicks - 1) : 0.0;
        const int y = plot.y + static_cast<int>(std::round(t * plot.height));
        painter->drawLine(plot.x, y, plot.x + plot.width, y, gridColor, 1);
    }
}

void RuntimeProfilerStallTimelineWidget::drawAxisLabels_(SwPainter* painter,
                                                         const SwRect& plot,
                                                         int xTicks,
                                                         int yTicks,
                                                         const SwColor& textColor,
                                                         const SwFont& font) const {
    if (!painter) {
        return;
    }

    for (int i = 0; i < xTicks; ++i) {
        const double t = (xTicks > 1) ? static_cast<double>(i) / static_cast<double>(xTicks - 1) : 0.0;
        const double seconds = xMinSeconds_ + ((xMaxSeconds_ - xMinSeconds_) * t);
        const int x = plot.x + static_cast<int>(std::round(t * plot.width));
        painter->drawText(SwRect{x - 28, plot.y + plot.height + 2, 56, 14},
                          SwString::number(seconds, 'f', 2),
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::Top | DrawTextFormat::SingleLine),
                          textColor,
                          font);
    }

    for (int i = 0; i < yTicks; ++i) {
        const double t = (yTicks > 1) ? static_cast<double>(i) / static_cast<double>(yTicks - 1) : 0.0;
        const double valueMs = yMaxMs_ - (yMaxMs_ * t);
        const int y = plot.y + static_cast<int>(std::round(t * plot.height));
        painter->drawText(SwRect{plot.x - 34, y - 7, 28, 14},
                          SwString::number(valueMs, 'f', 1),
                          DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          font);
    }
}

void RuntimeProfilerStallTimelineWidget::drawOccupancyAxisLabels_(SwPainter* painter,
                                                                  const SwRect& plot,
                                                                  int yTicks,
                                                                  const SwColor& textColor,
                                                                  const SwFont& font) const {
    if (!painter) {
        return;
    }

    for (int i = 0; i < yTicks; ++i) {
        const double t = (yTicks > 1) ? static_cast<double>(i) / static_cast<double>(yTicks - 1) : 0.0;
        const double value = loadMaxPercent_ - (loadMaxPercent_ * t);
        const int y = plot.y + static_cast<int>(std::round(t * plot.height));
        painter->drawText(SwRect{plot.x + plot.width + 6, y - 7, 24, 14},
                          SwString::number(value, 'f', 0),
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          font);
    }
}

void RuntimeProfilerStallTimelineWidget::drawLoadSeries_(SwPainter* painter,
                                                         const SwRect& plot) const {
    if (!painter || !loadSamples_ || loadSamples_->size() < 2) {
        return;
    }

    SwList<RuntimeProfilerLoadSeriesDescriptor_> seriesList;
    for (size_t i = 0; i < loadSamples_->size(); ++i) {
        const RuntimeProfilerDashboardLoadSample& sample = (*loadSamples_)[i];
        bool found = false;
        for (size_t j = 0; j < seriesList.size(); ++j) {
            if (runtimeProfilerMatchesLoadSeries_(sample, seriesList[j])) {
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }

        RuntimeProfilerLoadSeriesDescriptor_ series;
        series.applicationId = sample.applicationId;
        series.threadId = sample.threadId;
        series.label = sample.seriesLabel;
        seriesList.append(series);
    }

    for (size_t seriesIndex = 0; seriesIndex < seriesList.size(); ++seriesIndex) {
        const SwColor color = runtimeProfilerLoadSeriesColor_(static_cast<int>(seriesIndex));
        bool hasPrevious = false;
        int previousX = 0;
        int previousY = 0;
        for (size_t sampleIndex = 0; sampleIndex < loadSamples_->size(); ++sampleIndex) {
            const RuntimeProfilerDashboardLoadSample& sample = (*loadSamples_)[sampleIndex];
            if (!runtimeProfilerMatchesLoadSeries_(sample, seriesList[seriesIndex])) {
                continue;
            }

            const double seconds = secondsSinceLaunch_(sample.sampleTimeNs);
            if (seconds < xMinSeconds_ || seconds > xMaxSeconds_) {
                continue;
            }

            const int x = xForSeconds_(seconds, plot);
            const int y = yForOccupancy_(sample.loadPercentage, plot);
            if (hasPrevious) {
                painter->drawLine(previousX, previousY, x, y, color, 2);
            }
            previousX = x;
            previousY = y;
            hasPrevious = true;
        }
    }
}

double RuntimeProfilerStallTimelineWidget::secondsSinceLaunch_(long long sampleTimeNs) const {
    if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
        return 0.0;
    }
    return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
}

int RuntimeProfilerStallTimelineWidget::xForSeconds_(double seconds, const SwRect& plot) const {
    const double span = std::max(0.001, xMaxSeconds_ - xMinSeconds_);
    const double t = std::max(0.0, std::min(1.0, (seconds - xMinSeconds_) / span));
    return plot.x + static_cast<int>(std::round(t * plot.width));
}

int RuntimeProfilerStallTimelineWidget::yForDuration_(double durationMs, const SwRect& plot) const {
    const int inset = plot.height > 12 ? 3 : 1;
    const int innerHeight = std::max(1, plot.height - (inset * 2));
    const double clamped = std::max(0.0, std::min(yMaxMs_, durationMs));
    const double t = 1.0 - (clamped / std::max(1.0, yMaxMs_));
    return plot.y + inset + static_cast<int>(std::round(t * innerHeight));
}

int RuntimeProfilerStallTimelineWidget::yForOccupancy_(double occupancy, const SwRect& plot) const {
    const int inset = plot.height > 12 ? 3 : 1;
    const int innerHeight = std::max(1, plot.height - (inset * 2));
    const double clamped = std::max(0.0, std::min(loadMaxPercent_, occupancy));
    const double t = 1.0 - (clamped / std::max(1.0, loadMaxPercent_));
    return plot.y + inset + static_cast<int>(std::round(t * innerHeight));
}
