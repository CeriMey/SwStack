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
 * SwDoubleSpinBox - Qt-like floating-point spin box (≈ QDoubleSpinBox).
 *
 * Notes:
 * - Uses an internal SwLineEdit for text editing.
 * - Provides up/down buttons and basic input sanitizing.
 **************************************************************************************************/

#include "SwFrame.h"
#include "SwLineEdit.h"
#include "SwWidgetPlatformAdapter.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

class SwDoubleSpinBox : public SwFrame {
    SW_OBJECT(SwDoubleSpinBox, SwFrame)

public:
    explicit SwDoubleSpinBox(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        buildChildren();
        updateTextFromValue();
        updateLayout();
    }

    void setRange(double minimum, double maximum) {
        if (minimum > maximum) {
            const double tmp = minimum;
            minimum = maximum;
            maximum = tmp;
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
    }

    void setMinimum(double minimum) { setRange(minimum, m_maximum); }
    void setMaximum(double maximum) { setRange(m_minimum, maximum); }

    double minimum() const { return m_minimum; }
    double maximum() const { return m_maximum; }

    void setDecimals(int decimals) {
        m_decimals = clampInt(decimals, 0, 8);
        updateTextFromValue();
        update();
    }

    int decimals() const { return m_decimals; }

    void setSingleStep(double step) {
        if (step <= 0.0) {
            step = 0.1;
        }
        m_singleStep = step;
    }

    double singleStep() const { return m_singleStep; }

    void setValue(double value) {
        const double clamped = clampDouble(value, m_minimum, m_maximum);
        if (std::abs(m_value - clamped) < 1e-12) {
            updateTextFromValue();
            return;
        }
        m_value = clamped;
        updateTextFromValue();
        valueChanged(m_value);
        update();
    }

    double value() const { return m_value; }

    SwLineEdit* lineEdit() const { return m_edit; }

    DECLARE_SIGNAL(valueChanged, double);

protected:
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = getRect();
        StyleSheet* sheet = getToolSheet();

        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        if (getEnable() && m_edit && m_edit->getFocus()) {
            border = m_focusAccent;
        }

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRoundedRect(rect, radius, bg, border, borderWidth);
        } else {
            painter->drawRect(rect, border, borderWidth);
        }

        const int arrowW = arrowColumnWidth();
        const int innerX = rect.x + borderWidth;
        const int innerY = rect.y + borderWidth;
        const int innerW = std::max(0, rect.width - 2 * borderWidth);
        const int innerH = std::max(0, rect.height - 2 * borderWidth);
        const int dividerX = innerX + std::max(0, innerW - arrowW);
        const int rightEdge = innerX + innerW;

        SwColor divider{226, 232, 240};
        if (sheet) {
            SwString value = sheet->getStyleProperty("SwDoubleSpinBox", "divider-color");
            if (!value.isEmpty()) {
                try {
                    divider = sheet->parseColor(value.toStdString(), nullptr);
                } catch (...) {
                }
            }
        }
        const int dividerTop = innerY + 3;
        const int dividerBottom = innerY + std::max(0, innerH - 3);
        painter->drawLine(dividerX, dividerTop, dividerX, dividerBottom, divider, 1);

        const int half = innerH / 2;
        const int midY = innerY + half;
        if (innerH > 8) {
            painter->drawLine(dividerX + 1, midY, std::max(dividerX + 1, rightEdge - 1), midY, divider, 1);
        }

        paintChildren(event);
        painter->finalize();
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event) {
            return;
        }

        SwFrame::keyPressEvent(event);
        if (event->isAccepted()) {
            return;
        }

        const int key = event->key();
        if (SwWidgetPlatformAdapter::isUpArrowKey(key)) {
            stepUp();
            event->accept();
        } else if (SwWidgetPlatformAdapter::isDownArrowKey(key)) {
            stepDown();
            event->accept();
        } else if (SwWidgetPlatformAdapter::isReturnKey(key)) {
            commitEdit();
            event->accept();
        } else if (SwWidgetPlatformAdapter::isEscapeKey(key)) {
            updateTextFromValue();
            event->accept();
        }
    }

private:
    class ArrowButton final : public SwWidget {
        SW_OBJECT(ArrowButton, SwWidget)

    public:
        enum class Direction { Up, Down };

        explicit ArrowButton(Direction dir, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_dir(dir)
            , m_owner(dynamic_cast<SwDoubleSpinBox*>(parent)) {
            setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
            setCursor(CursorType::Hand);
            setFocusPolicy(FocusPolicyEnum::NoFocus);
        }

        DECLARE_SIGNAL_VOID(clicked);

    protected:
        CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

        void paintEvent(PaintEvent* event) override {
            SwPainter* painter = event ? event->painter() : nullptr;
            if (!painter) {
                return;
            }

            const SwRect r = getRect();

            StyleSheet* sheet = m_owner ? m_owner->getToolSheet() : getToolSheet();
            auto parseOwnerColor = [&](const char* propName, const SwColor& fallback, float* alphaOut = nullptr) -> SwColor {
                if (!sheet || !m_owner) {
                    if (alphaOut) {
                        *alphaOut = 1.0f;
                    }
                    return fallback;
                }
                SwString value = sheet->getStyleProperty("SwDoubleSpinBox", propName);
                if (value.isEmpty()) {
                    if (alphaOut) {
                        *alphaOut = 1.0f;
                    }
                    return fallback;
                }
                float a = 1.0f;
                try {
                    SwColor c = sheet->parseColor(value.toStdString(), &a);
                    if (alphaOut) {
                        *alphaOut = a;
                    }
                    return c;
                } catch (...) {
                    if (alphaOut) {
                        *alphaOut = 1.0f;
                    }
                    return fallback;
                }
            };

            const SwColor fgNormal = parseOwnerColor("arrow-color", SwColor{71, 85, 105});
            const SwColor fgHover = parseOwnerColor("arrow-hover-color", SwColor{51, 65, 85});
            const SwColor fgPressed = parseOwnerColor("arrow-pressed-color", SwColor{30, 41, 59});
            const SwColor fgDisabled = parseOwnerColor("arrow-disabled-color", SwColor{148, 163, 184});

            float bgAlpha = 1.0f;
            SwColor bg = parseOwnerColor("arrow-hover-background-color", SwColor{241, 245, 249}, &bgAlpha);
            SwColor fg = fgNormal;
            bool paintBg = false;

            if (!getEnable()) {
                fg = fgDisabled;
            } else if (getPressed()) {
                fg = fgPressed;
                bg = parseOwnerColor("arrow-pressed-background-color", SwColor{226, 232, 240}, &bgAlpha);
                paintBg = (bgAlpha > 0.0f);
            } else if (getHover()) {
                fg = fgHover;
                bg = parseOwnerColor("arrow-hover-background-color", SwColor{241, 245, 249}, &bgAlpha);
                paintBg = (bgAlpha > 0.0f);
            }

            if (paintBg) {
                SwRect hi = r;
                hi.x += 1;
                hi.width = std::max(0, hi.width - 3);
                if (m_dir == Direction::Up) {
                    hi.y += 2;
                    hi.height = std::max(0, hi.height - 2);
                } else {
                    hi.height = std::max(0, hi.height - 2);
                }
                painter->fillRect(hi, bg, bg, 0);
            }

            const int cx = r.x + r.width / 2;
            const int cy = r.y + r.height / 2;
            const int s = std::max(4, std::min(r.width, r.height) / 4);

            if (m_dir == Direction::Up) {
                painter->drawLine(cx - s, cy + s / 2, cx, cy - s, fg, 2);
                painter->drawLine(cx, cy - s, cx + s, cy + s / 2, fg, 2);
            } else {
                painter->drawLine(cx - s, cy - s / 2, cx, cy + s, fg, 2);
                painter->drawLine(cx, cy + s, cx + s, cy - s / 2, fg, 2);
            }
        }

        void mousePressEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (!getEnable() || !isPointInside(event->x(), event->y())) {
                SwWidget::mousePressEvent(event);
                return;
            }
            setPressed(true);
            event->accept();
        }

        void mouseReleaseEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }

            const bool wasPressed = getPressed();
            setPressed(false);

            if (wasPressed && getEnable() && isPointInside(event->x(), event->y())) {
                clicked();
                event->accept();
                return;
            }

            SwWidget::mouseReleaseEvent(event);
        }

    private:
        Direction m_dir{Direction::Up};
        SwDoubleSpinBox* m_owner{nullptr};
    };

    static int clampInt(int value, int minValue, int maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static double clampDouble(double value, double minValue, double maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    int arrowColumnWidth() const {
        return clampInt(m_arrowWidth, 18, 34);
    }

    void buildChildren() {
        m_edit = new SwLineEdit(this);
        m_edit->setStyleSheet(R"(
            SwLineEdit {
                background-color: rgba(0,0,0,0);
                border-width: 0px;
                border-radius: 0px;
                padding: 6px 10px;
                font-size: 14px;
            }
        )");

        m_up = new ArrowButton(ArrowButton::Direction::Up, this);
        m_down = new ArrowButton(ArrowButton::Direction::Down, this);

        SwObject::connect(m_up, &ArrowButton::clicked, [this]() { stepUp(); });
        SwObject::connect(m_down, &ArrowButton::clicked, [this]() { stepDown(); });

        SwObject::connect(m_edit, &SwLineEdit::TextChanged, [this](const SwString& txt) {
            if (m_internalTextUpdate) {
                return;
            }
            sanitizeEditText(txt);
        });

        SwObject::connect(m_edit, &SwLineEdit::FocusChanged, [this](bool focus) {
            if (!focus) {
                commitEdit();
            }
        });
    }

    void updateLayout() {
        const SwRect r = getRect();
        int bw = 1;
        int radius = 0;
        SwColor border{0, 0, 0};
        resolveBorder(getToolSheet(), border, bw, radius);
        bw = std::max(0, bw);
        const int arrowW = arrowColumnWidth();
        const int innerX = r.x + bw;
        const int innerY = r.y + bw;
        const int innerW = std::max(0, r.width - 2 * bw);
        const int innerH = std::max(0, r.height - 2 * bw);

        const int arrowsX = innerX + std::max(0, innerW - arrowW);
        const int arrowsY = innerY;
        const int arrowsH = innerH;
        const int half = arrowsH / 2;

        if (m_edit) {
            m_edit->move(innerX, innerY);
            m_edit->resize(std::max(0, innerW - arrowW), innerH);
        }
        if (m_up) {
            m_up->move(arrowsX, arrowsY);
            m_up->resize(arrowW, half);
        }
        if (m_down) {
            m_down->move(arrowsX, arrowsY + half);
            m_down->resize(arrowW, arrowsH - half);
        }
    }

    void stepUp() { setValue(m_value + m_singleStep); }
    void stepDown() { setValue(m_value - m_singleStep); }

    SwString formatValue(double v) const {
        std::ostringstream os;
        os.setf(std::ios::fixed);
        os << std::setprecision(m_decimals) << v;
        return SwString(os.str());
    }

    void updateTextFromValue() {
        if (!m_edit) {
            return;
        }
        m_internalTextUpdate = true;
        m_edit->setText(formatValue(m_value));
        m_internalTextUpdate = false;
    }

    void commitEdit() {
        if (!m_edit) {
            return;
        }
        bool ok = false;
        SwString t = m_edit->getText().trimmed();
        if (t.isEmpty() || t == "-" || t == "+" || t == "." || t == "-." || t == "+.") {
            updateTextFromValue();
            return;
        }
        const double v = t.toDouble(&ok);
        if (!ok) {
            updateTextFromValue();
            return;
        }
        setValue(v);
    }

    void sanitizeEditText(const SwString& txt) {
        SwString in = txt;
        SwString out;
        out.reserve(in.size());

        const bool allowSign = (m_minimum < 0.0);
        bool hasDot = false;

        for (size_t i = 0; i < in.size(); ++i) {
            const char c = in[i];
            if (c >= '0' && c <= '9') {
                out.append(c);
                continue;
            }
            if (allowSign && (c == '-' || c == '+') && out.isEmpty()) {
                out.append(c);
                continue;
            }
            if (c == '.' && !hasDot) {
                hasDot = true;
                out.append(c);
                continue;
            }
        }

        if (out != in) {
            m_internalTextUpdate = true;
            m_edit->setText(out);
            m_internalTextUpdate = false;
        }
    }

    void initDefaults() {
        resize(160, 34);
        setCursor(CursorType::IBeam);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFrameShape(Shape::Box);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setStyleSheet(R"(
            SwDoubleSpinBox {
                background-color: rgb(255, 255, 255);
                border-color: rgb(220, 224, 232);
                border-width: 1px;
                border-radius: 12px;
                divider-color: rgb(226, 232, 240);
                arrow-color: rgb(71, 85, 105);
                arrow-hover-color: rgb(51, 65, 85);
                arrow-pressed-color: rgb(30, 41, 59);
                arrow-disabled-color: rgb(148, 163, 184);
                arrow-hover-background-color: rgb(241, 245, 249);
                arrow-pressed-background-color: rgb(226, 232, 240);
            }
        )");
    }

    SwLineEdit* m_edit{nullptr};
    ArrowButton* m_up{nullptr};
    ArrowButton* m_down{nullptr};

    double m_minimum{0.0};
    double m_maximum{100.0};
    double m_value{0.0};
    double m_singleStep{0.1};
    int m_decimals{2};

    int m_arrowWidth{28};
    bool m_internalTextUpdate{false};
    SwColor m_focusAccent{59, 130, 246};
};
