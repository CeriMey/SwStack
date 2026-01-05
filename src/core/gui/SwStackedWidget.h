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

/***************************************************************************************************
 * SwStackedWidget - Qt-like stacked container (≈ QStackedWidget).
 *
 * Shows one child widget at a time; all others are hidden.
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwVector.h"

class SwStackedWidget : public SwWidget {
    SW_OBJECT(SwStackedWidget, SwWidget)

public:
    explicit SwStackedWidget(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
    }

    int addWidget(SwWidget* widget) {
        return insertWidget(m_widgets.size(), widget);
    }

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

    int count() const { return m_widgets.size(); }

    SwWidget* widget(int index) const {
        if (index < 0 || index >= m_widgets.size()) {
            return nullptr;
        }
        return m_widgets[index];
    }

    int currentIndex() const { return m_currentIndex; }

    SwWidget* currentWidget() const {
        return widget(m_currentIndex);
    }

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
        const SwRect r = getRect();
        const int pad = std::max(0, m_padding);
        const SwRect inner{r.x + pad,
                           r.y + pad,
                           std::max(0, r.width - 2 * pad),
                           std::max(0, r.height - 2 * pad)};

        for (int i = 0; i < m_widgets.size(); ++i) {
            SwWidget* w = m_widgets[i];
            if (!w) {
                continue;
            }
            w->move(inner.x, inner.y);
            w->resize(inner.width, inner.height);
        }
    }

    SwVector<SwWidget*> m_widgets;
    int m_currentIndex{-1};
    int m_padding{0};
};
