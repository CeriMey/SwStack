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
 * @file src/core/gui/SwMessageBox.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwMessageBox in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the message box interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwMessageBox.
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
 * SwMessageBox - message box.
 *
 * Focus:
 * - Built on SwDialog (native window by default on Windows).
 * - Standard buttons, icon support (Information, Warning, Critical, Question).
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwLabel.h"
#include "SwPushButton.h"

#include <algorithm>
#include <cmath>

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

    enum Icon {
        NoIcon = 0,
        Information,
        Warning,
        Critical,
        Question
    };

    /**
     * @brief Constructs a `SwMessageBox` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwMessageBox(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        resize(380, 140);
        buildUi();
        setStandardButtons(Ok);
#if defined(_WIN32)
        setNativeWindowIcon(createEnvelopeIcon_());
#endif
    }

    /**
     * @brief Sets the icon.
     * @param icon Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setIcon(Icon icon) {
        m_icon = icon;
        const bool showContentIcon = (icon == Information || icon == Warning || icon == Critical);
        if (m_iconWidget) {
            m_iconWidget->setIcon(icon);
            m_iconWidget->setVisible(showContentIcon);
        }
#if defined(_WIN32)
        HICON hIcon = nullptr;
        switch (icon) {
        case Information: hIcon = LoadIconA(nullptr, IDI_INFORMATION); break;
        case Question:    hIcon = createCircleLetterIcon_(L"?", 59, 130, 246); break;
        case Warning:     hIcon = LoadIconA(nullptr, IDI_WARNING); break;
        case Critical:    hIcon = LoadIconA(nullptr, IDI_ERROR);   break;
        default:          hIcon = createEnvelopeIcon_(); break;
        }
        if (hIcon) {
            setNativeWindowIcon(hIcon);
        }
#endif
        updateContentLayout();
        update();
    }

    /**
     * @brief Returns the current icon.
     * @return The current icon.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Icon icon() const { return m_icon; }

    /**
     * @brief Sets the text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setText(const SwString& text) {
        m_text = text;
        if (m_textLabel) {
            m_textLabel->setText(m_text);
            m_textLabel->update();
        }
        autoFitHeight();
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
     * @brief Sets the informative Text.
     * @param text Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setInformativeText(const SwString& text) {
        m_informative = text;
        if (m_infoLabel) {
            m_infoLabel->setText(m_informative);
            m_infoLabel->setVisible(!m_informative.isEmpty());
            m_infoLabel->update();
        }
        autoFitHeight();
        update();
    }

    /**
     * @brief Sets the standard Buttons.
     * @param buttons Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setStandardButtons(int buttons) {
        m_buttonsMask = buttons;
        createButtons();
        layoutButtons();
    }

    /**
     * @brief Returns the current standard Buttons.
     * @return The current standard Buttons.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int standardButtons() const { return m_buttonsMask; }

    /**
     * @brief Returns the current clicked Button.
     * @return The current clicked Button.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int clickedButton() const { return m_clicked; }

    /**
     * @brief Performs the `information` operation.
     * @param parent Optional parent object that owns this instance.
     * @param title Title text applied by the operation.
     * @param text Value passed to the method.
     * @param buttons Value passed to the method.
     * @return The requested information.
     */
    static int information(SwWidget* parent,
                           const SwString& title,
                           const SwString& text,
                           int buttons = Ok) {
        SwMessageBox box(parent);
        box.setWindowTitle(title);
        box.setIcon(Information);
        box.setText(text);
        box.setStandardButtons(buttons);
        box.exec();
        return box.clickedButton();
    }

    /**
     * @brief Performs the `question` operation.
     * @param parent Optional parent object that owns this instance.
     * @param title Title text applied by the operation.
     * @param text Value passed to the method.
     * @param buttons Value passed to the method.
     * @return The requested question.
     */
    static int question(SwWidget* parent,
                        const SwString& title,
                        const SwString& text,
                        int buttons = Yes | No) {
        SwMessageBox box(parent);
        box.setWindowTitle(title);
        box.setIcon(Question);
        box.setText(text);
        box.setStandardButtons(buttons);
        box.exec();
        return box.clickedButton();
    }

    /**
     * @brief Performs the `warning` operation.
     * @param parent Optional parent object that owns this instance.
     * @param title Title text applied by the operation.
     * @param text Value passed to the method.
     * @param buttons Value passed to the method.
     * @return The requested warning.
     */
    static int warning(SwWidget* parent,
                       const SwString& title,
                       const SwString& text,
                       int buttons = Ok) {
        SwMessageBox box(parent);
        box.setWindowTitle(title);
        box.setIcon(Warning);
        box.setText(text);
        box.setStandardButtons(buttons);
        box.exec();
        return box.clickedButton();
    }

    /**
     * @brief Performs the `critical` operation.
     * @param parent Optional parent object that owns this instance.
     * @param title Title text applied by the operation.
     * @param text Value passed to the method.
     * @param buttons Value passed to the method.
     * @return The requested critical.
     */
    static int critical(SwWidget* parent,
                        const SwString& title,
                        const SwString& text,
                        int buttons = Ok) {
        SwMessageBox box(parent);
        box.setWindowTitle(title);
        box.setIcon(Critical);
        box.setText(text);
        box.setStandardButtons(buttons);
        box.exec();
        return box.clickedButton();
    }

protected:
    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateContentLayout();
        layoutButtons();
    }

private:
    class IconWidget final : public SwWidget {
        SW_OBJECT(IconWidget, SwWidget)

    public:
        /**
         * @brief Performs the `IconWidget` operation.
         * @param parent Optional parent object that owns this instance.
         * @return The requested icon Widget.
         */
        explicit IconWidget(SwWidget* parent = nullptr)
            : SwWidget(parent) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        /**
         * @brief Sets the icon.
         * @param m_icon Value passed to the method.
         *
         * @details Call this method to replace the currently stored value with the caller-provided one.
         */
        void setIcon(Icon icon) { m_icon = icon; update(); }

        /**
         * @brief Handles the paint Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter || m_icon == NoIcon) {
                return;
            }
            const SwRect r = rect();
            const int sz = std::min(r.width, r.height);
            const int cx = r.x + r.width / 2;
            const int cy = r.y + r.height / 2;
            const SwRect iconRect{cx - sz / 2, cy - sz / 2, sz, sz};

            switch (m_icon) {
            case Information:
                paintInformation(painter, iconRect, sz);
                break;
            case Warning:
                paintWarning(painter, iconRect, sz);
                break;
            case Critical:
                paintCritical(painter, iconRect, sz);
                break;
            case Question:
                paintQuestion(painter, iconRect, sz);
                break;
            default:
                break;
            }
        }

    private:
        void paintInformation(SwPainter* painter, const SwRect& r, int sz) {
            // Blue circle with white "i"
            painter->fillEllipse(r, SwColor{59, 130, 246}, SwColor{59, 130, 246}, 0);
            const SwFont font(L"Segoe UI", std::max(8, sz * 55 / 100), Bold);
            const SwRect textR{r.x, r.y + sz / 10, r.width, r.height - sz / 10};
            painter->drawText(textR, SwString("i"),
                DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                SwColor{255, 255, 255}, font);
        }

        void paintWarning(SwPainter* painter, const SwRect& r, int sz) {
            // Orange/yellow circle with "!"
            painter->fillEllipse(r, SwColor{245, 158, 11}, SwColor{245, 158, 11}, 0);
            const SwFont font(L"Segoe UI", std::max(8, sz * 55 / 100), Bold);
            painter->drawText(r, SwString("!"),
                DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                SwColor{255, 255, 255}, font);
        }

        void paintCritical(SwPainter* painter, const SwRect& r, int sz) {
            // Red circle with "X"
            painter->fillEllipse(r, SwColor{239, 68, 68}, SwColor{239, 68, 68}, 0);
            const SwFont font(L"Segoe UI", std::max(8, sz * 45 / 100), Bold);
            painter->drawText(r, SwString::fromWString(L"\u2715"),
                DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                SwColor{255, 255, 255}, font);
        }

        void paintQuestion(SwPainter* painter, const SwRect& r, int sz) {
            // Blue circle with "?"
            painter->fillEllipse(r, SwColor{59, 130, 246}, SwColor{59, 130, 246}, 0);
            const SwFont font(L"Segoe UI", std::max(8, sz * 55 / 100), Bold);
            painter->drawText(r, SwString("?"),
                DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                SwColor{255, 255, 255}, font);
        }

        Icon m_icon{NoIcon};
    };

    static constexpr int kIconSize = 40;
    static constexpr int kIconGap = 14;

    void autoFitHeight() {
        const int pad = 18;
        const int barH = 56;
        const bool hasContentIcon = (m_icon == Information || m_icon == Warning || m_icon == Critical);
        const int iconOffset = hasContentIcon ? (kIconSize + kIconGap) : 0;
        const int contentW = std::max(100, width() - 2 * pad - iconOffset);

        int textH = 30;
        if (!m_text.isEmpty()) {
            const int len = static_cast<int>(m_text.length());
            const int charsPerLine = std::max(1, contentW / 7);
            const int lines = std::max(1, (len + charsPerLine - 1) / charsPerLine);
            textH = lines * 22;
        }

        int infoH = 0;
        if (!m_informative.isEmpty()) {
            const int len = static_cast<int>(m_informative.length());
            const int charsPerLine = std::max(1, contentW / 7);
            const int lines = std::max(1, (len + charsPerLine - 1) / charsPerLine);
            infoH = 8 + lines * 20;
        }

        const int totalH = pad + textH + infoH + 12 + barH;
        resize(width(), std::max(120, totalH));
    }

    void buildUi() {
        auto* content = contentWidget();
        if (!content) {
            return;
        }

        m_iconWidget = new IconWidget(content);
        m_iconWidget->resize(kIconSize, kIconSize);
        m_iconWidget->hide();

        m_textLabel = new SwLabel("", content);
        m_textLabel->setStyleSheet(R"(
            SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(24, 28, 36); font-size: 14px; }
        )");
        m_textLabel->setAlignment(DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter));

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
        const int w = content->width();
        const int h = content->height();

        int textX = 0;
        int textW = w;

        const bool hasContentIcon = (m_icon == Information || m_icon == Warning || m_icon == Critical);
        if (m_iconWidget && hasContentIcon) {
            m_iconWidget->move(0, 0);
            m_iconWidget->resize(kIconSize, kIconSize);
            textX = kIconSize + kIconGap;
            textW = std::max(0, w - textX);
        }

        m_textLabel->move(textX, 0);
        m_textLabel->resize(textW, std::min(60, h));

        m_infoLabel->move(textX, 64);
        m_infoLabel->resize(textW, std::max(0, h - 64));
    }

    void createButtons() {
        auto* bar = buttonBarWidget();
        if (!bar) {
            return;
        }

        for (int i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i]) {
                delete m_buttons[i];
            }
        }
        m_buttons.clear();
        m_buttonIds.clear();
        m_clicked = NoButton;

        SwVector<int> ids;
        if (m_buttonsMask & Yes) ids.push_back(Yes);
        if (m_buttonsMask & No) ids.push_back(No);
        if (m_buttonsMask & Ok) ids.push_back(Ok);
        if (m_buttonsMask & Cancel) ids.push_back(Cancel);

        for (int i = 0; i < ids.size(); ++i) {
            const int id = ids[i];
            auto* btn = new SwPushButton(buttonText(id), bar);
            btn->resize(90, 34);
            const bool isPrimary = (id == Ok || id == Yes);
            if (isPrimary) {
                btn->setStyleSheet(R"(
                    SwPushButton { background-color: rgb(59, 130, 246); color: #FFFFFF; border-radius: 8px; border-width: 0px; font-size: 13px; }
                )");
            } else {
                btn->setStyleSheet(R"(
                    SwPushButton { background-color: rgb(241, 245, 249); color: rgb(51, 65, 85); border-radius: 8px; border-color: rgb(203, 213, 225); border-width: 1px; font-size: 13px; }
                )");
            }
            SwObject::connect(btn, &SwPushButton::clicked, this, [this, id]() {
                m_clicked = id;
                done(Accepted);
            });
            m_buttons.push_back(btn);
            m_buttonIds.push_back(id);
        }
    }

    void layoutButtons() {
        auto* bar = buttonBarWidget();
        if (!bar || m_buttons.size() == 0) {
            return;
        }
        int x = bar->width() - 4;
        for (int i = m_buttons.size() - 1; i >= 0; --i) {
            SwPushButton* btn = m_buttons[i];
            if (!btn) {
                continue;
            }
            x -= btn->width();
            btn->move(x, 4);
            x -= 8;
        }
    }

#if defined(_WIN32)
    static HICON createEnvelopeIcon_() {
        const int sz = 32;
        Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Envelope body
        const int mx = 2, my = 6, mw = sz - 4, mh = sz - 12;
        Gdiplus::SolidBrush bodyBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Pen outlinePen(Gdiplus::Color(255, 59, 130, 246), 2.0f);
        g.FillRectangle(&bodyBrush, mx, my, mw, mh);
        g.DrawRectangle(&outlinePen, mx, my, mw, mh);

        // Envelope flap (V shape)
        Gdiplus::Pen flapPen(Gdiplus::Color(255, 59, 130, 246), 2.0f);
        flapPen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::PointF flapPts[3] = {
            {(float)mx, (float)my},
            {(float)(mx + mw / 2), (float)(my + mh * 0.55f)},
            {(float)(mx + mw), (float)my}
        };
        g.DrawLines(&flapPen, flapPts, 3);

        HICON hIcon = nullptr;
        bmp.GetHICON(&hIcon);
        return hIcon;
    }

    static HICON createCircleLetterIcon_(const wchar_t* letter, int r, int g_, int b) {
        const int sz = 32;
        Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        // Filled circle
        Gdiplus::SolidBrush circleBrush(Gdiplus::Color(255, (BYTE)r, (BYTE)g_, (BYTE)b));
        g.FillEllipse(&circleBrush, 1, 1, sz - 2, sz - 2);

        // White letter centered
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font font(&family, 18.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF textRect(0.0f, 0.0f, (float)sz, (float)sz);
        g.DrawString(letter, -1, &font, textRect, &fmt, &textBrush);

        HICON hIcon = nullptr;
        bmp.GetHICON(&hIcon);
        return hIcon;
    }
#endif

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

    Icon m_icon{NoIcon};
    SwString m_text;
    SwString m_informative;
    int m_buttonsMask{Ok};
    int m_clicked{NoButton};

    IconWidget* m_iconWidget{nullptr};
    SwLabel* m_textLabel{nullptr};
    SwLabel* m_infoLabel{nullptr};
    SwVector<SwPushButton*> m_buttons;
    SwVector<int> m_buttonIds;
};

