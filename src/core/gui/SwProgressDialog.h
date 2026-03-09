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
 * @file src/core/gui/SwProgressDialog.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwProgressDialog in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the progress dialog interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwProgressDialog.
 *
 * Dialog-oriented declarations here usually describe a bounded modal interaction: configuration
 * enters through setters or constructor state, the user edits the state through child widgets,
 * and the caller retrieves the accepted result through the public API.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * SwProgressDialog - progress dialog with cancel button.
 *
 * Focus:
 * - Label + progress bar + Cancel button.
 * - wasCanceled() check for cooperative cancellation.
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwLabel.h"
#include "SwProgressBar.h"
#include "SwPushButton.h"

#include <algorithm>

class SwProgressDialog : public SwDialog {
    SW_OBJECT(SwProgressDialog, SwDialog)

public:
    /**
     * @brief Constructs a `SwProgressDialog` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwProgressDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(400, 150);
        buildUi();
    }

    /**
     * @brief Constructs a `SwProgressDialog` instance.
     * @param labelText Value passed to the method.
     * @param cancelButtonText Value passed to the method.
     * @param minimum Value passed to the method.
     * @param maximum Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwProgressDialog(const SwString& labelText,
                     const SwString& cancelButtonText,
                     int minimum, int maximum,
                     SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(400, 150);
        buildUi();
        setLabelText(labelText);
        setCancelButtonText(cancelButtonText);
        setRange(minimum, maximum);
    }

    /**
     * @brief Sets the label Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLabelText(const SwString& text) {
        if (m_label) {
            m_label->setText(text);
            m_label->update();
        }
    }

    /**
     * @brief Sets the cancel Button Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCancelButtonText(const SwString& text) {
        if (m_cancelBtn) {
            m_cancelBtn->setText(text);
        }
    }

    /**
     * @brief Sets the range.
     * @param minimum Value passed to the method.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRange(int minimum, int maximum) {
        if (m_progressBar) {
            m_progressBar->setRange(minimum, maximum);
        }
        m_min = minimum;
        m_max = maximum;
    }

    /**
     * @brief Sets the minimum.
     * @param m_max Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMinimum(int v) { setRange(v, m_max); }
    /**
     * @brief Sets the maximum.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaximum(int v) { setRange(m_min, v); }
    /**
     * @brief Returns the current minimum.
     * @return The current minimum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int minimum() const { return m_min; }
    /**
     * @brief Returns the current maximum.
     * @return The current maximum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maximum() const { return m_max; }

    /**
     * @brief Sets the value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setValue(int value) {
        m_value = value;
        if (m_progressBar) {
            m_progressBar->setValue(value);
            m_progressBar->update();
        }
    }

    /**
     * @brief Returns the current value.
     * @return The current value.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int value() const { return m_value; }

    /**
     * @brief Returns the current was Canceled.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool wasCanceled() const { return m_canceled; }

    /**
     * @brief Resets the object to a baseline state.
     */
    void reset() {
        m_canceled = false;
        setValue(m_min);
    }

signals:
    DECLARE_SIGNAL_VOID(canceled);

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateProgressLayout();
    }

private:
    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) return;

        m_label = new SwLabel("", content);
        m_label->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24, 28, 36); font-size: 14px; }");
        m_label->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter));

        m_progressBar = new SwProgressBar(content);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);

        m_cancelBtn = new SwPushButton("Cancel", bar);
        m_cancelBtn->resize(90, 34);
        m_cancelBtn->setStyleSheet(R"(
            SwPushButton { background-color: rgb(241, 245, 249); color: rgb(51, 65, 85); border-radius: 8px; border-color: rgb(203, 213, 225); border-width: 1px; font-size: 13px; }
        )");
        SwObject::connect(m_cancelBtn, &SwPushButton::clicked, this, [this]() {
            m_canceled = true;
            canceled();
            reject();
        });

        updateProgressLayout();
    }

    void updateProgressLayout() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !m_label || !m_progressBar) return;
        const int cw = content->width();

        m_label->move(0, 0);
        m_label->resize(cw, 28);

        m_progressBar->move(0, 34);
        m_progressBar->resize(cw, 24);

        if (bar && m_cancelBtn) {
            int x = bar->width() - m_cancelBtn->width();
            m_cancelBtn->move(x, 4);
        }
    }

    int m_min{0};
    int m_max{100};
    int m_value{0};
    bool m_canceled{false};

    SwLabel* m_label{nullptr};
    SwProgressBar* m_progressBar{nullptr};
    SwPushButton* m_cancelBtn{nullptr};
};
