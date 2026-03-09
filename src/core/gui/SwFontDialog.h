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
 * @file SwFontDialog.h
 * @ingroup core_gui
 * @brief Declares `SwFontDialog`, a compact modal font chooser for the Sw GUI toolkit.
 *
 * @details
 * `SwFontDialog` provides a self-contained replacement for a native font picker when the toolkit
 * needs a predictable, fully themeable widget hierarchy. The implementation keeps the workflow
 * intentionally narrow:
 * - it exposes font family selection,
 * - it exposes point-size selection,
 * - it keeps an in-memory `SwFont` as the source of truth,
 * - it renders a live preview so the caller can confirm the resulting typography before accepting.
 *
 * The dialog synchronizes the UI in both directions. `setCurrentFont()` pushes a model font into
 * the controls, while `updateFromUi()` reconstructs the current `SwFont` from the edited widgets.
 * On Windows the family list is populated from the system font catalog; on other platforms the
 * dialog falls back to a curated list of common families so the component remains functional even
 * without a platform-specific enumeration backend.
 */

#include "SwDialog.h"
#include "SwComboBox.h"
#include "SwLabel.h"
#include "SwPushButton.h"
#include "SwSpinBox.h"

#include "core/types/SwVector.h"

#include <algorithm>
#include <string>

/**
 * @class SwFontDialog
 * @brief Modal dialog that lets the user choose a font family and point size.
 *
 * @details
 * The class builds its entire interface in-process from regular Sw widgets instead of delegating to
 * an operating-system dialog. This makes it suitable for snapshot testing, custom styling, and
 * cross-platform behavior that stays consistent with the rest of the toolkit.
 *
 * Internally the class follows a simple state flow:
 * - `m_font` stores the authoritative selection.
 * - `syncUiFromFont()` mirrors `m_font` into the combo box and spin box.
 * - `updateFromUi()` rebuilds `m_font` from the current controls.
 * - `updatePreview()` applies the new font to the preview label.
 * - `updateLayout()` keeps the controls aligned with the dialog content and button bar geometry.
 */
class SwFontDialog : public SwDialog {
    SW_OBJECT(SwFontDialog, SwDialog)

public:
    /**
     * @brief Constructs the dialog, builds the widget tree, and installs a default font.
     * @param parent Optional parent widget that owns the dialog.
     *
     * @details
     * The constructor creates the controls, wires their signals, and seeds the dialog with a
     * default `Segoe UI` 10 pt font so the preview area is immediately usable.
     */
    explicit SwFontDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(420, 240);
        setWindowTitle("Select font");
        buildUi();
        setCurrentFont(SwFont(L"Segoe UI", 10, Medium));
    }

    /**
     * @brief Replaces the current dialog selection.
     * @param font Font value to display and preview.
     *
     * @details
     * This method updates the internal model, synchronizes the controls to match the provided
     * family and point size, then refreshes the preview label so the visible sample always reflects
     * the effective selection.
     */
    void setCurrentFont(const SwFont& font) {
        m_font = font;
        syncUiFromFont();
        updatePreview();
    }

    /**
     * @brief Returns the font currently selected in the dialog.
     * @return The authoritative `SwFont` built from the current UI state.
     */
    SwFont currentFont() const { return m_font; }

    /**
     * @brief Opens a modal font picker and returns the accepted selection.
     * @param initial Font initially displayed when the dialog opens.
     * @param parent Optional parent widget.
     * @param ok Optional output flag set to `true` only when the user accepts the dialog.
     * @param title Window title shown for the modal dialog.
     * @return The accepted font, or `initial` when the dialog is cancelled.
     *
     * @details
     * This convenience helper mirrors the familiar "static dialog" pattern used by desktop widget
     * toolkits. It creates a temporary dialog instance, seeds it with the caller-provided font,
     * executes it modally, and preserves the original font when the interaction is rejected.
     */
    static SwFont getFont(const SwFont& initial,
                          SwWidget* parent = nullptr,
                          bool* ok = nullptr,
                          const SwString& title = "Select font") {
        SwFontDialog dlg(parent);
        dlg.setWindowTitle(title);
        dlg.setCurrentFont(initial);
        const int res = dlg.exec();
        if (ok) {
            *ok = (res == Accepted);
        }
        return (res == Accepted) ? dlg.currentFont() : initial;
    }

protected:
    /**
     * @brief Recomputes the geometry of the controls when the dialog is resized.
     * @param event Resize event forwarded from the underlying widget system.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateLayout();
    }

private:
    /**
     * @brief Builds the dialog widget tree and installs the initial signal wiring.
     *
     * @details
     * The method creates the labels, editors, preview label, and dialog buttons, applies their
     * default styling, connects accept / reject actions, populates the family list, and computes
     * the initial geometry used by the dialog.
     */
    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) {
            return;
        }

        m_fontLabel = new SwLabel("Font", content);
        m_fontLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(71, 85, 105); font-size: 13px; }");

        m_sizeLabel = new SwLabel("Size", content);
        m_sizeLabel->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(71, 85, 105); font-size: 13px; }");

        m_family = new SwComboBox(content);
        m_family->resize(260, 34);

        m_size = new SwSpinBox(content);
        m_size->resize(120, 34);
        m_size->setRange(6, 48);
        m_size->setSingleStep(1);

        m_preview = new SwLabel("The quick brown fox jumps over the lazy dog.", content);
        m_preview->setStyleSheet(R"(
            SwLabel {
                background-color: rgb(248, 250, 252);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
                border-radius: 14px;
                padding: 10px 12px;
                color: rgb(24, 28, 36);
                font-size: 14px;
            }
        )");
        m_preview->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::WordBreak));

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

        populateFamilies();
        hookSignals();
        updateLayout();
    }

    /**
     * @brief Populates the font family combo box from the platform or fallback defaults.
     *
     * @details
     * On Windows the dialog enumerates installed fonts with the native GDI API, removes duplicates,
     * and sorts the result case-insensitively. On non-Windows platforms the method inserts a curated
     * list of common families so the component remains usable without a dedicated font backend.
     */
    void populateFamilies() {
        if (!m_family) {
            return;
        }
        m_family->clear();

#if defined(_WIN32)
        SwVector<SwString> families;
        LOGFONTW lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfFaceName[0] = L'\0';
        HDC hdc = GetDC(nullptr);
        EnumFontFamiliesExW(hdc, &lf, [](const LOGFONTW* lpelfe, const TEXTMETRICW*, DWORD, LPARAM lParam) -> int {
            auto* vec = reinterpret_cast<SwVector<SwString>*>(lParam);
            SwString name = SwString::fromWString(lpelfe->lfFaceName);
            if (!name.isEmpty() && !name.startsWith("@")) {
                bool found = false;
                for (int i = 0; i < vec->size(); ++i) {
                    if ((*vec)[i] == name) { found = true; break; }
                }
                if (!found) vec->push_back(name);
            }
            return 1;
        }, reinterpret_cast<LPARAM>(&families), 0);
        ReleaseDC(nullptr, hdc);

        std::sort(families.begin(), families.end(), [](const SwString& a, const SwString& b) {
            return a.toLower() < b.toLower();
        });
        for (int i = 0; i < families.size(); ++i) {
            m_family->addItem(families[i]);
        }
#else
        const SwVector<SwString> defaults = {"Segoe UI", "Arial", "Consolas", "Times New Roman", "Calibri",
            "Courier New", "Georgia", "Verdana", "Tahoma", "Trebuchet MS", "Comic Sans MS"};
        for (int i = 0; i < defaults.size(); ++i) {
            m_family->addItem(defaults[i]);
        }
#endif
    }

    /**
     * @brief Connects control changes back to the internal `SwFont` model.
     *
     * @details
     * Both the family selector and point-size editor forward state changes to `updateFromUi()` so
     * `currentFont()` and the preview label always reflect the visible controls.
     */
    void hookSignals() {
        if (m_family) {
            SwObject::connect(m_family, &SwComboBox::currentTextChanged, this, [this](const SwString&) { updateFromUi(); });
        }
        if (m_size) {
            SwObject::connect(m_size, &SwSpinBox::valueChanged, this, [this](int) { updateFromUi(); });
        }
    }

    /**
     * @brief Rebuilds the current font value from the active widget state.
     *
     * @details
     * Only the family and point size are edited by this dialog. Other attributes already present in
     * `m_font`, such as weight or underline, are preserved when the updated value is constructed.
     */
    void updateFromUi() {
        if (!m_family || !m_size) {
            return;
        }
        const SwString family = m_family->currentText();
        const int pt = m_size->value();

        SwFont f = m_font;
        f.setFamily(family.toStdWString());
        f.setPointSize(pt);
        m_font = f;
        updatePreview();
    }

    /**
     * @brief Mirrors the internal font model into the visible controls.
     *
     * @details
     * If the font family is not already present in the combo box, the method appends it so fonts
     * provided by callers remain representable even when they are outside the pre-populated list.
     */
    void syncUiFromFont() {
        if (m_family) {
            const SwString fam = SwString::fromWString(m_font.getFamily());
            int idx = -1;
            for (int i = 0; i < m_family->count(); ++i) {
                if (m_family->itemText(i) == fam) {
                    idx = i;
                    break;
                }
            }
            if (idx >= 0) {
                m_family->setCurrentIndex(idx);
            } else {
                m_family->addItem(fam);
                m_family->setCurrentIndex(m_family->count() - 1);
            }
        }
        if (m_size) {
            m_size->setValue(m_font.getPointSize());
        }
    }

    /**
     * @brief Applies the current font to the preview label and schedules repainting.
     */
    void updatePreview() {
        if (m_preview) {
            m_preview->setFont(m_font);
            m_preview->update();
        }
    }

    /**
     * @brief Recomputes the geometry of the controls inside the dialog content area.
     *
     * @details
     * `SwFontDialog` lays out its children manually instead of relying on a layout manager. This
     * helper keeps the labels, editors, preview region, and button bar aligned after construction
     * and on every resize event.
     */
    void updateLayout() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar || !m_family || !m_size || !m_preview || !m_ok || !m_cancel || !m_fontLabel || !m_sizeLabel) {
            return;
        }

        const int cw = content->width();
        const int ch = content->height();
        const int sizeW = 80;
        const int gap = 10;
        int y = 0;

        m_fontLabel->move(0, y);
        m_fontLabel->resize(std::max(0, cw - sizeW - gap), 20);

        m_sizeLabel->move(cw - sizeW, y);
        m_sizeLabel->resize(sizeW, 20);

        y += 22;

        m_family->move(0, y);
        m_family->resize(std::max(0, cw - sizeW - gap), 34);

        m_size->move(cw - sizeW, y);
        m_size->resize(sizeW, 34);

        y += 42;

        const int previewH = std::max(60, std::min(100, ch - y));
        m_preview->move(0, y);
        m_preview->resize(cw, previewH);

        int bx = bar->width();
        bx -= m_ok->width();
        m_ok->move(bx, 4);
        bx -= 8 + m_cancel->width();
        m_cancel->move(bx, 4);
    }

    SwFont m_font;

    SwLabel* m_fontLabel{nullptr};
    SwLabel* m_sizeLabel{nullptr};
    SwComboBox* m_family{nullptr};
    SwSpinBox* m_size{nullptr};
    SwLabel* m_preview{nullptr};
    SwPushButton* m_ok{nullptr};
    SwPushButton* m_cancel{nullptr};

    int m_spacing{10};
};
