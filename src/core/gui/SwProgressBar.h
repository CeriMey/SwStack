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
 * @file src/core/gui/SwProgressBar.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwProgressBar in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the progress bar interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwProgressBar.
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
 * SwProgressBar - progress bar widget.
 *
 * Supported format tokens:
 * - %p : percentage (0-100)
 * - %v : current value
 * - %m : maximum value
 **************************************************************************************************/

#include "SwWidget.h"
#include "SwTimer.h"

class SwProgressBar : public SwWidget {
    SW_OBJECT(SwProgressBar, SwWidget)

public:
    enum class Orientation {
        Horizontal,
        Vertical
    };

    /**
     * @brief Constructs a `SwProgressBar` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwProgressBar(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults();
        m_shimmerTimer = new SwTimer(40, this);
        connect(m_shimmerTimer, &SwTimer::timeout, this, [this]() {
            m_shimmerPhase += 3;
            if (m_shimmerPhase > 600) {
                m_shimmerPhase = -120;
            }
            update();
        });
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
        update();
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
     * @brief Sets the value.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setValue(int value) {
        const int clamped = clampInt(value, m_minimum, m_maximum);
        if (m_value == clamped) {
            return;
        }
        m_value = clamped;
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
     * @brief Sets the text Visible.
     * @param on Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTextVisible(bool on) {
        if (m_textVisible == on) {
            return;
        }
        m_textVisible = on;
        update();
    }

    /**
     * @brief Returns whether the object reports text Visible.
     * @return `true` when the object reports text Visible; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isTextVisible() const { return m_textVisible; }

    /**
     * @brief Sets the format.
     * @param format Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFormat(const SwString& format) {
        m_format = format;
        update();
    }

    /**
     * @brief Returns the current format.
     * @return The current format.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString format() const { return m_format; }

    /**
     * @brief Returns the current text.
     * @return The current text.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString text() const {
        SwString out = m_format;
        out.replace("%p", SwString::number(percent()));
        out.replace("%v", SwString::number(m_value));
        out.replace("%m", SwString::number(m_maximum));
        return out;
    }

    /**
     * @brief Sets the orientation.
     * @param orientation Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setOrientation(Orientation orientation) {
        if (m_orientation == orientation) {
            return;
        }
        m_orientation = orientation;
        update();
    }

    /**
     * @brief Returns the current orientation.
     * @return The current orientation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Orientation orientation() const { return m_orientation; }

    /**
     * @brief Sets the accent Color.
     * @param color Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAccentColor(const SwColor& color) {
        m_accent = clampColor(color);
        update();
    }

    /**
     * @brief Returns the current accent Color.
     * @return The current accent Color.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwColor accentColor() const { return m_accent; }

    DECLARE_SIGNAL(valueChanged, int);

protected:
    SwSize sizeHint() const override {
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        SwSize hint = (m_orientation == Orientation::Horizontal)
                          ? SwSize{96, 24}
                          : SwSize{24, 120};
        hint.width = std::max(hint.width, std::max(minimumSize().width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minimumSize().height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maximumSize().width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maximumSize().height, styleMax.height));
        return hint;
    }

    SwSize minimumSizeHint() const override {
        const SwSize styleMin = resolvedStyleMinimumSize_();
        const SwSize styleMax = resolvedStyleMaximumSize_();
        SwSize hint = (m_orientation == Orientation::Horizontal)
                          ? SwSize{60, 22}
                          : SwSize{22, 60};
        hint.width = std::max(hint.width, std::max(minimumSize().width, styleMin.width));
        hint.height = std::max(hint.height, std::max(minimumSize().height, styleMin.height));
        hint.width = std::min(hint.width, std::min(maximumSize().width, styleMax.width));
        hint.height = std::min(hint.height, std::min(maximumSize().height, styleMax.height));
        return hint;
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

        const SwRect bounds = rect();
        const int radius = clampInt(bounds.height / 2, 5, 11);

        SwColor frame{224, 231, 240};
        SwColor background{245, 247, 251};
        SwColor textColor{46, 56, 70};
        SwColor fillColor = m_accent;
        SwColor fillHighlight{clampChannel_(fillColor.r + 28),
                              clampChannel_(fillColor.g + 22),
                              clampChannel_(fillColor.b + 14)};

        if (!getEnable()) {
            frame = SwColor{226, 232, 240};
            background = SwColor{248, 250, 252};
            textColor = SwColor{148, 163, 184};
            fillColor = SwColor{147, 197, 253};
            fillHighlight = SwColor{186, 214, 255};
        }

        painter->fillRoundedRect(bounds, radius, background, frame, 1);

        const int padding = 2;
        SwRect inner{bounds.x + padding,
                     bounds.y + padding,
                     clampInt(bounds.width - padding * 2, 0, 1000000),
                     clampInt(bounds.height - padding * 2, 0, 1000000)};

        const int range = m_maximum - m_minimum;
        int fillPixels = 0;
        if (range > 0) {
            const long long numerator = static_cast<long long>(m_value - m_minimum) * 1000LL;
            const long long ratio1000 = clampLL(numerator / range, 0LL, 1000LL);
            if (m_orientation == Orientation::Horizontal) {
                fillPixels = static_cast<int>((static_cast<long long>(inner.width) * ratio1000) / 1000LL);
            } else {
                fillPixels = static_cast<int>((static_cast<long long>(inner.height) * ratio1000) / 1000LL);
            }
        }

        if (fillPixels > 0) {
            const int innerRadius = clampInt(radius - 2, 2, 8);
            SwRect fill;
            if (m_orientation == Orientation::Horizontal) {
                fill = SwRect{inner.x, inner.y, fillPixels, inner.height};
            } else {
                fill = SwRect{inner.x, inner.y + (inner.height - fillPixels), inner.width, fillPixels};
            }
            painter->fillRoundedRect(fill, innerRadius, fillColor, fillColor, 0);

            if (fill.width > 6 && fill.height > 6) {
                SwRect sheen = fill;
                if (m_orientation == Orientation::Horizontal) {
                    sheen.x += 1;
                    sheen.y += 1;
                    sheen.width = std::max(0, sheen.width - 2);
                    sheen.height = std::max(2, (fill.height / 2) - 1);
                } else {
                    sheen.x += 1;
                    sheen.y += 1;
                    sheen.width = std::max(2, (fill.width / 2) - 1);
                    sheen.height = std::max(0, sheen.height - 2);
                }
                if (sheen.width > 0 && sheen.height > 0) {
                    painter->fillRoundedRect(sheen, innerRadius, fillHighlight, fillHighlight, 0);
                }
            }
        }

        if (m_textVisible) {
            const bool textMostlyOnFill = (m_orientation == Orientation::Horizontal)
                                              ? fillPixels >= (bounds.width / 2)
                                              : fillPixels >= (bounds.height / 2);
            painter->drawText(bounds,
                              text(),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textMostlyOnFill ? SwColor{255, 255, 255} : textColor,
                              getFont());
        }
    }

private:
    static long long clampLL(long long value, long long minValue, long long maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    static uint8_t clampChannel_(int value) {
        return static_cast<uint8_t>(clampInt(value, 0, 255));
    }

    int percent() const {
        const int range = m_maximum - m_minimum;
        if (range <= 0) {
            return 0;
        }
        const int clampedValue = clampInt(m_value, m_minimum, m_maximum);
        const long long numerator = static_cast<long long>(clampedValue - m_minimum) * 100LL;
        return static_cast<int>(clampLL(numerator / range, 0LL, 100LL));
    }

    void initDefaults() {
        resize(260, 22);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setFont(SwFont(L"Segoe UI", 9, Medium));
    }

    int m_minimum{0};
    int m_maximum{100};
    int m_value{25};
    bool m_textVisible{true};
    SwString m_format{"%p%"};
    Orientation m_orientation{Orientation::Horizontal};
    SwColor m_accent{59, 130, 246};
    SwTimer* m_shimmerTimer{nullptr};
    int m_shimmerPhase{-120};
};
