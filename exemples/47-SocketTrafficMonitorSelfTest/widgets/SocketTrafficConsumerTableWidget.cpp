#include "SocketTrafficConsumerTableWidget.h"

#include "SwHeaderView.h"
#include "SwLayout.h"
#include "SwScrollBar.h"

#include <algorithm>

namespace {

static SwString humanBytes_(unsigned long long bytes) {
    const double value = static_cast<double>(bytes);
    if (value >= 1024.0 * 1024.0 * 1024.0) {
        return SwString::number(value / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
    if (value >= 1024.0 * 1024.0) {
        return SwString::number(value / (1024.0 * 1024.0), 'f', 2) + " MB";
    }
    if (value >= 1024.0) {
        return SwString::number(value / 1024.0, 'f', 1) + " KB";
    }
    return SwString::number(bytes) + " B";
}

static SwString humanRate_(unsigned long long bytesPerSecond) {
    return humanBytes_(bytesPerSecond) + "/s";
}

static SwString percentString_(double value) {
    return SwString::number(value, 'f', value >= 10.0 ? 1 : 2) + " %";
}

static void applySocketTrafficScrollBarStyle_(SwScrollBar* scrollBar) {
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

class SocketTrafficConsumerTableView : public SwTableView {
    SW_OBJECT(SocketTrafficConsumerTableView, SwTableView)

public:
    explicit SocketTrafficConsumerTableView(SwWidget* parent = nullptr)
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
        const SwColor accentText{160, 200, 255};

        SwFont baseFont(L"Segoe UI", 9, Normal);
        SwFont emphasisFont(L"Segoe UI", 9, SemiBold);

        for (int row = firstRow; row < rows && y < dataArea.y + dataArea.height; ++row) {
            const SwRect rowRect{dataArea.x, y, dataArea.width, rowHeight()};
            const bool alternate = (row % 2) == 1;
            const SwModelIndex rowIndex = model()->index(row, 0, SwModelIndex());
            const bool selected = selectionModel() && rowIndex.isValid() && selectionModel()->isSelected(rowIndex);

            painter->fillRect(rowRect, alternate ? altFill : rowFill, alternate ? altFill : rowFill, 0);
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
                const SwRect textRect{cell.x + 10, cell.y, std::max(0, cell.width - 20), cell.height};

                SwColor textColor = primaryText;
                SwFont font = baseFont;
                if (column == 0) {
                    font = emphasisFont;
                } else if (column == 1 || column == 9) {
                    textColor = selected ? SwColor{255, 255, 255} : mutedText;
                } else if (column == 5) {
                    textColor = selected ? SwColor{225, 240, 255} : accentText;
                }

                painter->drawText(textRect,
                                  text,
                                  DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter |
                                                  DrawTextFormat::SingleLine),
                                  textColor,
                                  font);

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

SocketTrafficConsumerTableWidget::SocketTrafficConsumerTableWidget(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");

    tableView_ = new SocketTrafficConsumerTableView(this);
    tableModel_ = new SwStandardItemModel(0, 10, tableView_);
    resetModelColumns_();
    tableView_->setModel(tableModel_);
    tableView_->setRowHeight(30);
    tableView_->setHeaderHeight(30);
    tableView_->setShowGrid(false);
    tableView_->setAlternatingRowColors(false);
    tableView_->setColumnsFitToWidth(true);
    tableView_->setColumnStretches(SwList<int>{16, 11, 7, 10, 10, 10, 10, 10, 8, 8});
    tableView_->setVerticalScrollBarPolicy(SwScrollBarPolicy::ScrollBarAsNeeded);
    tableView_->setHorizontalScrollBarPolicy(SwScrollBarPolicy::ScrollBarAlwaysOff);
    applySocketTrafficScrollBarStyle_(tableView_->verticalScrollBar());
    applySocketTrafficScrollBarStyle_(tableView_->horizontalScrollBar());
    if (tableView_->horizontalHeader()) {
        tableView_->horizontalHeader()->setStyleSheet(R"(
            SwHeaderView {
                background-color: rgb(37, 37, 38);
                border-color: rgb(37, 37, 38);
                border-width: 0px;
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
        SwObject::connect(tableView_->selectionModel(),
                          &SwItemSelectionModel::currentChanged,
                          this,
                          [this](const SwModelIndex&, const SwModelIndex&) {
                              if (suppressSelectionSignal_) {
                                  return;
                              }
                              emit currentConsumerChanged(currentSelectedConsumerId());
                          });
    }

    SwVerticalLayout* layout = new SwVerticalLayout();
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(tableView_, 1, 220);
    setLayout(layout);
}

SwSize SocketTrafficConsumerTableWidget::minimumSizeHint() const {
    return SwSize{420, 180};
}

void SocketTrafficConsumerTableWidget::rebuild(const SwList<SwSocketTrafficTelemetryConsumerSnapshot>& consumers,
                                               unsigned long long preferredConsumerId,
                                               bool followTop) {
    unsigned long long anchorConsumerId = 0;
    int rowOffsetWithinAnchor = 0;
    if (tableView_ && tableView_->verticalScrollBar() && tableView_->rowHeight() > 0) {
        const int scrollValue = std::max(0, tableView_->verticalScrollBar()->value());
        const int firstVisibleRow = scrollValue / tableView_->rowHeight();
        if (firstVisibleRow >= 0 && firstVisibleRow < static_cast<int>(consumerIds_.size())) {
            anchorConsumerId = consumerIds_[static_cast<size_t>(firstVisibleRow)];
            rowOffsetWithinAnchor = scrollValue % tableView_->rowHeight();
        }
    }

    suppressSelectionSignal_ = true;
    consumerIds_.clear();
    tableModel_->clear();
    resetModelColumns_();

    for (size_t i = 0; i < consumers.size(); ++i) {
        const SwSocketTrafficTelemetryConsumerSnapshot& consumer = consumers[i];
        SwList<SwStandardItem*> row;

        const SwString tooltip = consumer.consumerLabel +
                                 " | sockets " + SwString::number(consumer.socketCount) +
                                 " | open " + SwString::number(consumer.openSocketCount) +
                                 " | share " + percentString_(consumer.sharePercentOfTotal);

        SwStandardItem* consumerItem = new SwStandardItem(consumer.consumerLabel);
        SwStandardItem* classItem = new SwStandardItem(consumer.consumerClassName);
        SwStandardItem* socketsItem = new SwStandardItem(SwString::number(consumer.socketCount));
        SwStandardItem* rxNowItem = new SwStandardItem(humanRate_(consumer.rxRateBytesPerSecond));
        SwStandardItem* txNowItem = new SwStandardItem(humanRate_(consumer.txRateBytesPerSecond));
        SwStandardItem* totalNowItem = new SwStandardItem(humanRate_(consumer.totalRateBytesPerSecond));
        SwStandardItem* rxTotalItem = new SwStandardItem(humanBytes_(consumer.rxBytesTotal));
        SwStandardItem* txTotalItem = new SwStandardItem(humanBytes_(consumer.txBytesTotal));
        SwStandardItem* shareItem = new SwStandardItem(percentString_(consumer.sharePercentOfTotal));
        SwStandardItem* stateItem = new SwStandardItem(consumer.stateLabel);

        consumerItem->setToolTip(tooltip);
        classItem->setToolTip(tooltip);
        socketsItem->setToolTip(tooltip);
        rxNowItem->setToolTip(tooltip);
        txNowItem->setToolTip(tooltip);
        totalNowItem->setToolTip(tooltip);
        rxTotalItem->setToolTip(tooltip);
        txTotalItem->setToolTip(tooltip);
        shareItem->setToolTip(tooltip);
        stateItem->setToolTip(tooltip);

        row.append(consumerItem);
        row.append(classItem);
        row.append(socketsItem);
        row.append(rxNowItem);
        row.append(txNowItem);
        row.append(totalNowItem);
        row.append(rxTotalItem);
        row.append(txTotalItem);
        row.append(shareItem);
        row.append(stateItem);
        tableModel_->appendRow(row);
        consumerIds_.append(consumer.consumerId);
    }

    int rowToSelect = -1;
    if (followTop && tableModel_->rowCount() > 0) {
        rowToSelect = 0;
    } else if (preferredConsumerId != 0) {
        rowToSelect = rowForConsumerId_(preferredConsumerId);
    }
    if (rowToSelect < 0 && tableModel_->rowCount() > 0) {
        rowToSelect = 0;
    }

    if (rowToSelect >= 0 && rowToSelect < tableModel_->rowCount()) {
        selectRow_(rowToSelect);
    }

    restoreViewport_(anchorConsumerId, rowOffsetWithinAnchor, followTop);
    suppressSelectionSignal_ = false;
    emit currentConsumerChanged(currentSelectedConsumerId());
}

void SocketTrafficConsumerTableWidget::clearEntries() {
    consumerIds_.clear();
    tableModel_->clear();
    resetModelColumns_();
    emit currentConsumerChanged(0);
}

unsigned long long SocketTrafficConsumerTableWidget::currentSelectedConsumerId() const {
    if (!tableView_ || !tableView_->selectionModel()) {
        return 0;
    }
    const SwModelIndex current = tableView_->selectionModel()->currentIndex();
    if (!current.isValid()) {
        return 0;
    }
    const int row = current.row();
    if (row < 0 || row >= static_cast<int>(consumerIds_.size())) {
        return 0;
    }
    return consumerIds_[static_cast<size_t>(row)];
}

bool SocketTrafficConsumerTableWidget::isTopSelected() const {
    if (!tableView_ || !tableView_->selectionModel()) {
        return true;
    }
    const SwModelIndex current = tableView_->selectionModel()->currentIndex();
    return !current.isValid() || current.row() == 0;
}

bool SocketTrafficConsumerTableWidget::isPinnedToTop() const {
    if (!tableView_ || !tableView_->verticalScrollBar()) {
        return true;
    }
    return tableView_->verticalScrollBar()->value() <= 0;
}

void SocketTrafficConsumerTableWidget::resetModelColumns_() {
    if (!tableModel_) {
        return;
    }
    tableModel_->setColumnCount(10);
    tableModel_->setHorizontalHeaderLabels(
        SwList<SwString>{"Consumer", "Class", "Sockets", "RX now", "TX now", "Total now", "RX total", "TX total", "Share", "State"});
}

int SocketTrafficConsumerTableWidget::rowForConsumerId_(unsigned long long consumerId) const {
    for (int row = 0; row < static_cast<int>(consumerIds_.size()); ++row) {
        if (consumerIds_[static_cast<size_t>(row)] == consumerId) {
            return row;
        }
    }
    return -1;
}

void SocketTrafficConsumerTableWidget::selectRow_(int row) {
    if (!tableView_ || !tableView_->selectionModel() || !tableModel_) {
        return;
    }
    SwList<SwModelIndex> selection;
    selection.append(tableModel_->index(row, 0));
    tableView_->selectionModel()->setSelectedIndexes(selection);
    tableView_->selectionModel()->setCurrentIndex(tableModel_->index(row, 0), false, false);
}

void SocketTrafficConsumerTableWidget::restoreViewport_(unsigned long long anchorConsumerId,
                                                        int rowOffsetWithinAnchor,
                                                        bool followTop) {
    if (!tableView_ || !tableView_->verticalScrollBar() || tableView_->rowHeight() <= 0) {
        return;
    }

    SwScrollBar* vBar = tableView_->verticalScrollBar();
    if (!followTop && anchorConsumerId != 0) {
        const int anchorRow = rowForConsumerId_(anchorConsumerId);
        if (anchorRow >= 0) {
            const int target = anchorRow * tableView_->rowHeight() + std::max(0, rowOffsetWithinAnchor);
            vBar->setValue(std::min(target, vBar->maximum()));
            return;
        }
    }

    if (followTop) {
        vBar->setValue(0);
    }
}
