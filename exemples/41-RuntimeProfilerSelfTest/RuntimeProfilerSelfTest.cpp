#include "SwFrame.h"
#include "SwGuiApplication.h"
#include "SwLabel.h"
#include "SwLayout.h"
#include "SwMainWindow.h"
#include "SwPushButton.h"
#include "SwScrollBar.h"
#include "SwSplitter.h"
#include "SwStandardItemModel.h"
#include "SwTableView.h"
#include "widgets/RuntimeProfilerProfilingView.h"
#include "widgets/RuntimeProfilerViewTypes.h"
#include "core/fs/SwMutex.h"
#include "core/runtime/SwRuntimeProfiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace {

static const int kTimingKindCount_ = 6;
static const int kChartHistoryLimit_ = 96;
static const int kLoadHistoryLimit_ = 480;
static const int kUiRefreshPeriodUs_ = 60000;
static const int kAutoFeedPeriodUs_ = 700000;

static SwString timingKindName_(SwRuntimeTimingKind kind);
static SwString laneName_(SwFiberLane lane);
static SwString durationMsString_(long long durationUs);
static SwString percentString_(double value);
static int timingKindIndex_(SwRuntimeTimingKind kind);
static int totalQueuedFibers_(const SwFiberPoolStats& stats);
static SwString hexAddress_(unsigned long long value);
static bool hasFlag_(int argc, char** argv, const char* flag);
static int intArgumentValue_(int argc, char** argv, const char* key, int fallbackValue);
static long long steadyNowNs_();
static void applyProfilerScrollBarStyle_(SwScrollBar* scrollBar);
static void applyProfilerSplitterStyle_(SwSplitter* splitter);

class RuntimeProfilerDashboardCard : public SwFrame {
    SW_OBJECT(RuntimeProfilerDashboardCard, SwFrame)

public:
    explicit RuntimeProfilerDashboardCard(SwWidget* parent = nullptr)
        : SwFrame(parent) {}
};

class RuntimeProfilerDashboardPanel : public SwFrame {
    SW_OBJECT(RuntimeProfilerDashboardPanel, SwFrame)

public:
    explicit RuntimeProfilerDashboardPanel(SwWidget* parent = nullptr)
        : SwFrame(parent) {}
};

class RuntimeProfilerStallTimelineView : public SwFrame {
    SW_OBJECT(RuntimeProfilerStallTimelineView, SwFrame)

public:
    explicit RuntimeProfilerStallTimelineView(SwWidget* parent = nullptr)
        : SwFrame(parent) {}

    virtual SwSize sizeHint() const override {
        return SwSize{320, 100};
    }

    virtual SwSize minimumSizeHint() const override {
        return SwSize{160, 100};
    }

    void setTitle(const SwString& title) {
        title_ = title;
        update();
    }

    void setLaunchTimeNs(long long launchTimeNs) {
        launchTimeNs_ = launchTimeNs;
        update();
    }

    void setStallThresholdMs(double thresholdMs) {
        stallThresholdMs_ = std::max(0.0, thresholdMs);
        update();
    }

    void setXRange(double minimum, double maximum) {
        xMinSeconds_ = minimum;
        xMaxSeconds_ = std::max(minimum, maximum);
        update();
    }

    void setYRange(double maximum) {
        yMaxMs_ = std::max(1.0, maximum);
        update();
    }

    void setStalls(const SwList<RuntimeProfilerDashboardStallEntry>* stalls) {
        stalls_ = stalls;
        update();
    }

    void setLoadSamples(const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples) {
        loadSamples_ = loadSamples;
        update();
    }

    void setLoadRange(double maximum) {
        loadMaxPercent_ = std::max(1.0, maximum);
        update();
    }

protected:
    void paintEvent(PaintEvent* event) override {
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
        const int topMargin = title_.isEmpty() ? 8 : 16;
        const int bottomMargin = 18;
        const SwRect plot{bounds.x + leftMargin,
                          bounds.y + topMargin,
                          std::max(1, bounds.width - leftMargin - rightMargin),
                          std::max(1, bounds.height - topMargin - bottomMargin)};

        painter->fillRect(plot, plotColor, borderColor, 1);

        const SwFont titleFont = getFont();
        SwFont axisFont = getFont();
        axisFont.setPointSize(std::max(7, axisFont.getPointSize() - 2));

        const int xTicks = 5;
        const int yTicks = 4;
        drawGrid_(painter, plot, xTicks, yTicks, gridColor);
        drawAxisLabels_(painter, plot, xTicks, yTicks, textColor, axisFont);
        drawOccupancyAxisLabels_(painter, plot, yTicks, SwColor{78, 201, 176}, axisFont);
        drawTitle_(painter, bounds, textColor, titleFont);

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
        drawLoadSeries_(painter, plot, SwColor{78, 201, 176});
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
        } else {
            painter->drawText(plot,
                              "No stalls captured in the current window.",
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              SwColor{128, 128, 128},
                              axisFont);
        }
        painter->popClipRect();
    }

private:
    void drawGrid_(SwPainter* painter,
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

    void drawAxisLabels_(SwPainter* painter,
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

    void drawTitle_(SwPainter* painter,
                    const SwRect& bounds,
                    const SwColor& textColor,
                    const SwFont& font) const {
        if (!painter || title_.isEmpty()) {
            return;
        }

        painter->drawText(SwRect{bounds.x + 10, bounds.y + 2, bounds.width - 20, 12},
                          title_,
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          font);
    }

    void drawOccupancyAxisLabels_(SwPainter* painter,
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

    void drawLoadSeries_(SwPainter* painter,
                         const SwRect& plot,
                         const SwColor& color) const {
        if (!painter || !loadSamples_ || loadSamples_->size() < 2) {
            return;
        }

        bool hasPrevious = false;
        int previousX = 0;
        int previousY = 0;
        for (size_t i = 0; i < loadSamples_->size(); ++i) {
            const RuntimeProfilerDashboardLoadSample& sample = (*loadSamples_)[i];
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

    double secondsSinceLaunch_(long long sampleTimeNs) const {
        if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
            return 0.0;
        }
        return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
    }

    int xForSeconds_(double seconds, const SwRect& plot) const {
        const double span = std::max(0.001, xMaxSeconds_ - xMinSeconds_);
        const double t = std::max(0.0, std::min(1.0, (seconds - xMinSeconds_) / span));
        return plot.x + static_cast<int>(std::round(t * plot.width));
    }

    int yForDuration_(double durationMs, const SwRect& plot) const {
        const double clamped = std::max(0.0, std::min(yMaxMs_, durationMs));
        const double t = 1.0 - (clamped / std::max(1.0, yMaxMs_));
        return plot.y + static_cast<int>(std::round(t * plot.height));
    }

    int yForOccupancy_(double occupancy, const SwRect& plot) const {
        const double clamped = std::max(0.0, std::min(loadMaxPercent_, occupancy));
        const double t = 1.0 - (clamped / std::max(1.0, loadMaxPercent_));
        return plot.y + static_cast<int>(std::round(t * plot.height));
    }

    const SwList<RuntimeProfilerDashboardStallEntry>* stalls_{nullptr};
    const SwList<RuntimeProfilerDashboardLoadSample>* loadSamples_{nullptr};
    SwString title_{"Stalls Over Time"};
    long long launchTimeNs_{0};
    double xMinSeconds_{0.0};
    double xMaxSeconds_{10.0};
    double yMaxMs_{16.0};
    double stallThresholdMs_{10.0};
    double loadMaxPercent_{25.0};
};

class RuntimeProfilerStallTableView : public SwTableView {
    SW_OBJECT(RuntimeProfilerStallTableView, SwTableView)

public:
    explicit RuntimeProfilerStallTableView(SwWidget* parent = nullptr)
        : SwTableView(parent) {}

protected:
    void paintEvent(PaintEvent* event) override {
        if (!event || !event->painter() || !isVisibleInHierarchy()) {
            return;
        }

        syncOverlayGeometry_();

        SwPainter* painter = event->painter();
        const SwRect bounds = rect();
        const SwColor frameFill{30, 30, 30};
        const SwColor frameBorder{62, 62, 66};
        const SwColor headerFill{37, 37, 38};
        const SwColor divider{51, 51, 55};
        painter->fillRoundedRect(bounds, 8, frameFill, frameBorder, 1);

        SwHeaderView* header = horizontalHeader();
        const SwRect headerRect = (header && header->isVisibleInHierarchy())
                                      ? header->geometry()
                                      : SwRect{6, 6, std::max(0, bounds.width - 12), headerHeight()};
        const SwRect headerCap{bounds.x + 1,
                               bounds.y + 1,
                               std::max(0, bounds.width - 2),
                               std::max(0, headerRect.y + headerRect.height - bounds.y - 1)};
        if (headerCap.width > 0 && headerCap.height > 0) {
            painter->fillRoundedRect(headerCap, 7, 7, 0, 0, headerFill, headerFill, 0);
            painter->drawLine(headerCap.x,
                              headerCap.y + headerCap.height - 1,
                              headerCap.x + headerCap.width,
                              headerCap.y + headerCap.height - 1,
                              divider,
                              1);
        }

        const SwScrollBar* hBar = horizontalScrollBar();
        const SwScrollBar* vBar = verticalScrollBar();
        const bool showH = hBar && hBar->getVisible();
        const int bottom = showH ? hBar->y() : (bounds.height - 6);

        const SwRect dataArea{headerRect.x,
                              headerRect.y + headerRect.height,
                              headerRect.width,
                              std::max(0, bottom - (headerRect.y + headerRect.height))};

        painter->pushClipRect(dataArea);
        paintRowsDark_(painter, dataArea, vBar ? vBar->value() : 0, hBar ? hBar->value() : 0);
        painter->popClipRect();

        for (SwObject* objChild : children()) {
            SwWidget* child = dynamic_cast<SwWidget*>(objChild);
            if (!child || !child->isVisibleInHierarchy()) {
                continue;
            }
            paintChild_(event, child);
        }
    }

private:
    void syncOverlayGeometry_() {
        SwHeaderView* header = horizontalHeader();
        SwScrollBar* vBar = verticalScrollBar();
        if (!header || !vBar || !vBar->getVisible()) {
            return;
        }

        const SwRect bounds = rect();
        const SwRect headerRect = header->geometry();
        const SwScrollBar* hBar = horizontalScrollBar();
        const bool showH = hBar && hBar->getVisible();
        const int top = headerRect.y + headerRect.height;
        const int bottom = showH ? hBar->y() : (bounds.height - 6);
        const int newHeight = std::max(0, bottom - top);
        if (vBar->y() != top || vBar->height() != newHeight) {
            vBar->move(vBar->x(), top);
            vBar->resize(vBar->width(), newHeight);
        }
    }

    void paintRowsDark_(SwPainter* painter,
                        const SwRect& dataArea,
                        int offsetY,
                        int offsetX) {
        if (!painter || !model()) {
            return;
        }

        const int rows = std::max(0, model()->rowCount());
        const int cols = std::max(0, model()->columnCount());
        if (rows == 0 || cols == 0 || rowHeight() <= 0) {
            return;
        }

        const int firstRow = std::max(0, offsetY / rowHeight());
        const int yWithin = offsetY - (firstRow * rowHeight());
        int y = dataArea.y - yWithin;

        const SwColor rowFill{37, 37, 38};
        const SwColor altFill{34, 34, 36};
        const SwColor rowBorder{44, 44, 48};
        const SwColor selectedFill{9, 71, 113};
        const SwColor selectedBorder{55, 148, 255};
        const SwColor primaryText{220, 220, 220};
        const SwColor mutedText{156, 156, 156};
        const SwColor accentText{255, 215, 0};

        SwFont baseFont(L"Segoe UI", 9, Normal);
        SwFont emphasisFont(L"Segoe UI", 9, SemiBold);

        for (int row = firstRow; row < rows && y < dataArea.y + dataArea.height; ++row) {
            const SwRect rowRect{dataArea.x, y, dataArea.width, rowHeight()};
            const bool alternate = (row % 2) == 1;
            const SwModelIndex rowIndex = model()->index(row, 0, SwModelIndex());
            const bool selected = selectionModel() && rowIndex.isValid() && selectionModel()->isSelected(rowIndex);

            painter->fillRect(rowRect,
                              alternate ? altFill : rowFill,
                              alternate ? altFill : rowFill,
                              0);
            painter->drawLine(rowRect.x,
                              rowRect.y + rowRect.height - 1,
                              rowRect.x + rowRect.width,
                              rowRect.y + rowRect.height - 1,
                              rowBorder,
                              1);

            if (selected) {
                const SwRect highlight{rowRect.x + 1,
                                       rowRect.y + 2,
                                       std::max(0, rowRect.width - 2),
                                       std::max(0, rowRect.height - 4)};
                painter->fillRoundedRect(highlight, 6, selectedFill, selectedBorder, 1);
            }

            int xContent = 0;
            for (int column = 0; column < cols; ++column) {
                const int columnWidthPx = columnWidth(column);
                const int x = dataArea.x + xContent - offsetX;
                const SwRect cell{x, y, columnWidthPx, rowHeight()};
                xContent += columnWidthPx;

                if (cell.x + cell.width < dataArea.x || cell.x > dataArea.x + dataArea.width) {
                    continue;
                }

                const SwModelIndex index = model()->index(row, column, SwModelIndex());
                const SwString text = model()->data(index, SwItemDataRole::DisplayRole).toString();
                SwRect textRect{cell.x + 10, cell.y, std::max(0, cell.width - 20), cell.height};

                SwColor textColor = primaryText;
                SwFont font = baseFont;
                DrawTextFormats alignment(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine);

                if (column == 0) {
                    textColor = selected ? SwColor{255, 255, 255} : mutedText;
                } else if (column == 1) {
                    textColor = selected ? SwColor{255, 255, 255} : accentText;
                    font = emphasisFont;
                } else if (column == 2) {
                    textColor = selected ? SwColor{225, 240, 255} : SwColor{160, 200, 255};
                }

                painter->drawText(textRect, text, alignment, textColor, font);

                if (column < cols - 1) {
                    painter->drawLine(cell.x + cell.width,
                                      cell.y + 6,
                                      cell.x + cell.width,
                                      cell.y + cell.height - 6,
                                      rowBorder,
                                      1);
                }
            }

            y += rowHeight();
        }
    }
};

class RuntimeProfilerStallTablePane : public SwWidget {
    SW_OBJECT(RuntimeProfilerStallTablePane, SwWidget)

public:
    explicit RuntimeProfilerStallTablePane(long long launchTimeNs,
                                           long long thresholdUs,
                                           SwWidget* parent = nullptr)
        : SwWidget(parent),
          launchTimeNs_(launchTimeNs),
          thresholdUs_(thresholdUs) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

        titleLabel_ = new SwLabel("Recent Stalls", this);
        titleLabel_->resize(12, 22);
        titleLabel_->setStyleSheet(R"(
            SwLabel {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(220, 220, 220);
                font-size: 17px;
            }
        )");

        subtitleLabel_ = new SwLabel(this);
        subtitleLabel_->resize(12, 18);
        subtitleLabel_->setStyleSheet(R"(
            SwLabel {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(156, 156, 156);
                font-size: 12px;
            }
        )");

        tableView_ = new RuntimeProfilerStallTableView(this);
        tableModel_ = new SwStandardItemModel(0, 4, tableView_);
        resetModelColumns_();
        tableView_->setModel(tableModel_);
        tableView_->setRowHeight(30);
        tableView_->setHeaderHeight(30);
        tableView_->setShowGrid(false);
        tableView_->setAlternatingRowColors(false);
        tableView_->setColumnsFitToWidth(true);
        tableView_->setColumnStretches(SwList<int>{10, 12, 18, 38});
        tableView_->setVerticalScrollBarPolicy(SwScrollBarPolicy::ScrollBarAsNeeded);
        tableView_->setHorizontalScrollBarPolicy(SwScrollBarPolicy::ScrollBarAlwaysOff);
        applyProfilerScrollBarStyle_(tableView_->verticalScrollBar());
        applyProfilerScrollBarStyle_(tableView_->horizontalScrollBar());
        if (tableView_->horizontalHeader()) {
            tableView_->horizontalHeader()->setStyleSheet(R"(
                SwHeaderView {
                    background-color: rgb(37, 37, 38);
                    border-color: rgb(37, 37, 38);
                    border-width: 0px;
                    border-top-left-radius: 0px;
                    border-top-right-radius: 0px;
                    border-bottom-left-radius: 0px;
                    border-bottom-right-radius: 0px;
                    text-color: rgb(204, 204, 204);
                    divider-color: rgb(51, 51, 55);
                    indicator-color: rgb(55, 148, 255);
                    padding-left: 10px;
                    padding-right: 8px;
                    padding-top: 0px;
                    padding-bottom: 0px;
                }
            )");
        }

        if (tableView_->selectionModel()) {
            SwObject::connect(tableView_->selectionModel(), &SwItemSelectionModel::currentChanged, this,
                              [this](const SwModelIndex&, const SwModelIndex&) {
                                  emit currentSequenceChanged(currentSelectedSequence());
                              });
        }

        SwVerticalLayout* layout = new SwVerticalLayout();
        layout->setMargin(14);
        layout->setSpacing(8);
        layout->addWidget(titleLabel_, 0, 22);
        layout->addWidget(subtitleLabel_, 0, 18);
        layout->addWidget(tableView_, 1, 240);
        setLayout(layout);

        updateHeaderText_();
    }

    virtual SwSize minimumSizeHint() const override {
        return SwSize{360, 180};
    }

    void setLaunchTimeNs(long long launchTimeNs) {
        launchTimeNs_ = launchTimeNs;
    }

    void setThresholdUs(long long thresholdUs) {
        thresholdUs_ = thresholdUs;
        updateHeaderText_();
    }

    void rebuild(const SwList<RuntimeProfilerDashboardStallEntry>& entries,
                 unsigned long long preferredSequence,
                 bool followLatest) {
        sequences_.clear();
        tableModel_->clear();
        resetModelColumns_();

        for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
            const RuntimeProfilerDashboardStallEntry& entry = entries[i];
            SwList<SwStandardItem*> row;

            SwStandardItem* timeItem = new SwStandardItem(SwString::number(secondsSinceLaunch_(entry.sampleTimeNs), 'f', 3));
            SwStandardItem* durationItem = new SwStandardItem(durationMsString_(entry.elapsedUs));
            SwStandardItem* kindItem = new SwStandardItem(timingKindName_(entry.kind));
            SwStandardItem* scopeItem = new SwStandardItem(entry.label.isEmpty() ? SwString("<unnamed>") : entry.label);

            const SwString tooltip = "#" + SwString::number(entry.sequence) +
                                     "  |  lane " + laneName_(entry.lane) +
                                     "  |  thread " + SwString::number(entry.threadId);
            timeItem->setToolTip(tooltip);
            durationItem->setToolTip(tooltip);
            kindItem->setToolTip(tooltip);
            scopeItem->setToolTip(tooltip);

            row.append(timeItem);
            row.append(durationItem);
            row.append(kindItem);
            row.append(scopeItem);
            tableModel_->appendRow(row);
            sequences_.append(entry.sequence);
        }

        updateHeaderText_();

        int rowToSelect = -1;
        if (followLatest && tableModel_->rowCount() > 0) {
            rowToSelect = 0;
        } else if (preferredSequence != 0) {
            rowToSelect = rowForSequence_(preferredSequence);
        }
        if (rowToSelect < 0 && tableModel_->rowCount() > 0) {
            rowToSelect = 0;
        }

        if (rowToSelect >= 0 && rowToSelect < tableModel_->rowCount()) {
            selectRow_(rowToSelect);
        } else {
            emit currentSequenceChanged(0);
        }
    }

    void clearEntries() {
        sequences_.clear();
        tableModel_->clear();
        resetModelColumns_();
        updateHeaderText_();
        emit currentSequenceChanged(0);
    }

    unsigned long long currentSelectedSequence() const {
        if (!tableView_ || !tableView_->selectionModel()) {
            return 0;
        }
        const SwModelIndex current = tableView_->selectionModel()->currentIndex();
        if (!current.isValid()) {
            return 0;
        }
        const int row = current.row();
        if (row < 0 || row >= static_cast<int>(sequences_.size())) {
            return 0;
        }
        return sequences_[static_cast<size_t>(row)];
    }

    bool isLatestSelected() const {
        if (!tableView_ || !tableView_->selectionModel()) {
            return true;
        }
        const SwModelIndex current = tableView_->selectionModel()->currentIndex();
        return !current.isValid() || current.row() == 0;
    }

signals:
    DECLARE_SIGNAL(currentSequenceChanged, unsigned long long)

private:
    void resetModelColumns_() {
        if (!tableModel_) {
            return;
        }
        tableModel_->setColumnCount(4);
        tableModel_->setHorizontalHeaderLabels(SwList<SwString>{"Time", "Duration", "Kind", "Scope"});
    }

    void updateHeaderText_() {
        if (titleLabel_) {
            if (sequences_.isEmpty()) {
                titleLabel_->setText("Recent Stalls");
            } else {
                titleLabel_->setText("Recent Stalls (" + SwString::number(sequences_.size()) + ")");
            }
        }
        if (subtitleLabel_) {
            subtitleLabel_->setText("Threshold " + durationMsString_(thresholdUs_) + "  |  newest first");
        }
    }

    int rowForSequence_(unsigned long long sequence) const {
        for (int row = 0; row < static_cast<int>(sequences_.size()); ++row) {
            if (sequences_[static_cast<size_t>(row)] == sequence) {
                return row;
            }
        }
        return -1;
    }

    void selectRow_(int row) {
        if (!tableView_ || !tableView_->selectionModel() || !tableModel_) {
            return;
        }
        SwList<SwModelIndex> selection;
        selection.append(tableModel_->index(row, 0));
        tableView_->selectionModel()->setSelectedIndexes(selection);
        tableView_->selectionModel()->setCurrentIndex(tableModel_->index(row, 0), false, false);
    }

    double secondsSinceLaunch_(long long sampleTimeNs) const {
        if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
            return 0.0;
        }
        return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
    }

    long long launchTimeNs_{0};
    long long thresholdUs_{0};
    SwLabel* titleLabel_{nullptr};
    SwLabel* subtitleLabel_{nullptr};
    RuntimeProfilerStallTableView* tableView_{nullptr};
    SwStandardItemModel* tableModel_{nullptr};
    SwList<unsigned long long> sequences_;
};

class RuntimeProfilerDashboardSink : public SwRuntimeProfileSink {
public:
    struct Snapshot {
        SwRuntimeCountersSnapshot counters;
        SwList<RuntimeProfilerDashboardStallEntry> newStalls;
        unsigned long long stallCount;
        unsigned long long timingCount;
        unsigned long long batchCount;
        long long worstStallUs;
        unsigned long long timingByKind[kTimingKindCount_];

        Snapshot()
            : stallCount(0),
              timingCount(0),
              batchCount(0),
              worstStallUs(0) {
            for (int i = 0; i < kTimingKindCount_; ++i) {
                timingByKind[i] = 0;
            }
        }
    };

    void onRuntimeBatch(const SwList<SwRuntimeTimingRecord>& records,
                        const SwRuntimeCountersSnapshot& counters) override;
    void onStall(const SwRuntimeStallReport& report) override;
    Snapshot consume();

private:
    SwMutex mutex_;
    SwRuntimeCountersSnapshot latestCounters_;
    SwList<RuntimeProfilerDashboardStallEntry> pendingStalls_;
    unsigned long long stallSequence_{0};
    unsigned long long timingCount_{0};
    unsigned long long batchCount_{0};
    long long worstStallUs_{0};
    unsigned long long timingByKind_[kTimingKindCount_]{0, 0, 0, 0, 0, 0};
};

class RuntimeProfilerObjectReceiver : public SwObject {
public:
    explicit RuntimeProfilerObjectReceiver(SwGuiApplication* app);

    void queueStall(int durationMs);
    bool event(SwEvent* event) override;

private:
    SwGuiApplication* app_{nullptr};
    SwMutex mutex_;
    SwList<int> pendingDurationsMs_;
};

class RuntimeProfilerDashboardWindow : public SwMainWindow {
    SW_OBJECT(RuntimeProfilerDashboardWindow, SwMainWindow)

public:
    RuntimeProfilerDashboardWindow(SwGuiApplication* app,
                                   const SwRuntimeProfileConfig& profilerConfig);

    SwRuntimeProfileSink* sink();
    void start();

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    SwFrame* createMetricCard_(SwWidget* parent,
                               const SwString& title,
                               const SwString& accentRgb,
                               SwLabel*& valueLabelOut);
    RuntimeProfilerDashboardPanel* createPanel_(SwWidget* parent);
    SwPushButton* createActionButton_(SwWidget* parent,
                                      const SwString& text,
                                      const SwString& backgroundRgb);

    void buildUi_();
    void stripChrome_();
    void updateAutoFeedButtonText_();

    void triggerManualStall_(const char* label, int durationMs);
    void triggerPostedStall_(int durationMs);
    void triggerTimerStall_(int durationMs);
    void triggerObjectStall_(int durationMs);
    void triggerBurst_();
    void scheduleNextAutoStall_();
    void setMonitoringEnabled_(bool enabled);
    void setThresholdUs_(long long thresholdUs);

    void pullProfilerState_();
    void applySnapshot_(const RuntimeProfilerDashboardSink::Snapshot& snapshot);
    void refreshProfilingView_(unsigned long long preferredSequence, bool followLatest);
    unsigned long long currentSelectedSequence_() const;
    bool isLatestSelected_() const;
    void updateSummary_();
    void clearUi_();

    SwGuiApplication* app_{nullptr};
    SwRuntimeProfileConfig profilerConfig_{};
    RuntimeProfilerDashboardSink sink_{};
    RuntimeProfilerObjectReceiver objectReceiver_;
    long long launchTimeNs_{0};

    SwWidget* root_{nullptr};
    RuntimeProfilerProfilingView* profilingView_{nullptr};

    SwPushButton* autoFeedButton_{nullptr};
    SwLabel* subtitleLabel_{nullptr};
    SwLabel* stallCountValue_{nullptr};
    SwLabel* worstStallValue_{nullptr};
    SwLabel* loopLoadValue_{nullptr};
    SwLabel* droppedValue_{nullptr};
    SwLabel* queueValue_{nullptr};
    SwLabel* footerLabel_{nullptr};

    SwList<RuntimeProfilerDashboardStallEntry> stallHistory_;
    SwList<RuntimeProfilerDashboardLoadSample> loadHistory_;
    SwRuntimeCountersSnapshot latestCounters_{};
    unsigned long long latestTimingCount_{0};
    unsigned long long latestBatchCount_{0};
    unsigned long long latestTimingByKind_[kTimingKindCount_]{0, 0, 0, 0, 0, 0};
    long long latestWorstStallUs_{0};

    int refreshTimerId_{-1};
    int autoFeedTimerId_{-1};
    int autoPatternIndex_{0};
    bool autoFeedEnabled_{false};
    bool monitoringActive_{false};
};

static SwString timingKindName_(SwRuntimeTimingKind kind) {
    switch (kind) {
    case SwRuntimeTimingKind::PlatformPump:
        return "PlatformPump";
    case SwRuntimeTimingKind::PostedEvent:
        return "PostedEvent";
    case SwRuntimeTimingKind::ObjectEvent:
        return "ObjectEvent";
    case SwRuntimeTimingKind::Timer:
        return "Timer";
    case SwRuntimeTimingKind::FiberTask:
        return "FiberTask";
    case SwRuntimeTimingKind::ManualScope:
    default:
        return "ManualScope";
    }
}

static SwString laneName_(SwFiberLane lane) {
    switch (lane) {
    case SwFiberLane::Control:
        return "Control";
    case SwFiberLane::Input:
        return "Input";
    case SwFiberLane::Normal:
        return "Normal";
    case SwFiberLane::Background:
    default:
        return "Background";
    }
}

static SwString durationMsString_(long long durationUs) {
    const double durationMs = static_cast<double>(durationUs) / 1000.0;
    const int precision = durationMs >= 100.0 ? 0 : 1;
    return SwString::number(durationMs, 'f', precision) + " ms";
}

static SwString percentString_(double value) {
    return SwString::number(value, 'f', 1) + "%";
}

static int timingKindIndex_(SwRuntimeTimingKind kind) {
    const int rawIndex = static_cast<int>(kind);
    if (rawIndex < 0) {
        return 0;
    }
    if (rawIndex >= kTimingKindCount_) {
        return kTimingKindCount_ - 1;
    }
    return rawIndex;
}

static int totalQueuedFibers_(const SwFiberPoolStats& stats) {
    return stats.queuedCountByLane.control +
           stats.queuedCountByLane.input +
           stats.queuedCountByLane.normal +
           stats.queuedCountByLane.background;
}

static SwString hexAddress_(unsigned long long value) {
    return "0x" + SwString::number(value, 16);
}

static bool hasFlag_(int argc, char** argv, const char* flag) {
    if (!flag) {
        return false;
    }
    const SwString flagText(flag);
    for (int i = 1; i < argc; ++i) {
        if (SwString(argv[i]) == flagText) {
            return true;
        }
    }
    return false;
}

static int intArgumentValue_(int argc, char** argv, const char* key, int fallbackValue) {
    if (!key) {
        return fallbackValue;
    }
    const SwString keyText(key);
    const SwString prefix = keyText + "=";
    for (int i = 1; i < argc; ++i) {
        const SwString arg(argv[i]);
        if (arg == keyText && (i + 1) < argc) {
            bool ok = false;
            const int parsed = SwString(argv[i + 1]).toInt(&ok);
            if (ok) {
                return parsed;
            }
        }
        if (arg.startsWith(prefix)) {
            bool ok = false;
            const int parsed = arg.mid(prefix.size()).toInt(&ok);
            if (ok) {
                return parsed;
            }
        }
    }
    return fallbackValue;
}

static long long steadyNowNs_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void applyProfilerScrollBarStyle_(SwScrollBar* scrollBar) {
    if (!scrollBar) {
        return;
    }

    scrollBar->setStyleSheet(R"(
        SwScrollBar {
            background-color: rgb(37, 37, 38);
            border-color: rgb(37, 37, 38);
            background-color-disabled: rgb(37, 37, 38);
            border-color-disabled: rgb(37, 37, 38);
            border-width: 0px;
            border-radius: 6px;
            padding: 3px;
            thumb-color: rgb(96, 103, 112);
            thumb-border-color: rgb(96, 103, 112);
            thumb-color-hover: rgb(122, 128, 138);
            thumb-border-color-hover: rgb(122, 128, 138);
            thumb-color-pressed: rgb(148, 154, 164);
            thumb-border-color-pressed: rgb(148, 154, 164);
            thumb-color-disabled: rgb(70, 70, 74);
            thumb-border-color-disabled: rgb(70, 70, 74);
            thumb-radius: 5px;
            thumb-min-length: 30px;
        }
    )");
}

static void applyProfilerSplitterStyle_(SwSplitter* splitter) {
    if (!splitter) {
        return;
    }

    splitter->setStyleSheet(R"(
        SwSplitter {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            handle-color: rgb(58, 58, 62);
            handle-color-hover: rgb(0, 122, 204);
            handle-color-pressed: rgb(55, 148, 255);
            handle-border-color: rgb(58, 58, 62);
            handle-border-color-hover: rgb(0, 122, 204);
            handle-border-color-pressed: rgb(55, 148, 255);
            grip-color: rgba(0,0,0,0);
            grip-color-hover: rgba(0,0,0,0);
            grip-color-pressed: rgba(0,0,0,0);
            handle-visual-width: 1px;
            handle-border-width: 0px;
            handle-radius: 0px;
        }
    )");
}

void RuntimeProfilerDashboardSink::onRuntimeBatch(const SwList<SwRuntimeTimingRecord>& records,
                                                  const SwRuntimeCountersSnapshot& counters) {
    SwMutexLocker locker(mutex_);
    latestCounters_ = counters;
    ++batchCount_;
    timingCount_ += static_cast<unsigned long long>(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        const int index = timingKindIndex_(records[i].kind);
        ++timingByKind_[index];
    }
}

void RuntimeProfilerDashboardSink::onStall(const SwRuntimeStallReport& report) {
    RuntimeProfilerDashboardStallEntry entry;
    entry.sequence = ++stallSequence_;
    entry.kind = report.kind;
    entry.label = report.label ? SwString(report.label) : SwString();
    entry.elapsedUs = report.elapsedUs;
    entry.sampleTimeNs = steadyNowNs_();
    entry.lane = report.lane;
    entry.threadId = report.threadId;
    entry.frames = report.frames;
    entry.resolvedFrames = report.resolvedFrames;
    entry.symbols = report.symbols;
    entry.symbolBackend = report.symbolBackend;
    entry.symbolSearchPath = report.symbolSearchPath;

    SwMutexLocker locker(mutex_);
    pendingStalls_.append(entry);
    if (entry.elapsedUs > worstStallUs_) {
        worstStallUs_ = entry.elapsedUs;
    }
}

RuntimeProfilerDashboardSink::Snapshot RuntimeProfilerDashboardSink::consume() {
    Snapshot snapshot;
    SwMutexLocker locker(mutex_);
    snapshot.counters = latestCounters_;
    snapshot.newStalls = pendingStalls_;
    snapshot.stallCount = stallSequence_;
    snapshot.timingCount = timingCount_;
    snapshot.batchCount = batchCount_;
    snapshot.worstStallUs = worstStallUs_;
    for (int i = 0; i < kTimingKindCount_; ++i) {
        snapshot.timingByKind[i] = timingByKind_[i];
    }
    pendingStalls_.clear();
    return snapshot;
}

RuntimeProfilerObjectReceiver::RuntimeProfilerObjectReceiver(SwGuiApplication* app)
    : app_(app) {}

void RuntimeProfilerObjectReceiver::queueStall(int durationMs) {
    {
        SwMutexLocker locker(mutex_);
        pendingDurationsMs_.append(durationMs);
    }
    if (app_) {
        app_->postEvent(this, new SwEvent(EventType::User));
    }
}

bool RuntimeProfilerObjectReceiver::event(SwEvent* event) {
    if (!event || event->type() != EventType::User) {
        return SwObject::event(event);
    }

    int durationMs = 0;
    {
        SwMutexLocker locker(mutex_);
        if (!pendingDurationsMs_.isEmpty()) {
            durationMs = pendingDurationsMs_[0];
            pendingDurationsMs_.removeFirst();
        }
    }

    if (durationMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    }
    event->accept();
    return true;
}

RuntimeProfilerDashboardWindow::RuntimeProfilerDashboardWindow(SwGuiApplication* app,
                                                               const SwRuntimeProfileConfig& profilerConfig)
    : SwMainWindow(L"Runtime Profiler Control Room", 1480, 930),
      app_(app),
      profilerConfig_(profilerConfig),
      objectReceiver_(app),
      launchTimeNs_(steadyNowNs_()) {
    setStyleSheet("SwMainWindow { background-color: rgb(30, 30, 30); }");
    stripChrome_();
    buildUi_();
    updateSummary_();
}

SwRuntimeProfileSink* RuntimeProfilerDashboardWindow::sink() {
    return &sink_;
}

void RuntimeProfilerDashboardWindow::start() {
    refreshTimerId_ = app_ ? app_->addTimer([this]() { pullProfilerState_(); }, kUiRefreshPeriodUs_) : -1;
    autoFeedTimerId_ = app_ ? app_->addTimer([this]() { scheduleNextAutoStall_(); }, kAutoFeedPeriodUs_) : -1;
    setMonitoringEnabled_(app_ && app_->profilerCaptureEnabled());
    autoFeedEnabled_ = true;
    if (autoFeedButton_) {
        autoFeedButton_->setChecked(true);
        updateAutoFeedButtonText_();
    }
    if (app_) {
        app_->addTimer([this]() { triggerBurst_(); }, 180000, true);
    }
}

void RuntimeProfilerDashboardWindow::resizeEvent(ResizeEvent* event) {
    SwMainWindow::resizeEvent(event);
    if (root_) {
        const SwRect bounds = rect();
        root_->resize(bounds.width, bounds.height);
    }
}

SwFrame* RuntimeProfilerDashboardWindow::createMetricCard_(SwWidget* parent,
                                                           const SwString& title,
                                                           const SwString& accentRgb,
                                                           SwLabel*& valueLabelOut) {
    RuntimeProfilerDashboardCard* card = new RuntimeProfilerDashboardCard(parent);
    card->setStyleSheet(R"(
        RuntimeProfilerDashboardCard {
            background-color: rgb(45, 45, 48);
            border-color: rgb(62, 62, 66);
            border-width: 1px;
            border-radius: 10px;
        }
    )");

    SwLabel* valueLabel = new SwLabel("0", card);
    valueLabel->resize(190, 34);
    valueLabel->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(220, 220, 220);
            font-size: 24px;
        }
    )");

    SwLabel* titleLabel = new SwLabel(title, card);
    titleLabel->resize(190, 18);
    titleLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: " +
                              accentRgb +
                              "; font-size: 11px; }");

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(12);
    layout->setSpacing(3);
    layout->addWidget(titleLabel, 0, 18);
    layout->addWidget(valueLabel, 0, 32);
    card->setLayout(layout);

    valueLabelOut = valueLabel;
    return card;
}

RuntimeProfilerDashboardPanel* RuntimeProfilerDashboardWindow::createPanel_(SwWidget* parent) {
    RuntimeProfilerDashboardPanel* panel = new RuntimeProfilerDashboardPanel(parent);
    panel->setStyleSheet(R"(
        RuntimeProfilerDashboardPanel {
            background-color: rgb(37, 37, 38);
            border-color: rgb(62, 62, 66);
            border-width: 1px;
            border-radius: 10px;
        }
    )");
    return panel;
}

SwPushButton* RuntimeProfilerDashboardWindow::createActionButton_(SwWidget* parent,
                                                                  const SwString& text,
                                                                  const SwString& accentRgb) {
    SwPushButton* button = new SwPushButton(text, parent);
    button->setStyleSheet("SwPushButton { background-color: rgb(45, 45, 48); color: " + accentRgb +
                          "; border-color: " + accentRgb +
                          "; border-width: 1px; border-radius: 8px; padding: 8px 12px; font-size: 12px; }");
    return button;
}

void RuntimeProfilerDashboardWindow::buildUi_() {
    root_ = new SwWidget(this);
    root_->move(0, 0);
    root_->setStyleSheet("SwWidget { background-color: rgb(30, 30, 30); border-width: 0px; }");

    SwWidget* chromeBar = new SwWidget(root_);
    chromeBar->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(37, 37, 38);
            border-color: rgb(62, 62, 66);
            border-width: 1px;
            border-radius: 10px;
        }
    )");

    SwLabel* overlineLabel = new SwLabel("SWSTACK  /  RUNTIME PROFILER", chromeBar);
    overlineLabel->resize(420, 18);
    overlineLabel->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(55, 148, 255);
            font-size: 11px;
        }
    )");

    SwLabel* titleLabel = new SwLabel("Main Thread Stall Inspector", chromeBar);
    titleLabel->resize(640, 30);
    titleLabel->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(220, 220, 220);
            font-size: 24px;
        }
    )");

    subtitleLabel_ = new SwLabel(chromeBar);
    subtitleLabel_->resize(860, 20);
    subtitleLabel_->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(156, 156, 156);
            font-size: 12px;
        }
    )");
    subtitleLabel_->setText("Timeline des stalls, main-thread occupancy, et stacks capturees hors thread UI.");

    SwWidget* headerTextBlock = new SwWidget(chromeBar);
    headerTextBlock->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    SwVerticalLayout* headerTextLayout = new SwVerticalLayout();
    headerTextLayout->setMargin(0);
    headerTextLayout->setSpacing(2);
    headerTextLayout->addWidget(overlineLabel, 0, 16);
    headerTextLayout->addWidget(titleLabel, 0, 30);
    headerTextLayout->addWidget(subtitleLabel_, 0, 18);
    headerTextBlock->setLayout(headerTextLayout);

    SwLabel* headerBadge = new SwLabel("UI thread monitor  |  off-thread stack capture", chromeBar);
    headerBadge->resize(340, 22);
    headerBadge->setStyleSheet(R"(
        SwLabel {
            background-color: rgb(30, 30, 30);
            border-color: rgb(62, 62, 66);
            border-width: 1px;
            border-radius: 8px;
            color: rgb(204, 204, 204);
            font-size: 11px;
        }
    )");

    SwHorizontalLayout* chromeLayout = new SwHorizontalLayout();
    chromeLayout->setMargin(14);
    chromeLayout->setSpacing(12);
    chromeLayout->addWidget(headerTextBlock, 1, 360);
    chromeLayout->addWidget(headerBadge, 0, 300);
    chromeBar->setLayout(chromeLayout);

    SwWidget* shellRow = new SwWidget(root_);
    shellRow->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    RuntimeProfilerDashboardPanel* sidebarPanel = createPanel_(shellRow);

    SwLabel* sidebarTitle = new SwLabel("Session Overview", sidebarPanel);
    sidebarTitle->resize(220, 22);
    sidebarTitle->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(220, 220, 220);
            font-size: 17px;
        }
    )");

    SwLabel* sidebarSubtitle = new SwLabel("Mesures live du runtime et commandes de test.", sidebarPanel);
    sidebarSubtitle->resize(250, 32);
    sidebarSubtitle->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(156, 156, 156);
            font-size: 12px;
        }
    )");

    SwWidget* cardsColumn = new SwWidget(sidebarPanel);
    cardsColumn->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    SwVerticalLayout* cardsLayout = new SwVerticalLayout();
    cardsLayout->setMargin(0);
    cardsLayout->setSpacing(8);
    cardsLayout->addWidget(createMetricCard_(cardsColumn, "STALL COUNT", "rgb(55, 148, 255)", stallCountValue_), 0, 72);
    cardsLayout->addWidget(createMetricCard_(cardsColumn, "WORST STALL", "rgb(209, 105, 105)", worstStallValue_), 0, 72);
    cardsLayout->addWidget(createMetricCard_(cardsColumn, "THREAD OCCUPANCY", "rgb(78, 201, 176)", loopLoadValue_), 0, 72);
    cardsLayout->addWidget(createMetricCard_(cardsColumn, "DROPPED TIMINGS", "rgb(220, 220, 170)", droppedValue_), 0, 72);
    cardsLayout->addWidget(createMetricCard_(cardsColumn, "QUEUED FIBERS", "rgb(197, 134, 192)", queueValue_), 0, 72);
    cardsColumn->setLayout(cardsLayout);

    SwLabel* controlsTitle = new SwLabel("Synthetic Controls", sidebarPanel);
    controlsTitle->resize(220, 22);
    controlsTitle->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(220, 220, 220);
            font-size: 17px;
        }
    )");

    SwLabel* controlsSubtitle = new SwLabel("Injection de scenarios pour valider le profiler.", sidebarPanel);
    controlsSubtitle->resize(250, 18);
    controlsSubtitle->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(156, 156, 156);
            font-size: 12px;
        }
    )");

    SwWidget* actionsColumn = new SwWidget(sidebarPanel);
    actionsColumn->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    SwVerticalLayout* actionsLayout = new SwVerticalLayout();
    actionsLayout->setMargin(0);
    actionsLayout->setSpacing(8);

    SwPushButton* manualButton = createActionButton_(actionsColumn, "Manual scope 18 ms", "rgb(55, 148, 255)");
    SwPushButton* postedButton = createActionButton_(actionsColumn, "Posted event 26 ms", "rgb(86, 156, 214)");
    SwPushButton* timerButton = createActionButton_(actionsColumn, "Timer callback 22 ms", "rgb(78, 201, 176)");
    SwPushButton* objectButton = createActionButton_(actionsColumn, "Object event 24 ms", "rgb(206, 145, 120)");
    SwPushButton* burstButton = createActionButton_(actionsColumn, "Burst sequence x4", "rgb(197, 134, 192)");
    autoFeedButton_ = createActionButton_(actionsColumn, "Auto feed OFF", "rgb(128, 128, 128)");
    autoFeedButton_->setCheckable(true);
    SwPushButton* clearButton = createActionButton_(actionsColumn, "Clear inspector", "rgb(204, 204, 204)");

    actionsLayout->addWidget(manualButton, 0, 36);
    actionsLayout->addWidget(postedButton, 0, 36);
    actionsLayout->addWidget(timerButton, 0, 36);
    actionsLayout->addWidget(objectButton, 0, 36);
    actionsLayout->addWidget(burstButton, 0, 36);
    actionsLayout->addWidget(autoFeedButton_, 0, 36);
    actionsLayout->addWidget(clearButton, 0, 36);
    actionsColumn->setLayout(actionsLayout);

    SwLabel* sidebarNote = new SwLabel("Chaque bloc du graphe des stalls occupe sa duree reelle sur l'axe du temps.", sidebarPanel);
    sidebarNote->resize(250, 44);
    sidebarNote->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(128, 128, 128);
            font-size: 11px;
        }
    )");

    SwVerticalLayout* sidebarLayout = new SwVerticalLayout();
    sidebarLayout->setMargin(14);
    sidebarLayout->setSpacing(10);
    sidebarLayout->addWidget(sidebarTitle, 0, 22);
    sidebarLayout->addWidget(sidebarSubtitle, 0, 28);
    sidebarLayout->addWidget(cardsColumn, 0, 400);
    sidebarLayout->addWidget(controlsTitle, 0, 22);
    sidebarLayout->addWidget(controlsSubtitle, 0, 18);
    sidebarLayout->addWidget(actionsColumn, 0, 260);
    sidebarLayout->addWidget(sidebarNote, 0, 40);
    sidebarLayout->addStretch(1);
    sidebarPanel->setLayout(sidebarLayout);

    SwWidget* workspaceColumn = new SwWidget(shellRow);
    workspaceColumn->setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    profilingView_ = new RuntimeProfilerProfilingView(workspaceColumn);
    profilingView_->setLaunchTimeNs(launchTimeNs_);
    profilingView_->setThresholdUs(profilerConfig_.stallThresholdUs);
    profilingView_->setMonitoringActive(monitoringActive_);

    SwVerticalLayout* workspaceLayout = new SwVerticalLayout();
    workspaceLayout->setMargin(0);
    workspaceLayout->setSpacing(0);
    workspaceLayout->addWidget(profilingView_, 1, 540);
    workspaceColumn->setLayout(workspaceLayout);

    SwHorizontalLayout* shellLayout = new SwHorizontalLayout();
    shellLayout->setMargin(0);
    shellLayout->setSpacing(10);
    shellLayout->addWidget(sidebarPanel, 0, 310);
    shellLayout->addWidget(workspaceColumn, 1, 900);
    shellRow->setLayout(shellLayout);

    SwWidget* statusBarWidget = new SwWidget(root_);
    statusBarWidget->setStyleSheet(R"(
        SwWidget {
            background-color: rgb(0, 122, 204);
            border-width: 0px;
            border-radius: 8px;
        }
    )");

    footerLabel_ = new SwLabel(statusBarWidget);
    footerLabel_->resize(1200, 18);
    footerLabel_->setStyleSheet(R"(
        SwLabel {
            background-color: rgba(0,0,0,0);
            border-width: 0px;
            color: rgb(255, 255, 255);
            font-size: 11px;
        }
    )");

    SwHorizontalLayout* statusLayout = new SwHorizontalLayout();
    statusLayout->setMargin(8);
    statusLayout->setSpacing(8);
    statusLayout->addWidget(footerLabel_, 1, 900);
    statusBarWidget->setLayout(statusLayout);

    SwVerticalLayout* rootLayout = new SwVerticalLayout();
    rootLayout->setMargin(12);
    rootLayout->setSpacing(10);
    rootLayout->addWidget(chromeBar, 0, 78);
    rootLayout->addWidget(shellRow, 1, 700);
    rootLayout->addWidget(statusBarWidget, 0, 28);
    root_->setLayout(rootLayout);

    SwObject::connect(manualButton, &SwPushButton::clicked, this, [this]() {
        triggerManualStall_("render/frame", 18);
    });
    SwObject::connect(postedButton, &SwPushButton::clicked, this, [this]() {
        triggerPostedStall_(26);
    });
    SwObject::connect(timerButton, &SwPushButton::clicked, this, [this]() {
        triggerTimerStall_(22);
    });
    SwObject::connect(objectButton, &SwPushButton::clicked, this, [this]() {
        triggerObjectStall_(24);
    });
    SwObject::connect(burstButton, &SwPushButton::clicked, this, [this]() {
        triggerBurst_();
    });
    SwObject::connect(autoFeedButton_, &SwPushButton::toggled, this, [this](bool checked) {
        autoFeedEnabled_ = checked;
        updateAutoFeedButtonText_();
        updateSummary_();
    });
    SwObject::connect(clearButton, &SwPushButton::clicked, this, [this]() {
        clearUi_();
    });
    if (profilingView_) {
        SwObject::connect(profilingView_, &RuntimeProfilerProfilingView::monitoringToggleRequested, this,
                          [this](bool enabled) {
                              setMonitoringEnabled_(enabled);
                          });
        SwObject::connect(profilingView_, &RuntimeProfilerProfilingView::thresholdChangedUs, this,
                          [this](long long thresholdUs) {
                              setThresholdUs_(thresholdUs);
                          });
    }
    const SwRect bounds = rect();
    root_->resize(bounds.width, bounds.height);
    updateAutoFeedButtonText_();
}

void RuntimeProfilerDashboardWindow::stripChrome_() {
    if (menuBar()) {
        menuBar()->hide();
    }
    if (toolBar()) {
        toolBar()->hide();
    }
    if (statusBar()) {
        statusBar()->hide();
    }
    if (centralWidget()) {
        centralWidget()->hide();
    }
    SwWidget::setLayout(nullptr);
}

void RuntimeProfilerDashboardWindow::updateAutoFeedButtonText_() {
    if (!autoFeedButton_) {
        return;
    }
    autoFeedButton_->setText(autoFeedEnabled_ ? "Auto feed ON" : "Auto feed OFF");
    autoFeedButton_->setStyleSheet(autoFeedEnabled_
                                       ? "SwPushButton { background-color: rgb(0, 122, 204); color: rgb(255, 255, 255); border-color: rgb(0, 122, 204); border-width: 1px; border-radius: 8px; padding: 8px 12px; font-size: 12px; }"
                                       : "SwPushButton { background-color: rgb(45, 45, 48); color: rgb(128, 128, 128); border-color: rgb(90, 90, 95); border-width: 1px; border-radius: 8px; padding: 8px 12px; font-size: 12px; }");
}

void RuntimeProfilerDashboardWindow::triggerManualStall_(const char* label, int durationMs) {
    if (!app_ || !label) {
        return;
    }
    app_->postEvent([label, durationMs]() {
        swProfileScope(label);
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    });
}

void RuntimeProfilerDashboardWindow::triggerPostedStall_(int durationMs) {
    if (!app_) {
        return;
    }
    app_->postEvent([durationMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    });
}

void RuntimeProfilerDashboardWindow::triggerTimerStall_(int durationMs) {
    if (!app_) {
        return;
    }
    app_->addTimer([durationMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
    }, 1000, true);
}

void RuntimeProfilerDashboardWindow::triggerObjectStall_(int durationMs) {
    objectReceiver_.queueStall(durationMs);
}

void RuntimeProfilerDashboardWindow::triggerBurst_() {
    triggerManualStall_("scene/rebuild", 17);
    triggerPostedStall_(29);
    triggerTimerStall_(23);
    triggerObjectStall_(25);
}

void RuntimeProfilerDashboardWindow::scheduleNextAutoStall_() {
    if (!autoFeedEnabled_) {
        return;
    }

    struct Pattern {
        SwRuntimeTimingKind displayKind;
        const char* label;
        int durationMs;
    };

    static const Pattern patterns[] = {
        {SwRuntimeTimingKind::ManualScope, "paint/frame", 16},
        {SwRuntimeTimingKind::PostedEvent, "", 24},
        {SwRuntimeTimingKind::Timer, "", 21},
        {SwRuntimeTimingKind::ObjectEvent, "", 23},
        {SwRuntimeTimingKind::ManualScope, "assets/decompress", 34},
        {SwRuntimeTimingKind::PostedEvent, "", 42}
    };

    const int patternCount = static_cast<int>(sizeof(patterns) / sizeof(patterns[0]));
    const Pattern& pattern = patterns[autoPatternIndex_ % patternCount];
    ++autoPatternIndex_;

    switch (pattern.displayKind) {
    case SwRuntimeTimingKind::ManualScope:
        triggerManualStall_(pattern.label, pattern.durationMs);
        break;
    case SwRuntimeTimingKind::Timer:
        triggerTimerStall_(pattern.durationMs);
        break;
    case SwRuntimeTimingKind::ObjectEvent:
        triggerObjectStall_(pattern.durationMs);
        break;
    case SwRuntimeTimingKind::PostedEvent:
    default:
        triggerPostedStall_(pattern.durationMs);
        break;
    }
}

void RuntimeProfilerDashboardWindow::setMonitoringEnabled_(bool enabled) {
    if (!app_) {
        monitoringActive_ = false;
        if (profilingView_) {
            profilingView_->setMonitoringActive(false);
        }
        updateSummary_();
        return;
    }

    if (!app_->profilerEnabled()) {
        if (!enabled) {
            monitoringActive_ = false;
            if (profilingView_) {
                profilingView_->setMonitoringActive(false);
            }
            updateSummary_();
            return;
        }
        app_->installProfiler(sink(), profilerConfig_);
    }

    if (app_->profilerEnabled()) {
        app_->setProfilerCaptureEnabled(enabled);
    }

    monitoringActive_ = app_->profilerCaptureEnabled();
    if (profilingView_) {
        profilingView_->setMonitoringActive(monitoringActive_);
    }
    updateSummary_();
}

void RuntimeProfilerDashboardWindow::setThresholdUs_(long long thresholdUs) {
    const long long clampedThresholdUs = std::max(1000LL, thresholdUs);
    if (profilerConfig_.stallThresholdUs == clampedThresholdUs) {
        if (profilingView_) {
            profilingView_->setThresholdUs(clampedThresholdUs);
        }
        updateSummary_();
        return;
    }

    profilerConfig_.stallThresholdUs = clampedThresholdUs;
    if (profilingView_) {
        profilingView_->setThresholdUs(profilerConfig_.stallThresholdUs);
    }

    if (app_ && app_->profilerEnabled()) {
        app_->setProfilerStallThresholdUs(profilerConfig_.stallThresholdUs);
    }

    updateSummary_();
}

void RuntimeProfilerDashboardWindow::pullProfilerState_() {
    if (!monitoringActive_) {
        return;
    }
    RuntimeProfilerDashboardSink::Snapshot snapshot = sink_.consume();
    applySnapshot_(snapshot);
}

void RuntimeProfilerDashboardWindow::applySnapshot_(const RuntimeProfilerDashboardSink::Snapshot& snapshot) {
    RuntimeProfilerDashboardLoadSample loadSample;
    loadSample.sampleTimeNs = steadyNowNs_();
    loadSample.loadPercentage = snapshot.counters.lastSecondLoadPercentage > 0.0
                                    ? snapshot.counters.lastSecondLoadPercentage
                                    : snapshot.counters.loadPercentage;
    loadHistory_.append(loadSample);
    while (static_cast<int>(loadHistory_.size()) > kLoadHistoryLimit_) {
        loadHistory_.removeFirst();
    }

    const unsigned long long selectedSequence = currentSelectedSequence_();
    const bool followLatest = (selectedSequence == 0) || isLatestSelected_();

    if (!snapshot.newStalls.isEmpty()) {
        for (size_t i = 0; i < snapshot.newStalls.size(); ++i) {
            stallHistory_.append(snapshot.newStalls[i]);
        }
        while (static_cast<int>(stallHistory_.size()) > kChartHistoryLimit_) {
            stallHistory_.removeFirst();
        }
    }

    stallCountValue_->setText(SwString::number(snapshot.stallCount));
    worstStallValue_->setText(durationMsString_(snapshot.worstStallUs));
    loopLoadValue_->setText(percentString_(snapshot.counters.lastSecondLoadPercentage > 0.0
                                               ? snapshot.counters.lastSecondLoadPercentage
                                               : snapshot.counters.loadPercentage));
    droppedValue_->setText(SwString::number(snapshot.counters.droppedRecords));
    queueValue_->setText(SwString::number(totalQueuedFibers_(snapshot.counters.fiberPoolStats)));
    if (profilingView_) {
        profilingView_->setObservedStallCount(snapshot.stallCount);
    }

    latestCounters_ = snapshot.counters;
    latestTimingCount_ = snapshot.timingCount;
    latestBatchCount_ = snapshot.batchCount;
    latestWorstStallUs_ = snapshot.worstStallUs;
    for (int i = 0; i < kTimingKindCount_; ++i) {
        latestTimingByKind_[i] = snapshot.timingByKind[i];
    }

    refreshProfilingView_(selectedSequence, followLatest);
    updateSummary_();
}

void RuntimeProfilerDashboardWindow::refreshProfilingView_(unsigned long long preferredSequence, bool followLatest) {
    if (!profilingView_) {
        return;
    }
    profilingView_->setLaunchTimeNs(launchTimeNs_);
    profilingView_->setThresholdUs(profilerConfig_.stallThresholdUs);
    profilingView_->rebuild(stallHistory_, loadHistory_, preferredSequence, followLatest);
}

unsigned long long RuntimeProfilerDashboardWindow::currentSelectedSequence_() const {
    return profilingView_ ? profilingView_->currentSelectedSequence() : 0;
}

bool RuntimeProfilerDashboardWindow::isLatestSelected_() const {
    return profilingView_ ? profilingView_->isLatestSelected() : true;
}

void RuntimeProfilerDashboardWindow::updateSummary_() {
    if (!footerLabel_) {
        return;
    }

    SwString summary;
    summary.append("Monitoring ");
    summary.append(monitoringActive_ ? "on" : "off");
    summary.append("  |  ");
    summary.append("Threshold ");
    summary.append(durationMsString_(profilerConfig_.stallThresholdUs));
    summary.append("  |  batches ");
    summary.append(SwString::number(latestBatchCount_));
    summary.append("  |  timings ");
    summary.append(SwString::number(latestTimingCount_));
    summary.append("  |  posted ");
    summary.append(SwString::number(latestCounters_.priorityPostedEventCount + latestCounters_.postedEventCount));
    summary.append("  |  timers ");
    summary.append(SwString::number(latestCounters_.timerCount));
    summary.append("  |  auto ");
    summary.append(autoFeedEnabled_ ? "on" : "off");
    summary.append("  |  latest occupancy ");
    summary.append(percentString_(latestCounters_.lastSecondLoadPercentage > 0.0
                                      ? latestCounters_.lastSecondLoadPercentage
                                      : latestCounters_.loadPercentage));
    summary.append("  |  worst ");
    summary.append(durationMsString_(latestWorstStallUs_));
    footerLabel_->setText(summary);
}

void RuntimeProfilerDashboardWindow::clearUi_() {
    stallHistory_.clear();
    loadHistory_.clear();
    if (profilingView_) {
        profilingView_->clearEntries();
        profilingView_->setObservedStallCount(0);
    }
    updateSummary_();
}

} // namespace

int main(int argc, char** argv) {
    SwGuiApplication app;

    SwRuntimeProfileConfig config;
    config.stallThresholdUs = 10000;
    config.monitorPeriodUs = 2000;
    config.maxStackFrames = 64;
    config.recordCapacity = 4096;
    config.enableAutoRuntimeScopes = true;
    config.enableManualScopes = true;
    config.enableStackCaptureOnStall = true;

    RuntimeProfilerDashboardWindow window(&app, config);
    if (!app.installProfiler(window.sink(), config)) {
        return 10;
    }

    window.show();
    window.start();

    if (hasFlag_(argc, argv, "--smoke")) {
        const int smokeMs = std::max(1200, intArgumentValue_(argc, argv, "--smoke-ms", 2800));
        app.addTimer([&app]() { app.quit(); }, smokeMs * 1000, true);
    }

    const int exitCode = app.exec();
    app.uninstallProfiler();
    return exitCode;
}
