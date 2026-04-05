#pragma once

/**
 * @file
 * @ingroup core_gui
 * @brief Declares `SwStatusLight`, a compact painted status indicator widget.
 */

#include "SwWidget.h"

#include <algorithm>

class SwStatusLight : public SwWidget {
    SW_OBJECT(SwStatusLight, SwWidget)

public:
    enum class Tone {
        Neutral,
        Success,
        Warning,
        Danger
    };

    explicit SwStatusLight(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        initDefaults_();
    }

    void setTone(Tone tone) {
        if (m_tone == tone) {
            return;
        }
        m_tone = tone;
        update();
    }

    Tone tone() const { return m_tone; }

    void setDiameter(int diameter) {
        const int clamped = std::max(10, std::min(diameter, 48));
        if (m_diameter == clamped) {
            return;
        }
        m_diameter = clamped;
        setMinimumSize(m_diameter + 4, m_diameter + 4);
        update();
    }

    int diameter() const { return m_diameter; }

    SwSize sizeHint() const override {
        return SwSize{m_diameter + 4, m_diameter + 4};
    }

protected:
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        const int diameter = std::max(8, std::min(bounds.width, bounds.height) - 2);
        const int x = bounds.x + (bounds.width - diameter) / 2;
        const int y = bounds.y + (bounds.height - diameter) / 2;

        const SwColor ring = SwColor{25, 36, 44};
        const SwColor bezel = SwColor{14, 22, 28};
        const SwColor glow = glowColor_(m_tone);
        const SwColor core = coreColor_(m_tone);
        const SwColor highlight = highlightColor_(m_tone);

        const SwRect outer{x, y, diameter, diameter};
        const SwRect inner{x + 2, y + 2, std::max(4, diameter - 4), std::max(4, diameter - 4)};
        const SwRect coreRect{x + 4, y + 4, std::max(2, diameter - 8), std::max(2, diameter - 8)};
        const SwRect highlightRect{x + diameter / 4,
                                   y + diameter / 5,
                                   std::max(3, diameter / 3),
                                   std::max(2, diameter / 4)};

        painter->fillRoundedRect(outer, diameter / 2, bezel, ring, 1);
        painter->fillRoundedRect(inner, std::max(2, inner.width / 2), glow, glow, 0);
        painter->fillRoundedRect(coreRect, std::max(1, coreRect.width / 2), core, core, 0);
        painter->fillRoundedRect(highlightRect,
                                 std::max(1, highlightRect.height / 2),
                                 highlight,
                                 highlight,
                                 0);

    }

private:
    static SwColor coreColor_(Tone tone) {
        switch (tone) {
        case Tone::Success: return SwColor{74, 165, 132};
        case Tone::Warning: return SwColor{214, 154, 88};
        case Tone::Danger: return SwColor{196, 93, 93};
        case Tone::Neutral:
        default: return SwColor{83, 99, 109};
        }
    }

    static SwColor glowColor_(Tone tone) {
        switch (tone) {
        case Tone::Success: return SwColor{45, 95, 78};
        case Tone::Warning: return SwColor{104, 77, 43};
        case Tone::Danger: return SwColor{101, 52, 52};
        case Tone::Neutral:
        default: return SwColor{38, 52, 60};
        }
    }

    static SwColor highlightColor_(Tone tone) {
        switch (tone) {
        case Tone::Success: return SwColor{149, 214, 191};
        case Tone::Warning: return SwColor{242, 203, 146};
        case Tone::Danger: return SwColor{233, 167, 167};
        case Tone::Neutral:
        default: return SwColor{157, 170, 178};
        }
    }

    void initDefaults_() {
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setCursor(CursorType::Arrow);
        setMinimumSize(m_diameter + 4, m_diameter + 4);
        setStyleSheet(R"(
            SwStatusLight {
                background-color: rgba(0, 0, 0, 0);
                border-width: 0px;
            }
        )");
    }

    Tone m_tone = Tone::Neutral;
    int m_diameter = 12;
};
