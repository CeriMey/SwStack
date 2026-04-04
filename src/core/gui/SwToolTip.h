#pragma once

/**
 * @file
 * @ingroup core_gui
 * @brief Declares the tooltip manager used by the widget layer.
 *
 * `SwToolTip` tracks hover state, delays popup creation, measures tooltip text, and paints
 * the floating tooltip widget on demand. The implementation is centralized here so widgets
 * can expose tooltip text without each one reimplementing timing and popup behavior.
 */



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

#include "SwPainter.h"
#include "SwWidget.h"
#include "SwWidgetPlatformAdapter.h"

#include "core/runtime/SwTimer.h"
#include "core/types/SwString.h"

#include <algorithm>

class SwToolTip {
public:
    /**
     * @brief Performs the `handleMouseMove` operation.
     * @param root Value passed to the method.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @return The requested handle Mouse Move.
     */
    static void handleMouseMove(SwWidget* root, int x, int y) { instance_().handleMouseMove_(root, nullptr, x, y); }
    static void handleMouseMove(SwWidget* root, SwWidget* hoveredWidget, int x, int y) { instance_().handleMouseMove_(root, hoveredWidget, x, y); }
    /**
     * @brief Performs the `handleMousePress` operation.
     * @return The requested handle Mouse Press.
     */
    static void handleMousePress() { instance_().hide_(); }
    /**
     * @brief Performs the `handleKeyPress` operation.
     * @return The requested handle Key Press.
     */
    static void handleKeyPress() { instance_().hide_(); }

    /**
     * @brief Performs the `showText` operation.
     * @param root Value passed to the method.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param text Value passed to the method.
     * @return The requested show Text.
     */
    static void showText(SwWidget* root, int x, int y, const SwString& text) { instance_().showText_(root, x, y, text); }
    /**
     * @brief Performs the `hideText` operation.
     * @return The requested hide Text.
     */
    static void hideText() { instance_().hide_(); }
    /**
     * @brief Returns whether the object reports visible.
     * @return The requested visible.
     *
     * @details This query does not modify the object state.
     */
    static bool isVisible() { return instance_().m_popup && instance_().m_popup->getVisible(); }

private:
    class Popup final : public SwWidget {
        SW_OBJECT(Popup, SwWidget)

    public:
        /**
         * @brief Performs the `Popup` operation.
         * @param parent Optional parent object that owns this instance.
         * @return The requested popup.
         */
        explicit Popup(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            setFocusPolicy(FocusPolicyEnum::NoFocus);
            setCursor(CursorType::Arrow);
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        /**
         * @brief Sets the text.
         * @param text Value passed to the method.
         *
         * @details Call this method to replace the currently stored value with the caller-provided one.
         */
        void setText(const SwString& text) {
            if (m_text == text) {
                return;
            }
            m_text = text;
            updateMetrics_();
            update();
        }

        /**
         * @brief Returns the current text.
         * @return The current text.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        SwString text() const { return m_text; }

        /**
         * @brief Returns the current size Hint.
         * @return The current size Hint.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        SwSize sizeHint() const override {
            return SwSize{m_cachedW, m_cachedH};
        }

        /**
         * @brief Handles the paint Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void paintEvent(PaintEvent* event) override {
            if (!event || !event->painter() || !isVisibleInHierarchy()) {
                return;
            }
            SwPainter* painter = event->painter();
            const SwRect r = rect();
            if (r.width <= 0 || r.height <= 0) {
                return;
            }

            const SwColor fill{15, 23, 42};       // slate-900
            const SwColor border{30, 41, 59};     // slate-800
            const SwColor textColor{241, 245, 249}; // slate-100
            const SwFont font(L"Segoe UI", 9, Normal);

            painter->fillRoundedRect(r, 10, fill, border, 1);

            const int padX = 10;
            const int padY = 6;
            SwRect textRect{r.x + padX, r.y + padY, std::max(0, r.width - padX * 2), std::max(0, r.height - padY * 2)};
            painter->drawText(textRect,
                              m_text,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak),
                              textColor,
                              font);
        }

    private:
        void updateMetrics_() {
            const SwFont font(L"Segoe UI", 9, Normal);
            const int maxW = 520;
            const int padX = 10;
            const int padY = 6;
            const int lineH = 18;
            const SwList<SwString> lines = m_text.split('\n');

            int maxLineWidth = 0;
            int wrappedLineCount = 0;
            if (lines.isEmpty()) {
                const int fallback = static_cast<int>(m_text.size()) * 7;
                int textW = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), m_text, font, m_text.size(), fallback);
                if (textW < 0) {
                    textW = fallback;
                }
                maxLineWidth = std::max(0, textW);
                wrappedLineCount = (maxLineWidth > 0) ? std::max(1, (maxLineWidth + std::min(maxW, maxLineWidth) - 1) / std::max(1, std::min(maxW, maxLineWidth))) : 1;
            } else {
                for (int i = 0; i < lines.size(); ++i) {
                    const SwString& line = lines[i];
                    const int fallback = std::max(1, static_cast<int>(line.size()) * 7);
                    int lineWidth = SwWidgetPlatformAdapter::textWidthUntil(nativeWindowHandle(), line, font, line.size(), fallback);
                    if (lineWidth < 0) {
                        lineWidth = fallback;
                    }
                    lineWidth = std::max(0, lineWidth);
                    maxLineWidth = std::max(maxLineWidth, lineWidth);
                    const int effectiveWidth = std::max(1, std::min(maxW, std::max(1, lineWidth)));
                    wrappedLineCount += std::max(1, (lineWidth + effectiveWidth - 1) / effectiveWidth);
                }
            }

            const int wrappedW = std::min(maxW, std::max(0, maxLineWidth));
            m_cachedW = std::max(40, wrappedW + padX * 2);
            m_cachedH = std::max(24, std::max(1, wrappedLineCount) * lineH + padY * 2);
        }

        SwString m_text;
        int m_cachedW{120};
        int m_cachedH{32};
    };

    static SwToolTip& instance_() {
        static SwToolTip inst;
        return inst;
    }

    static SwWidget* deepestChildAt_(SwWidget* root, int x, int y) {
        if (!root) {
            return nullptr;
        }
        SwWidget* child = root->getChildUnderCursor(x, y);
        return child ? child : root;
    }

    static SwWidget* tooltipProviderFor_(SwWidget* widget) {
        SwWidget* w = widget;
        while (w) {
            if (w->isVisibleInHierarchy() && !w->getToolTips().isEmpty()) {
                return w;
            }
            w = dynamic_cast<SwWidget*>(w->parent());
        }
        return nullptr;
    }

    void handleMouseMove_(SwWidget* root, SwWidget* hoveredWidget, int x, int y) {
        if (!root) {
            hide_();
            return;
        }

        SwWidget* deepest = hoveredWidget;
        if (!deepest || !SwObject::isLive(deepest) || !deepest->isVisibleInHierarchy()) {
            deepest = deepestChildAt_(root, x, y);
        }
        SwWidget* provider = tooltipProviderFor_(deepest);
        const SwString text = provider ? provider->getToolTips() : SwString();

        if (text.isEmpty()) {
            hide_();
            m_pendingWidget = nullptr;
            m_pendingText.clear();
            return;
        }

        if (provider != m_pendingWidget || text != m_pendingText) {
            hide_();
            m_root = root;
            m_pendingWidget = provider;
            m_pendingText = text;
            m_lastX = x;
            m_lastY = y;
            startTimer_();
            return;
        }

        m_lastX = x;
        m_lastY = y;
    }

    void startTimer_() {
        if (!m_timer) {
            m_timer = new SwTimer(nullptr);
            m_timer->setSingleShot(true);
            SwObject::connect(m_timer, &SwTimer::timeout, this, [this]() { showPending_(); });
        }
        m_timer->stop();
        m_timer->start(m_delayMs);
    }

    void ensurePopup_() {
        if (!m_root) {
            return;
        }
        if (m_popup && m_popup->parent() == m_root) {
            return;
        }
        if (m_popup) {
            delete m_popup;
            m_popup = nullptr;
        }
        m_popup = new Popup(m_root);
        m_popup->hide();
    }

    void showPending_() {
        if (!m_root || !m_pendingWidget || m_pendingText.isEmpty()) {
            return;
        }
        ensurePopup_();
        if (!m_popup) {
            return;
        }

        m_popup->setText(m_pendingText);

        const SwSize hint = m_popup->sizeHint();
        const int w = std::max(0, hint.width);
        const int h = std::max(0, hint.height);

        const int margin = 6;
        const int offsetX = 16;
        const int offsetY = 22;

        int x = m_lastX + offsetX;
        int y = m_lastY + offsetY;

        const int maxX = std::max(margin, m_root->width() - w - margin);
        const int maxY = std::max(margin, m_root->height() - h - margin);
        x = std::max(margin, std::min(x, maxX));
        y = std::max(margin, std::min(y, maxY));

        m_popup->move(x, y);
        m_popup->resize(w, h);
        m_popup->show();
        m_popup->update();
    }

    void showText_(SwWidget* root, int x, int y, const SwString& text) {
        if (!root || text.isEmpty()) {
            hide_();
            return;
        }
        m_root = root;
        m_pendingWidget = root;
        m_pendingText = text;
        m_lastX = x;
        m_lastY = y;
        showPending_();
    }

    void hide_() {
        if (m_timer) {
            m_timer->stop();
        }
        if (m_popup) {
            m_popup->hide();
        }
        m_pendingWidget = nullptr;
        m_pendingText.clear();
    }

    SwToolTip() = default;
    ~SwToolTip() = default;

    SwWidget* m_root{nullptr};
    Popup* m_popup{nullptr};
    SwTimer* m_timer{nullptr};
    SwWidget* m_pendingWidget{nullptr};
    SwString m_pendingText;
    int m_lastX{0};
    int m_lastY{0};
    int m_delayMs{700};
};

