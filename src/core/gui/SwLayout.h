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
#include <functional>
#include <limits>

#include "SwVector.h"
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
        m_widget->setGeometry(rect);
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

inline SwString policyToString_(SwSizePolicy::Policy policy) {
    switch (policy) {
    case SwSizePolicy::Fixed:
        return SwString("Fixed");
    case SwSizePolicy::Minimum:
        return SwString("Minimum");
    case SwSizePolicy::Maximum:
        return SwString("Maximum");
    case SwSizePolicy::Preferred:
        return SwString("Preferred");
    case SwSizePolicy::MinimumExpanding:
        return SwString("MinimumExpanding");
    case SwSizePolicy::Expanding:
        return SwString("Expanding");
    case SwSizePolicy::Ignored:
        return SwString("Ignored");
    }
    return SwString("Preferred");
}

template<typename T>
inline int sumVector_(const SwVector<T>& values) {
    int total = 0;
    for (const auto& value : values) {
        total += static_cast<int>(value);
    }
    return total;
}

inline bool isSpacerWidget_(SwWidgetInterface* widget) {
    return widget && widget->propertyExist("__SwCreator_IsSpacer") &&
           widget->property("__SwCreator_IsSpacer").toBool();
}

inline SwSizePolicy defaultWidgetSizePolicy_(SwWidgetInterface* widget) {
    if (!widget) {
        return SwSizePolicy{};
    }

    const SwString className = widget->className();
    if (className == "SwPushButton" ||
        className == "SwToolButton" ||
        className == "SwCheckBox" ||
        className == "SwRadioButton") {
        return SwSizePolicy(SwSizePolicy::Minimum, SwSizePolicy::Fixed);
    }

    if (className == "SwLineEdit") {
        return SwSizePolicy(SwSizePolicy::Expanding, SwSizePolicy::Fixed);
    }

    if (className == "SwComboBox" ||
        className == "SwSpinBox" ||
        className == "SwDoubleSpinBox") {
        return SwSizePolicy(SwSizePolicy::Preferred, SwSizePolicy::Fixed);
    }

    if (className == "SwProgressBar" ||
        className == "SwSlider") {
        return SwSizePolicy(SwSizePolicy::Expanding, SwSizePolicy::Fixed);
    }

    if (className == "SwScrollArea" ||
        className == "SwSplitter" ||
        className == "SwTabWidget" ||
        className == "SwToolBox" ||
        className == "SwStackedWidget" ||
        className == "SwTextEdit" ||
        className == "SwPlainTextEdit") {
        return SwSizePolicy(SwSizePolicy::Expanding, SwSizePolicy::Expanding);
    }

    return SwSizePolicy(SwSizePolicy::Preferred, SwSizePolicy::Preferred);
}

inline SwSizePolicy widgetSizePolicy_(SwWidgetInterface* widget) {
    SwSizePolicy sizePolicy = defaultWidgetSizePolicy_(widget);
    if (!widget) {
        return sizePolicy;
    }

    SwString horizontalPolicy = policyToString_(sizePolicy.horizontalPolicy());
    SwString verticalPolicy = policyToString_(sizePolicy.verticalPolicy());
    if (widget->propertyExist("HorizontalPolicy")) {
        horizontalPolicy = widget->property("HorizontalPolicy").get<SwString>();
    }
    if (widget->propertyExist("VerticalPolicy")) {
        verticalPolicy = widget->property("VerticalPolicy").get<SwString>();
    }
    sizePolicy.setHorizontalPolicy(spacerPolicyFromString_(horizontalPolicy));
    sizePolicy.setVerticalPolicy(spacerPolicyFromString_(verticalPolicy));
    return sizePolicy;
}

inline SwSizePolicy itemSizePolicy_(SwLayoutItem* item) {
    if (!item) {
        return SwSizePolicy{};
    }

    if (SwSpacerItem* spacer = item->spacerItem()) {
        return spacer->sizePolicy();
    }

    return widgetSizePolicy_(item->widget());
}

inline bool itemHasExpandFlagOnAxis_(SwLayoutItem* item, bool horizontal) {
    const SwSizePolicy sizePolicy = itemSizePolicy_(item);
    const SwSizePolicy::Policy policy = horizontal ? sizePolicy.horizontalPolicy() : sizePolicy.verticalPolicy();
    return SwSizePolicy::hasExpandFlag(policy);
}

inline bool itemCanGrowOnAxis_(SwLayoutItem* item, bool horizontal) {
    if (!item) {
        return false;
    }
    const SwSizePolicy sizePolicy = itemSizePolicy_(item);
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
        , m_leftMargin(8)
        , m_topMargin(8)
        , m_rightMargin(8)
        , m_bottomMargin(8) {}

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
        const int clamped = std::max(0, margin);
        m_leftMargin = clamped;
        m_topMargin = clamped;
        m_rightMargin = clamped;
        m_bottomMargin = clamped;
        updateGeometry();
    }

    int margin() const {
        return m_leftMargin;
    }

    void setLeftMargin(int margin) {
        m_leftMargin = std::max(0, margin);
        updateGeometry();
    }

    void setTopMargin(int margin) {
        m_topMargin = std::max(0, margin);
        updateGeometry();
    }

    void setRightMargin(int margin) {
        m_rightMargin = std::max(0, margin);
        updateGeometry();
    }

    void setBottomMargin(int margin) {
        m_bottomMargin = std::max(0, margin);
        updateGeometry();
    }

    int leftMargin() const {
        return m_leftMargin;
    }

    int topMargin() const {
        return m_topMargin;
    }

    int rightMargin() const {
        return m_rightMargin;
    }

    int bottomMargin() const {
        return m_bottomMargin;
    }

    void setContentsMargins(int left, int top, int right, int bottom) {
        m_leftMargin = std::max(0, left);
        m_topMargin = std::max(0, top);
        m_rightMargin = std::max(0, right);
        m_bottomMargin = std::max(0, bottom);
        updateGeometry();
    }

    virtual SwSize sizeHint() const {
        return {0, 0};
    }

    virtual SwSize minimumSizeHint() const {
        return {0, 0};
    }

    virtual void forEachManagedWidget(const std::function<void(SwWidgetInterface*)>& visitor) const {
        SW_UNUSED(visitor);
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
    void adoptWidgetToParent_(SwWidgetInterface* widget) const {
        if (!widget || !m_parentWidget) {
            return;
        }
        if (widget->parent() == m_parentWidget) {
            return;
        }
        widget->setParent(m_parentWidget);
    }

    SwRect contentRect() const {
        if (!m_parentWidget) {
            return {0, 0, 0, 0};
        }
        const SwRect parentRect = m_parentWidget->clientRect();
        SwRect rect;
        rect.x = parentRect.x + m_leftMargin;
        rect.y = parentRect.y + m_topMargin;
        rect.width = std::max(0, parentRect.width - m_leftMargin - m_rightMargin);
        rect.height = std::max(0, parentRect.height - m_topMargin - m_bottomMargin);
        return rect;
    }

    virtual void applyLayout(const SwRect& rect) = 0;

private:
    SwWidgetInterface* m_parentWidget;
    int m_spacing;
    int m_leftMargin;
    int m_topMargin;
    int m_rightMargin;
    int m_bottomMargin;
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
        adoptWidgetToParent_(widget);
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

    void forEachManagedWidget(const std::function<void(SwWidgetInterface*)>& visitor) const override {
        if (!visitor) {
            return;
        }
        for (const Item& item : m_items) {
            SwWidgetInterface* widget = item.item ? item.item->widget() : nullptr;
            if (widget) {
                visitor(widget);
            }
        }
    }

    SwSize sizeHint() const override {
        return layoutSizeHint_(false);
    }

    SwSize minimumSizeHint() const override {
        return layoutSizeHint_(true);
    }

protected:
    const SwVector<Item>& items() const {
        return m_items;
    }

    virtual SwOrientationFlag primaryOrientation() const = 0;

    void applyBoxLayout_(const SwRect& rect) {
        const auto& entries = items();
        if (entries.isEmpty()) {
            return;
        }

        const bool horizontal = primaryOrientation() == SwOrientationFlag::Horizontal;
        const int spacingAmount = spacing();
        const int availablePrimary = std::max(
            0,
            (horizontal ? rect.width : rect.height) - std::max(0, static_cast<int>(entries.size()) - 1) * spacingAmount);

        SwVector<int> minimumSizes;
        SwVector<int> preferredSizes;
        SwVector<int> finalSizes;
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
            // Respect the minimum contract even when the parent is undersized.
            // Qt keeps controls at their minimum sizes and lets clipping/overflow
            // happen rather than proportionally crushing child widgets.
            finalSizes = minimumSizes;
        } else if (totalPreferred > availablePrimary) {
            int deficit = totalPreferred - availablePrimary;
            SwVector<int> shrinkCapacity;
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

        const int allocated = SwLayoutDetail::sumVector_(finalSizes);
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
            SwVector<size_t> growable;
            growable.reserve(entries.size());
            const auto collectGrowable = [&](bool spacersOnly, bool requireExpandFlag) -> bool {
                growable.clear();
                for (size_t i = 0; i < entries.size(); ++i) {
                    if (!entries[i].item) {
                        continue;
                    }

                    const bool isSpacer = entries[i].item->spacerItem() != nullptr;
                    if (isSpacer != spacersOnly) {
                        continue;
                    }

                    const bool acceptsExtra = requireExpandFlag
                                                 ? SwLayoutDetail::itemHasExpandFlagOnAxis_(entries[i].item, horizontal)
                                                 : spacerCanGrow_(entries[i], horizontal);
                    if (acceptsExtra) {
                        growable.push_back(i);
                    }
                }
                return !growable.isEmpty();
            };

            if (!collectGrowable(true, true) &&
                !collectGrowable(false, true) &&
                !collectGrowable(true, false)) {
                collectGrowable(false, false);
            }

            if (!growable.isEmpty()) {
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

            const int crossAvailable = std::max(0, horizontal ? rect.height : rect.width);
            const int crossSize = resolvedCrossSize_(entries[i], horizontal, crossAvailable);
            if (horizontal) {
                const int crossPos = rect.y + std::max(0, (rect.height - crossSize) / 2);
                entries[i].item->setGeometry(SwRect{primaryPos, crossPos, finalSizes[i], crossSize});
            } else {
                const int crossPos = rect.x + std::max(0, (rect.width - crossSize) / 2);
                entries[i].item->setGeometry(SwRect{crossPos, primaryPos, crossSize, finalSizes[i]});
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
        adoptWidgetToParent_(item->widget());
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
        if (entries.isEmpty()) {
            return {leftMargin() + rightMargin(), topMargin() + bottomMargin()};
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
            return {primary + leftMargin() + rightMargin(), cross + topMargin() + bottomMargin()};
        }
        return {cross + leftMargin() + rightMargin(), primary + topMargin() + bottomMargin()};
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

    int crossPreferredSize_(const Item& item, bool horizontal) const {
        if (!item.item) {
            return 0;
        }
        const SwSize size = item.item->sizeHint();
        return std::max(0, horizontal ? size.height : size.width);
    }

    int crossMinimumSize_(const Item& item, bool horizontal) const {
        if (!item.item) {
            return 0;
        }
        const SwSize size = item.item->minimumSize();
        return std::max(0, horizontal ? size.height : size.width);
    }

    int resolvedCrossSize_(const Item& item, bool horizontal, int availableCross) const {
        if (!item.item) {
            return 0;
        }

        const int minimum = crossMinimumSize_(item, horizontal);
        const int preferred = std::max(minimum, crossPreferredSize_(item, horizontal));
        if (SwLayoutDetail::itemCanGrowOnAxis_(item.item, !horizontal)) {
            return std::max(minimum, availableCross);
        }

        return std::max(minimum, std::min(availableCross, preferred));
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

    SwVector<Item> m_items;
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
        adoptWidgetToParent_(item->widget());
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
        adoptWidgetToParent_(widget);
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

    void forEachManagedWidget(const std::function<void(SwWidgetInterface*)>& visitor) const override {
        if (!visitor) {
            return;
        }
        for (const Cell& cell : m_cells) {
            SwWidgetInterface* widget = cell.item ? cell.item->widget() : nullptr;
            if (widget) {
                visitor(widget);
            }
        }
    }

protected:
    void applyLayout(const SwRect& rect) override {
        if (m_cells.isEmpty()) {
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

        SwVector<int> rowMin(static_cast<size_t>(rows), 0);
        SwVector<int> columnMin(static_cast<size_t>(columns), 0);
        SwVector<bool> rowOccupied(static_cast<size_t>(rows), false);
        SwVector<bool> columnOccupied(static_cast<size_t>(columns), false);
        SwVector<bool> rowGrow(static_cast<size_t>(rows), false);
        SwVector<bool> columnGrow(static_cast<size_t>(columns), false);

        auto requiredSpanExtent_ = [](int totalExtent, int spacingAmount, int span) {
            return std::max(0, totalExtent - std::max(0, span - 1) * spacingAmount);
        };

        auto ensureSpanMinimum_ = [](SwVector<int>& mins,
                                     const SwVector<bool>& growable,
                                     int start,
                                     int span,
                                     int required) {
            if (required <= 0 || span <= 0) {
                return;
            }

            int current = 0;
            SwVector<int> growableIndices;
            SwVector<int> allIndices;
            growableIndices.reserve(static_cast<size_t>(span));
            allIndices.reserve(static_cast<size_t>(span));

            for (int i = 0; i < span; ++i) {
                const int index = start + i;
                if (index < 0 || static_cast<size_t>(index) >= mins.size()) {
                    continue;
                }
                current += mins[static_cast<size_t>(index)];
                allIndices.push_back(index);
                if (growable[static_cast<size_t>(index)]) {
                    growableIndices.push_back(index);
                }
            }

            if (current >= required || allIndices.isEmpty()) {
                return;
            }

            const int deficit = required - current;
            const SwVector<int>& targetIndices = growableIndices.isEmpty() ? allIndices : growableIndices;
            const int per = deficit / static_cast<int>(targetIndices.size());
            int remainder = deficit % static_cast<int>(targetIndices.size());
            for (const int index : targetIndices) {
                mins[static_cast<size_t>(index)] += per;
                if (remainder > 0) {
                    ++mins[static_cast<size_t>(index)];
                    --remainder;
                }
            }
        };

        auto resolveItemExtent_ = [](SwLayoutItem* item, bool horizontal, int availableExtent) {
            if (!item) {
                return 0;
            }

            const SwSize minimumSize = item->minimumSize();
            const SwSize preferredSize = item->sizeHint();
            const int minimum = std::max(0, horizontal ? minimumSize.width : minimumSize.height);
            const int preferred = std::max(minimum, horizontal ? preferredSize.width : preferredSize.height);
            if (SwLayoutDetail::itemCanGrowOnAxis_(item, horizontal)) {
                return std::max(minimum, availableExtent);
            }
            return std::max(minimum, std::min(availableExtent, preferred));
        };

        for (const Cell& cell : m_cells) {
            if (!cell.item) {
                continue;
            }

            const SwSize preferred = cell.item->sizeHint();
            const SwSize minimum = cell.item->minimumSize();
            const int totalHeight = std::max(minimum.height, preferred.height);
            const int totalWidth = std::max(minimum.width, preferred.width);

            for (int i = 0; i < cell.rowSpan; ++i) {
                const int rowIndex = cell.row + i;
                if (rowIndex < rows) {
                    rowOccupied[static_cast<size_t>(rowIndex)] = true;
                    if (SwLayoutDetail::itemHasExpandFlagOnAxis_(cell.item, false)) {
                        rowGrow[static_cast<size_t>(rowIndex)] = true;
                    }
                }
            }

            for (int i = 0; i < cell.columnSpan; ++i) {
                const int columnIndex = cell.column + i;
                if (columnIndex < columns) {
                    columnOccupied[static_cast<size_t>(columnIndex)] = true;
                    if (SwLayoutDetail::itemHasExpandFlagOnAxis_(cell.item, true)) {
                        columnGrow[static_cast<size_t>(columnIndex)] = true;
                    }
                }
            }

            if (cell.rowSpan == 1 && cell.row < rows) {
                rowMin[static_cast<size_t>(cell.row)] = std::max(rowMin[static_cast<size_t>(cell.row)], totalHeight);
            }
            if (cell.columnSpan == 1 && cell.column < columns) {
                columnMin[static_cast<size_t>(cell.column)] = std::max(columnMin[static_cast<size_t>(cell.column)], totalWidth);
            }
        }

        for (const Cell& cell : m_cells) {
            if (!cell.item) {
                continue;
            }

            const SwSize preferred = cell.item->sizeHint();
            const SwSize minimum = cell.item->minimumSize();
            const int totalHeight = std::max(minimum.height, preferred.height);
            const int totalWidth = std::max(minimum.width, preferred.width);

            if (cell.rowSpan > 1) {
                ensureSpanMinimum_(rowMin,
                                   rowGrow,
                                   cell.row,
                                   cell.rowSpan,
                                   requiredSpanExtent_(totalHeight, m_verticalSpacing, cell.rowSpan));
            }
            if (cell.columnSpan > 1) {
                ensureSpanMinimum_(columnMin,
                                   columnGrow,
                                   cell.column,
                                   cell.columnSpan,
                                   requiredSpanExtent_(totalWidth, m_horizontalSpacing, cell.columnSpan));
            }
        }

        auto distributeSizes = [](int available,
                                  SwVector<int>& mins,
                                  const SwVector<int>& stretches,
                                  const SwVector<bool>& occupied,
                                  const SwVector<bool>& growable,
                                  int count) {
            int totalMin = SwLayoutDetail::sumVector_(mins);
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

            SwVector<int> growableIndices;
            growableIndices.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (occupied[static_cast<size_t>(i)] && growable[static_cast<size_t>(i)]) {
                    growableIndices.push_back(i);
                }
            }

            if (!growableIndices.isEmpty()) {
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

            SW_UNUSED(extra);
        };

        const int totalVerticalSpacing = std::max(0, rows - 1) * m_verticalSpacing;
        const int totalHorizontalSpacing = std::max(0, columns - 1) * m_horizontalSpacing;

        distributeSizes(std::max(0, rect.height - totalVerticalSpacing), rowMin, m_rowStretch, rowOccupied, rowGrow, rows);
        distributeSizes(std::max(0, rect.width - totalHorizontalSpacing), columnMin, m_columnStretch, columnOccupied, columnGrow, columns);

        SwVector<int> rowPositions(static_cast<size_t>(rows), rect.y);
        SwVector<int> columnPositions(static_cast<size_t>(columns), rect.x);

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

            const int targetWidth = resolveItemExtent_(cell.item, true, width);
            const int targetHeight = resolveItemExtent_(cell.item, false, height);
            const int targetX = x;
            const int targetY = y + std::max(0, (height - targetHeight) / 2);

            cell.item->setGeometry(SwRect{targetX, targetY, targetWidth, targetHeight});
        }
    }

private:
    SwSize layoutSizeHint_(bool minimum) const {
        if (m_cells.isEmpty()) {
            return {leftMargin() + rightMargin(), topMargin() + bottomMargin()};
        }

        int rows = 0;
        int columns = 0;
        for (const Cell& cell : m_cells) {
            rows = std::max(rows, cell.row + cell.rowSpan);
            columns = std::max(columns, cell.column + cell.columnSpan);
        }
        if (rows <= 0 || columns <= 0) {
            return {leftMargin() + rightMargin(), topMargin() + bottomMargin()};
        }

        SwVector<int> rowSizes(static_cast<size_t>(rows), 0);
        SwVector<int> columnSizes(static_cast<size_t>(columns), 0);

        auto requiredSpanExtent_ = [](int totalExtent, int spacingAmount, int span) {
            return std::max(0, totalExtent - std::max(0, span - 1) * spacingAmount);
        };

        auto ensureSpanMinimum_ = [](SwVector<int>& mins, int start, int span, int required) {
            if (required <= 0 || span <= 0) {
                return;
            }

            int current = 0;
            SwVector<int> indices;
            indices.reserve(static_cast<size_t>(span));
            for (int i = 0; i < span; ++i) {
                const int index = start + i;
                if (index < 0 || static_cast<size_t>(index) >= mins.size()) {
                    continue;
                }
                current += mins[static_cast<size_t>(index)];
                indices.push_back(index);
            }

            if (current >= required || indices.isEmpty()) {
                return;
            }

            const int deficit = required - current;
            const int per = deficit / static_cast<int>(indices.size());
            int remainder = deficit % static_cast<int>(indices.size());
            for (const int index : indices) {
                mins[static_cast<size_t>(index)] += per;
                if (remainder > 0) {
                    ++mins[static_cast<size_t>(index)];
                    --remainder;
                }
            }
        };

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

            if (cell.rowSpan == 1 && cell.row < rows) {
                rowSizes[static_cast<size_t>(cell.row)] = std::max(rowSizes[static_cast<size_t>(cell.row)], std::max(0, size.height));
            }

            if (cell.columnSpan == 1 && cell.column < columns) {
                columnSizes[static_cast<size_t>(cell.column)] = std::max(columnSizes[static_cast<size_t>(cell.column)], std::max(0, size.width));
            }
        }

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

            if (cell.rowSpan > 1) {
                ensureSpanMinimum_(rowSizes,
                                   cell.row,
                                   cell.rowSpan,
                                   requiredSpanExtent_(std::max(0, size.height), m_verticalSpacing, cell.rowSpan));
            }

            if (cell.columnSpan > 1) {
                ensureSpanMinimum_(columnSizes,
                                   cell.column,
                                   cell.columnSpan,
                                   requiredSpanExtent_(std::max(0, size.width), m_horizontalSpacing, cell.columnSpan));
            }
        }

        const int totalHeight = SwLayoutDetail::sumVector_(rowSizes) +
                                std::max(0, rows - 1) * m_verticalSpacing + topMargin() + bottomMargin();
        const int totalWidth = SwLayoutDetail::sumVector_(columnSizes) +
                               std::max(0, columns - 1) * m_horizontalSpacing + leftMargin() + rightMargin();
        return {totalWidth, totalHeight};
    }

    void clearCells_() {
        for (Cell& cell : m_cells) {
            delete cell.item;
            cell.item = nullptr;
        }
        m_cells.clear();
    }

    SwVector<Cell> m_cells;
    SwVector<int> m_columnStretch;
    SwVector<int> m_rowStretch;
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
        adoptWidgetToParent_(label);
        adoptWidgetToParent_(field);
        addRowItems_(new SwWidgetItem(label), new SwWidgetItem(field));
    }

    void setItem(int row, int column, SwLayoutItem* item) {
        if (!item || row < 0) {
            delete item;
            return;
        }
        adoptWidgetToParent_(item->widget());

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
        adoptWidgetToParent_(widget);
        setItem(row, column, new SwWidgetItem(widget));
    }

    void addItem(SwLayoutItem* item) {
        if (!item) {
            return;
        }
        adoptWidgetToParent_(item->widget());
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
        adoptWidgetToParent_(widget);
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

    void forEachManagedWidget(const std::function<void(SwWidgetInterface*)>& visitor) const override {
        if (!visitor) {
            return;
        }
        if (m_pending) {
            SwWidgetInterface* widget = m_pending->widget();
            if (widget) {
                visitor(widget);
            }
        }
        for (const Row& row : m_rows) {
            SwWidgetInterface* label = row.label ? row.label->widget() : nullptr;
            if (label) {
                visitor(label);
            }
            SwWidgetInterface* field = row.field ? row.field->widget() : nullptr;
            if (field) {
                visitor(field);
            }
        }
    }

protected:
    void applyLayout(const SwRect& rect) override {
        if (m_rows.isEmpty()) {
            return;
        }

        int labelWidth = 0;
        SwVector<size_t> validRows;
        validRows.reserve(m_rows.size());
        for (size_t i = 0; i < m_rows.size(); ++i) {
            const Row& row = m_rows[i];
            if (!row.label || !row.field) {
                continue;
            }
            validRows.push_back(i);
            labelWidth = std::max(labelWidth, std::max(0, row.label->sizeHint().width));
        }

        if (validRows.isEmpty()) {
            return;
        }

        const int columnSpacing = spacing();
        labelWidth = std::min(labelWidth, std::max(0, rect.width / 2));
        const int fieldX = rect.x + labelWidth + columnSpacing;
        const int fieldWidth = std::max(0, rect.width - labelWidth - columnSpacing);

        SwVector<int> rowHeights;
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
            return {leftMargin() + rightMargin(), topMargin() + bottomMargin()};
        }

        totalHeight += std::max(0, count - 1) * spacing();
        const int totalWidth = labelWidth + fieldWidth + spacing();
        return {totalWidth + leftMargin() + rightMargin(), totalHeight + topMargin() + bottomMargin()};
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

    SwVector<Row> m_rows;
    SwLayoutItem* m_pending{nullptr};
};
