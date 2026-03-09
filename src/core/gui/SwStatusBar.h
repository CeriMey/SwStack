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
 * @file src/core/gui/SwStatusBar.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwStatusBar in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the status bar interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwStatusBar.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwStatusBar - status bar widget.
 *
 * Focus:
 * - Bottom bar with a message area + optional permanent widgets on the right.
 * - Snapshot-friendly, styleable container.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwLabel.h"
#include "SwTimer.h"

#include "core/types/SwVector.h"

#include <algorithm>

class SwStatusBar : public SwFrame {
    SW_OBJECT(SwStatusBar, SwFrame)

public:
    /**
     * @brief Constructs a `SwStatusBar` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwStatusBar(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        buildChildren();
    }

    /**
     * @brief Performs the `showMessage` operation.
     * @param message Value passed to the method.
     * @param timeoutMs Timeout expressed in milliseconds.
     */
    void showMessage(const SwString& message, int timeoutMs = 0) {
        if (m_messageLabel) {
            m_messageLabel->setText(message);
            m_messageLabel->update();
        }
        if (timeoutMs > 0) {
            ensureTimer();
            if (m_clearTimer) {
                m_clearTimer->start(timeoutMs);
            }
        }
        updateLayout();
    }

    /**
     * @brief Clears the current object state.
     */
    void clearMessage() {
        if (m_messageLabel) {
            m_messageLabel->setText("");
        }
        if (m_clearTimer) {
            m_clearTimer->stop();
        }
        updateLayout();
    }

    /**
     * @brief Adds the specified permanent Widget.
     * @param widget Widget associated with the operation.
     */
    void addPermanentWidget(SwWidget* widget) {
        if (!widget) {
            return;
        }
        if (!widget->parent()) {
            widget->setParent(this);
        }
        m_permanent.push_back(widget);
        updateLayout();
    }

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
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
    void initDefaults() {
        resize(520, 26);
        setCursor(CursorType::Arrow);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFrameShape(Shape::NoFrame);
        setStyleSheet(R"(
            SwStatusBar {
                background-color: rgb(243, 243, 243);
                border-color: rgb(218, 218, 218);
                border-width: 1px;
                border-radius: 0px;
            }
        )");
    }

    void buildChildren() {
        if (m_messageLabel) {
            return;
        }
        m_messageLabel = new SwLabel("", this);
        m_messageLabel->setStyleSheet(R"(
            SwLabel {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                color: rgb(51, 65, 85);
                font-size: 12px;
            }
        )");
        m_messageLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
        updateLayout();
    }

    void ensureTimer() {
        if (m_clearTimer) {
            return;
        }
        m_clearTimer = new SwTimer(0, this);
        SwObject::connect(m_clearTimer, &SwTimer::timeout, this, [this]() { clearMessage(); });
    }

    void updateLayout() {
        int borderWidth = 1;
        int radius = 0;
        SwColor border{0, 0, 0};
        resolveBorder(getToolSheet(), border, borderWidth, radius);
        borderWidth = std::max(0, borderWidth);

        // Use local coordinates (relative to this widget) for child positioning
        const int w = width();
        const int h = height();
        const int x0 = borderWidth + m_margin;
        const int y0 = borderWidth + m_margin;
        const int innerH = std::max(0, h - 2 * borderWidth - 2 * m_margin);

        int right = w - borderWidth - m_margin;
        for (int i = m_permanent.size() - 1; i >= 0; --i) {
            SwWidget* pw = m_permanent[i];
            if (!pw) {
                continue;
            }
            SwSize hint = pw->sizeHint();
            const int ww = std::max(24, hint.width);
            right -= ww;
            pw->move(right, y0);
            pw->resize(ww, innerH);
            right -= m_spacing;
        }

        if (m_messageLabel) {
            const int msgW = std::max(0, right - x0);
            m_messageLabel->move(x0, y0);
            m_messageLabel->resize(msgW, innerH);
        }

        update();
    }

    SwLabel* m_messageLabel{nullptr};
    SwVector<SwWidget*> m_permanent;
    SwTimer* m_clearTimer{nullptr};

    int m_margin{4};
    int m_spacing{6};
};

