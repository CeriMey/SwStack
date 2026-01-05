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
 * SwStyledItemDelegate - Qt-like item delegate base (≈ QStyledItemDelegate).
 *
 * Goal:
 * - Provide a reusable paint/sizeHint hook for model-based views (List/Table/Tree).
 * - Keep the default behaviour simple and "web-ish" (premium, padded, selectable rows).
 **************************************************************************************************/

#include "SwAbstractItemModel.h"
#include "SwObject.h"
#include "SwPainter.h"

#include "graphics/SwFontMetrics.h"

struct SwStyleOptionViewItem {
    SwRect rect{};
    SwFont font{};
    bool enabled{true};
    bool selected{false};
    bool hovered{false};
    bool hasFocus{false};
    bool alternate{false};
};

class SwStyledItemDelegate : public SwObject {
    SW_OBJECT(SwStyledItemDelegate, SwObject)

public:
    explicit SwStyledItemDelegate(SwObject* parent = nullptr)
        : SwObject(parent) {}

    virtual ~SwStyledItemDelegate() = default;

    virtual void paint(SwPainter* painter,
                       const SwStyleOptionViewItem& option,
                       const SwModelIndex& index) const {
        if (!painter || !index.isValid() || !index.model()) {
            return;
        }

        // Background (selection / hover)
        if (option.selected) {
            const SwColor fill{219, 234, 254}; // blue-100
            painter->fillRoundedRect(option.rect, 10, fill, fill, 0);
        } else if (option.hovered) {
            const SwColor fill{248, 250, 252}; // slate-50
            painter->fillRoundedRect(option.rect, 10, fill, fill, 0);
        } else if (option.alternate) {
            const SwColor fill{250, 251, 252}; // very light
            painter->fillRoundedRect(option.rect, 10, fill, fill, 0);
        }

        // Text
        const SwString text = index.model()->data(index, SwItemDataRole::DisplayRole).toString();
        SwRect textRect = option.rect;
        textRect.x += 12;
        textRect.width -= 24;
        if (textRect.width < 0) {
            textRect.width = 0;
        }

        const SwColor textColor = option.selected ? SwColor{30, 64, 175} : SwColor{15, 23, 42};
        painter->drawText(textRect,
                          text,
                          DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          option.font);
    }

    virtual SwSize sizeHint(const SwStyleOptionViewItem& option,
                            const SwModelIndex& index) const {
        if (!index.isValid() || !index.model()) {
            return SwSize{0, 0};
        }
        const SwString text = index.model()->data(index, SwItemDataRole::DisplayRole).toString();
        const SwFontMetrics fm(option.font);
        const int h = fm.height();
        const int w = fm.horizontalAdvance(text);

        const int padX = 24;
        const int padY = 12;
        const int minH = 28;
        const int outW = w + padX;
        const int outH = ((h + padY) > minH) ? (h + padY) : minH;
        return SwSize{outW, outH};
    }
};
