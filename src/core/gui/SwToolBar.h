#pragma once
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

#include "SwAction.h"
#include "SwFrame.h"
#include "SwToolButton.h"

#include "core/types/SwVector.h"

#include <algorithm>
#include <functional>

class SwToolBar : public SwFrame {
    SW_OBJECT(SwToolBar, SwFrame)

public:
    explicit SwToolBar(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
    }

    SwAction* addAction(const SwString& text, const std::function<void()>& callback = {}) {
        auto* action = new SwAction(text, this);
        if (callback) {
            SwObject::connect(action, &SwAction::triggered, this, [callback](bool) { callback(); });
        }
        addAction(action);
        return action;
    }

    void addAction(SwAction* action) {
        if (!action) {
            return;
        }
        if (!action->parent()) {
            action->setParent(this);
        }

        auto* btn = new SwToolButton(action->getText(), this);
        btn->resize(96, height() - 12);
        btn->setCheckable(action->getCheckable());
        btn->setChecked(action->getChecked());
        btn->setEnable(action->getEnabled());

        SwObject::connect(btn, &SwToolButton::clicked, this, [action](bool) { action->trigger(); });
        SwObject::connect(action, &SwAction::changed, this, [this]() { updateButtonsFromActions(); });

        Item item;
        item.action = action;
        item.widget = btn;
        item.separator = false;
        m_items.push_back(item);

        updateLayout();
    }

    void addSeparator() {
        auto* sep = new Separator(this);
        Item item;
        item.separator = true;
        item.widget = sep;
        m_items.push_back(item);
        updateLayout();
    }

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = getRect();
        StyleSheet* sheet = getToolSheet();

        SwColor bg{248, 250, 252};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{226, 232, 240};
        int borderWidth = 1;
        int radius = 14;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        painter->fillRoundedRect(bounds, radius, bg, border, borderWidth);
        paintChildren(event);
        painter->finalize();
    }

private:
    class Separator final : public SwWidget {
        SW_OBJECT(Separator, SwWidget)

    public:
        explicit Separator(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }
            const SwRect r = getRect();
            const int x = r.x + r.width / 2;
            painter->drawLine(x, r.y + 8, x, r.y + r.height - 8, SwColor{226, 232, 240}, 1);
        }
    };

    struct Item {
        SwAction* action{nullptr};
        SwWidget* widget{nullptr};
        bool separator{false};
    };

    void initDefaults() {
        resize(520, 52);
        setCursor(CursorType::Arrow);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFrameShape(Shape::NoFrame);
        setStyleSheet(R"(
            SwToolBar {
                background-color: rgb(248, 250, 252);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 16px;
            }
        )");
    }

    void updateButtonsFromActions() {
        for (int i = 0; i < m_items.size(); ++i) {
            Item& item = m_items[i];
            if (item.separator || !item.action || !item.widget) {
                continue;
            }
            auto* btn = dynamic_cast<SwToolButton*>(item.widget);
            if (!btn) {
                continue;
            }
            btn->setText(item.action->getText());
            btn->setEnable(item.action->getEnabled());
            btn->setCheckable(item.action->getCheckable());
            btn->setChecked(item.action->getChecked());
        }
        updateLayout();
    }

    void updateLayout() {
        const SwRect bounds = getRect();
        int borderWidth = 1;
        int radius = 0;
        SwColor border{0, 0, 0};
        resolveBorder(getToolSheet(), border, borderWidth, radius);
        borderWidth = std::max(0, borderWidth);

        const int x0 = bounds.x + borderWidth + m_margin;
        const int y0 = bounds.y + borderWidth;
        const int h = std::max(0, bounds.height - 2 * borderWidth);
        const int innerH = std::max(0, h - 2 * m_margin);

        int x = x0;
        for (int i = 0; i < m_items.size(); ++i) {
            Item& item = m_items[i];
            if (!item.widget) {
                continue;
            }
            if (item.separator) {
                item.widget->move(x, y0);
                item.widget->resize(14, h);
                x += 14 + m_spacing;
                continue;
            }

            SwRect hint = item.widget->sizeHint();
            const int w = clampInt(hint.width > 0 ? hint.width : 96, 64, 240);
            item.widget->move(x, y0 + m_margin);
            item.widget->resize(w, innerH);
            x += w + m_spacing;
        }
        update();
    }

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    SwVector<Item> m_items;
    int m_margin{8};
    int m_spacing{8};
};
