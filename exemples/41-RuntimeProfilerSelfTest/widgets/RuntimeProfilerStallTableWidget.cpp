#include "RuntimeProfilerStallTableWidget.h"

#include "SwHeaderView.h"
#include "SwLayout.h"
#include "SwScrollBar.h"

namespace {

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

} // namespace

RuntimeProfilerStallTableWidget::RuntimeProfilerStallTableWidget(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

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
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(tableView_, 1, 220);
    setLayout(layout);
}

SwSize RuntimeProfilerStallTableWidget::minimumSizeHint() const {
    return SwSize{360, 180};
}

void RuntimeProfilerStallTableWidget::setLaunchTimeNs(long long launchTimeNs) {
    launchTimeNs_ = launchTimeNs;
}

void RuntimeProfilerStallTableWidget::setThresholdUs(long long thresholdUs) {
    thresholdUs_ = thresholdUs;
}

void RuntimeProfilerStallTableWidget::rebuild(const SwList<RuntimeProfilerDashboardStallEntry>& entries,
                                              unsigned long long preferredSequence,
                                              bool followLatest) {
    unsigned long long anchorSequence = 0;
    int rowOffsetWithinAnchor = 0;
    if (tableView_ && tableView_->verticalScrollBar() && tableView_->rowHeight() > 0) {
        const int scrollValue = std::max(0, tableView_->verticalScrollBar()->value());
        const int firstVisibleRow = scrollValue / tableView_->rowHeight();
        if (firstVisibleRow >= 0 && firstVisibleRow < static_cast<int>(sequences_.size())) {
            anchorSequence = sequences_[static_cast<size_t>(firstVisibleRow)];
            rowOffsetWithinAnchor = scrollValue % tableView_->rowHeight();
        }
    }

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
                                 "  |  thread " + SwString::number(entry.threadId) +
                                 "  |  threshold " + durationMsString_(thresholdUs_);
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

    restoreViewport_(anchorSequence, rowOffsetWithinAnchor, followLatest);
}

void RuntimeProfilerStallTableWidget::clearEntries() {
    sequences_.clear();
    tableModel_->clear();
    resetModelColumns_();
    emit currentSequenceChanged(0);
}

unsigned long long RuntimeProfilerStallTableWidget::currentSelectedSequence() const {
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

bool RuntimeProfilerStallTableWidget::isLatestSelected() const {
    if (!tableView_ || !tableView_->selectionModel()) {
        return true;
    }
    const SwModelIndex current = tableView_->selectionModel()->currentIndex();
    return !current.isValid() || current.row() == 0;
}

bool RuntimeProfilerStallTableWidget::isPinnedToTop() const {
    if (!tableView_ || !tableView_->verticalScrollBar()) {
        return true;
    }
    return tableView_->verticalScrollBar()->value() <= 0;
}

void RuntimeProfilerStallTableWidget::resetModelColumns_() {
    if (!tableModel_) {
        return;
    }
    tableModel_->setColumnCount(4);
    tableModel_->setHorizontalHeaderLabels(SwList<SwString>{"Time", "Duration", "Kind", "Scope"});
}

int RuntimeProfilerStallTableWidget::rowForSequence_(unsigned long long sequence) const {
    for (int row = 0; row < static_cast<int>(sequences_.size()); ++row) {
        if (sequences_[static_cast<size_t>(row)] == sequence) {
            return row;
        }
    }
    return -1;
}

void RuntimeProfilerStallTableWidget::selectRow_(int row) {
    if (!tableView_ || !tableView_->selectionModel() || !tableModel_) {
        return;
    }
    SwList<SwModelIndex> selection;
    selection.append(tableModel_->index(row, 0));
    tableView_->selectionModel()->setSelectedIndexes(selection);
    tableView_->selectionModel()->setCurrentIndex(tableModel_->index(row, 0), false, false);
}

void RuntimeProfilerStallTableWidget::restoreViewport_(unsigned long long anchorSequence,
                                                       int rowOffsetWithinAnchor,
                                                       bool followLatest) {
    if (!tableView_ || !tableView_->verticalScrollBar() || tableView_->rowHeight() <= 0) {
        return;
    }

    SwScrollBar* vBar = tableView_->verticalScrollBar();
    if (!followLatest && anchorSequence != 0) {
        const int anchorRow = rowForSequence_(anchorSequence);
        if (anchorRow >= 0) {
            const int target = anchorRow * tableView_->rowHeight() + std::max(0, rowOffsetWithinAnchor);
            vBar->setValue(std::min(target, vBar->maximum()));
            return;
        }
    }

    if (followLatest) {
        vBar->setValue(0);
    }
}

double RuntimeProfilerStallTableWidget::secondsSinceLaunch_(long long sampleTimeNs) const {
    if (sampleTimeNs <= 0 || launchTimeNs_ <= 0) {
        return 0.0;
    }
    return static_cast<double>(sampleTimeNs - launchTimeNs_) / 1000000000.0;
}
