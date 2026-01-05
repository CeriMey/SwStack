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
 * SwMessageBox - Qt-like message box (≈ QMessageBox).
 *
 * Focus:
 * - Built on SwDialog (native window by default on Windows).
 * - Standard buttons and simple icon-less layout (V1).
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwLabel.h"
#include "SwPushButton.h"

#include <algorithm>

class SwMessageBox : public SwDialog {
    SW_OBJECT(SwMessageBox, SwDialog)

public:
    enum StandardButton {
        NoButton = 0x0,
        Ok = 0x1,
        Cancel = 0x2,
        Yes = 0x4,
        No = 0x8
    };

    explicit SwMessageBox(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        buildUi();
        setStandardButtons(Ok);
    }

    void setText(const SwString& text) {
        m_text = text;
        if (m_textLabel) {
            m_textLabel->setText(m_text);
            m_textLabel->update();
        }
        update();
    }

    SwString text() const { return m_text; }

    void setInformativeText(const SwString& text) {
        m_informative = text;
        if (m_infoLabel) {
            m_infoLabel->setText(m_informative);
            m_infoLabel->setVisible(!m_informative.isEmpty());
            m_infoLabel->update();
        }
        update();
    }

    void setStandardButtons(int buttons) {
        m_buttonsMask = buttons;
        rebuildButtons();
    }

    int standardButtons() const { return m_buttonsMask; }

    int clickedButton() const { return m_clicked; }

    static int information(SwWidget* parent,
                           const SwString& title,
                           const SwString& text,
                           int buttons = Ok) {
        SwMessageBox box(parent);
        box.setWindowTitle(title);
        box.setText(text);
        box.setStandardButtons(buttons);
        return box.exec();
    }

    static int question(SwWidget* parent,
                        const SwString& title,
                        const SwString& text,
                        int buttons = Yes | No) {
        SwMessageBox box(parent);
        box.setWindowTitle(title);
        box.setText(text);
        box.setStandardButtons(buttons);
        return box.exec();
    }

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateContentLayout();
        rebuildButtons();
    }

    void done(int code) {
        SwDialog::done(code);
    }

private:
    void buildUi() {
        auto* content = contentWidget();
        if (!content) {
            return;
        }
        m_textLabel = new SwLabel("", content);
        m_textLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24, 28, 36); font-size: 14px; }
        )");
        m_textLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top));

        m_infoLabel = new SwLabel("", content);
        m_infoLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(71, 85, 105); font-size: 13px; }
        )");
        m_infoLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::Top | DrawTextFormat::WordBreak));
        m_infoLabel->hide();

        updateContentLayout();
    }

    void updateContentLayout() {
        auto* content = contentWidget();
        if (!content || !m_textLabel || !m_infoLabel) {
            return;
        }
        const SwRect r = content->getRect();
        const int w = r.width;
        int y = r.y;

        m_textLabel->move(r.x, y);
        m_textLabel->resize(w, 80);
        y += 84;

        m_infoLabel->move(r.x, y);
        m_infoLabel->resize(w, std::max(0, r.height - (y - r.y)));
    }

    SwPushButton* addButton(const SwString& text, int id) {
        auto* bar = buttonBarWidget();
        if (!bar) {
            return nullptr;
        }
        auto* btn = new SwPushButton(text, bar);
        btn->resize(110, 40);
        SwObject::connect(btn, &SwPushButton::clicked, this, [this, id]() {
            m_clicked = id;
            done(id == Cancel ? Rejected : Accepted);
        });
        return btn;
    }

    void rebuildButtons() {
        auto* bar = buttonBarWidget();
        if (!bar) {
            return;
        }

        for (SwWidget* child : bar->findChildren<SwWidget>()) {
            if (child) {
                child->deleteLater();
            }
        }

        m_clicked = NoButton;

        SwVector<int> ids;
        if (m_buttonsMask & Yes) ids.push_back(Yes);
        if (m_buttonsMask & No) ids.push_back(No);
        if (m_buttonsMask & Ok) ids.push_back(Ok);
        if (m_buttonsMask & Cancel) ids.push_back(Cancel);

        int x = bar->getRect().x + std::max(0, bar->getRect().width - m_marginRight);
        const int y = bar->getRect().y + 6;
        for (int i = ids.size() - 1; i >= 0; --i) {
            const int id = ids[i];
            const SwString label = buttonText(id);
            auto* btn = addButton(label, id);
            if (!btn) {
                continue;
            }
            const int w = btn->width();
            x -= w;
            btn->move(x, y);
            x -= m_spacing;
        }
    }

    static SwString buttonText(int id) {
        switch (id) {
        case Yes: return "Yes";
        case No: return "No";
        case Cancel: return "Cancel";
        case Ok:
        default:
            return "OK";
        }
    }

    SwString m_text;
    SwString m_informative;
    int m_buttonsMask{Ok};
    int m_clicked{NoButton};

    SwLabel* m_textLabel{nullptr};
    SwLabel* m_infoLabel{nullptr};

    int m_spacing{10};
    int m_marginRight{12};
};
