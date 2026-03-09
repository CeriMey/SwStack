#pragma once

/**
 * @file src/core/gui/SwSpacer.h
 * @ingroup core_gui
 * @brief Declares the design/runtime helper widget exposed by SwSpacer in the CoreSw GUI layer.
 */

#include "SwWidget.h"

class SwSpacer : public SwWidget {
    SW_OBJECT(SwSpacer, SwWidget)

    PROPERTY(SwString, Orientation, SwString("Horizontal"))
    PROPERTY(SwString, HorizontalPolicy, SwString("Expanding"))
    PROPERTY(SwString, VerticalPolicy, SwString("Minimum"))
    PROPERTY(int, SizeHintWidth, 26)
    PROPERTY(int, SizeHintHeight, 20)

public:
    enum class Direction {
        Horizontal,
        Vertical
    };

    explicit SwSpacer(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        init_();
    }

    explicit SwSpacer(Direction direction, SwWidget* parent = nullptr)
        : SwWidget(parent) {
        init_();
        setDirection(direction);
    }

    void setDirection(Direction direction) {
        setOrientation(direction == Direction::Horizontal ? SwString("Horizontal") : SwString("Vertical"));
        if (direction == Direction::Horizontal) {
            setHorizontalPolicy(policyToString(SwSizePolicy::Expanding));
            setVerticalPolicy(policyToString(SwSizePolicy::Minimum));
            if (getSizeHintWidth() <= 0) {
                setSizeHintWidth(26);
            }
            if (getSizeHintHeight() <= 0) {
                setSizeHintHeight(20);
            }
        } else {
            setHorizontalPolicy(policyToString(SwSizePolicy::Minimum));
            setVerticalPolicy(policyToString(SwSizePolicy::Expanding));
            if (getSizeHintWidth() <= 0) {
                setSizeHintWidth(20);
            }
            if (getSizeHintHeight() <= 0) {
                setSizeHintHeight(26);
            }
        }
    }

    Direction direction() const {
        const SwString value = getOrientation().trimmed().toLower();
        return value == "vertical" ? Direction::Vertical : Direction::Horizontal;
    }

    void setSizePolicy(const SwSizePolicy& policy) {
        setHorizontalPolicy(policyToString(policy.horizontalPolicy()));
        setVerticalPolicy(policyToString(policy.verticalPolicy()));
    }

    SwSizePolicy sizePolicy() const {
        return SwSizePolicy(policyFromString(getHorizontalPolicy()), policyFromString(getVerticalPolicy()));
    }

    void changeSize(int width,
                    int height,
                    SwSizePolicy::Policy horizontalPolicy = SwSizePolicy::Minimum,
                    SwSizePolicy::Policy verticalPolicy = SwSizePolicy::Minimum) {
        setSizeHintWidth(std::max(0, width));
        setSizeHintHeight(std::max(0, height));
        setHorizontalPolicy(policyToString(horizontalPolicy));
        setVerticalPolicy(policyToString(verticalPolicy));
        update();
    }

    SwSize sizeHint() const override {
        return {std::max(0, getSizeHintWidth()), std::max(0, getSizeHintHeight())};
    }

    SwSize minimumSizeHint() const override {
        SwSpacerItem item(getSizeHintWidth(),
                          getSizeHintHeight(),
                          policyFromString(getHorizontalPolicy()),
                          policyFromString(getVerticalPolicy()));
        return item.minimumSize();
    }

    static SwString policyToString(SwSizePolicy::Policy policy) {
        switch (policy) {
        case SwSizePolicy::Fixed:
            return SwString("Fixed");
        case SwSizePolicy::Minimum:
            return SwString("Minimum");
        case SwSizePolicy::Maximum:
            return SwString("Maximum");
        case SwSizePolicy::Preferred:
            return SwString("Preferred");
        case SwSizePolicy::MinimumExpanding:
            return SwString("MinimumExpanding");
        case SwSizePolicy::Expanding:
            return SwString("Expanding");
        case SwSizePolicy::Ignored:
            return SwString("Ignored");
        }
        return SwString("Minimum");
    }

    static SwSizePolicy::Policy policyFromString(SwString value) {
        value = value.trimmed();
        const size_t sep = value.lastIndexOf(':');
        if (sep != static_cast<size_t>(-1)) {
            value = value.mid(static_cast<int>(sep + 1));
        }
        value = value.trimmed();
        if (value == "Fixed") return SwSizePolicy::Fixed;
        if (value == "Minimum") return SwSizePolicy::Minimum;
        if (value == "Maximum") return SwSizePolicy::Maximum;
        if (value == "Preferred") return SwSizePolicy::Preferred;
        if (value == "MinimumExpanding") return SwSizePolicy::MinimumExpanding;
        if (value == "Expanding") return SwSizePolicy::Expanding;
        if (value == "Ignored") return SwSizePolicy::Ignored;
        return SwSizePolicy::Minimum;
    }

private:
    void init_() {
        setStyleSheet("SwSpacer { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setMinimumSize(0, 0);
        setFocusPolicy(FocusPolicyEnum::NoFocus);
        setProperty("__SwCreator_IsSpacer", SwAny(true));
    }
};
