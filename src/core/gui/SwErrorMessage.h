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
 * @file src/core/gui/SwErrorMessage.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwErrorMessage in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the error message interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwErrorMessage.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/**
 * @file SwErrorMessage.h
 * @brief Declares `SwErrorMessage`, a modal error dialog with opt-out suppression.
 *
 * @details
 * `SwErrorMessage` provides a lightweight, toolkit-native replacement for one-off error popups. In
 * addition to showing an icon and human-readable text, it can remember messages that the user chose
 * to suppress so repetitive non-critical alerts do not keep interrupting the workflow.
 */

#include "SwDialog.h"
#include "SwLabel.h"
#include "SwCheckBox.h"
#include "SwPushButton.h"

#include "core/types/SwMap.h"

#include <algorithm>

/**
 * @class SwErrorMessage
 * @brief Modal error dialog that can suppress repeated messages.
 *
 * @details
 * The dialog stores a set of previously suppressed message strings in memory. When `showMessage()`
 * is called with a text that has already been opted out, the dialog returns immediately. Otherwise
 * it updates the visible label, resets the checkbox state, executes modally, and records the
 * message if the user requests not to see it again.
 */
class SwErrorMessage : public SwDialog {
    SW_OBJECT(SwErrorMessage, SwDialog)

public:
    /**
     * @brief Constructs the dialog and creates its icon, text, checkbox, and action button.
     * @param parent Optional parent widget.
     */
    explicit SwErrorMessage(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(420, 180);
        setWindowTitle("Error");
        buildUi();
#if defined(_WIN32)
        setNativeWindowIcon(LoadIconA(nullptr, IDI_ERROR));
#endif
    }

    /**
     * @brief Shows the given error unless the user already suppressed that exact message.
     * @param message Error text to display.
     *
     * @details
     * Suppression is tracked by message content for the lifetime of the dialog object. This keeps
     * the API simple for callers that repeatedly surface the same warning or validation error.
     */
    void showMessage(const SwString& message) {
        if (m_suppressed.contains(message)) {
            return;
        }
        m_currentMessage = message;
        if (m_textLabel) {
            m_textLabel->setText(message);
            m_textLabel->update();
        }
        if (m_checkBox) {
            m_checkBox->setChecked(false);
        }
        exec();
        if (m_checkBox && m_checkBox->isChecked()) {
            m_suppressed.insert(message, true);
        }
    }

    /**
     * @brief Clears the in-memory suppression table.
     *
     * @details
     * After this call, messages that were previously hidden through the checkbox can be shown again.
     */
    void clearSuppressed() {
        m_suppressed.clear();
    }

protected:
    /**
     * @brief Recomputes the icon, label, checkbox, and button layout when the dialog is resized.
     * @param event Resize event forwarded by the base dialog.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateErrorLayout();
    }

private:
    static constexpr int kIconSize = 40;
    static constexpr int kIconGap = 14;
#if defined(_WIN32)
    static constexpr const wchar_t* kFontFamily = L"Segoe UI";
#else
    static constexpr const wchar_t* kFontFamily = L"Sans";
#endif

    /**
     * @brief Small custom widget that paints the red circular error glyph.
     *
     * @details
     * The widget keeps the icon rendering independent from platform resources so the dialog remains
     * visually consistent even when a native window icon is not available.
     */
    class ErrorIconWidget final : public SwWidget {
        SW_OBJECT(ErrorIconWidget, SwWidget)
    public:
        /**
         * @brief Performs the `ErrorIconWidget` operation.
         * @param parent Optional parent object that owns this instance.
         * @return The requested error Icon Widget.
         */
        explicit ErrorIconWidget(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
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
            const SwRect r = rect();
            const int sz = std::min(r.width, r.height);
            const int cx = r.x + r.width / 2;
            const int cy = r.y + r.height / 2;
            const SwRect iconRect{cx - sz / 2, cy - sz / 2, sz, sz};
            // Red circle with X
            painter->fillEllipse(iconRect, SwColor{239, 68, 68}, SwColor{239, 68, 68}, 0);
            const SwFont font(kFontFamily, std::max(8, sz * 45 / 100), Bold);
            painter->drawText(iconRect, SwString::fromWString(L"\u2715"),
                DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                SwColor{255, 255, 255}, font);
        }
    };

    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) return;

        m_iconWidget = new ErrorIconWidget(content);
        m_iconWidget->resize(kIconSize, kIconSize);

        m_textLabel = new SwLabel("", content);
        m_textLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24, 28, 36); font-size: 14px; }
        )");
        m_textLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));

        m_checkBox = new SwCheckBox("Don't show this message again", content);

        m_okBtn = new SwPushButton("OK", bar);
        m_okBtn->resize(90, 34);
        m_okBtn->setStyleSheet(R"(
            SwPushButton { background-color: rgb(59, 130, 246); color: #FFFFFF; border-radius: 8px; border-width: 0px; font-size: 13px; }
        )");
        SwObject::connect(m_okBtn, &SwPushButton::clicked, this, [this]() { accept(); });

        updateErrorLayout();
    }

    void updateErrorLayout() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !m_iconWidget || !m_textLabel || !m_checkBox) return;
        const int cw = content->width();
        const int ch = content->height();

        m_iconWidget->move(0, 0);
        m_iconWidget->resize(kIconSize, kIconSize);

        const int textX = kIconSize + kIconGap;
        const int textW = std::max(0, cw - textX);
        m_textLabel->move(textX, 0);
        m_textLabel->resize(textW, std::max(40, ch - 34));

        m_checkBox->move(textX, std::max(44, ch - 28));
        m_checkBox->resize(textW, 24);

        if (bar && m_okBtn) {
            int x = bar->width() - m_okBtn->width();
            m_okBtn->move(x, 4);
        }
    }

    SwString m_currentMessage;
    SwMap<SwString, bool> m_suppressed;

    ErrorIconWidget* m_iconWidget{nullptr};
    SwLabel* m_textLabel{nullptr};
    SwCheckBox* m_checkBox{nullptr};
    SwPushButton* m_okBtn{nullptr};
};

