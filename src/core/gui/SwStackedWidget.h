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
 * @file src/core/gui/SwStackedWidget.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwStackedWidget in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the stacked widget interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwStackedWidget.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwStackedWidget - stacked container.
 *
 * Shows one child widget at a time; all others are hidden.
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwVector.h"

class SwStackedWidget : public SwWidget {
    SW_OBJECT(SwStackedWidget, SwWidget)

public:
    /**
     * @brief Constructs a `SwStackedWidget` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwStackedWidget(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    /**
     * @brief Adds the specified widget.
     * @param widget Widget associated with the operation.
     * @return The requested widget.
     */
    int addWidget(SwWidget* widget) {
        return insertWidget(m_widgets.size(), widget);
    }

    /**
     * @brief Performs the `insertWidget` operation.
     * @param index Value passed to the method.
     * @param widget Widget associated with the operation.
     * @return The requested insert Widget.
     */
    int insertWidget(int index, SwWidget* widget) {
        if (!widget) {
            return -1;
        }
        if (index < 0) {
            index = 0;
        }
        const int n = m_widgets.size();
        if (index > n) {
            index = n;
        }

        if (widget->parent() != this) {
            widget->setParent(this);
        }

        SwVector<SwWidget*> copy;
        copy.reserve(static_cast<SwVector<SwWidget*>::size_type>(n + 1));
        for (int i = 0; i < n + 1; ++i) {
            if (i == index) {
                copy.push_back(widget);
            } else {
                const int src = (i < index) ? i : (i - 1);
                if (src >= 0 && src < n) {
                    copy.push_back(m_widgets[src]);
                }
            }
        }
        m_widgets = copy;

        if (m_currentIndex < 0) {
            m_currentIndex = 0;
        } else if (m_currentIndex >= index) {
            ++m_currentIndex;
        }

        applyVisibility();
        updateLayout();
        update();
        return index;
    }

    /**
     * @brief Removes the specified widget.
     * @param widget Widget associated with the operation.
     */
    void removeWidget(SwWidget* widget) {
        if (!widget) {
            return;
        }
        for (int i = 0; i < m_widgets.size(); ++i) {
            if (m_widgets[i] == widget) {
                m_widgets.erase(m_widgets.begin() + i);
                if (m_currentIndex == i) {
                    const int newSize = m_widgets.size();
                    m_currentIndex = (newSize > 0) ? ((i < newSize) ? i : (newSize - 1)) : -1;
                    currentChanged(m_currentIndex);
                } else if (m_currentIndex > i) {
                    --m_currentIndex;
                }
                widget->setVisible(false);
                updateLayout();
                update();
                return;
            }
        }
    }

    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const { return m_widgets.size(); }

    /**
     * @brief Performs the `widget` operation.
     * @param index Value passed to the method.
     * @return The requested widget.
     */
    SwWidget* widget(int index) const {
        if (index < 0 || index >= m_widgets.size()) {
            return nullptr;
        }
        return m_widgets[index];
    }

    /**
     * @brief Returns the current current Index.
     * @return The current current Index.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int currentIndex() const { return m_currentIndex; }

    /**
     * @brief Returns the current current Widget.
     * @return The current current Widget.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWidget* currentWidget() const {
        return widget(m_currentIndex);
    }

    /**
     * @brief Sets the current Index.
     * @param index Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCurrentIndex(int index) {
        if (index < 0 || index >= m_widgets.size()) {
            return;
        }
        if (index == m_currentIndex) {
            return;
        }
        m_currentIndex = index;
        applyVisibility();
        updateLayout();
        currentChanged(m_currentIndex);
        update();
    }

    DECLARE_SIGNAL(currentChanged, int);

protected:
    SwSize sizeHint() const override {
        return stackedSizeHint_(false);
    }

    SwSize minimumSizeHint() const override {
        return stackedSizeHint_(true);
    }

    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwWidget::resizeEvent(event);
        updateLayout();
    }

private:
    void initDefaults() {
        setStyleSheet("SwStackedWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::NoFocus);
    }

    void applyVisibility() {
        for (int i = 0; i < m_widgets.size(); ++i) {
            if (!m_widgets[i]) {
                continue;
            }
            m_widgets[i]->setVisible(i == m_currentIndex);
        }
    }

    void updateLayout() {
        const int pad = std::max(0, m_padding);
        const int innerX = pad;
        const int innerY = pad;
        const int innerW = std::max(0, width() - 2 * pad);
        const int innerH = std::max(0, height() - 2 * pad);

        for (int i = 0; i < m_widgets.size(); ++i) {
            SwWidget* w = m_widgets[i];
            if (!w) {
                continue;
            }
            w->move(innerX, innerY);
            w->resize(innerW, innerH);
        }
    }

    SwSize stackedSizeHint_(bool minimum) const {
        SwSize pageHint{0, 0};
        auto pageSizeHint = [minimum](SwWidget* page) -> SwSize {
            if (!page) {
                return SwSize{0, 0};
            }
            return minimum ? page->minimumSizeHint() : page->sizeHint();
        };

        if (SwWidget* current = currentWidget()) {
            pageHint = pageSizeHint(current);
        } else {
            for (int i = 0; i < m_widgets.size(); ++i) {
                const SwSize childHint = pageSizeHint(m_widgets[i]);
                pageHint.width = std::max(pageHint.width, childHint.width);
                pageHint.height = std::max(pageHint.height, childHint.height);
            }
        }

        const SwSize minSize = minimumSize();
        const SwSize maxSize = maximumSize();
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        SwSize hint{
            pageHint.width + (m_padding * 2),
            pageHint.height + (m_padding * 2)
        };
        hint.width = std::max(hint.width, std::max(minSize.width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minSize.height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maxSize.width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maxSize.height, styleMax.height));
        return hint;
    }

    SwVector<SwWidget*> m_widgets;
    int m_currentIndex{-1};
    int m_padding{0};
};
