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
 * @file src/core/gui/SwColorDialog.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwColorDialog in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the color dialog interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwColorDialog.
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
 * SwColorDialog - color dialog.
 *
 * V3 scope:
 * - Snapshot-friendly color dialog UI.
 * - Standard-ish round color picker (Hue ring + SV disk) + preview.
 **************************************************************************************************/

#include "SwDialog.h"
#include "SwLineEdit.h"
#include "SwPushButton.h"
#include "SwRoundColorPicker.h"

#include "Sw.h"

#include "core/types/SwVector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

class SwColorDialog : public SwDialog {
    SW_OBJECT(SwColorDialog, SwDialog)

public:
    /**
     * @brief Constructs a `SwColorDialog` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwColorDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        setMinimumSize(400, 320);
        resize(440, 340);
        setWindowTitle("Select color");
        buildUi();
        setCurrentColor(SwColor{59, 130, 246});
    }

    /**
     * @brief Sets the current Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCurrentColor(const SwColor& color) {
        setCurrentColorInternal(color, /*syncPicker=*/true);
    }

    /**
     * @brief Returns the current current Color.
     * @return The current current Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor currentColor() const { return m_current; }

    /**
     * @brief Performs the `getColor` operation.
     * @param initial Value passed to the method.
     * @param parent Optional parent object that owns this instance.
     * @param ok Optional flag updated to report success.
     * @param title Title text applied by the operation.
     * @return The requested color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwColor getColor(const SwColor& initial,
                            SwWidget* parent = nullptr,
                            bool* ok = nullptr,
                            const SwString& title = "Select color") {
        SwColorDialog dlg(parent);
        dlg.setWindowTitle(title);
        dlg.setCurrentColor(initial);
        const int res = dlg.exec();
        if (ok) {
            *ok = (res == Accepted);
        }
        return (res == Accepted) ? dlg.currentColor() : initial;
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
        updateLayout();
    }

private:
    class PreviewWidget final : public SwWidget {
        SW_OBJECT(PreviewWidget, SwWidget)

    public:
        /**
         * @brief Performs the `PreviewWidget` operation.
         * @param owner Value passed to the method.
         * @param parent Optional parent object that owns this instance.
         * @param owner Value passed to the method.
         * @return The requested preview Widget.
         */
        explicit PreviewWidget(SwColorDialog* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner) {
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
            if (!painter) {
                return;
            }
            const SwRect r = rect();
            const SwColor c = m_owner ? m_owner->m_current : SwColor{0, 0, 0};
            painter->fillRoundedRect(r, 12, c, SwColor{226, 232, 240}, 1);
        }

    private:
        SwColorDialog* m_owner{nullptr};
    };

    class ColorSwatch final : public SwWidget {
        SW_OBJECT(ColorSwatch, SwWidget)

    public:
        /**
         * @brief Performs the `ColorSwatch` operation.
         * @param c Value passed to the method.
         * @param valid Value passed to the method.
         * @param owner Value passed to the method.
         * @param parent Optional parent object that owns this instance.
         * @param owner Value passed to the method.
         */
        ColorSwatch(const SwColor& c, bool valid, SwColorDialog* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_color(c)
            , m_valid(valid)
            , m_owner(owner) {
            setCursor(m_valid ? CursorType::Hand : CursorType::Arrow);
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        /**
         * @brief Sets the color.
         * @param c Value passed to the method.
         *
         * @details Call this method to replace the currently stored value with the caller-provided one.
         */
        void setColor(const SwColor& c) {
            m_color = c;
            update();
        }

        /**
         * @brief Sets the valid.
         * @param valid Value passed to the method.
         *
         * @details Call this method to replace the currently stored value with the caller-provided one.
         */
        void setValid(bool valid) {
            if (m_valid == valid) {
                return;
            }
            m_valid = valid;
            setCursor(m_valid ? CursorType::Hand : CursorType::Arrow);
            update();
        }

        /**
         * @brief Returns the current color.
         * @return The current color.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        SwColor color() const { return m_color; }
        /**
         * @brief Returns whether the object reports valid.
         * @return `true` when the object reports valid; otherwise `false`.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        bool isValid() const { return m_valid; }

        /**
         * @brief Handles the paint Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }
            const SwRect r = rect();
            const int radius = std::min(6, std::max(1, std::min(r.width, r.height) / 4));

            if (!m_valid) {
                painter->fillRoundedRect(r, radius, SwColor{255, 255, 255}, SwColor{226, 232, 240}, 1);
                return;
            }

            const bool selected = m_owner && colorsEqual(m_owner->m_current, m_color);
            const SwColor border = selected ? SwColor{59, 130, 246} : SwColor{226, 232, 240};
            painter->fillRoundedRect(r, radius, m_color, border, selected ? 2 : 1);
        }

        /**
         * @brief Handles the mouse Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event || !m_owner || !m_valid) {
                return;
            }
            m_owner->setCurrentColor(m_color);
            event->accept();
        }

    private:
        SwColor m_color{0, 0, 0};
        bool m_valid{false};
        SwColorDialog* m_owner{nullptr};
    };

    static bool colorsEqual(const SwColor& a, const SwColor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    static SwColor* recentColors_() {
        static SwColor s_colors[5];
        return s_colors;
    }

    static bool* recentValid_() {
        static bool s_valid[5] = {false, false, false, false, false};
        return s_valid;
    }

    static void pushRecentColor_(const SwColor& color) {
        SwColor* colors = recentColors_();
        bool* valid = recentValid_();

        int found = -1;
        for (int i = 0; i < 5; ++i) {
            if (valid[i] && colorsEqual(colors[i], color)) {
                found = i;
                break;
            }
        }

        if (found == 0) {
            return;
        }

        if (found > 0) {
            const SwColor moved = colors[found];
            for (int i = found; i > 0; --i) {
                colors[i] = colors[i - 1];
                valid[i] = valid[i - 1];
            }
            colors[0] = moved;
            valid[0] = true;
            return;
        }

        for (int i = 4; i > 0; --i) {
            colors[i] = colors[i - 1];
            valid[i] = valid[i - 1];
        }
        colors[0] = color;
        valid[0] = true;
    }

    static int hexNibble_(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    }

    static bool parseHexColor_(const SwString& text, SwColor& out) {
        SwString s = text.trimmed();
        if (s.startsWith("#")) {
            s = s.substr(1);
        }
        if (s.length() == 3) {
            const int r = hexNibble_(s[0]);
            const int g = hexNibble_(s[1]);
            const int b = hexNibble_(s[2]);
            if (r < 0 || g < 0 || b < 0) {
                return false;
            }
            out = SwColor{r * 17, g * 17, b * 17};
            return true;
        }
        if (s.length() == 6) {
            const int r0 = hexNibble_(s[0]);
            const int r1 = hexNibble_(s[1]);
            const int g0 = hexNibble_(s[2]);
            const int g1 = hexNibble_(s[3]);
            const int b0 = hexNibble_(s[4]);
            const int b1 = hexNibble_(s[5]);
            if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 || b0 < 0 || b1 < 0) {
                return false;
            }
            out = SwColor{(r0 << 4) | r1, (g0 << 4) | g1, (b0 << 4) | b1};
            return true;
        }
        return false;
    }

    static char hexDigit_(int v) {
        static const char* kHex = "0123456789ABCDEF";
        v = std::max(0, std::min(15, v));
        return kHex[v];
    }

    static SwString colorToHex_(const SwColor& c) {
        const int r = std::max(0, std::min(255, c.r));
        const int g = std::max(0, std::min(255, c.g));
        const int b = std::max(0, std::min(255, c.b));
        char buf[8];
        buf[0] = '#';
        buf[1] = hexDigit_((r >> 4) & 0xF);
        buf[2] = hexDigit_(r & 0xF);
        buf[3] = hexDigit_((g >> 4) & 0xF);
        buf[4] = hexDigit_(g & 0xF);
        buf[5] = hexDigit_((b >> 4) & 0xF);
        buf[6] = hexDigit_(b & 0xF);
        buf[7] = '\0';
        return SwString(buf);
    }

    void buildUi() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar) {
            return;
        }

        buildPalette(content);

        m_picker = new SwRoundColorPicker(content);
        SwObject::connect(m_picker, &SwRoundColorPicker::colorChanged, [this](const SwColor& c) {
            if (m_syncingPicker) {
                return;
            }
            setCurrentColorInternal(c, /*syncPicker=*/false);
        });

        buildRecent(content);

        m_preview = new PreviewWidget(this, content);

        m_hexEdit = new SwLineEdit("#RRGGBB", content);
        m_hexEdit->resize(160, 34);
        SwObject::connect(m_hexEdit, &SwLineEdit::TextChanged, [this](const SwString& text) {
            if (m_syncingHex) {
                return;
            }
            SwColor parsed;
            if (!parseHexColor_(text, parsed)) {
                return;
            }
            setCurrentColorInternal(parsed, /*syncPicker=*/true);
        });

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

        SwObject::connect(m_ok, &SwPushButton::clicked, this, [this]() {
            pushRecentColor_(m_current);
            refreshRecentSwatches_();
            accept();
        });
        SwObject::connect(m_cancel, &SwPushButton::clicked, this, [this]() { reject(); });

        refreshRecentSwatches_();
        updateLayout();
    }

    void buildPalette(SwWidget* parent) {
        if (!parent) {
            return;
        }
        m_paletteSwatches.clear();

        const SwColor palette[15] = {
            SwColor{239, 68, 68},   SwColor{245, 158, 11},  SwColor{34, 197, 94},
            SwColor{6, 182, 212},   SwColor{59, 130, 246},  SwColor{139, 92, 246},
            SwColor{236, 72, 153},  SwColor{15, 23, 42},    SwColor{100, 116, 139},
            SwColor{148, 163, 184}, SwColor{203, 213, 225}, SwColor{226, 232, 240},
            SwColor{248, 250, 252}, SwColor{0, 0, 0},       SwColor{255, 255, 255},
        };

        for (int i = 0; i < 15; ++i) {
            auto* sw = new ColorSwatch(palette[i], true, this, parent);
            sw->resize(44, 34);
            m_paletteSwatches.push_back(sw);
        }
    }

    void buildRecent(SwWidget* parent) {
        if (!parent) {
            return;
        }
        m_recentSwatches.clear();
        for (int i = 0; i < 5; ++i) {
            auto* sw = new ColorSwatch(SwColor{0, 0, 0}, false, this, parent);
            sw->resize(24, 24);
            m_recentSwatches.push_back(sw);
        }
    }

    void refreshRecentSwatches_() {
        if (m_recentSwatches.size() == 0) {
            return;
        }
        SwColor* colors = recentColors_();
        bool* valid = recentValid_();
        const int n = std::min(5, m_recentSwatches.size());
        for (int i = 0; i < n; ++i) {
            ColorSwatch* sw = m_recentSwatches[i];
            if (!sw) {
                continue;
            }
            sw->setValid(valid[i]);
            if (valid[i]) {
                sw->setColor(colors[i]);
                sw->show();
            } else {
                sw->hide();
            }
        }
    }

    void updatePreview() {
        if (m_hexEdit) {
            const SwString hex = colorToHex_(m_current);
            if (m_hexEdit->getText() != hex) {
                m_syncingHex = true;
                m_hexEdit->setText(hex);
                m_syncingHex = false;
            }
        }
        if (m_preview) {
            m_preview->update();
        }
    }

    void updateLayout() {
        auto* content = contentWidget();
        auto* bar = buttonBarWidget();
        if (!content || !bar || !m_preview || !m_picker || !m_hexEdit || !m_ok || !m_cancel) {
            return;
        }

        const int x = 0;
        const int y = 0;
        const int availW = std::max(0, content->width());
        const int availH = std::max(0, content->height());

        // Layout target (matches the reference image):
        // - Top row: 3x5 palette on the left, wheel on the right.
        // - Bottom row: preview + recent colors on the left, hex field on the right.

        const int cols = 3;
        const int rows = 5;
        const int groupGapX = 12;
        const int rowGapY = 10;

        const int previewW = 70;
        const int previewH = 36;
        const int bottomRowH = previewH;

        const int hexH = 34;
        const int hexW = 120;

        const int baseCellW = 52;
        const int baseCellH = 34;
        const int baseGap = 6;
        const int basePaletteW = cols * baseCellW + (cols - 1) * baseGap;
        const int basePaletteH = rows * baseCellH + (rows - 1) * baseGap;

        const int topMaxH = std::max(0, availH - rowGapY - bottomRowH);
        const int widthBudget = std::max(0, availW - groupGapX);
        const int widthDenom = basePaletteW + basePaletteH;

        double scale = 1.0;
        if (basePaletteH > 0) {
            scale = std::min(scale, static_cast<double>(topMaxH) / static_cast<double>(basePaletteH));
        }
        if (widthDenom > 0) {
            scale = std::min(scale, static_cast<double>(widthBudget) / static_cast<double>(widthDenom));
        }
        if (scale < 0.6) {
            scale = 0.6;
        }

        const int cellW = std::max(32, static_cast<int>(std::round(baseCellW * scale)));
        const int cellH = std::max(26, static_cast<int>(std::round(baseCellH * scale)));
        const int gap = std::max(4, static_cast<int>(std::round(baseGap * scale)));

        const int paletteW = cols * cellW + (cols - 1) * gap;
        const int paletteH = rows * cellH + (rows - 1) * gap;
        const int wheelSpace = std::max(0, availW - paletteW - groupGapX);
        const int wheelSize = std::max(0, std::min(paletteH, wheelSpace));

        const int groupX = x;
        const int topY = y;

        const int paletteX = groupX;
        const int wheelX = paletteX + paletteW + groupGapX + (wheelSpace - wheelSize) / 2;

        // Top palette (3x5).
        const int paletteCount = std::min(cols * rows, m_paletteSwatches.size());
        for (int i = 0; i < paletteCount; ++i) {
            SwWidget* sw = m_paletteSwatches[i];
            if (!sw) {
                continue;
            }
            const int row = i / cols;
            const int col = i % cols;
            sw->move(paletteX + col * (cellW + gap), topY + row * (cellH + gap));
            sw->resize(cellW, cellH);
        }

        // Wheel (right of palette).
        m_picker->move(wheelX, topY);
        m_picker->resize(wheelSize, wheelSize);

        // Bottom row: hex field, then preview, then recent swatches â€” all left-justified.
        const int topH = std::max(paletteH, wheelSize);
        const int rowY = topY + topH + rowGapY;

        int rx = paletteX;

        const int hexY = rowY + (previewH - hexH) / 2;
        m_hexEdit->move(rx, hexY);
        m_hexEdit->resize(hexW, hexH);
        rx += hexW + gap;

        m_preview->move(rx, rowY);
        m_preview->resize(previewW, previewH);
        rx += previewW + gap;

        const int recentCount = std::min(5, m_recentSwatches.size());
        const int recentGap = gap;
        int recentW = std::max(26, std::min(previewH, cellW));
        const int recentH = previewH;
        for (int i = 0; i < recentCount; ++i) {
            SwWidget* sw = m_recentSwatches[i];
            if (!sw) {
                continue;
            }
            sw->move(rx + i * (recentW + recentGap), rowY);
            sw->resize(recentW, recentH);
        }

        int bx = bar->width();
        bx -= m_ok->width();
        m_ok->move(bx, 6);
        bx -= m_spacing + m_cancel->width();
        m_cancel->move(bx, 6);
    }

    void setCurrentColorInternal(const SwColor& color, bool syncPicker) {
        m_current = color;
        if (syncPicker && m_picker) {
            m_syncingPicker = true;
            m_picker->setColor(color);
            m_syncingPicker = false;
        }
        updatePreview();
        for (int i = 0; i < m_paletteSwatches.size(); ++i) {
            if (m_paletteSwatches[i]) {
                m_paletteSwatches[i]->update();
            }
        }
        for (int i = 0; i < m_recentSwatches.size(); ++i) {
            if (m_recentSwatches[i]) {
                m_recentSwatches[i]->update();
            }
        }
    }

    SwColor m_current{0, 0, 0};

    PreviewWidget* m_preview{nullptr};
    SwLineEdit* m_hexEdit{nullptr};
    SwVector<SwWidget*> m_paletteSwatches;
    SwVector<ColorSwatch*> m_recentSwatches;
    SwRoundColorPicker* m_picker{nullptr};
    SwPushButton* m_ok{nullptr};
    SwPushButton* m_cancel{nullptr};

    int m_spacing{10};
    bool m_syncingPicker{false};
    bool m_syncingHex{false};
};

