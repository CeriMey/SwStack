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
 * SwColorDialog - Qt-like color dialog (≈ QColorDialog).
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
    explicit SwColorDialog(SwWidget* parent = nullptr)
        : SwDialog(parent) {
        setMinimumSize(560, 460);
        resize(620, 480);
        setWindowTitle("Select color");
        buildUi();
        setCurrentColor(SwColor{59, 130, 246});
    }

    void setCurrentColor(const SwColor& color) {
        setCurrentColorInternal(color, /*syncPicker=*/true);
    }

    SwColor currentColor() const { return m_current; }

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
    void resizeEvent(ResizeEvent* event) override {
        SwDialog::resizeEvent(event);
        updateLayout();
    }

private:
    class PreviewWidget final : public SwWidget {
        SW_OBJECT(PreviewWidget, SwWidget)

    public:
        explicit PreviewWidget(SwColorDialog* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_owner(owner) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }
            const SwRect r = getRect();
            const SwColor c = m_owner ? m_owner->m_current : SwColor{0, 0, 0};
            painter->fillRoundedRect(r, 12, c, SwColor{226, 232, 240}, 1);
        }

    private:
        SwColorDialog* m_owner{nullptr};
    };

    class ColorSwatch final : public SwWidget {
        SW_OBJECT(ColorSwatch, SwWidget)

    public:
        ColorSwatch(const SwColor& c, bool valid, SwColorDialog* owner, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_color(c)
            , m_valid(valid)
            , m_owner(owner) {
            setCursor(m_valid ? CursorType::Hand : CursorType::Arrow);
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        }

        void setColor(const SwColor& c) {
            m_color = c;
            update();
        }

        void setValid(bool valid) {
            if (m_valid == valid) {
                return;
            }
            m_valid = valid;
            setCursor(m_valid ? CursorType::Hand : CursorType::Arrow);
            update();
        }

        SwColor color() const { return m_color; }
        bool isValid() const { return m_valid; }

        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }
            const SwRect r = getRect();
            const int radius = std::min(14, std::max(1, std::min(r.width, r.height) / 2));

            if (!m_valid) {
                painter->fillRoundedRect(r, radius, SwColor{255, 255, 255}, SwColor{226, 232, 240}, 1);
                return;
            }

            const bool selected = m_owner && colorsEqual(m_owner->m_current, m_color);
            const SwColor border = selected ? SwColor{59, 130, 246} : SwColor{226, 232, 240};
            painter->fillRoundedRect(r, radius, m_color, border, selected ? 2 : 1);
        }

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
        m_ok->resize(120, 40);
        m_cancel->resize(120, 40);

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

        const SwRect cr = content->getRect();
        const int x = cr.x;
        const int y = cr.y;
        const int availW = std::max(0, cr.width);
        const int availH = std::max(0, cr.height);

        // Layout target (matches the reference image):
        // - Top row: 3x5 palette on the left, wheel on the right.
        // - Bottom row: preview + recent colors on the left, hex field on the right.

        const int cols = 3;
        const int rows = 5;
        const int groupGapX = 24;
        const int rowGapY = 24;

        const int previewW = 90;
        const int previewH = 46;
        const int bottomRowH = previewH;

        const int hexH = 34;
        const int hexW = 120;

        const int baseCellW = 72;
        const int baseCellH = 44;
        const int baseGap = 12;
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

        const int cellW = std::max(44, static_cast<int>(std::round(baseCellW * scale)));
        const int cellH = std::max(34, static_cast<int>(std::round(baseCellH * scale)));
        const int gap = std::max(8, static_cast<int>(std::round(baseGap * scale)));

        const int paletteW = cols * cellW + (cols - 1) * gap;
        const int paletteH = rows * cellH + (rows - 1) * gap;
        const int wheelSize = std::max(0, std::min(paletteH, availW - paletteW - groupGapX));

        const int groupW = paletteW + groupGapX + wheelSize;
        const int groupX = x;
        const int topY = y;

        const int paletteX = groupX;
        const int wheelX = paletteX + paletteW + groupGapX;

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

        // Bottom row.
        const int topH = std::max(paletteH, wheelSize);
        const int rowY = topY + topH + rowGapY;

        m_preview->move(paletteX, rowY);
        m_preview->resize(previewW, previewH);

        const int hexX = groupX + groupW - hexW;
        const int hexY = rowY + (previewH - hexH) / 2;
        m_hexEdit->move(hexX, hexY);
        m_hexEdit->resize(hexW, hexH);

        const int recentCount = std::min(5, m_recentSwatches.size());
        const int recentGap = gap;
        const int recentStartX = paletteX + previewW + gap;
        const int recentEndX = hexX - gap;
        const int recentAreaW = std::max(0, recentEndX - recentStartX);
        int recentW = 0;
        if (recentCount > 0) {
            recentW = (recentAreaW - (recentCount - 1) * recentGap) / recentCount;
            recentW = std::max(26, std::min(recentW, cellW));
        }

        const int recentH = previewH;
        for (int i = 0; i < recentCount; ++i) {
            SwWidget* sw = m_recentSwatches[i];
            if (!sw) {
                continue;
            }
            sw->move(recentStartX + i * (recentW + recentGap), rowY);
            sw->resize(recentW, recentH);
        }

        const SwRect br = bar->getRect();
        const int by = br.y + 6;
        int bx = br.x + br.width;
        bx -= m_ok->width();
        m_ok->move(bx, by);
        bx -= m_spacing + m_cancel->width();
        m_cancel->move(bx, by);
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
