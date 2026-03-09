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
 * @file src/core/gui/SwWizard.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwWizard in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the wizard interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwWizardPage and SwWizard.
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
 * SwWizard - modern multi-page wizard dialog.
 *
 * Focus:
 * - Horizontal stepper bar at top (circles + connector lines + labels).
 * - addPage() with title and content widget.
 * - Back / Next / Finish / Cancel buttons.
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwLabel.h"
#include "SwPushButton.h"
#include "SwStackedWidget.h"

#include <algorithm>

class SwWizardPage : public SwWidget {
    SW_OBJECT(SwWizardPage, SwWidget)
public:
    /**
     * @brief Constructs a `SwWizardPage` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwWizardPage(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    }

    /**
     * @brief Sets the title.
     * @param t Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitle(const SwString& t) { m_title = t; }
    /**
     * @brief Returns the current title.
     * @return The current title.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString title() const { return m_title; }

    /**
     * @brief Sets the sub Title.
     * @param t Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSubTitle(const SwString& t) { m_subTitle = t; }
    /**
     * @brief Returns the current sub Title.
     * @return The current sub Title.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString subTitle() const { return m_subTitle; }

private:
    SwString m_title;
    SwString m_subTitle;
};

class SwWizard : public SwDialog {
    SW_OBJECT(SwWizard, SwDialog)

public:
    /**
     * @brief Constructs a `SwWizard` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwWizard(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(560, 400);
        buildUi();
    }

    /**
     * @brief Adds the specified page.
     * @param page Value passed to the method.
     * @return The requested page.
     */
    int addPage(SwWizardPage* page) {
        if (!page || !m_stack) return -1;
        m_pages.push_back(page);
        m_stack->addWidget(page);
        updateButtons();
        updateStepper();
        updateWizardLayout();
        return m_pages.size() - 1;
    }

    /**
     * @brief Returns the current current Id.
     * @return The current current Id.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int currentId() const { return m_currentIndex; }

    /**
     * @brief Returns the current current Page.
     * @return The current current Page.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwWizardPage* currentPage() const {
        if (m_currentIndex >= 0 && m_currentIndex < m_pages.size())
            return m_pages[m_currentIndex];
        return nullptr;
    }

    /**
     * @brief Performs the `next` operation.
     */
    void next() {
        if (m_currentIndex < m_pages.size() - 1) {
            m_currentIndex++;
            if (m_stack) m_stack->setCurrentIndex(m_currentIndex);
            currentIdChanged(m_currentIndex);
            updateButtons();
            updateStepper();
        }
    }

    /**
     * @brief Performs the `back` operation.
     */
    void back() {
        if (m_currentIndex > 0) {
            m_currentIndex--;
            if (m_stack) m_stack->setCurrentIndex(m_currentIndex);
            currentIdChanged(m_currentIndex);
            updateButtons();
            updateStepper();
        }
    }

signals:
    DECLARE_SIGNAL(currentIdChanged, int);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateWizardLayout();
    }

private:
    static constexpr int kStepperHeight = 90;
    static constexpr int kDotSize = 24;
    static constexpr int kSmallDot = 8;
#if defined(_WIN32)
    static constexpr const wchar_t* kFontFamily = L"Segoe UI";
#else
    static constexpr const wchar_t* kFontFamily = L"Sans";
#endif

    // ---- Horizontal stepper bar ----
    class StepperWidget final : public SwWidget {
        SW_OBJECT(StepperWidget, SwWidget)
    public:
        /**
         * @brief Performs the `StepperWidget` operation.
         * @param parent Optional parent object that owns this instance.
         * @return The requested stepper Widget.
         */
        explicit StepperWidget(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        /**
         * @brief Sets the steps.
         * @param titles Value passed to the method.
         * @param current Value passed to the method.
         *
         * @details Call this method to replace the currently stored value with the caller-provided one.
         */
        void setSteps(const SwVector<SwString>& titles, int current) {
            m_titles = titles;
            m_current = current;
            update();
        }

        /**
         * @brief Handles the paint Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) return;

            const int count = m_titles.size();
            if (count == 0) return;

            const SwRect r = rect();
            const int ox = r.x;  // local origin x
            const int oy = r.y;  // local origin y
            const int w = r.width;
            const int h = r.height;
            const int dotR = kDotSize / 2;
            const int cy = oy + dotR + 8;
            const int margin = dotR + 54;
            const int usable = w - 2 * margin;
            const int spacing = (count > 1) ? usable / (count - 1) : 0;

            // Draw connector lines first (behind dots)
            for (int i = 0; i < count - 1; ++i) {
                const int x1 = ox + margin + i * spacing + kDotSize / 2 + 2;
                const int x2 = ox + margin + (i + 1) * spacing - kDotSize / 2 - 2;
                if (x2 > x1) {
                    const bool done = (i < m_current);
                    SwColor lineColor = done
                        ? SwColor{59, 130, 246}
                        : SwColor{226, 232, 240};
                    SwRect lineRect{x1, cy - 1, x2 - x1, 3};
                    painter->fillRoundedRect(lineRect, 1, 1, 1, 1, lineColor, lineColor, 0);
                }
            }

            // Draw dots and labels
            for (int i = 0; i < count; ++i) {
                const int cx = ox + margin + i * spacing;

                if (i < m_current) {
                    // Completed: blue filled dot with white checkmark
                    SwRect dotRect{cx - kDotSize / 2, cy - kDotSize / 2, kDotSize, kDotSize};
                    painter->fillEllipse(dotRect, SwColor{59, 130, 246}, SwColor{59, 130, 246}, 0);
                    const SwFont f(kFontFamily, 11, Bold);
                    painter->drawText(dotRect, SwString::fromWString(L"\u2713"),
                        DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                        SwColor{255, 255, 255}, f);
                } else if (i == m_current) {
                    // Current: blue ring with blue number, white fill
                    SwRect dotRect{cx - kDotSize / 2, cy - kDotSize / 2, kDotSize, kDotSize};
                    painter->fillEllipse(dotRect, SwColor{239, 246, 255}, SwColor{59, 130, 246}, 2);
                    const SwFont f(kFontFamily, 11, Bold);
                    painter->drawText(dotRect, SwString::number(i + 1),
                        DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                        SwColor{59, 130, 246}, f);
                } else {
                    // Future: gray small dot
                    SwRect dotRect{cx - kSmallDot / 2, cy - kSmallDot / 2, kSmallDot, kSmallDot};
                    painter->fillEllipse(dotRect, SwColor{226, 232, 240}, SwColor{226, 232, 240}, 0);
                }

                // Label below dot
                const int labelW = (count > 1) ? std::max(100, spacing) : w;
                SwRect labelRect{cx - labelW / 2, cy + kDotSize / 2 + 6, labelW, 20};
                SwColor textColor;
                int fontSize;
                FontWeight weight;
                if (i == m_current) {
                    textColor = SwColor{30, 41, 59};
                    fontSize = 12;
                    weight = Bold;
                } else if (i < m_current) {
                    textColor = SwColor{59, 130, 246};
                    fontSize = 11;
                    weight = Medium;
                } else {
                    textColor = SwColor{148, 163, 184};
                    fontSize = 11;
                    weight = Medium;
                }
                const SwFont labelFont(kFontFamily, fontSize, weight);
                painter->drawText(labelRect, m_titles[i],
                    DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                    textColor, labelFont);
            }
        }

    private:
        SwVector<SwString> m_titles;
        int m_current{0};
    };

    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) return;

        m_stepper = new StepperWidget(content);

        // Separator line under stepper
        m_separator = new SwWidget(content);
        m_separator->setStyleSheet("SwWidget { background-color: rgb(226, 232, 240); border-width: 0px; }");
        m_separator->resize(100, 1);

        m_titleLabel = new SwLabel("", content);
        m_titleLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(15, 23, 42); font-size: 20px; font-weight: bold; }
        )");
        m_titleLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter));

        m_subTitleLabel = new SwLabel("", content);
        m_subTitleLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(100, 116, 139); font-size: 13px; }
        )");
        m_subTitleLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter));

        m_stack = new SwStackedWidget(content);

        // Buttons
        m_backBtn = new SwPushButton("Back", bar);
        m_nextBtn = new SwPushButton("Next", bar);
        m_finishBtn = new SwPushButton("Finish", bar);
        m_cancelBtn = new SwPushButton("Cancel", bar);

        m_backBtn->resize(90, 36);
        m_nextBtn->resize(90, 36);
        m_finishBtn->resize(90, 36);
        m_cancelBtn->resize(90, 36);

        SwString primaryStyle = R"(
            SwPushButton { background-color: rgb(59, 130, 246); color: #FFFFFF; border-radius: 10px; border-width: 0px; font-size: 14px; }
        )";
        SwString secondaryStyle = R"(
            SwPushButton { background-color: rgb(255, 255, 255); color: rgb(51, 65, 85); border-radius: 10px; border-color: rgb(203, 213, 225); border-width: 1px; font-size: 14px; }
        )";

        m_nextBtn->setStyleSheet(primaryStyle);
        m_finishBtn->setStyleSheet(primaryStyle);
        m_backBtn->setStyleSheet(secondaryStyle);
        m_cancelBtn->setStyleSheet(secondaryStyle);

        SwObject::connect(m_backBtn, &SwPushButton::clicked, this, [this]() { back(); });
        SwObject::connect(m_nextBtn, &SwPushButton::clicked, this, [this]() { next(); });
        SwObject::connect(m_finishBtn, &SwPushButton::clicked, this, [this]() { accept(); });
        SwObject::connect(m_cancelBtn, &SwPushButton::clicked, this, [this]() { reject(); });

        updateButtons();
        updateWizardLayout();
    }

    void updateButtons() {
        const int count = m_pages.size();
        const bool isFirst = (m_currentIndex <= 0);
        const bool isLast = (m_currentIndex >= count - 1);

        if (m_backBtn) m_backBtn->setVisible(!isFirst);
        if (m_nextBtn) m_nextBtn->setVisible(!isLast);
        if (m_finishBtn) m_finishBtn->setVisible(isLast);

        layoutWizardButtons();
    }

    void updateStepper() {
        if (!m_stepper) return;
        SwVector<SwString> titles;
        for (int i = 0; i < m_pages.size(); ++i) {
            titles.push_back(m_pages[i]->title());
        }
        m_stepper->setSteps(titles, m_currentIndex);

        SwWizardPage* page = currentPage();
        if (m_titleLabel) {
            m_titleLabel->setText(page ? page->title() : SwString());
            m_titleLabel->update();
        }
        if (m_subTitleLabel) {
            SwString sub = page ? page->subTitle() : SwString();
            m_subTitleLabel->setText(sub);
            m_subTitleLabel->setVisible(!sub.isEmpty());
            m_subTitleLabel->update();
        }
    }

    void updateWizardLayout() {
        auto* content = contentWidget();
        if (!content || !m_stepper || !m_titleLabel || !m_stack) return;
        const int cw = content->width();
        const int ch = content->height();
        const int pad = 24;

        // Stepper at top
        m_stepper->move(0, 0);
        m_stepper->resize(cw, kStepperHeight);

        // Separator
        if (m_separator) {
            m_separator->move(pad, kStepperHeight);
            m_separator->resize(cw - 2 * pad, 1);
        }

        // Title + subtitle
        int y = kStepperHeight + 12;
        m_titleLabel->move(pad, y);
        m_titleLabel->resize(cw - 2 * pad, 28);
        y += 30;

        if (m_subTitleLabel && m_subTitleLabel->getVisible()) {
            m_subTitleLabel->move(pad, y);
            m_subTitleLabel->resize(cw - 2 * pad, 20);
            y += 24;
        }

        y += 8;

        // Content stack
        m_stack->move(pad, y);
        m_stack->resize(std::max(0, cw - 2 * pad), std::max(0, ch - y));

        layoutWizardButtons();
    }

    void layoutWizardButtons() {
        auto* bar = buttonBarWidget();
        if (!bar) return;

        int x = bar->width() - 4;

        if (m_finishBtn && m_finishBtn->getVisible()) {
            x -= m_finishBtn->width();
            m_finishBtn->move(x, 4);
            x -= 10;
        }
        if (m_nextBtn && m_nextBtn->getVisible()) {
            x -= m_nextBtn->width();
            m_nextBtn->move(x, 4);
            x -= 10;
        }
        if (m_backBtn && m_backBtn->getVisible()) {
            x -= m_backBtn->width();
            m_backBtn->move(x, 4);
            x -= 10;
        }

        // Cancel on the far left
        if (m_cancelBtn) {
            m_cancelBtn->move(4, 4);
        }
    }

    SwVector<SwWizardPage*> m_pages;
    int m_currentIndex{0};

    StepperWidget* m_stepper{nullptr};
    SwWidget* m_separator{nullptr};
    SwLabel* m_titleLabel{nullptr};
    SwLabel* m_subTitleLabel{nullptr};
    SwStackedWidget* m_stack{nullptr};

    SwPushButton* m_backBtn{nullptr};
    SwPushButton* m_nextBtn{nullptr};
    SwPushButton* m_finishBtn{nullptr};
    SwPushButton* m_cancelBtn{nullptr};
};

