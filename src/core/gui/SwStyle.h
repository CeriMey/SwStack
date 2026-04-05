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
 * @file src/core/gui/SwStyle.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwStyle in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the style interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are WidgetState, WidgetStateHelper, WidgetStyle, and
 * SwStyle.
 *
 * Style-oriented declarations here capture reusable visual parameters so rendering code can stay
 * deterministic while still allowing higher-level customization.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
 *
 */


/***************************************************************************************************
 * Platform-agnostic styling helpers.
 **************************************************************************************************/

#include <string>
#include <vector>
#include <algorithm>
#include <algorithm>
#include <sstream>

#include "Sw.h"
#include "SwPainter.h"
#include "SwString.h"
#include "SwWidgetInterface.h"
#include "StyleSheet.h"

enum class WidgetState {
    Normal = 0x00,
    Hovered = 0x01,
    Pressed = 0x02,
    Disabled = 0x04,
    Focused = 0x08,
    Checked = 0x10
};

class WidgetStateHelper {
public:
    /**
     * @brief Returns whether the object reports state.
     * @param state Value passed to the method.
     * @param flag Value passed to the method.
     * @return The requested state.
     *
     * @details This query does not modify the object state.
     */
    static bool isState(WidgetState state, WidgetState flag) {
        return (static_cast<int>(state) & static_cast<int>(flag)) != 0;
    }

    /**
     * @brief Sets the state.
     * @param state Value passed to the method.
     * @param flag Value passed to the method.
     * @return The requested state.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    static WidgetState setState(WidgetState state, WidgetState flag) {
        return static_cast<WidgetState>(static_cast<int>(state) | static_cast<int>(flag));
    }

    /**
     * @brief Clears the current object state.
     * @param state Value passed to the method.
     * @param flag Value passed to the method.
     * @return The requested state.
     */
    static WidgetState clearState(WidgetState state, WidgetState flag) {
        return static_cast<WidgetState>(static_cast<int>(state) & ~static_cast<int>(flag));
    }
};

enum class WidgetStyle {
    WidgetStyle,
    PushButtonStyle,
    LineEditStyle,
    CheckBoxStyle,
    RadioButtonStyle,
    LabelStyle,
    ComboBoxStyle,
    SpinBoxStyle,
    ProgressBarStyle,
    SliderStyle,
    TextEditStyle,
    ScrollBarStyle,
    ToolButtonStyle,
    TabWidgetStyle,
    ListViewStyle,
    TableViewStyle,
    TreeViewStyle,
    DialogStyle,
    MainWindowStyle,
    StatusBarStyle,
    MenuBarStyle,
    ToolBarStyle,
    SplitterStyle,
    ScrollAreaStyle,
    GroupBoxStyle,
    CalendarStyle,
    MessageBoxStyle
};

class SwStyle {
public:
    /**
     * @brief Constructs a `SwStyle` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwStyle()
        : normalColor{243, 243, 243},
          hoverColor{229, 241, 251},
          pressedColor{204, 228, 247},
          borderColor{173, 173, 173},
          textColor{18, 18, 18} {}

    /**
     * @brief Performs the `drawControl` operation.
     * @param style Value passed to the method.
     * @param rect Rectangle used by the operation.
     * @param painter Value passed to the method.
     * @param widget Widget associated with the operation.
     * @param state Value passed to the method.
     */
    void drawControl(WidgetStyle style,
                     const SwRect& rect,
                     SwPainter* painter,
                     SwWidgetInterface* widget,
                     WidgetState state) {
        if (!widget || !painter) {
            return;
        }

        SwColor fill = chooseFillColor(style, state);
        SwColor border = borderColor;
        SwColor text = textColor;
        int borderWidth = 1;
        int radius = (style == WidgetStyle::PushButtonStyle) ? 4 : 0;
        bool paintBackground = true;
        float backgroundAlpha = 1.0f;
        Padding padding{};
        SwFont font = widget->getFont();

        if (style == WidgetStyle::LabelStyle) {
            // Labels are transparent by default; they only paint a background when styled explicitly.
            borderWidth = 0;
            paintBackground = false;
            text = SwColor{32, 32, 32};
        }
        const unsigned int stateFlags = toStyleSheetStateFlags(state);
        widget->resolveStyledBackground(stateFlags, fill, backgroundAlpha, paintBackground);
        widget->resolveStyledBorder(stateFlags, border, borderWidth, radius);
        text = widget->resolveStyledTextColor(stateFlags, text);
        if (borderWidth == 0) {
            border = fill;
        }

        const StyleSheet::BoxEdges edges = widget->resolveStyledPadding(stateFlags);
        padding.top = edges.top;
        padding.right = edges.right;
        padding.bottom = edges.bottom;
        padding.left = edges.left;
        font = widget->resolveStyledFont(stateFlags);

        const bool explicitStateFill = widget->hasExplicitStyledStateProperty(stateFlags, "background-color");
        const bool explicitStateBorder = widget->hasExplicitStyledStateProperty(stateFlags, "border-color");
        const bool explicitStateText = widget->hasExplicitStyledStateProperty(stateFlags, "color");
        if (style == WidgetStyle::PushButtonStyle && !explicitStateFill && !explicitStateBorder && !explicitStateText) {
            applyNativePushButtonPalette_(state, fill, border, text);
        } else if (!explicitStateFill) {
            if (WidgetStateHelper::isState(state, WidgetState::Pressed)) {
                fill = darkenColor(fill, 30);
            } else {
                if (WidgetStateHelper::isState(state, WidgetState::Checked)) {
                    fill = darkenColor(fill, 16);
                }
                if (WidgetStateHelper::isState(state, WidgetState::Hovered)) {
                    fill = lightenColor(fill, 20);
                }
            }
        }

        StyleSheet* sheet = widget->getToolSheet();
        const SwString boxShadowValue = widget->resolveStyledBoxShadow(stateFlags);
        BoxShadow shadow = parseBoxShadow(boxShadowValue, sheet);
        if (shadow.valid) {
            SwRect shadowRect = rect;
            shadowRect.x += shadow.offsetX - shadow.spread;
            shadowRect.y += shadow.offsetY - shadow.spread;
            shadowRect.width += shadow.spread * 2;
            shadowRect.height += shadow.spread * 2;
            SwColor shadowColor = applyAlpha(shadow.color, shadow.alpha);
            int shadowRadius = std::max(0, radius + shadow.spread);
            painter->fillRoundedRect(shadowRect, shadowRadius, shadowColor, shadowColor, 0);
        }

        if (paintBackground) {
            if (radius > 0) {
                painter->fillRoundedRect(rect, radius, fill, border, borderWidth);
            } else {
                painter->fillRect(rect, fill, border, borderWidth);
            }
        } else if (borderWidth > 0) {
            painter->drawRect(rect, border, borderWidth);
        }

        const int contentBorder = std::max(0, borderWidth);
        SwRect textRect = rect;
        textRect.x += contentBorder + padding.left;
        textRect.y += contentBorder + padding.top;
        textRect.width = std::max(0, textRect.width - (contentBorder * 2) - padding.left - padding.right);
        textRect.height = std::max(0, textRect.height - (contentBorder * 2) - padding.top - padding.bottom);

        SwString widgetText;
        if (widget->propertyExist("Text")) {
            widgetText = widget->property("Text").get<SwString>();
        } else if (widget->propertyExist("DisplayText")) {
            widgetText = widget->property("DisplayText").get<SwString>();
        } else if (widget->propertyExist("Placeholder")) {
            widgetText = widget->property("Placeholder").get<SwString>();
        }

        DrawTextFormats alignment(DrawTextFormat::Left | DrawTextFormat::VCenter);
        if (widget->propertyExist("Alignment")) {
            alignment = widget->property("Alignment").get<DrawTextFormats>();
        }

        if (!widgetText.isEmpty()) {
            painter->drawText(textRect, widgetText, alignment, text, font);
        }

        painter->finalize();
    }

    /**
     * @brief Performs the `drawBackground` operation.
     * @param rect Rectangle used by the operation.
     * @param painter Value passed to the method.
     * @param color Value passed to the method.
     * @param borderless Value passed to the method.
     */
    void drawBackground(const SwRect& rect, SwPainter* painter, const SwColor& color, bool borderless = true) {
        if (!painter) {
            return;
        }
        painter->fillRect(rect, color, borderless ? color : borderColor, borderless ? 0 : 1);
        painter->finalize();
    }

private:
    static unsigned int toStyleSheetStateFlags(WidgetState state) {
        unsigned int flags = StyleSheet::StateNone;
        if (WidgetStateHelper::isState(state, WidgetState::Hovered)) flags |= StyleSheet::StateHovered;
        if (WidgetStateHelper::isState(state, WidgetState::Pressed)) flags |= StyleSheet::StatePressed;
        if (WidgetStateHelper::isState(state, WidgetState::Disabled)) flags |= StyleSheet::StateDisabled;
        if (WidgetStateHelper::isState(state, WidgetState::Focused)) flags |= StyleSheet::StateFocused;
        if (WidgetStateHelper::isState(state, WidgetState::Checked)) flags |= StyleSheet::StateChecked;
        return flags;
    }

    static int pixelSizeToPointSize(int pixelSize) {
        return std::max(1, (pixelSize * 72 + 48) / 96);
    }

    SwColor chooseFillColor(WidgetStyle style, WidgetState state) const {
        if (WidgetStateHelper::isState(state, WidgetState::Pressed)) {
            return pressedColor;
        }
        if (WidgetStateHelper::isState(state, WidgetState::Hovered)) {
            return hoverColor;
        }
        return normalColor;
    }

    void overrideColor(const SwString& value, SwColor& target, StyleSheet* sheet, float* alphaOut = nullptr) {
        if (!value.isEmpty()) {
            float alpha = 1.0f;
            target = sheet->parseColor(value, &alpha);
            if (alphaOut) {
                *alphaOut = alpha;
            }
        }
    }

    static SwColor lightenColor(const SwColor& color, int amount) {
        SwColor result = color;
        result.r = (std::min)(255, result.r + amount);
        result.g = (std::min)(255, result.g + amount);
        result.b = (std::min)(255, result.b + amount);
        return result;
    }

    static SwColor darkenColor(const SwColor& color, int amount) {
        SwColor result = color;
        result.r = (std::max)(0, result.r - amount);
        result.g = (std::max)(0, result.g - amount);
        result.b = (std::max)(0, result.b - amount);
        return result;
    }

    static void applyNativePushButtonPalette_(WidgetState state, SwColor& fill, SwColor& border, SwColor& text) {
        fill = SwColor{243, 243, 243};
        border = SwColor{173, 173, 173};
        text = SwColor{18, 18, 18};

        if (WidgetStateHelper::isState(state, WidgetState::Disabled)) {
            fill = SwColor{244, 244, 244};
            border = SwColor{205, 205, 205};
            text = SwColor{122, 122, 122};
            return;
        }

        if (WidgetStateHelper::isState(state, WidgetState::Pressed)) {
            fill = SwColor{204, 228, 247};
            border = SwColor{0, 84, 153};
            return;
        }

        if (WidgetStateHelper::isState(state, WidgetState::Checked)) {
            fill = SwColor{214, 231, 248};
            border = SwColor{0, 120, 215};
            return;
        }

        if (WidgetStateHelper::isState(state, WidgetState::Hovered)) {
            fill = SwColor{229, 241, 251};
            border = SwColor{0, 120, 215};
        }
    }

    struct BoxShadow {
        int offsetX{0};
        int offsetY{0};
        int blur{0};
        int spread{0};
        SwColor color{0, 0, 0};
        float alpha{1.0f};
        bool valid{false};
    };

    static SwColor applyAlpha(const SwColor& c, float alpha) {
        float a = (std::max)(0.0f, (std::min)(1.0f, alpha));
        return SwColor{
            static_cast<int>(c.r * a),
            static_cast<int>(c.g * a),
            static_cast<int>(c.b * a)};
    }

    struct Padding {
        int top{0};
        int right{0};
        int bottom{0};
        int left{0};
    };

    static int parsePixelValue(const SwString& value, int defaultValue) {
        if (value.isEmpty()) {
            return defaultValue;
        }
        std::string s = value.toStdString();
        // Remove "px" or other trailing characters
        size_t pos = s.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        try {
            return std::stoi(s);
        } catch (...) {
            return defaultValue;
        }
    }

    static Padding parsePadding(const SwString& value) {
        Padding padding;
        if (value.isEmpty()) {
            return padding;
        }

        std::vector<std::string> tokens;
        std::istringstream ss(value.toStdString());
        std::string token;
        while (ss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) {
            return padding;
        }

        auto resolve = [](const std::string& t) -> int {
            std::string copy = t;
            size_t pos = copy.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%");
            if (pos != std::string::npos) {
                copy = copy.substr(0, pos);
            }
            try {
                return std::stoi(copy);
            } catch (...) {
                return 0;
            }
        };

        if (tokens.size() == 1) {
            int v = resolve(tokens[0]);
            padding.top = padding.right = padding.bottom = padding.left = v;
        } else if (tokens.size() == 2) {
            int v1 = resolve(tokens[0]);
            int v2 = resolve(tokens[1]);
            padding.top = padding.bottom = v1;
            padding.left = padding.right = v2;
        } else if (tokens.size() == 3) {
            int v1 = resolve(tokens[0]);
            int v2 = resolve(tokens[1]);
            int v3 = resolve(tokens[2]);
            padding.top = v1;
            padding.left = padding.right = v2;
            padding.bottom = v3;
        } else {
            int v1 = resolve(tokens[0]);
            int v2 = resolve(tokens[1]);
            int v3 = resolve(tokens[2]);
            int v4 = resolve(tokens[3]);
            padding.top = v1;
            padding.right = v2;
            padding.bottom = v3;
            padding.left = v4;
        }

        return padding;
    }

    BoxShadow parseBoxShadow(const SwString& value, StyleSheet* sheet) const {
        BoxShadow shadow;
        if (value.isEmpty()) {
            return shadow;
        }
        std::string s = value.toStdString();
        int colorPos = -1;
        const char* markers[] = {"rgba(", "rgb(", "#"};
        for (auto marker : markers) {
            int pos = static_cast<int>(s.find(marker));
            if (pos != -1) {
                colorPos = pos;
                break;
            }
        }

        std::string numericPart = (colorPos >= 0) ? s.substr(0, static_cast<size_t>(colorPos)) : s;
        std::string colorPart = (colorPos >= 0) ? s.substr(static_cast<size_t>(colorPos)) : "";

        auto stripPx = [](const std::string& token) -> int {
            std::string cleaned = token;
            size_t pos = cleaned.find("px");
            if (pos != std::string::npos) {
                cleaned = cleaned.substr(0, pos);
            }
            try {
                return std::stoi(cleaned);
            } catch (...) {
                return 0;
            }
        };

        std::stringstream ss(numericPart);
        std::string tok;
        std::vector<int> parts;
        while (ss >> tok) {
            parts.push_back(stripPx(tok));
        }

        if (parts.size() >= 2) {
            shadow.offsetX = parts[0];
            shadow.offsetY = parts[1];
            if (parts.size() >= 3) {
                shadow.blur = parts[2];
            }
            if (parts.size() >= 4) {
                shadow.spread = parts[3];
            }
            shadow.valid = true;
        }

        if (!colorPart.empty()) {
            float alpha = 1.0f;
            shadow.color = sheet->parseColor(colorPart, &alpha);
            shadow.alpha = alpha;
            shadow.valid = true;
        }

        return shadow;
    }

    SwColor normalColor;
    SwColor hoverColor;
    SwColor pressedColor;
    SwColor borderColor;
    SwColor textColor;
};
