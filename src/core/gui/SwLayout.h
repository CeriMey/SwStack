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

/**
 * @file src/core/gui/SwLayout.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwLayout in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the layout interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwSizePolicy, SwLayoutItem, SwSpacerItem,
 * SwAbstractLayout, SwBoxLayout, SwVerticalLayout, SwHorizontalLayout, SwGridLayout, and
 * SwFormLayout.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

#include "SwWidgetInterface.h"

enum class SwOrientationFlag {
    Horizontal = 0x1,
    Vertical = 0x2
};

using SwOrientations = SwFlagSet<SwOrientationFlag>;

class SwSizePolicy {
public:
    enum PolicyFlag {
        GrowFlag = 0x1,
        ExpandFlag = 0x2,
        ShrinkFlag = 0x4,
        IgnoreFlag = 0x8
    };

    enum Policy {
        Fixed = 0,
        Minimum = GrowFlag,
        Maximum = ShrinkFlag,
        Preferred = GrowFlag | ShrinkFlag,
        MinimumExpanding = GrowFlag | ExpandFlag,
        Expanding = GrowFlag | ShrinkFlag | ExpandFlag,
        Ignored = GrowFlag | ShrinkFlag | IgnoreFlag
    };

    explicit SwSizePolicy(Policy horizontalPolicy = Preferred, Policy verticalPolicy = Preferred)
        : m_horizontalPolicy(horizontalPolicy)
        , m_verticalPolicy(verticalPolicy) {}

    void setHorizontalPolicy(Policy policy) {
        m_horizontalPolicy = policy;
    }

    void setVerticalPolicy(Policy policy) {
        m_verticalPolicy = policy;
    }

    Policy horizontalPolicy() const {
        return m_horizontalPolicy;
    }

    Policy verticalPolicy() const {
        return m_verticalPolicy;
    }

    SwOrientations expandingDirections() const {
        SwOrientations out;
        if (hasExpandFlag(m_horizontalPolicy)) {
            out |= SwOrientationFlag::Horizontal;
        }
        if (hasExpandFlag(m_verticalPolicy)) {
            out |= SwOrientationFlag::Vertical;
        }
        return out;
    }

    static bool canGrow(Policy policy) {
        return (static_cast<int>(policy) & GrowFlag) != 0;
    }

    static bool canShrink(Policy policy) {
        return (static_cast<int>(policy) & ShrinkFlag) != 0;
    }

    static bool hasExpandFlag(Policy policy) {
        return (static_cast<int>(policy) & ExpandFlag) != 0;
    }

    static bool shouldIgnoreSizeHint(Policy policy) {
        return (static_cast<int>(policy) & IgnoreFlag) != 0;
    }

private:
    Policy m_horizontalPolicy;
    Policy m_verticalPolicy;
};

class SwAbstractLayout;
class SwLayoutItem;
class SwSpacerItem;

class SwLayoutItem {
public:
    explicit SwLayoutItem(int alignment = 0)
        : m_alignment(alignment) {}

    virtual ~SwLayoutItem() = default;

    int alignment() const {
        return m_alignment;
    }

    void setAlignment(int alignment) {
        m_alignment = alignment;
    }

    virtual SwOrientations expandingDirections() const = 0;
    virtual SwRect geometry() const = 0;
    virtual bool isEmpty() const = 0;
    virtual SwAbstractLayout* layout() {
        return nullptr;
    }
    virtual SwSize maximumSize() const = 0;
    virtual SwSize minimumSize() const = 0;
    virtual void setGeometry(const SwRect& rect) = 0;
    virtual SwSize sizeHint() const = 0;
    virtual SwSpacerItem* spacerItem() {
        return nullptr;
    }
    virtual SwWidgetInterface* widget() const {
        return nullptr;
    }

private:
    int m_alignment;
};

class SwWidgetItem : public SwLayoutItem {
public:
    explicit SwWidgetItem(SwWidgetInterface* widget)
        : m_widget(widget) {}

    SwOrientations expandingDirections() const override {
        return {};
    }

    SwRect geometry() const override {
        return m_geometry;
    }

    bool isEmpty() const override {
        return m_widget == nullptr;
    }

    SwSize maximumSize() const override {
        return {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    }

    SwSize minimumSize() const override {
        return m_widget ? m_widget->minimumSizeHint() : SwSize{0, 0};
    }

    void setGeometry(const SwRect& rect) override {
        m_geometry = rect;
        if (!m_widget) {
            return;
        }
        m_widget->move(rect.x, rect.y);
        m_widget->resize(rect.width, rect.height);
    }

    SwSize sizeHint() const override {
        return m_widget ? m_widget->sizeHint() : SwSize{0, 0};
    }

    SwWidgetInterface* widget() const override {
        return m_widget;
    }

private:
    SwWidgetInterface* m_widget{nullptr};
    SwRect m_geometry{0, 0, 0, 0};
};

class SwSpacerItem : public SwLayoutItem {
public:
    explicit SwSpacerItem(int width,
                          int height,
                          SwSizePolicy::Policy horizontalPolicy = SwSizePolicy::Minimum,
                          SwSizePolicy::Policy verticalPolicy = SwSizePolicy::Minimum)
        : m_width(std::max(0, width))
        , m_height(std::max(0, height))
        , m_sizePolicy(horizontalPolicy, verticalPolicy) {}

    void changeSize(int width,
                    int height,
                    SwSizePolicy::Policy horizontalPolicy = SwSizePolicy::Minimum,
                    SwSizePolicy::Policy verticalPolicy = SwSizePolicy::Minimum) {
        m_width = std::max(0, width);
        m_height = std::max(0, height);
        m_sizePolicy.setHorizontalPolicy(horizontalPolicy);
        m_sizePolicy.setVerticalPolicy(verticalPolicy);
    }

    SwSizePolicy sizePolicy() const {
        return m_sizePolicy;
    }

    SwOrientations expandingDirections() const override {
        return m_sizePolicy.expandingDirections();
    }

    SwRect geometry() const override {
        return m_geometry;
    }

    bool isEmpty() const override {
        return true;
    }

    SwSize maximumSize() const override {
        return {
            maximumForAxis_(m_width, m_sizePolicy.horizontalPolicy()),
            maximumForAxis_(m_height, m_sizePolicy.verticalPolicy())
        };
    }

    SwSize minimumSize() const override {
        return {
            minimumForAxis_(m_width, m_sizePolicy.horizontalPolicy()),
            minimumForAxis_(m_height, m_sizePolicy.verticalPolicy())
        };
    }

    void setGeometry(const SwRect& rect) override {
        m_geometry = rect;
    }

    SwSize sizeHint() const override {
        return {m_width, m_height};
    }

    SwSpacerItem* spacerItem() override {
        return this;
    }

private:
    static int minimumForAxis_(int hint, SwSizePolicy::Policy policy) {
        if (policy == SwSizePolicy::Fixed ||
            policy == SwSizePolicy::Minimum ||
            policy == SwSizePolicy::MinimumExpanding) {
            return hint;
        }
        return 0;
    }

    static int maximumForAxis_(int hint, SwSizePolicy::Policy policy) {
        if (policy == SwSizePolicy::Fixed || policy == SwSizePolicy::Maximum) {
            return hint;
        }
        return std::numeric_limits<int>::max();
    }

    int m_width{0};
    int m_height{0};
    SwSizePolicy m_sizePolicy;
    SwRect m_geometry{0, 0, 0, 0};
};

namespace SwLayoutDetail {

inline SwSizePolicy::Policy spacerPolicyFromString_(SwString value) {
    value = value.trimmed();
    const size_t sep = value.lastIndexOf(':');
    if (sep != static_cast<size_t>(-1)) {
        value = value.mid(static_cast<int>(sep + 1));
    }
    value = value.trimmed();
    if (value == "Fixed") return SwSizePolicy::Fixed;
    if (value == "Minimum") return SwSizePolicy::Minimum;
    if (value == "Maximum") return SwSizePolicy::Maximum;
    if (value == "Preferred") return SwSizePolicy::Preferred;
    if (value == "MinimumExpanding") return SwSizePolicy::MinimumExpanding;
    if (value == "Expanding") return SwSizePolicy::Expanding;
    if (value == "Ignored") return SwSizePolicy::Ignored;
    return SwSizePolicy::Minimum;
}

inline bool isSpacerWidget_(SwWidgetInterface* widget) {
    return widget && widget->propertyExist("__SwCreator_IsSpacer") &&
           widget->property("__SwCreator_IsSpacer").toBool();
}

inline SwSizePolicy spacerWidgetSizePolicy_(SwWidgetInterface* widget) {
    if (!isSpacerWidget_(widget)) {
        return SwSizePolicy{};
    }

    SwString horizontalPolicy("Minimum");
    SwString verticalPolicy("Minimum");
    if (widget->propertyExist("HorizontalPolicy")) {
        horizontalPolicy = widget->property("HorizontalPolicy").get<SwString>();
    }
    if (widget->propertyExist("VerticalPolicy")) {
        verticalPolicy = widget->property("VerticalPolicy").get<SwString>();
    }
    return SwSizePolicy(spacerPolicyFromString_(horizontalPolicy), spacerPolicyFromString_(verticalPolicy));
}

inline bool itemCanGrowOnAxis_(SwLayoutItem* item, bool horizontal) {
    if (!item) {
        return false;
    }

    if (SwSpacerItem* spacer = item->spacerItem()) {
        const SwSizePolicy sizePolicy = spacer->sizePolicy();
        const SwSizePolicy::Policy policy = horizontal ? sizePolicy.horizontalPolicy() : sizePolicy.verticalPolicy();
        return SwSizePolicy::canGrow(policy) || SwSizePolicy::shouldIgnoreSizeHint(policy);
    }

    SwWidgetInterface* widget = item->widget();
    if (!isSpacerWidget_(widget)) {
        return false;
    }

    const SwSizePolicy sizePolicy = spacerWidgetSizePolicy_(widget);
    const SwSizePolicy::Policy policy = horizontal ? sizePolicy.horizontalPolicy() : sizePolicy.verticalPolicy();
    return SwSizePolicy::canGrow(policy) || SwSizePolicy::shouldIgnoreSizeHint(policy);
}

} // namespace SwLayoutDetail

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

    virtual SwSize sizeHint() const {
        return {0, 0};
    }

    virtual SwSize minimumSizeHint() const {
        return {0, 0};
    }

    void updateGeometry() {
        if (!m_parentWidget) {
            return;
        }
        const SwRect rect = contentRect();
        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }
        applyLayout(rect);
    }

protected:
    SwRect contentRect() const {
        if (!m_parentWidget) {
            return {0, 0, 0, 0};
        }
        const SwSize parentSize = m_parentWidget->clientSize();
        SwRect rect;
        rect.x = m_margin;
        rect.y = m_margin;
        rect.width = std::max(0, parentSize.width - 2 * m_margin);
        rect.height = std::max(0, parentSize.height - 2 * m_margin);
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
        SwLayoutItem* item{nullptr};
        int stretch{0};
        int minSize{-1};
    };

    explicit SwBoxLayout(SwWidgetInterface* parent = nullptr)
        : SwAbstractLayout(parent) {}

    virtual ~SwBoxLayout() {
        clearItems_();
    }

    void addItem(SwLayoutItem* item) {
        insertItem(static_cast<int>(m_items.size()), item);
    }

    void insertItem(int index, SwLayoutItem* item) {
        insertItemInternal_(normalizeIndex_(index), item, 0, -1);
    }

    void addSpacerItem(SwSpacerItem* spacerItem) {
        addItem(spacerItem);
    }

    void insertSpacerItem(int index, SwSpacerItem* spacerItem) {
        insertItem(index, spacerItem);
    }

    void addSpacing(int size) {
        insertSpacing(static_cast<int>(m_items.size()), size);
    }

    void insertSpacing(int index, int size) {
        insertItemInternal_(normalizeIndex_(index), makeSpacingItem_(size), 0, -1);
    }

    void addStretch(int stretch = 0) {
        insertStretch(static_cast<int>(m_items.size()), stretch);
    }

    void insertStretch(int index, int stretch = 0) {
        insertItemInternal_(normalizeIndex_(index), makeStretchItem_(), std::max(0, stretch), -1);
    }

    void addWidget(SwWidgetInterface* widget, int stretch = 0, int minSize = -1) {
        insertWidget(static_cast<int>(m_items.size()), widget, stretch, minSize);
    }

    void insertWidget(int index, SwWidgetInterface* widget, int stretch = 0, int minSize = -1) {
        if (!widget || containsWidget_(widget)) {
            return;
        }
        insertItemInternal_(normalizeIndex_(index), new SwWidgetItem(widget), std::max(0, stretch), minSize);
    }

    void removeWidget(SwWidgetInterface* widget) {
        removeIf_([widget](const Item& item) {
            return item.item && item.item->widget() == widget;
        });
    }

    void setStretch(int index, int stretch) {
        if (index < 0 || static_cast<size_t>(index) >= m_items.size()) {
            return;
        }
        m_items[static_cast<size_t>(index)].stretch = std::max(0, stretch);
        updateGeometry();
    }

    bool setStretchFactor(SwWidgetInterface* widget, int stretch) {
        if (!widget) {
            return false;
        }
        for (Item& item : m_items) {
            if (item.item && item.item->widget() == widget) {
                item.stretch = std::max(0, stretch);
                updateGeometry();
                return true;
            }
        }
        return false;
    }

    int stretch(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= m_items.size()) {
            return 0;
        }
        return m_items[static_cast<size_t>(index)].stretch;
    }

    SwSize sizeHint() const override {
        return layoutSizeHint_(false);
    }

    SwSize minimumSizeHint() const override {
        return layoutSizeHint_(true);
    }

protected:
    const std::vector<Item>& items() const {
        return m_items;
    }

    virtual SwOrientationFlag primaryOrientation() const = 0;

    void applyBoxLayout_(const SwRect& rect) {
        const auto& entries = items();
        if (entries.empty()) {
            return;
        }

        const bool horizontal = primaryOrientation() == SwOrientationFlag::Horizontal;
        const int spacingAmount = spacing();
        const int availablePrimary = std::max(
            0,
            (horizontal ? rect.width : rect.height) - std::max(0, static_cast<int>(entries.size()) - 1) * spacingAmount);

        std::vector<int> minimumSizes;
        std::vector<int> preferredSizes;
        std::vector<int> finalSizes;
        minimumSizes.reserve(entries.size());
        preferredSizes.reserve(entries.size());
        finalSizes.reserve(entries.size());

        int totalMinimum = 0;
        int totalPreferred = 0;
        int totalStretch = 0;
        for (const Item& item : entries) {
            const int minimum = primaryMinimumSize_(item, horizontal);
            const int preferred = std::max(minimum, primaryPreferredSize_(item, horizontal));
            minimumSizes.push_back(minimum);
            preferredSizes.push_back(preferred);
            finalSizes.push_back(preferred);
            totalMinimum += minimum;
            totalPreferred += preferred;
            totalStretch += std::max(0, item.stretch);
        }

        if (totalMinimum > availablePrimary && totalMinimum > 0) {
            int scaledTotal = 0;
            for (size_t i = 0; i < minimumSizes.size(); ++i) {
                finalSizes[i] = static_cast<int>((static_cast<long long>(minimumSizes[i]) * availablePrimary) / totalMinimum);
                scaledTotal += finalSizes[i];
            }
            int remainder = availablePrimary - scaledTotal;
            for (size_t i = 0; remainder > 0 && i < finalSizes.size(); ++i, --remainder) {
                ++finalSizes[i];
            }
        } else if (totalPreferred > availablePrimary) {
            int deficit = totalPreferred - availablePrimary;
            std::vector<int> shrinkCapacity;
            shrinkCapacity.reserve(entries.size());
            int totalShrinkCapacity = 0;
            for (size_t i = 0; i < entries.size(); ++i) {
                const int capacity = std::max(0, preferredSizes[i] - minimumSizes[i]);
                shrinkCapacity.push_back(capacity);
                totalShrinkCapacity += capacity;
            }

            if (totalShrinkCapacity > 0) {
                int remainingDeficit = deficit;
                int remainingCapacity = totalShrinkCapacity;
                for (size_t i = 0; i < entries.size(); ++i) {
                    const int capacity = shrinkCapacity[i];
                    if (capacity <= 0 || remainingDeficit <= 0 || remainingCapacity <= 0) {
                        continue;
                    }
                    const int shrink = (remainingCapacity == capacity)
                                           ? remainingDeficit
                                           : static_cast<int>((static_cast<long long>(remainingDeficit) * capacity) / remainingCapacity);
                    finalSizes[i] = std::max(minimumSizes[i], finalSizes[i] - shrink);
                    remainingDeficit -= shrink;
                    remainingCapacity -= capacity;
                }
            }
        }

        const int allocated = std::accumulate(finalSizes.begin(), finalSizes.end(), 0);
        int extra = std::max(0, availablePrimary - allocated);

        if (extra > 0 && totalStretch > 0) {
            int remainingExtra = extra;
            int remainingStretch = totalStretch;
            for (size_t i = 0; i < entries.size(); ++i) {
                const int stretchFactor = std::max(0, entries[i].stretch);
                if (stretchFactor <= 0 || remainingExtra <= 0 || remainingStretch <= 0) {
                    continue;
                }
                const int added = (remainingStretch == stretchFactor)
                                      ? remainingExtra
                                      : static_cast<int>((static_cast<long long>(remainingExtra) * stretchFactor) / remainingStretch);
                finalSizes[i] += added;
                remainingExtra -= added;
                remainingStretch -= stretchFactor;
            }
            extra = 0;
        }

        if (extra > 0) {
            std::vector<size_t> growable;
            growable.reserve(entries.size());
            for (size_t i = 0; i < entries.size(); ++i) {
                if (spacerCanGrow_(entries[i], horizontal)) {
                    growable.push_back(i);
                }
            }

            if (!growable.empty()) {
                const int perItem = extra / static_cast<int>(growable.size());
                int remainder = extra % static_cast<int>(growable.size());
                for (size_t index : growable) {
                    finalSizes[index] += perItem;
                    if (remainder > 0) {
                        ++finalSizes[index];
                        --remainder;
                    }
                }
            }
        }

        int primaryPos = horizontal ? rect.x : rect.y;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!entries[i].item) {
                continue;
            }

            if (horizontal) {
                entries[i].item->setGeometry(SwRect{primaryPos, rect.y, finalSizes[i], rect.height});
            } else {
                entries[i].item->setGeometry(SwRect{rect.x, primaryPos, rect.width, finalSizes[i]});
            }
            primaryPos += finalSizes[i] + spacingAmount;
        }
    }

private:
    int normalizeIndex_(int index) const {
        return std::max(0, std::min(index, static_cast<int>(m_items.size())));
    }

    bool containsWidget_(SwWidgetInterface* widget) const {
        if (!widget) {
            return false;
        }
        for (const Item& item : m_items) {
            if (item.item && item.item->widget() == widget) {
                return true;
            }
        }
        return false;
    }

    void insertItemInternal_(int index, SwLayoutItem* item, int stretch, int minSize) {
        if (!item) {
            return;
        }
        Item entry;
        entry.item = item;
        entry.stretch = std::max(0, stretch);
        entry.minSize = minSize;
        m_items.insert(m_items.begin() + normalizeIndex_(index), entry);
        updateGeometry();
    }

    template<typename Predicate>
    void removeIf_(Predicate predicate) {
        bool removed = false;
        for (auto it = m_items.begin(); it != m_items.end();) {
            if (!predicate(*it)) {
                ++it;
                continue;
            }
            delete it->item;
            it = m_items.erase(it);
            removed = true;
        }
        if (removed) {
            updateGeometry();
        }
    }

    void clearItems_() {
        for (Item& item : m_items) {
            delete item.item;
            item.item = nullptr;
        }
        m_items.clear();
    }

    SwSize layoutSizeHint_(bool minimum) const {
        const auto& entries = items();
        if (entries.empty()) {
            const int extent = 2 * margin();
            return {extent, extent};
        }

        const bool horizontal = primaryOrientation() == SwOrientationFlag::Horizontal;
        int primary = 0;
        int cross = 0;
        int count = 0;

        for (const Item& item : entries) {
            if (!item.item) {
                continue;
            }

            const int primarySize = minimum ? primaryMinimumSize_(item, horizontal)
                                            : std::max(primaryMinimumSize_(item, horizontal),
                                                       primaryPreferredSize_(item, horizontal));
            const SwSize size = minimum ? item.item->minimumSize() : item.item->sizeHint();
            const int crossSize = std::max(0, horizontal ? size.height : size.width);
            primary += std::max(0, primarySize);
            cross = std::max(cross, crossSize);
            ++count;
        }

        if (count > 1) {
            primary += (count - 1) * spacing();
        }

        if (horizontal) {
            return {primary + 2 * margin(), cross + 2 * margin()};
        }
        return {cross + 2 * margin(), primary + 2 * margin()};
    }

    int primaryPreferredSize_(const Item& item, bool horizontal) const {
        if (!item.item) {
            return 0;
        }
        if (item.minSize >= 0) {
            return item.minSize;
        }
        const SwSize size = item.item->sizeHint();
        return std::max(0, horizontal ? size.width : size.height);
    }

    int primaryMinimumSize_(const Item& item, bool horizontal) const {
        if (!item.item) {
            return 0;
        }
        if (item.minSize >= 0) {
            return item.minSize;
        }
        const SwSize size = item.item->minimumSize();
        return std::max(0, horizontal ? size.width : size.height);
    }

    bool spacerCanGrow_(const Item& item, bool horizontal) const {
        return SwLayoutDetail::itemCanGrowOnAxis_(item.item, horizontal);
    }

    SwSpacerItem* makeSpacingItem_(int size) const {
        size = std::max(0, size);
        if (primaryOrientation() == SwOrientationFlag::Horizontal) {
            return new SwSpacerItem(size, 0, SwSizePolicy::Fixed, SwSizePolicy::Minimum);
        }
        return new SwSpacerItem(0, size, SwSizePolicy::Minimum, SwSizePolicy::Fixed);
    }

    SwSpacerItem* makeStretchItem_() const {
        if (primaryOrientation() == SwOrientationFlag::Horizontal) {
            return new SwSpacerItem(0, 0, SwSizePolicy::Expanding, SwSizePolicy::Minimum);
        }
        return new SwSpacerItem(0, 0, SwSizePolicy::Minimum, SwSizePolicy::Expanding);
    }

    std::vector<Item> m_items;
};

class SwVerticalLayout : public SwBoxLayout {
public:
    explicit SwVerticalLayout(SwWidgetInterface* parent = nullptr)
        : SwBoxLayout(parent) {}

protected:
    SwOrientationFlag primaryOrientation() const override {
        return SwOrientationFlag::Vertical;
    }

    void applyLayout(const SwRect& rect) override {
        applyBoxLayout_(rect);
    }
};

class SwHorizontalLayout : public SwBoxLayout {
public:
    explicit SwHorizontalLayout(SwWidgetInterface* parent = nullptr)
        : SwBoxLayout(parent) {}

protected:
    SwOrientationFlag primaryOrientation() const override {
        return SwOrientationFlag::Horizontal;
    }

    void applyLayout(const SwRect& rect) override {
        applyBoxLayout_(rect);
    }
};

class SwGridLayout : public SwAbstractLayout {
public:
    struct Cell {
        SwLayoutItem* item{nullptr};
        int row{0};
        int column{0};
        int rowSpan{1};
        int columnSpan{1};
    };

    explicit SwGridLayout(SwWidgetInterface* parent = nullptr)
        : SwAbstractLayout(parent)
        , m_horizontalSpacing(spacing())
        , m_verticalSpacing(spacing()) {}

    ~SwGridLayout() override {
        clearCells_();
    }

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
            m_columnStretch.resize(static_cast<size_t>(column + 1), 0);
        }
        m_columnStretch[static_cast<size_t>(column)] = std::max(0, stretch);
        updateGeometry();
    }

    void setRowStretch(int row, int stretch) {
        if (row < 0) {
            return;
        }
        if (static_cast<size_t>(row) >= m_rowStretch.size()) {
            m_rowStretch.resize(static_cast<size_t>(row + 1), 0);
        }
        m_rowStretch[static_cast<size_t>(row)] = std::max(0, stretch);
        updateGeometry();
    }

    void addItem(SwLayoutItem* item, int row, int column, int rowSpan = 1, int columnSpan = 1) {
        if (!item || row < 0 || column < 0 || rowSpan <= 0 || columnSpan <= 0) {
            return;
        }
        Cell cell;
        cell.item = item;
        cell.row = row;
        cell.column = column;
        cell.rowSpan = rowSpan;
        cell.columnSpan = columnSpan;
        m_cells.push_back(cell);
        updateGeometry();
    }

    void addWidget(SwWidgetInterface* widget, int row, int column, int rowSpan = 1, int columnSpan = 1) {
        if (!widget) {
            return;
        }
        for (const Cell& existing : m_cells) {
            if (existing.item && existing.item->widget() == widget) {
                return;
            }
        }
        addItem(new SwWidgetItem(widget), row, column, rowSpan, columnSpan);
    }

    void removeWidget(SwWidgetInterface* widget) {
        bool removed = false;
        for (auto it = m_cells.begin(); it != m_cells.end();) {
            if (!it->item || it->item->widget() != widget) {
                ++it;
                continue;
            }
            delete it->item;
            it = m_cells.erase(it);
            removed = true;
        }
        if (removed) {
            updateGeometry();
        }
    }

    SwSize sizeHint() const override {
        return layoutSizeHint_(false);
    }

    SwSize minimumSizeHint() const override {
        return layoutSizeHint_(true);
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
            m_rowStretch.resize(static_cast<size_t>(rows), 0);
        }
        if (static_cast<size_t>(columns) > m_columnStretch.size()) {
            m_columnStretch.resize(static_cast<size_t>(columns), 0);
        }

        std::vector<int> rowMin(static_cast<size_t>(rows), 0);
        std::vector<int> columnMin(static_cast<size_t>(columns), 0);
        std::vector<bool> rowOccupied(static_cast<size_t>(rows), false);
        std::vector<bool> columnOccupied(static_cast<size_t>(columns), false);
        std::vector<bool> rowGrow(static_cast<size_t>(rows), false);
        std::vector<bool> columnGrow(static_cast<size_t>(columns), false);

        for (const Cell& cell : m_cells) {
            if (!cell.item) {
                continue;
            }

            const SwSize preferred = cell.item->sizeHint();
            const SwSize minimum = cell.item->minimumSize();
            const int totalHeight = std::max(minimum.height, preferred.height);
            const int totalWidth = std::max(minimum.width, preferred.width);
            const int sharedHeight = cell.rowSpan > 0 ? (totalHeight + cell.rowSpan - 1) / cell.rowSpan : totalHeight;
            const int sharedWidth = cell.columnSpan > 0 ? (totalWidth + cell.columnSpan - 1) / cell.columnSpan : totalWidth;

            for (int i = 0; i < cell.rowSpan; ++i) {
                const int rowIndex = cell.row + i;
                if (rowIndex < rows) {
                    rowOccupied[static_cast<size_t>(rowIndex)] = true;
                    rowMin[static_cast<size_t>(rowIndex)] = std::max(rowMin[static_cast<size_t>(rowIndex)], sharedHeight);
                    if (SwLayoutDetail::itemCanGrowOnAxis_(cell.item, false)) {
                        rowGrow[static_cast<size_t>(rowIndex)] = true;
                    }
                }
            }

            for (int i = 0; i < cell.columnSpan; ++i) {
                const int columnIndex = cell.column + i;
                if (columnIndex < columns) {
                    columnOccupied[static_cast<size_t>(columnIndex)] = true;
                    columnMin[static_cast<size_t>(columnIndex)] = std::max(columnMin[static_cast<size_t>(columnIndex)], sharedWidth);
                    if (SwLayoutDetail::itemCanGrowOnAxis_(cell.item, true)) {
                        columnGrow[static_cast<size_t>(columnIndex)] = true;
                    }
                }
            }
        }

        auto distributeSizes = [](int available,
                                  std::vector<int>& mins,
                                  const std::vector<int>& stretches,
                                  const std::vector<bool>& occupied,
                                  const std::vector<bool>& growable,
                                  int count) {
            int totalMin = std::accumulate(mins.begin(), mins.end(), 0);
            if (available < totalMin && totalMin > 0) {
                int scaledTotal = 0;
                for (int i = 0; i < count; ++i) {
                    mins[static_cast<size_t>(i)] = static_cast<int>((static_cast<long long>(mins[static_cast<size_t>(i)]) * available) / totalMin);
                    scaledTotal += mins[static_cast<size_t>(i)];
                }
                int remainder = available - scaledTotal;
                for (int i = 0; remainder > 0 && i < count; ++i, --remainder) {
                    ++mins[static_cast<size_t>(i)];
                }
                totalMin = available;
            }

            int extra = std::max(0, available - totalMin);
            int stretchTotal = 0;
            for (int i = 0; i < count; ++i) {
                stretchTotal += std::max(0, stretches[static_cast<size_t>(i)]);
            }
            if (stretchTotal > 0) {
                int remainingExtra = extra;
                int remainingStretch = stretchTotal;
                for (int i = 0; i < count; ++i) {
                    const int stretch = std::max(0, stretches[static_cast<size_t>(i)]);
                    if (stretch <= 0 || remainingExtra <= 0 || remainingStretch <= 0) {
                        continue;
                    }
                    const int allocated = (remainingStretch == stretch)
                                              ? remainingExtra
                                              : static_cast<int>((static_cast<long long>(remainingExtra) * stretch) / remainingStretch);
                    mins[static_cast<size_t>(i)] += allocated;
                    remainingExtra -= allocated;
                    remainingStretch -= stretch;
                }
                return;
            }

            if (extra <= 0) {
                return;
            }

            std::vector<int> growableIndices;
            growableIndices.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (occupied[static_cast<size_t>(i)] && growable[static_cast<size_t>(i)]) {
                    growableIndices.push_back(i);
                }
            }

            if (!growableIndices.empty()) {
                const int per = extra / static_cast<int>(growableIndices.size());
                int remainder = extra % static_cast<int>(growableIndices.size());
                for (const int index : growableIndices) {
                    mins[static_cast<size_t>(index)] += per;
                    if (remainder > 0) {
                        ++mins[static_cast<size_t>(index)];
                        --remainder;
                    }
                }
                return;
            }

            for (int i = count - 1; i >= 0; --i) {
                if (!occupied[static_cast<size_t>(i)]) {
                    continue;
                }
                mins[static_cast<size_t>(i)] += extra;
                return;
            }
        };

        const int totalVerticalSpacing = std::max(0, rows - 1) * m_verticalSpacing;
        const int totalHorizontalSpacing = std::max(0, columns - 1) * m_horizontalSpacing;

        distributeSizes(std::max(0, rect.height - totalVerticalSpacing), rowMin, m_rowStretch, rowOccupied, rowGrow, rows);
        distributeSizes(std::max(0, rect.width - totalHorizontalSpacing), columnMin, m_columnStretch, columnOccupied, columnGrow, columns);

        std::vector<int> rowPositions(static_cast<size_t>(rows), rect.y);
        std::vector<int> columnPositions(static_cast<size_t>(columns), rect.x);

        int current = rect.y;
        for (int i = 0; i < rows; ++i) {
            rowPositions[static_cast<size_t>(i)] = current;
            current += rowMin[static_cast<size_t>(i)] + m_verticalSpacing;
        }

        current = rect.x;
        for (int i = 0; i < columns; ++i) {
            columnPositions[static_cast<size_t>(i)] = current;
            current += columnMin[static_cast<size_t>(i)] + m_horizontalSpacing;
        }

        for (const Cell& cell : m_cells) {
            if (!cell.item) {
                continue;
            }

            int x = columnPositions[static_cast<size_t>(cell.column)];
            int y = rowPositions[static_cast<size_t>(cell.row)];
            int width = 0;
            int height = 0;

            for (int i = 0; i < cell.columnSpan && (cell.column + i) < columns; ++i) {
                width += columnMin[static_cast<size_t>(cell.column + i)];
            }
            width += m_horizontalSpacing * std::max(0, cell.columnSpan - 1);

            for (int i = 0; i < cell.rowSpan && (cell.row + i) < rows; ++i) {
                height += rowMin[static_cast<size_t>(cell.row + i)];
            }
            height += m_verticalSpacing * std::max(0, cell.rowSpan - 1);

            cell.item->setGeometry(SwRect{x, y, width, height});
        }
    }

private:
    SwSize layoutSizeHint_(bool minimum) const {
        if (m_cells.empty()) {
            const int extent = 2 * margin();
            return {extent, extent};
        }

        int rows = 0;
        int columns = 0;
        for (const Cell& cell : m_cells) {
            rows = std::max(rows, cell.row + cell.rowSpan);
            columns = std::max(columns, cell.column + cell.columnSpan);
        }
        if (rows <= 0 || columns <= 0) {
            const int extent = 2 * margin();
            return {extent, extent};
        }

        std::vector<int> rowSizes(static_cast<size_t>(rows), 0);
        std::vector<int> columnSizes(static_cast<size_t>(columns), 0);

        for (const Cell& cell : m_cells) {
            if (!cell.item) {
                continue;
            }

            SwSize size = minimum ? cell.item->minimumSize() : cell.item->sizeHint();
            if (!minimum) {
                const SwSize minSize = cell.item->minimumSize();
                size.width = std::max(size.width, minSize.width);
                size.height = std::max(size.height, minSize.height);
            }

            const int sharedHeight = cell.rowSpan > 0 ? (std::max(0, size.height) + cell.rowSpan - 1) / cell.rowSpan : std::max(0, size.height);
            const int sharedWidth = cell.columnSpan > 0 ? (std::max(0, size.width) + cell.columnSpan - 1) / cell.columnSpan : std::max(0, size.width);

            for (int i = 0; i < cell.rowSpan; ++i) {
                const int rowIndex = cell.row + i;
                if (rowIndex < rows) {
                    rowSizes[static_cast<size_t>(rowIndex)] = std::max(rowSizes[static_cast<size_t>(rowIndex)], sharedHeight);
                }
            }

            for (int i = 0; i < cell.columnSpan; ++i) {
                const int columnIndex = cell.column + i;
                if (columnIndex < columns) {
                    columnSizes[static_cast<size_t>(columnIndex)] = std::max(columnSizes[static_cast<size_t>(columnIndex)], sharedWidth);
                }
            }
        }

        const int totalHeight = std::accumulate(rowSizes.begin(), rowSizes.end(), 0) +
                                std::max(0, rows - 1) * m_verticalSpacing + 2 * margin();
        const int totalWidth = std::accumulate(columnSizes.begin(), columnSizes.end(), 0) +
                               std::max(0, columns - 1) * m_horizontalSpacing + 2 * margin();
        return {totalWidth, totalHeight};
    }

    void clearCells_() {
        for (Cell& cell : m_cells) {
            delete cell.item;
            cell.item = nullptr;
        }
        m_cells.clear();
    }

    std::vector<Cell> m_cells;
    std::vector<int> m_columnStretch;
    std::vector<int> m_rowStretch;
    int m_horizontalSpacing;
    int m_verticalSpacing;
};

class SwFormLayout : public SwAbstractLayout {
public:
    struct Row {
        SwLayoutItem* label{nullptr};
        SwLayoutItem* field{nullptr};
    };

    explicit SwFormLayout(SwWidgetInterface* parent = nullptr)
        : SwAbstractLayout(parent) {}

    ~SwFormLayout() override {
        clearRows_();
        delete m_pending;
        m_pending = nullptr;
    }

    void addRow(SwWidgetInterface* label, SwWidgetInterface* field) {
        if (!label || !field) {
            return;
        }
        addRowItems_(new SwWidgetItem(label), new SwWidgetItem(field));
    }

    void setItem(int row, int column, SwLayoutItem* item) {
        if (!item || row < 0) {
            delete item;
            return;
        }

        const size_t rowIndex = static_cast<size_t>(row);
        if (rowIndex >= m_rows.size()) {
            m_rows.resize(rowIndex + 1);
        }

        SwLayoutItem** slot = nullptr;
        if (column == 0) {
            slot = &m_rows[rowIndex].label;
        } else if (column == 1) {
            slot = &m_rows[rowIndex].field;
        } else {
            slot = m_rows[rowIndex].field ? &m_rows[rowIndex].label : &m_rows[rowIndex].field;
        }

        delete *slot;
        *slot = item;
        updateGeometry();
    }

    void setCell(int row, int column, SwWidgetInterface* widget) {
        if (!widget) {
            return;
        }
        setItem(row, column, new SwWidgetItem(widget));
    }

    void addItem(SwLayoutItem* item) {
        if (!item) {
            return;
        }
        if (!m_pending) {
            m_pending = item;
            return;
        }
        addRowItems_(m_pending, item);
        m_pending = nullptr;
    }

    void addWidget(SwWidgetInterface* widget) {
        if (!widget) {
            return;
        }
        addItem(new SwWidgetItem(widget));
    }

    void removeWidget(SwWidgetInterface* widget) {
        if (!widget) {
            return;
        }

        bool removed = false;
        if (m_pending && m_pending->widget() == widget) {
            delete m_pending;
            m_pending = nullptr;
            removed = true;
        }

        for (auto it = m_rows.begin(); it != m_rows.end();) {
            bool eraseRow = false;
            if (it->label && it->label->widget() == widget) {
                delete it->label;
                it->label = nullptr;
                eraseRow = true;
            }
            if (it->field && it->field->widget() == widget) {
                delete it->field;
                it->field = nullptr;
                eraseRow = true;
            }
            if (eraseRow) {
                it = m_rows.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }

        if (removed) {
            updateGeometry();
        }
    }

    SwSize sizeHint() const override {
        return layoutSizeHint_(false);
    }

    SwSize minimumSizeHint() const override {
        return layoutSizeHint_(true);
    }

protected:
    void applyLayout(const SwRect& rect) override {
        if (m_rows.empty()) {
            return;
        }

        int labelWidth = 0;
        std::vector<size_t> validRows;
        validRows.reserve(m_rows.size());
        for (size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            if (!row.label || !row.field) {
                continue;
            }
            validRows.push_back(i);
            labelWidth = std::max(labelWidth, std::max(0, row.label->sizeHint().width));
        }

        if (validRows.empty()) {
            return;
        }

        const int columnSpacing = spacing();
        labelWidth = std::min(labelWidth, std::max(0, rect.width / 2));
        const int fieldX = rect.x + labelWidth + columnSpacing;
        const int fieldWidth = std::max(0, rect.width - labelWidth - columnSpacing);

        std::vector<int> rowHeights;
        rowHeights.reserve(validRows.size());
        int totalPreferredHeights = 0;
        for (size_t rowIndex : validRows) {
            const Row& row = m_rows[rowIndex];
            const int preferredHeight = std::max(row.label->sizeHint().height, row.field->sizeHint().height);
            const int minimumHeight = std::max(row.label->minimumSize().height, row.field->minimumSize().height);
            const int rowHeight = std::max(minimumHeight, preferredHeight);
            rowHeights.push_back(rowHeight);
            totalPreferredHeights += rowHeight;
        }

        const int rowSpacing = spacing();
        const int totalSpacing = rowSpacing * std::max(0, static_cast<int>(rowHeights.size()) - 1);
        const int availableHeight = std::max(0, rect.height - totalSpacing);

        if (totalPreferredHeights > availableHeight && totalPreferredHeights > 0) {
            int scaledTotal = 0;
            for (size_t i = 0; i < rowHeights.size(); ++i) {
                rowHeights[i] = static_cast<int>((static_cast<long long>(rowHeights[i]) * availableHeight) / totalPreferredHeights);
                scaledTotal += rowHeights[i];
            }
            int remainder = availableHeight - scaledTotal;
            for (size_t i = 0; remainder > 0 && i < rowHeights.size(); ++i, --remainder) {
                ++rowHeights[i];
            }
        } else if (!rowHeights.empty()) {
            rowHeights.back() += std::max(0, availableHeight - totalPreferredHeights);
        }

        int y = rect.y;
        for (size_t i = 0; i < validRows.size(); ++i) {
            const Row& row = m_rows[validRows[i]];
            const int rowHeight = rowHeights[i];
            row.label->setGeometry(SwRect{rect.x, y, labelWidth, rowHeight});
            row.field->setGeometry(SwRect{fieldX, y, fieldWidth, rowHeight});
            y += rowHeight;
            if (i + 1 < validRows.size()) {
                y += rowSpacing;
            }
        }
    }

private:
    SwSize layoutSizeHint_(bool minimum) const {
        int labelWidth = 0;
        int fieldWidth = 0;
        int totalHeight = 0;
        int count = 0;

        for (const Row& row : m_rows) {
            if (!row.label || !row.field) {
                continue;
            }

            const SwSize labelSize = minimum ? row.label->minimumSize() : row.label->sizeHint();
            const SwSize fieldSize = minimum ? row.field->minimumSize() : row.field->sizeHint();
            labelWidth = std::max(labelWidth, std::max(0, labelSize.width));
            fieldWidth = std::max(fieldWidth, std::max(0, fieldSize.width));
            totalHeight += std::max(std::max(0, labelSize.height), std::max(0, fieldSize.height));
            ++count;
        }

        if (count == 0) {
            const int extent = 2 * margin();
            return {extent, extent};
        }

        totalHeight += std::max(0, count - 1) * spacing();
        const int totalWidth = labelWidth + fieldWidth + spacing();
        return {totalWidth + 2 * margin(), totalHeight + 2 * margin()};
    }

    void addRowItems_(SwLayoutItem* label, SwLayoutItem* field) {
        if (!label || !field) {
            delete label;
            delete field;
            return;
        }
        Row row;
        row.label = label;
        row.field = field;
        m_rows.push_back(row);
        updateGeometry();
    }

    void clearRows_() {
        for (Row& row : m_rows) {
            delete row.label;
            delete row.field;
            row.label = nullptr;
            row.field = nullptr;
        }
        m_rows.clear();
    }

    std::vector<Row> m_rows;
    SwLayoutItem* m_pending{nullptr};
};
