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
 * @file src/core/gui/SwInputDialog.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwInputDialog in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the input dialog interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwInputDialog.
 *
 * Dialog-oriented declarations here usually describe a bounded modal interaction: configuration
 * enters through setters or constructor state, the user edits the state through child widgets,
 * and the caller retrieves the accepted result through the public API.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/**
 * @file SwInputDialog.h
 * @brief Declares `SwInputDialog`, a compact modal dialog for one-off user input.
 *
 * @details
 * `SwInputDialog` exposes a small, toolkit-native alternative to the classic input helpers found in
 * desktop UI frameworks. A single dialog instance can switch between text, integer, floating-point,
 * and item-selection modes, while the static helper functions provide a simple call pattern for
 * common application code.
 */

#include "SwDialog.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwSpinBox.h"
#include "SwDoubleSpinBox.h"
#include "SwComboBox.h"
#include "SwPushButton.h"

#include <algorithm>

/**
 * @class SwInputDialog
 * @brief Modal dialog that captures a single value from the user.
 *
 * @details
 * The dialog owns one label and four mutually exclusive editor widgets:
 * - `SwLineEdit` for free-form text,
 * - `SwSpinBox` for integers,
 * - `SwDoubleSpinBox` for floating-point values,
 * - `SwComboBox` for choosing from a predefined list.
 *
 * `setInputMode()` toggles which editor is visible, while the static helpers construct a temporary
 * dialog, configure it, run it modally, and return either the accepted value or the original input.
 */
class SwInputDialog : public SwDialog {
    SW_OBJECT(SwInputDialog, SwDialog)

public:
    /**
     * @enum InputMode
     * @brief Selects which editor widget is active inside the dialog.
     */
    enum InputMode {
        TextInput,   ///< Use the line edit for free-form text entry.
        IntInput,    ///< Use the integer spin box.
        DoubleInput, ///< Use the floating-point spin box.
        ItemInput    ///< Use the combo box for predefined choices.
    };

    /**
     * @brief Constructs the dialog and creates all editor widgets.
     * @param parent Optional parent widget.
     */
    explicit SwInputDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(400, 160);
        buildUi();
    }

    /**
     * @brief Sets the descriptive label shown above the active editor.
     * @param text Prompt text displayed to the user.
     */
    void setLabelText(const SwString& text) {
        if (m_label) {
            m_label->setText(text);
        }
    }

    /**
     * @brief Switches the dialog to a different input widget type.
     * @param mode New editor mode.
     *
     * @details
     * Only the editor associated with `mode` remains visible after this call. The dialog then
     * recomputes the input field geometry so the active control fills the available content area.
     */
    void setInputMode(InputMode mode) {
        m_mode = mode;
        if (m_lineEdit)      m_lineEdit->setVisible(mode == TextInput);
        if (m_spinBox)        m_spinBox->setVisible(mode == IntInput);
        if (m_doubleSpinBox)  m_doubleSpinBox->setVisible(mode == DoubleInput);
        if (m_comboBox)       m_comboBox->setVisible(mode == ItemInput);
        updateInputLayout();
    }

    /**
     * @brief Sets the current text value.
     * @param text Text pushed into the line edit.
     */
    void setTextValue(const SwString& text) {
        if (m_lineEdit) m_lineEdit->setText(text);
    }

    /**
     * @brief Returns the text currently stored in the line edit.
     * @return Current text value, or an empty string when the line edit is unavailable.
     */
    SwString textValue() const {
        return m_lineEdit ? m_lineEdit->getText() : SwString();
    }

    /**
     * @brief Sets the valid integer range for integer mode.
     * @param min Minimum accepted integer.
     * @param max Maximum accepted integer.
     */
    void setIntRange(int min, int max) {
        if (m_spinBox) m_spinBox->setRange(min, max);
    }

    /**
     * @brief Sets the current integer value.
     * @param val Integer pushed into the spin box.
     */
    void setIntValue(int val) {
        if (m_spinBox) m_spinBox->setValue(val);
    }

    /**
     * @brief Returns the integer currently shown by the spin box.
     * @return Current integer value, or `0` when the spin box is unavailable.
     */
    int intValue() const {
        return m_spinBox ? m_spinBox->value() : 0;
    }

    /**
     * @brief Sets the valid floating-point range for double mode.
     * @param min Minimum accepted value.
     * @param max Maximum accepted value.
     */
    void setDoubleRange(double min, double max) {
        if (m_doubleSpinBox) m_doubleSpinBox->setRange(min, max);
    }

    /**
     * @brief Sets the current floating-point value.
     * @param val Floating-point value pushed into the editor.
     */
    void setDoubleValue(double val) {
        if (m_doubleSpinBox) m_doubleSpinBox->setValue(val);
    }

    /**
     * @brief Returns the value currently shown by the floating-point editor.
     * @return Current floating-point value, or `0.0` when the editor is unavailable.
     */
    double doubleValue() const {
        return m_doubleSpinBox ? m_doubleSpinBox->value() : 0.0;
    }

    /**
     * @brief Replaces the items available in combo-box mode.
     * @param items List of labels to append to the combo box.
     */
    void setComboBoxItems(const SwVector<SwString>& items) {
        if (!m_comboBox) return;
        m_comboBox->clear();
        for (int i = 0; i < items.size(); ++i) {
            m_comboBox->addItem(items[i]);
        }
    }

    /**
     * @brief Returns the currently selected combo-box label.
     * @return Current item text, or an empty string when the combo box is unavailable.
     */
    SwString itemText() const {
        return m_comboBox ? m_comboBox->currentText() : SwString();
    }

    /**
     * @brief Opens a modal text-input dialog.
     * @param parent Optional parent widget.
     * @param title Window title.
     * @param label Prompt shown inside the dialog.
     * @param text Initial text value.
     * @param ok Optional output flag set to `true` only when the dialog is accepted.
     * @return Accepted text, or `text` when the dialog is cancelled.
     */

    static SwString getText(SwWidget* parent,
                            const SwString& title,
                            const SwString& label,
                            const SwString& text = SwString(),
                            bool* ok = nullptr) {
        SwInputDialog dlg(parent);
        dlg.setWindowTitle(title);
        dlg.setLabelText(label);
        dlg.setInputMode(TextInput);
        dlg.setTextValue(text);
        const int res = dlg.exec();
        if (ok) *ok = (res == Accepted);
        return (res == Accepted) ? dlg.textValue() : text;
    }

    /**
     * @brief Opens a modal integer-input dialog.
     * @param parent Optional parent widget.
     * @param title Window title.
     * @param label Prompt shown inside the dialog.
     * @param value Initial integer value.
     * @param min Minimum accepted integer.
     * @param max Maximum accepted integer.
     * @param ok Optional output flag set to `true` only when the dialog is accepted.
     * @return Accepted value, or `value` when the dialog is cancelled.
     */
    static int getInt(SwWidget* parent,
                      const SwString& title,
                      const SwString& label,
                      int value = 0,
                      int min = -2147483647,
                      int max = 2147483647,
                      bool* ok = nullptr) {
        SwInputDialog dlg(parent);
        dlg.setWindowTitle(title);
        dlg.setLabelText(label);
        dlg.setInputMode(IntInput);
        dlg.setIntRange(min, max);
        dlg.setIntValue(value);
        const int res = dlg.exec();
        if (ok) *ok = (res == Accepted);
        return (res == Accepted) ? dlg.intValue() : value;
    }

    /**
     * @brief Opens a modal floating-point input dialog.
     * @param parent Optional parent widget.
     * @param title Window title.
     * @param label Prompt shown inside the dialog.
     * @param value Initial value.
     * @param min Minimum accepted value.
     * @param max Maximum accepted value.
     * @param ok Optional output flag set to `true` only when the dialog is accepted.
     * @return Accepted value, or `value` when the dialog is cancelled.
     */
    static double getDouble(SwWidget* parent,
                            const SwString& title,
                            const SwString& label,
                            double value = 0.0,
                            double min = -2147483647.0,
                            double max = 2147483647.0,
                            bool* ok = nullptr) {
        SwInputDialog dlg(parent);
        dlg.setWindowTitle(title);
        dlg.setLabelText(label);
        dlg.setInputMode(DoubleInput);
        dlg.setDoubleRange(min, max);
        dlg.setDoubleValue(value);
        const int res = dlg.exec();
        if (ok) *ok = (res == Accepted);
        return (res == Accepted) ? dlg.doubleValue() : value;
    }

    /**
     * @brief Opens a modal item-selection dialog.
     * @param parent Optional parent widget.
     * @param title Window title.
     * @param label Prompt shown inside the dialog.
     * @param items Candidate item labels.
     * @param current Initially selected item index.
     * @param ok Optional output flag set to `true` only when the dialog is accepted.
     * @return Accepted item label, or an empty string when the dialog is cancelled.
     */
    static SwString getItem(SwWidget* parent,
                            const SwString& title,
                            const SwString& label,
                            const SwVector<SwString>& items,
                            int current = 0,
                            bool* ok = nullptr) {
        SwInputDialog dlg(parent);
        dlg.setWindowTitle(title);
        dlg.setLabelText(label);
        dlg.setInputMode(ItemInput);
        dlg.setComboBoxItems(items);
        if (dlg.m_comboBox && current >= 0 && current < dlg.m_comboBox->count()) {
            dlg.m_comboBox->setCurrentIndex(current);
        }
        const int res = dlg.exec();
        if (ok) *ok = (res == Accepted);
        return (res == Accepted) ? dlg.itemText() : SwString();
    }

protected:
    /**
     * @brief Updates the editor and button geometry after a resize.
     * @param event Resize event forwarded by the base dialog.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateInputLayout();
        layoutButtons();
    }

private:
    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) return;

        m_label = new SwLabel("", content);
        m_label->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24, 28, 36); font-size: 14px; }");
        m_label->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter));

        m_lineEdit = new SwLineEdit(content);
        m_lineEdit->resize(100, 34);

        m_spinBox = new SwSpinBox(content);
        m_spinBox->resize(100, 34);
        m_spinBox->setRange(-2147483647, 2147483647);
        m_spinBox->hide();

        m_doubleSpinBox = new SwDoubleSpinBox(content);
        m_doubleSpinBox->resize(100, 34);
        m_doubleSpinBox->setRange(-2147483647.0, 2147483647.0);
        m_doubleSpinBox->hide();

        m_comboBox = new SwComboBox(content);
        m_comboBox->resize(100, 34);
        m_comboBox->hide();

        m_ok = new SwPushButton("OK", bar);
        m_cancel = new SwPushButton("Cancel", bar);
        m_ok->resize(90, 34);
        m_cancel->resize(90, 34);
        m_ok->setStyleSheet(R"(
            SwPushButton { background-color: rgb(59, 130, 246); color: #FFFFFF; border-radius: 8px; border-width: 0px; font-size: 13px; }
        )");
        m_cancel->setStyleSheet(R"(
            SwPushButton { background-color: rgb(241, 245, 249); color: rgb(51, 65, 85); border-radius: 8px; border-color: rgb(203, 213, 225); border-width: 1px; font-size: 13px; }
        )");

        SwObject::connect(m_ok, &SwPushButton::clicked, this, [this]() { accept(); });
        SwObject::connect(m_cancel, &SwPushButton::clicked, this, [this]() { reject(); });

        updateInputLayout();
        layoutButtons();
    }

    void updateInputLayout() {
        auto* content = contentWidget();
        if (!content || !m_label) return;
        const int cw = content->width();

        m_label->move(0, 0);
        m_label->resize(cw, 28);

        const int inputY = 32;
        const int inputH = 34;
        if (m_lineEdit && m_lineEdit->getVisible())         { m_lineEdit->move(0, inputY);      m_lineEdit->resize(cw, inputH); }
        if (m_spinBox && m_spinBox->getVisible())            { m_spinBox->move(0, inputY);       m_spinBox->resize(cw, inputH); }
        if (m_doubleSpinBox && m_doubleSpinBox->getVisible()){ m_doubleSpinBox->move(0, inputY); m_doubleSpinBox->resize(cw, inputH); }
        if (m_comboBox && m_comboBox->getVisible())          { m_comboBox->move(0, inputY);      m_comboBox->resize(cw, inputH); }
    }

    void layoutButtons() {
        auto* bar = buttonBarWidget();
        if (!bar || !m_ok || !m_cancel) return;
        int x = bar->width();
        x -= m_ok->width();
        m_ok->move(x, 4);
        x -= 8 + m_cancel->width();
        m_cancel->move(x, 4);
    }

    InputMode m_mode{TextInput};
    SwLabel* m_label{nullptr};
    SwLineEdit* m_lineEdit{nullptr};
    SwSpinBox* m_spinBox{nullptr};
    SwDoubleSpinBox* m_doubleSpinBox{nullptr};
    SwComboBox* m_comboBox{nullptr};
    SwPushButton* m_ok{nullptr};
    SwPushButton* m_cancel{nullptr};
};
