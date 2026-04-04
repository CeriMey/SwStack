#include "SocketTrafficConsumerTimelineWidget.h"

#include <algorithm>
#include <cmath>

SocketTrafficConsumerTimelineWidget::SocketTrafficConsumerTimelineWidget(SwWidget* parent)
    : SwFrame(parent) {}

SwSize SocketTrafficConsumerTimelineWidget::sizeHint() const {
    return SwSize{320, 220};
}

SwSize SocketTrafficConsumerTimelineWidget::minimumSizeHint() const {
    return SwSize{180, 200};
}

void SocketTrafficConsumerTimelineWidget::setLaunchTimeNs(long long launchTimeNs) {
    launchTimeNs_ = launchTimeNs;
    update();
}

void SocketTrafficConsumerTimelineWidget::setHistory(const SwList<SocketTrafficDashboardHistoryPoint>* history) {
    history_ = history;
    update();
}

void SocketTrafficConsumerTimelineWidget::setSelectedConsumerLabel(const SwString& label) {
    selectedConsumerLabel_ = label;
    update();
}

void SocketTrafficConsumerTimelineWidget::setXRange(double minimum, double maximum) {
    xMinSeconds_ = minimum;
    xMaxSeconds_ = std::max(minimum, maximum);
    update();
}

void SocketTrafficConsumerTimelineWidget::setYRange(double maximumBytesPerSecond) {
    yMaxBytesPerSecond_ = std::max(1.0, maximumBytesPerSecond);
    update();
}

void SocketTrafficConsumerTimelineWidget::paintEvent(PaintEvent* event) {
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

    painter->fillRect(bounds, background, borderColor, 1);

    const int leftMargin = 54;
    const int rightMargin = 14;
    const int topMargin = 24;
    const int bottomMargin = 22;
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
    drawLegend_(painter, bounds, axisFont);

    const int zeroY = plot.y + plot.height;
    painter->drawLine(plot.x, zeroY, plot.x + plot.width, zeroY, axisColor, 1);
    painter->drawLine(plot.x, plot.y, plot.x, plot.y + plot.height, axisColor, 1);

    painter->pushClipRect(plot);
    drawSeries_(painter, plot, SwColor{78, 201, 176}, 2, 0);
    drawSeries_(painter, plot, SwColor{255, 159, 67}, 2, 1);
    drawSeries_(painter, plot, SwColor{97, 218, 251}, 2, 2);
    drawSeries_(painter, plot, SwColor{240, 98, 146}, 2, 3);
    if (!history_ || history_->isEmpty()) {
        painter->drawText(plot,
                          "No network samples in the current window.",
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          SwColor{128, 128, 128},
                          axisFont);
    }
    painter->popClipRect();
}

void SocketTrafficConsumerTimelineWidget::drawGrid_(SwPainter* painter,
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

void SocketTrafficConsumerTimelineWidget::drawAxisLabels_(SwPainter* painter,
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
        const double value = yMaxBytesPerSecond_ - (yMaxBytesPerSecond_ * t);
        const int y = plot.y + static_cast<int>(std::round(t * plot.height));
        painter->drawText(SwRect{plot.x - 48, y - 7, 42, 14},
                          rateTickText_(value),
                          DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          font);
    }
}

void SocketTrafficConsumerTimelineWidget::drawLegend_(SwPainter* painter,
                                                      const SwRect& bounds,
                                                      const SwFont& font) const {
    if (!painter) {
        return;
    }

    const SwString focusLabel = selectedConsumerLabel_.isEmpty() ? SwString("<none>") : selectedConsumerLabel_;
    painter->drawText(SwRect{bounds.x + 10, bounds.y + 4, bounds.width - 20, 14},
                      "RX total  |  TX total  |  RX selected  |  TX selected  |  Focus: " + focusLabel,
                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                      SwColor{196, 196, 196},
                      font);
}

void SocketTrafficConsumerTimelineWidget::drawSeries_(SwPainter* painter,
                                                      const SwRect& plot,
                                                      const SwColor& color,
                                                      int lineWidth,
                                                      int seriesKind) const {
    if (!painter || !history_ || history_->size() < 2) {
        return;
    }

    bool hasPrevious = false;
    int previousX = 0;
    int previousY = 0;
    for (size_t i = 0; i < history_->size(); ++i) {
        const SocketTrafficDashboardHistoryPoint& point = (*history_)[i];
        const double seconds = secondsSinceLaunch_(point.sampleTimeNs);
        if (seconds < xMinSeconds_ || seconds > xMaxSeconds_) {
            continue;
        }

        double rate = 0.0;
        switch (seriesKind) {
        case 0:
            rate = static_cast<double>(point.totalRxRateBytesPerSecond);
            break;
        case 1:
            rate = static_cast<double>(point.totalTxRateBytesPerSecond);
            break;
        case 2:
            rate = static_cast<double>(point.selectedRxRateBytesPerSecond);
            break;
        case 3:
            rate = static_cast<double>(point.selectedTxRateBytesPerSecond);
            break;
        default:
            break;
        }

        const int x = xForSeconds_(seconds, plot);
        const int y = yForRate_(rate, plot);
        if (hasPrevious) {
            painter->drawLine(previousX, previousY, x, y, color, lineWidth);
        }
        previousX = x;
        previousY = y;
        hasPrevious = true;
    }
}

double SocketTrafficConsumerTimelineWidget::secondsSinceLaunch_(long long sampleTimeNs) const {
    if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
        return 0.0;
    }
    return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
}

int SocketTrafficConsumerTimelineWidget::xForSeconds_(double seconds, const SwRect& plot) const {
    if (xMaxSeconds_ <= xMinSeconds_) {
        return plot.x;
    }
    const double normalized = (seconds - xMinSeconds_) / (xMaxSeconds_ - xMinSeconds_);
    return plot.x + static_cast<int>(std::round(normalized * plot.width));
}

int SocketTrafficConsumerTimelineWidget::yForRate_(double bytesPerSecond, const SwRect& plot) const {
    const double clamped = std::max(0.0, std::min(yMaxBytesPerSecond_, bytesPerSecond));
    const double normalized = yMaxBytesPerSecond_ <= 0.0 ? 0.0 : (clamped / yMaxBytesPerSecond_);
    return plot.y + plot.height - static_cast<int>(std::round(normalized * plot.height));
}

SwString SocketTrafficConsumerTimelineWidget::rateTickText_(double bytesPerSecond) const {
    if (bytesPerSecond >= 1024.0 * 1024.0) {
        return SwString::number(bytesPerSecond / (1024.0 * 1024.0), 'f', 1) + " MB";
    }
    if (bytesPerSecond >= 1024.0) {
        return SwString::number(bytesPerSecond / 1024.0, 'f', 0) + " KB";
    }
    return SwString::number(bytesPerSecond, 'f', 0) + " B";
}
