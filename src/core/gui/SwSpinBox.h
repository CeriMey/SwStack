#pragma once

/**
 * @file src/core/gui/SwSpinBox.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwSpinBox in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the spin box interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwSpinBox.
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

#include "SwFrame.h"
#include "SwLineEdit.h"
#include "SwWidgetPlatformAdapter.h"

#include <algorithm>

class SwSpinBox : public SwFrame {
    SW_OBJECT(SwSpinBox, SwFrame)

public:
    /**
     * @brief Constructs a `SwSpinBox` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwSpinBox(SwWidget* parent = nullptr)
        : SwFrame(parent) {
        initDefaults();
        buildChildren();
        updateTextFromValue();
        updateLayout();
    }

    /**
     * @brief Sets the range.
     * @param minimum Value passed to the method.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRange(int minimum, int maximum) {
        if (minimum > maximum) {
            const int tmp = minimum;
            minimum = maximum;
            maximum = tmp;
        }
        m_minimum = minimum;
        m_maximum = maximum;
        setValue(m_value);
    }

    /**
     * @brief Sets the minimum.
     * @param m_maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMinimum(int minimum) { setRange(minimum, m_maximum); }
    /**
     * @brief Sets the maximum.
     * @param maximum Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMaximum(int maximum) { setRange(m_minimum, maximum); }

    /**
     * @brief Returns the current minimum.
     * @return The current minimum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int minimum() const { return m_minimum; }
    /**
     * @brief Returns the current maximum.
     * @return The current maximum.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int maximum() const { return m_maximum; }

    /**
     * @brief Sets the single Step.
     * @param m_singleStep Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSingleStep(int step) { m_singleStep = std::max(1, step); }
    /**
     * @brief Returns the current single Step.
     * @return The current single Step.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int singleStep() const { return m_singleStep; }

    /**
     * @brief Sets the value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setValue(int value) {
        const int clamped = clampInt(value, m_minimum, m_maximum);
        if (m_value == clamped) {
            updateTextFromValue();
            return;
        }
        m_value = clamped;
        updateTextFromValue();
        valueChanged(m_value);
        update();
    }

    /**
     * @brief Returns the current value.
     * @return The current value.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int value() const { return m_value; }

    /**
     * @brief Returns the current line Edit.
     * @return The current line Edit.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwLineEdit* lineEdit() const { return m_edit; }

    DECLARE_SIGNAL(valueChanged, int);

protected:
    SwSize sizeHint() const override {
        int borderWidth = 1;
        int radiusTopRight = 0;
        int radiusBottomRight = 0;
        const_cast<SwSpinBox*>(this)->resolveFrameMetrics_(borderWidth, radiusTopRight, radiusBottomRight);

        const SwSize editHint = m_edit ? m_edit->sizeHint() : SwSize{56, 28};
        const SwSize minSize = minimumSize();
        const SwSize maxSize = maximumSize();
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        SwSize hint{
            editHint.width + arrowColumnWidth() + (borderWidth * 2),
            editHint.height + (borderWidth * 2)
        };
        hint.width = std::max(hint.width, std::max(minSize.width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minSize.height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maxSize.width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maxSize.height, styleMax.height));
        return hint;
    }

    SwSize minimumSizeHint() const override {
        return sizeHint();
    }

    /**
     * @brief Handles the resize Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void resizeEvent(ResizeEvent* event) override {
        SwFrame::resizeEvent(event);
        updateLayout();
    }

    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        if (!isVisibleInHierarchy()) {
            return;
        }
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect rect = this->rect();
        StyleSheet* sheet = getToolSheet();

        SwColor bg{255, 255, 255};
        float bgAlpha = 1.0f;
        bool paintBackground = true;

        SwColor border{220, 224, 232};
        int borderWidth = 1;
        int radius = 12;

        resolveBackground(sheet, bg, bgAlpha, paintBackground);
        resolveBorder(sheet, border, borderWidth, radius);

        const bool explicitHoverBackground =
            hasExplicitStyledStateProperty(StyleSheet::StateHovered, "background-color");
        const bool explicitHoverBorder =
            hasExplicitStyledStateProperty(StyleSheet::StateHovered, "border-color");
        const bool explicitFocusBorder =
            hasExplicitStyledStateProperty(StyleSheet::StateFocused, "border-color");

        if (getEnable()) {
            const bool isDark = (bg.r + bg.g + bg.b) < 384;
            if (getHover()) {
                if (!explicitHoverBackground) {
                    bg = isDark
                             ? SwColor{(uint8_t)std::min(255, bg.r + 5),
                                       (uint8_t)std::min(255, bg.g + 5),
                                       (uint8_t)std::min(255, bg.b + 5)}
                             : SwColor{250, 251, 253};
                }
                if (!explicitHoverBorder) {
                    border = isDark ? SwColor{85, 85, 85} : SwColor{150, 150, 150};
                }
            }
            if (m_edit && m_edit->getFocus() && !explicitFocusBorder) {
                border = m_focusAccent;
            }
        }

        if (paintBackground && bgAlpha > 0.0f) {
            painter->fillRoundedRect(rect, radius, bg, border, borderWidth);
        } else {
            painter->drawRect(rect, border, borderWidth);
        }

        // Divider between text area and arrows.
        const int arrowW = arrowColumnWidth();
        const int innerX = rect.x + borderWidth;
        const int innerY = rect.y + borderWidth;
        const int innerW = std::max(0, rect.width - 2 * borderWidth);
        const int innerH = std::max(0, rect.height - 2 * borderWidth);
        const int dividerX = innerX + std::max(0, innerW - arrowW);
        const int rightEdge = innerX + innerW;

        SwColor divider{226, 232, 240};
        if (sheet) {
            SwString value = sheet->getStyleProperty("SwSpinBox", "divider-color");
            if (!value.isEmpty()) {
                try {
                    divider = sheet->parseColor(value, nullptr);
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
            const int midRight = std::max(dividerX + 1, rightEdge - std::max(1, borderWidth));
            painter->drawLine(dividerX + 1, midY, midRight, midY, divider, 1);
        }

        paintChildren(event);
        painter->finalize();
    }

    /**
     * @brief Handles the key Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
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
    struct ArrowFillRadii {
        int tl{0};
        int tr{0};
        int br{0};
        int bl{0};
    };

    class ArrowButton final : public SwWidget {
        SW_OBJECT(ArrowButton, SwWidget)

    public:
        enum class Direction { Up, Down };

        /**
         * @brief Performs the `ArrowButton` operation.
         * @param dir Value passed to the method.
         * @param parent Optional parent object that owns this instance.
         * @return The requested arrow Button.
         */
        explicit ArrowButton(Direction dir, SwWidget* parent = nullptr)
            : SwWidget(parent)
            , m_dir(dir)
            , m_owner(dynamic_cast<SwSpinBox*>(parent)) {
            setCursor(CursorType::Hand);
            setFocusPolicy(FocusPolicyEnum::NoFocus);
        }

        DECLARE_SIGNAL_VOID(clicked);

    protected:
        CUSTOM_PROPERTY(bool, Pressed, false) { update(); }

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

            StyleSheet* sheet = m_owner ? m_owner->getToolSheet() : getToolSheet();
            auto parseOwnerColor = [&](const char* propName, const SwColor& fallback, float* alphaOut = nullptr) -> SwColor {
                if (!sheet || !m_owner) {
                    if (alphaOut) {
                        *alphaOut = 1.0f;
                    }
                    return fallback;
                }
                SwString value = sheet->getStyleProperty("SwSpinBox", propName);
                if (value.isEmpty()) {
                    if (alphaOut) {
                        *alphaOut = 1.0f;
                    }
                    return fallback;
                }
                float a = 1.0f;
                try {
                    SwColor c = sheet->parseColor(value, &a);
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
                SwRect hi = m_owner ? m_owner->arrowHighlightRect_(r, m_dir) : r;
                ArrowFillRadii radii = m_owner ? m_owner->arrowHighlightRadii_(m_dir) : ArrowFillRadii{};
                if (hi.width > 0 && hi.height > 0) {
                    if (radii.tl == 0 && radii.tr == 0 && radii.br == 0 && radii.bl == 0) {
                        painter->fillRect(hi, bg, bg, 0);
                    } else {
                        painter->fillRoundedRect(hi, radii.tl, radii.tr, radii.br, radii.bl, bg, bg, 0);
                    }
                }
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

        /**
         * @brief Handles the mouse Press Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
        void mousePressEvent(MouseEvent* event) override {
            if (!event) {
                return;
            }
            if (!getEnable() || !isPointInside(event->x(), event->y())) {
                SwWidget::mousePressEvent(event);
                return;
            }
            if (m_owner) {
                m_owner->focusEditorForStepping_();
            }
            setPressed(true);
            event->accept();
        }

        /**
         * @brief Handles the mouse Release Event forwarded by the framework.
         * @param event Event object forwarded by the framework.
         *
         * @details Override this hook when the default framework behavior needs to be extended or replaced.
         */
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
        SwSpinBox* m_owner{nullptr};
    };

    int arrowColumnWidth() const {
        return clampInt(m_arrowWidth, 18, 34);
    }

    void resolveFrameMetrics_(int& borderWidth,
                              int& radiusTopRight,
                              int& radiusBottomRight) {
        SwColor border{220, 224, 232};
        int radius = 12;
        borderWidth = 1;
        resolveBorder(getToolSheet(), border, borderWidth, radius);
        borderWidth = std::max(0, borderWidth);

        int tl = std::max(0, radius);
        int tr = tl;
        int br = tl;
        int bl = tl;
        resolveBorderCornerRadii(getToolSheet(), tl, tr, br, bl);
        radiusTopRight = std::max(0, tr);
        radiusBottomRight = std::max(0, br);
    }

    SwRect arrowHighlightRect_(const SwRect& buttonRect,
                               ArrowButton::Direction dir) {
        int borderWidth = 1;
        int radiusTopRight = 0;
        int radiusBottomRight = 0;
        resolveFrameMetrics_(borderWidth, radiusTopRight, radiusBottomRight);

        const int leftInset = 1;
        const int rightInset = std::max(0, borderWidth);
        const int outerInset = std::max(0, borderWidth);
        const int centerInset = 1;

        const int topInset = (dir == ArrowButton::Direction::Up) ? outerInset : centerInset;
        const int bottomInset = (dir == ArrowButton::Direction::Up) ? centerInset : outerInset;

        SwRect hi = buttonRect;
        hi.x += leftInset;
        hi.y += topInset;
        hi.width = std::max(0, hi.width - leftInset - rightInset);
        hi.height = std::max(0, hi.height - topInset - bottomInset);
        return hi;
    }

    ArrowFillRadii arrowHighlightRadii_(ArrowButton::Direction dir) {
        int borderWidth = 1;
        int radiusTopRight = 0;
        int radiusBottomRight = 0;
        resolveFrameMetrics_(borderWidth, radiusTopRight, radiusBottomRight);

        ArrowFillRadii radii;
        if (dir == ArrowButton::Direction::Up) {
            radii.tr = std::max(0, radiusTopRight - borderWidth);
        } else {
            radii.br = std::max(0, radiusBottomRight - borderWidth);
        }
        return radii;
    }

    void buildChildren() {
        m_edit = new SwLineEdit(this);
        m_edit->setFrameShape(Shape::NoFrame);

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
        int bw = 1;
        int radius = 0;
        SwColor border{0, 0, 0};
        resolveBorder(getToolSheet(), border, bw, radius);
        bw = std::max(0, bw);
        const int arrowW = arrowColumnWidth();
        const int innerX = bw;
        const int innerY = bw;
        const int innerW = std::max(0, width() - 2 * bw);
        const int innerH = std::max(0, height() - 2 * bw);

        const int arrowsX = innerX + std::max(0, innerW - arrowW);
        const int arrowsY = innerY;
        const int arrowsH = innerH;
        const int half = arrowsH / 2;
        const int editorOverlap = (arrowW > 0) ? 1 : 0;

        if (m_edit) {
            m_edit->move(innerX, innerY);
            m_edit->resize(std::max(0, innerW - arrowW + editorOverlap), innerH);
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

    void stepUp() {
        setValue(m_value + m_singleStep);
        refreshStepSelection_();
    }

    void stepDown() {
        setValue(m_value - m_singleStep);
        refreshStepSelection_();
    }

    void updateTextFromValue() {
        if (!m_edit) {
            return;
        }
        m_internalTextUpdate = true;
        m_edit->setText(SwString::number(m_value));
        m_internalTextUpdate = false;
    }

    void commitEdit() {
        if (!m_edit) {
            return;
        }
        bool ok = false;
        SwString t = m_edit->getText().trimmed();
        if (t.isEmpty() || t == "-" || t == "+") {
            updateTextFromValue();
            return;
        }
        const int v = t.toInt(&ok);
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

        const bool allowSign = (m_minimum < 0);

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
        }

        if (out != in) {
            m_internalTextUpdate = true;
            m_edit->setText(out);
            m_internalTextUpdate = false;
        }
    }

    void focusEditorForStepping_() {
        if (!m_edit) {
            return;
        }
        m_edit->setFocus(true);
        m_edit->selectAllFromStart();
    }

    void refreshStepSelection_() {
        if (!m_edit || !m_edit->getFocus()) {
            return;
        }
        m_edit->selectAllFromStart();
    }

    void initDefaults() {
        resize(160, 34);
        setCursor(CursorType::IBeam);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFrameShape(Shape::Box);
        SwFont font(L"Segoe UI", 10, Medium);
        font.setPixelSize(14);
        setFont(font);
    }

    SwLineEdit* m_edit{nullptr};
    ArrowButton* m_up{nullptr};
    ArrowButton* m_down{nullptr};

    int m_minimum{0};
    int m_maximum{99};
    int m_value{0};
    int m_singleStep{1};

    int m_arrowWidth{28};
    bool m_internalTextUpdate{false};
    SwColor m_focusAccent{59, 130, 246};
};
