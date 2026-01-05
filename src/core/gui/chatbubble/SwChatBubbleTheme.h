#pragma once
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

#include "Sw.h"
#include "SwFont.h"

struct SwChatBubbleLayoutConfig {
    // Row spacing inside the list view.
    int rowPaddingY{4};

    // Horizontal margins in the chat viewport.
    int marginXDivisor{28};
    int marginXMin{18};
    int marginXMax{48};

    // Bubble paddings.
    int bubblePaddingX{12};
    int bubblePaddingTop{6};
    int bubblePaddingBottom{14}; // includes meta area (time/ticks)

    // Bubble width constraints.
    int bubbleMinWidth{90};
    int maxBubbleMin{180};
    int maxBubbleMax{620};

    // Image message constraints.
    int imageBubbleMinWidth{260};
    int imageBubbleMaxWidth{440};
    int imageInnerMinWidth{240};
    int imageInnerMaxWidth{420};
    int imageMinHeight{140};
    int imageMaxHeight{260};

    // Reaction pill.
    int reactionExtraHeight{26};
};

struct SwChatBubbleStyle {
    SwColor bubbleFill{255, 255, 255};
    SwColor bubbleBorder{255, 255, 255};
    int bubbleBorderWidth{0};
    int bubbleRadius{10};

    bool showTail{true};
    int tailWidth{10};
    int tailHeight{14};

    SwColor textColor{17, 27, 33};
    SwColor metaColor{102, 119, 129};

    bool showTicks{false};
    SwColor tickColor{83, 189, 235};

    SwFont messageFont{L"Segoe UI", 10, Normal};
    SwFont metaFont{L"Segoe UI", 8, Normal};

    // Reaction pill
    SwColor reactionFill{255, 255, 255};
    SwColor reactionBorder{233, 237, 239};
    SwColor reactionTextColor{17, 27, 33};
    SwFont reactionFont{L"Segoe UI Emoji", 9, Normal};
};

struct SwChatBubbleTheme {
    SwChatBubbleLayoutConfig layout;
    SwChatBubbleStyle user;
    SwChatBubbleStyle bot;
};

inline SwChatBubbleTheme swChatBubbleWhatsAppTheme() {
    SwChatBubbleTheme theme;

    // WhatsApp-ish colors.
    theme.user.bubbleFill = SwColor{217, 253, 211}; // #d9fdd3
    theme.user.bubbleBorder = theme.user.bubbleFill;
    theme.user.bubbleBorderWidth = 0;
    theme.user.bubbleRadius = 10;
    theme.user.showTail = true;
    theme.user.showTicks = true;
    theme.user.tickColor = SwColor{83, 189, 235};

    theme.bot.bubbleFill = SwColor{255, 255, 255};
    theme.bot.bubbleBorder = theme.bot.bubbleFill;
    theme.bot.bubbleBorderWidth = 0;
    theme.bot.bubbleRadius = 10;
    theme.bot.showTail = true;
    theme.bot.showTicks = false;

    // Tail geometry (curved triangle).
    theme.user.tailWidth = 12;
    theme.user.tailHeight = 16;
    theme.bot.tailWidth = 12;
    theme.bot.tailHeight = 16;

    theme.user.reactionTextColor = SwColor{239, 68, 68};
    theme.bot.reactionTextColor = SwColor{239, 68, 68};

    return theme;
}
