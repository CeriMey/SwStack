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
 * SwFontDialog - Qt-like font dialog (≈ QFontDialog).
 *
 * V1 scope:
 * - Snapshot-friendly font dialog UI.
 * - Simple font family picker + point size + preview.
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwComboBox.h"
#include "SwLabel.h"
#include "SwPushButton.h"
#include "SwSpinBox.h"

#include "core/types/SwVector.h"

#include <algorithm>
#include <string>

class SwFontDialog : public SwDialog {
    SW_OBJECT(SwFontDialog, SwDialog)

public:
    explicit SwFontDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        setWindowTitle("Select font");
        buildUi();
        setCurrentFont(SwFont(L"Segoe UI", 10, Medium));
    }

    void setCurrentFont(const SwFont& font) {
        m_font = font;
        syncUiFromFont();
        updatePreview();
    }

    SwFont currentFont() const { return m_font; }

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
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateLayout();
    }

private:
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
        m_ok->resize(120, 40);
        m_cancel->resize(120, 40);

        SwObject::connect(m_ok, &SwPushButton::clicked, this, [this]() { accept(); });
        SwObject::connect(m_cancel, &SwPushButton::clicked, this, [this]() { reject(); });

        populateFamilies();
        hookSignals();
        updateLayout();
    }

    void populateFamilies() {
        if (!m_family) {
            return;
        }
        m_family->clear();
        const SwVector<SwString> defaults = {"Segoe UI", "Arial", "Consolas", "Times New Roman", "Calibri"};
        for (int i = 0; i < defaults.size(); ++i) {
            m_family->addItem(defaults[i]);
        }
    }

    void hookSignals() {
        if (m_family) {
            SwObject::connect(m_family, &SwComboBox::currentTextChanged, this, [this](const SwString&) { updateFromUi(); });
        }
        if (m_size) {
            SwObject::connect(m_size, &SwSpinBox::valueChanged, this, [this](int) { updateFromUi(); });
        }
    }

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

    void updatePreview() {
        if (m_preview) {
            m_preview->setFont(m_font);
            m_preview->update();
        }
    }

    void updateLayout() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar || !m_family || !m_size || !m_preview || !m_ok || !m_cancel || !m_fontLabel || !m_sizeLabel) {
            return;
        }

        const SwRect cr = content->getRect();
        const int x = cr.x;
        int y = cr.y + 6;

        m_fontLabel->move(x, y);
        m_fontLabel->resize(260, 22);

        m_sizeLabel->move(x + std::max(0, cr.width - 120), y);
        m_sizeLabel->resize(120, 22);

        y += 26;

        m_family->move(x, y);
        m_family->resize(std::max(0, cr.width - 130), 34);

        m_size->move(x + std::max(0, cr.width - 120), y);
        m_size->resize(120, 34);

        y += 46;

        m_preview->move(x, y);
        m_preview->resize(std::max(0, cr.width), std::max(120, cr.height - (y - cr.y)));

        const SwRect br = bar->getRect();
        const int by = br.y + 6;
        int bx = br.x + br.width;
        bx -= m_ok->width();
        m_ok->move(bx, by);
        bx -= m_spacing + m_cancel->width();
        m_cancel->move(bx, by);
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
