/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

#include <algorithm>
#include <numeric>
#include <vector>

#include "SwWidgetInterface.h"

class SwAbstractLayout : public SwObject {
public:
    explicit SwAbstractLayout(SwWidgetInterface* parent = nullptr)
        : SwObject(parent)
        , m_parentWidget(parent)
        , m_spacing(8)
        , m_margin(8) {}

    virtual ~SwAbstractLayout() = default;

    void setParentWidget(SwWidgetInterface* parent) {
        m_parentWidget = parent;
    }

    SwWidgetInterface* parentWidget() const {
        return m_parentWidget;
    }

    void setSpacing(int spacing) {
        m_spacing = std::max(0, spacing);
        updateGeometry();
    }

    int spacing() const {
        return m_spacing;
    }

    void setMargin(int margin) {
        m_margin = std::max(0, margin);
        updateGeometry();
    }

    int margin() const {
        return m_margin;
    }

    void updateGeometry() {
        if (!m_parentWidget) {
            return;
        }
        SwRect rect = contentRect();
        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }
        applyLayout(rect);
    }

protected:
    SwRect contentRect() const {
        SwRect parentRect = {0, 0, 0, 0};
        if (m_parentWidget) {
            parentRect = m_parentWidget->getRect();
        }
        SwRect rect;
        rect.x = parentRect.x + m_margin;
        rect.y = parentRect.y + m_margin;
        rect.width = std::max(0, parentRect.width - 2 * m_margin);
        rect.height = std::max(0, parentRect.height - 2 * m_margin);
        return rect;
    }

    virtual void applyLayout(const SwRect& rect) = 0;

private:
    SwWidgetInterface* m_parentWidget;
    int m_spacing;
    int m_margin;
};

class SwBoxLayout : public SwAbstractLayout {
public:
    struct Item {
        SwWidgetInterface* widget{nullptr};
        int stretch{0};
        int minSize{-1};
    };

    explicit SwBoxLayout(SwWidgetInterface* parent = nullptr)
        : SwAbstractLayout(parent) {}

    virtual ~SwBoxLayout() = default;

    void addWidget(SwWidgetInterface* widget, int stretch = 0, int minSize = -1) {
        if (!widget) {
            return;
        }
        for (const Item& existing : m_items) {
            if (existing.widget == widget) {
                return;
            }
        }
        Item item;
        item.widget = widget;
        item.stretch = stretch;
        item.minSize = minSize;
        m_items.push_back(item);
        updateGeometry();
    }

    void insertWidget(size_t index, SwWidgetInterface* widget, int stretch = 0, int minSize = -1) {
        if (!widget) {
            return;
        }
        index = std::min(index, m_items.size());
        Item item;
        item.widget = widget;
        item.stretch = stretch;
        item.minSize = minSize;
        m_items.insert(m_items.begin() + index, item);
        updateGeometry();
    }

    void removeWidget(SwWidgetInterface* widget) {
        m_items.erase(std::remove_if(m_items.begin(),
                                     m_items.end(),
                                     [widget](const Item& item) { return item.widget == widget; }),
                      m_items.end());
        updateGeometry();
    }

protected:
    const std::vector<Item>& items() const {
        return m_items;
    }

private:
    std::vector<Item> m_items;
};

class SwVerticalLayout : public SwBoxLayout {
public:
    explicit SwVerticalLayout(SwWidgetInterface* parent = nullptr)
        : SwBoxLayout(parent) {}

protected:
    using BoxItem = SwBoxLayout::Item;

    void applyLayout(const SwRect& rect) override {
        const auto& entries = items();
        if (entries.empty()) {
            return;
        }

        int spacingAmount = spacing();
        int availableHeight = std::max(0, rect.height - std::max(0, static_cast<int>(entries.size()) - 1) * spacingAmount);

        std::vector<int> minSizes;
        minSizes.reserve(entries.size());
        int totalMin = 0;
        int totalStretch = 0;
        for (const BoxItem& item : entries) {
            int preferred = item.minSize >= 0 ? item.minSize : item.widget->minimumSizeHint().height;
            minSizes.push_back(preferred);
            totalMin += preferred;
            totalStretch += std::max(0, item.stretch);
        }

        if (totalMin > availableHeight && totalMin > 0) {
            int scaledTotal = 0;
            for (size_t i = 0; i < minSizes.size(); ++i) {
                minSizes[i] = static_cast<int>((static_cast<long long>(minSizes[i]) * availableHeight) / totalMin);
                scaledTotal += minSizes[i];
            }
            int remainder = availableHeight - scaledTotal;
            for (size_t i = 0; remainder > 0 && i < minSizes.size(); ++i, --remainder) {
                ++minSizes[i];
            }
            totalMin = availableHeight;
        }

        int extra = std::max(0, availableHeight - totalMin);

        int y = rect.y;
        int remainingExtra = extra;
        int remainingStretch = totalStretch;
        for (size_t i = 0; i < entries.size(); ++i) {
            const BoxItem& item = entries[i];
            int height = minSizes[i];
            if (remainingExtra > 0 && remainingStretch > 0 && item.stretch > 0) {
                int allocated = (remainingStretch == item.stretch)
                                    ? remainingExtra
                                    : static_cast<int>((static_cast<long long>(remainingExtra) * item.stretch) / remainingStretch);
                height += allocated;
                remainingExtra -= allocated;
                remainingStretch -= item.stretch;
            }
            item.widget->move(rect.x, y);
            item.widget->resize(rect.width, height);
            y += height + spacingAmount;
        }
    }
};

class SwHorizontalLayout : public SwBoxLayout {
public:
    explicit SwHorizontalLayout(SwWidgetInterface* parent = nullptr)
        : SwBoxLayout(parent) {}

protected:
    using BoxItem = SwBoxLayout::Item;

    void applyLayout(const SwRect& rect) override {
        const auto& entries = items();
        if (entries.empty()) {
            return;
        }

        int spacingAmount = spacing();
        int availableWidth = std::max(0, rect.width - std::max(0, static_cast<int>(entries.size()) - 1) * spacingAmount);

        std::vector<int> minSizes;
        minSizes.reserve(entries.size());
        int totalMin = 0;
        int totalStretch = 0;
        for (const BoxItem& item : entries) {
            int preferred = item.minSize >= 0 ? item.minSize : item.widget->minimumSizeHint().width;
            minSizes.push_back(preferred);
            totalMin += preferred;
            totalStretch += std::max(0, item.stretch);
        }

        if (totalMin > availableWidth && totalMin > 0) {
            int scaledTotal = 0;
            for (size_t i = 0; i < minSizes.size(); ++i) {
                minSizes[i] = static_cast<int>((static_cast<long long>(minSizes[i]) * availableWidth) / totalMin);
                scaledTotal += minSizes[i];
            }
            int remainder = availableWidth - scaledTotal;
            for (size_t i = 0; remainder > 0 && i < minSizes.size(); ++i, --remainder) {
                ++minSizes[i];
            }
            totalMin = availableWidth;
        }

        int extra = std::max(0, availableWidth - totalMin);

        int x = rect.x;
        int remainingExtra = extra;
        int remainingStretch = totalStretch;
        for (size_t i = 0; i < entries.size(); ++i) {
            const BoxItem& item = entries[i];
            int width = minSizes[i];
            if (remainingExtra > 0 && remainingStretch > 0 && item.stretch > 0) {
                int allocated = (remainingStretch == item.stretch)
                                    ? remainingExtra
                                    : static_cast<int>((static_cast<long long>(remainingExtra) * item.stretch) / remainingStretch);
                width += allocated;
                remainingExtra -= allocated;
                remainingStretch -= item.stretch;
            }
            item.widget->move(x, rect.y);
            item.widget->resize(width, rect.height);
            x += width + spacingAmount;
        }
    }
};

class SwGridLayout : public SwAbstractLayout {
public:
    struct Cell {
        SwWidgetInterface* widget{nullptr};
        int row{0};
        int column{0};
        int rowSpan{1};
        int columnSpan{1};
    };

    explicit SwGridLayout(SwWidgetInterface* parent = nullptr)
        : SwAbstractLayout(parent)
        , m_horizontalSpacing(spacing())
        , m_verticalSpacing(spacing()) {}

    void setHorizontalSpacing(int value) {
        m_horizontalSpacing = std::max(0, value);
        updateGeometry();
    }

    void setVerticalSpacing(int value) {
        m_verticalSpacing = std::max(0, value);
        updateGeometry();
    }

    void setColumnStretch(int column, int stretch) {
        if (column < 0) {
            return;
        }
        if (static_cast<size_t>(column) >= m_columnStretch.size()) {
            m_columnStretch.resize(column + 1, 1);
        }
        m_columnStretch[column] = std::max(0, stretch);
        updateGeometry();
    }

    void setRowStretch(int row, int stretch) {
        if (row < 0) {
            return;
        }
        if (static_cast<size_t>(row) >= m_rowStretch.size()) {
            m_rowStretch.resize(row + 1, 1);
        }
        m_rowStretch[row] = std::max(0, stretch);
        updateGeometry();
    }

    void addWidget(SwWidgetInterface* widget, int row, int column, int rowSpan = 1, int columnSpan = 1) {
        if (!widget || row < 0 || column < 0 || rowSpan <= 0 || columnSpan <= 0) {
            return;
        }
        Cell cell;
        cell.widget = widget;
        cell.row = row;
        cell.column = column;
        cell.rowSpan = rowSpan;
        cell.columnSpan = columnSpan;
        m_cells.push_back(cell);
        updateGeometry();
    }

    void removeWidget(SwWidgetInterface* widget) {
        m_cells.erase(std::remove_if(m_cells.begin(),
                                     m_cells.end(),
                                     [widget](const Cell& cell) { return cell.widget == widget; }),
                      m_cells.end());
        updateGeometry();
    }

protected:
    void applyLayout(const SwRect& rect) override {
        if (m_cells.empty()) {
            return;
        }

        int rows = 0;
        int columns = 0;
        for (const Cell& cell : m_cells) {
            rows = std::max(rows, cell.row + cell.rowSpan);
            columns = std::max(columns, cell.column + cell.columnSpan);
        }
        if (rows == 0 || columns == 0) {
            return;
        }

        if (static_cast<size_t>(rows) > m_rowStretch.size()) {
            m_rowStretch.resize(rows, 1);
        }
        if (static_cast<size_t>(columns) > m_columnStretch.size()) {
            m_columnStretch.resize(columns, 1);
        }

        std::vector<int> rowMin(rows, 0);
        std::vector<int> columnMin(columns, 0);

        for (const Cell& cell : m_cells) {
            SwRect pref = cell.widget->getRect();
            int shareHeight = cell.rowSpan > 0 ? pref.height / cell.rowSpan : pref.height;
            for (int i = 0; i < cell.rowSpan; ++i) {
                int rowIndex = cell.row + i;
                if (rowIndex < rows) {
                    rowMin[rowIndex] = std::max(rowMin[rowIndex], shareHeight);
                }
            }

            int shareWidth = cell.columnSpan > 0 ? pref.width / cell.columnSpan : pref.width;
            for (int i = 0; i < cell.columnSpan; ++i) {
                int colIndex = cell.column + i;
                if (colIndex < columns) {
                    columnMin[colIndex] = std::max(columnMin[colIndex], shareWidth);
                }
            }
        }

        auto distributeSizes = [](int available, std::vector<int>& mins, const std::vector<int>& stretches, int count) {
            int totalMin = std::accumulate(mins.begin(), mins.end(), 0);
            if (available < totalMin && totalMin > 0) {
                int scaledTotal = 0;
                for (int i = 0; i < count; ++i) {
                    mins[i] = static_cast<int>((static_cast<long long>(mins[i]) * available) / totalMin);
                    scaledTotal += mins[i];
                }
                int remainder = available - scaledTotal;
                for (int i = 0; remainder > 0 && i < count; ++i, --remainder) {
                    ++mins[i];
                }
                totalMin = available;
            }

            int extra = std::max(0, available - totalMin);
            int stretchTotal = 0;
            for (int i = 0; i < count; ++i) {
                stretchTotal += std::max(0, stretches[i]);
            }
            if (stretchTotal > 0) {
                int remainingExtra = extra;
                int remainingStretch = stretchTotal;
                for (int i = 0; i < count; ++i) {
                    int stretch = std::max(0, stretches[i]);
                    if (stretch > 0 && remainingExtra > 0 && remainingStretch > 0) {
                        int allocated = (remainingStretch == stretch)
                                            ? remainingExtra
                                            : static_cast<int>((static_cast<long long>(remainingExtra) * stretch) / remainingStretch);
                        mins[i] += allocated;
                        remainingExtra -= allocated;
                        remainingStretch -= stretch;
                    }
                }
            } else if (extra > 0 && count > 0) {
                int per = extra / count;
                int remainder = extra % count;
                for (int i = 0; i < count; ++i) {
                    mins[i] += per;
                    if (remainder > 0) {
                        ++mins[i];
                        --remainder;
                    }
                }
            }
        };

        int totalVerticalSpacing = std::max(0, rows - 1) * m_verticalSpacing;
        int totalHorizontalSpacing = std::max(0, columns - 1) * m_horizontalSpacing;

        distributeSizes(std::max(0, rect.height - totalVerticalSpacing), rowMin, m_rowStretch, rows);
        distributeSizes(std::max(0, rect.width - totalHorizontalSpacing), columnMin, m_columnStretch, columns);

        std::vector<int> rowPositions(rows, rect.y);
        std::vector<int> columnPositions(columns, rect.x);

        int current = rect.y;
        for (int i = 0; i < rows; ++i) {
            rowPositions[i] = current;
            current += rowMin[i] + m_verticalSpacing;
        }
        current = rect.x;
        for (int i = 0; i < columns; ++i) {
            columnPositions[i] = current;
            current += columnMin[i] + m_horizontalSpacing;
        }

        for (const Cell& cell : m_cells) {
            int x = columnPositions[cell.column];
            int y = rowPositions[cell.row];

            int width = 0;
            for (int i = 0; i < cell.columnSpan && (cell.column + i) < columns; ++i) {
                width += columnMin[cell.column + i];
            }
            width += m_horizontalSpacing * (cell.columnSpan - 1);

            int height = 0;
            for (int i = 0; i < cell.rowSpan && (cell.row + i) < rows; ++i) {
                height += rowMin[cell.row + i];
            }
            height += m_verticalSpacing * (cell.rowSpan - 1);

            cell.widget->move(x, y);
            cell.widget->resize(width, height);
        }
    }

private:
    std::vector<Cell> m_cells;
    std::vector<int> m_columnStretch;
    std::vector<int> m_rowStretch;
    int m_horizontalSpacing;
    int m_verticalSpacing;
};

class SwFormLayout : public SwAbstractLayout {
public:
    struct Row {
        SwWidgetInterface* label{nullptr};
        SwWidgetInterface* field{nullptr};
    };

    explicit SwFormLayout(SwWidgetInterface* parent = nullptr)
        : SwAbstractLayout(parent) {}

    void addRow(SwWidgetInterface* label, SwWidgetInterface* field) {
        if (!label || !field) {
            return;
        }
        Row r;
        r.label = label;
        r.field = field;
        m_rows.push_back(r);
        updateGeometry();
    }

    void setCell(int row, int column, SwWidgetInterface* widget) {
        if (!widget || row < 0) {
            return;
        }

        const size_t rowIndex = static_cast<size_t>(row);
        if (rowIndex >= m_rows.size()) {
            m_rows.resize(rowIndex + 1);
        }

        if (column == 0) {
            m_rows[rowIndex].label = widget;
        } else if (column == 1) {
            m_rows[rowIndex].field = widget;
        } else {
            // QFormLayout is typically 2 columns; keep best-effort behavior.
            if (!m_rows[rowIndex].field) {
                m_rows[rowIndex].field = widget;
            } else {
                m_rows[rowIndex].label = widget;
            }
        }

        updateGeometry();
    }

    // Convenience: pair widgets sequentially (label, field, label, field...)
    void addWidget(SwWidgetInterface* widget) {
        if (!widget) {
            return;
        }
        if (!m_pending) {
            m_pending = widget;
            return;
        }
        addRow(m_pending, widget);
        m_pending = nullptr;
    }

    void removeWidget(SwWidgetInterface* widget) {
        if (!widget) {
            return;
        }
        if (m_pending == widget) {
            m_pending = nullptr;
        }
        m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [widget](const Row& r) {
            return r.label == widget || r.field == widget;
        }), m_rows.end());
        updateGeometry();
    }

protected:
    void applyLayout(const SwRect& rect) override {
        if (m_rows.empty()) {
            return;
        }

        int labelW = 0;
        std::vector<size_t> rowIndices;
        rowIndices.reserve(m_rows.size());
        for (size_t i = 0; i < m_rows.size(); ++i) {
            const Row& r = m_rows[i];
            if (!r.label || !r.field) {
                continue;
            }
            rowIndices.push_back(i);
            const SwRect minR = r.label->minimumSizeHint();
            labelW = std::max(labelW, std::max(0, minR.width));
        }
        if (rowIndices.empty()) {
            return;
        }

        const int colSpacing = spacing();
        labelW = std::min(labelW, std::max(0, rect.width / 2));

        const int fieldX = rect.x + labelW + colSpacing;
        const int fieldW = std::max(0, rect.width - labelW - colSpacing);

        std::vector<int> rowHeights;
        rowHeights.reserve(rowIndices.size());

        int totalMinHeights = 0;
        for (size_t idx : rowIndices) {
            const Row& r = m_rows[idx];
            const SwRect minL = r.label->minimumSizeHint();
            const SwRect minF = r.field->minimumSizeHint();
            const int rowH = std::max(20, std::max(minL.height, minF.height));
            rowHeights.push_back(rowH);
            totalMinHeights += rowH;
        }

        const int rowSpacing = spacing();
        const int totalSpacing = rowSpacing * std::max(0, static_cast<int>(rowHeights.size()) - 1);
        const int required = totalMinHeights + totalSpacing;
        const int extra = rect.height - required;
        if (extra > 0) {
            rowHeights.back() += extra;
        }

        int y = rect.y;
        for (size_t i = 0; i < rowIndices.size(); ++i) {
            const Row& r = m_rows[rowIndices[i]];
            const int rowH = rowHeights[i];

            r.label->move(rect.x, y);
            r.label->resize(labelW, rowH);

            r.field->move(fieldX, y);
            r.field->resize(fieldW, rowH);

            y += rowH;
            if (i + 1 < rowIndices.size()) {
                y += rowSpacing;
            }
        }
    }

private:
    std::vector<Row> m_rows;
    SwWidgetInterface* m_pending{nullptr};
};
