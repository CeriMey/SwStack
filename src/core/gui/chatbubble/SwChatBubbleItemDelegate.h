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

#include "SwStyledItemDelegate.h"

#include "chatbubble/SwChatBubble.h"

struct SwChatBubbleColumns {
    int text{0};
    int role{1};
    int kind{2};
    int meta{3};
    int reaction{4};
};

class SwChatBubbleItemDelegate : public SwStyledItemDelegate {
    SW_OBJECT(SwChatBubbleItemDelegate, SwStyledItemDelegate)

public:
    explicit SwChatBubbleItemDelegate(SwObject* parent = nullptr)
        : SwStyledItemDelegate(parent)
        , m_theme(swChatBubbleWhatsAppTheme()) {}

    void setTheme(const SwChatBubbleTheme& theme) { m_theme = theme; }
    const SwChatBubbleTheme& theme() const { return m_theme; }

    void setColumns(const SwChatBubbleColumns& columns) { m_columns = columns; }
    SwChatBubbleColumns columns() const { return m_columns; }

    void setDefaultImage(const SwImage& image) { m_defaultImage = image; }
    const SwImage& defaultImage() const { return m_defaultImage; }

    SwSize sizeHint(const SwStyleOptionViewItem& option,
                    const SwModelIndex& index) const override {
        if (!index.isValid() || !index.model()) {
            return SwSize{0, 0};
        }
        SwChatBubbleMessage msg = messageForIndex_(index);
        if (msg.kind == SwChatMessageKind::Image && (!msg.image || msg.image->isNull()) && !m_defaultImage.isNull()) {
            msg.image = &m_defaultImage;
        }
        const int rowW = option.rect.width > 0 ? option.rect.width : 800;
        return SwChatBubble::sizeHintForRow(rowW, msg, m_theme);
    }

    void paint(SwPainter* painter,
               const SwStyleOptionViewItem& option,
               const SwModelIndex& index) const override {
        if (!painter || !index.isValid() || !index.model()) {
            return;
        }
        SwChatBubbleMessage msg = messageForIndex_(index);
        if (msg.kind == SwChatMessageKind::Image && (!msg.image || msg.image->isNull()) && !m_defaultImage.isNull()) {
            msg.image = &m_defaultImage;
        }
        SwChatBubble::paintRow(painter, option.rect, msg, m_theme);
    }

protected:
    virtual SwChatBubbleRole bubbleRoleForIndex(const SwModelIndex& index) const {
        if (!index.isValid() || !index.model()) {
            return SwChatBubbleRole::Bot;
        }
        const int r = index.row();
        const int roleCol = m_columns.role;
        if (roleCol < 0 || roleCol >= index.model()->columnCount()) {
            return SwChatBubbleRole::Bot;
        }
        const SwString role = index.model()->data(index.model()->index(r, roleCol), SwItemDataRole::DisplayRole).toString();
        if (role == "out" || role == "user" || role == "me") {
            return SwChatBubbleRole::User;
        }
        return SwChatBubbleRole::Bot;
    }

private:
    SwChatBubbleMessage messageForIndex_(const SwModelIndex& index) const {
        SwChatBubbleMessage msg;
        if (!index.isValid() || !index.model()) {
            return msg;
        }

        const int r = index.row();
        const int cols = index.model()->columnCount();

        // Text
        if (m_columns.text >= 0 && m_columns.text < cols) {
            msg.text = index.model()->data(index.model()->index(r, m_columns.text), SwItemDataRole::DisplayRole).toString();
        }

        // Role
        msg.role = bubbleRoleForIndex(index);

        // Kind
        if (m_columns.kind >= 0 && m_columns.kind < cols) {
            const SwString kind = index.model()->data(index.model()->index(r, m_columns.kind), SwItemDataRole::DisplayRole).toString();
            msg.kind = (kind == "image") ? SwChatMessageKind::Image : SwChatMessageKind::Text;
        }

        // Meta (time / edited)
        if (m_columns.meta >= 0 && m_columns.meta < cols) {
            msg.meta = index.model()->data(index.model()->index(r, m_columns.meta), SwItemDataRole::DisplayRole).toString();
        }

        // Reaction
        if (m_columns.reaction >= 0 && m_columns.reaction < cols) {
            msg.reaction = index.model()->data(index.model()->index(r, m_columns.reaction), SwItemDataRole::DisplayRole).toString();
        }

        return msg;
    }

    SwChatBubbleTheme m_theme;
    SwChatBubbleColumns m_columns;
    SwImage m_defaultImage;
};

class SwChatUserItemDelegate : public SwChatBubbleItemDelegate {
    SW_OBJECT(SwChatUserItemDelegate, SwChatBubbleItemDelegate)

public:
    explicit SwChatUserItemDelegate(SwObject* parent = nullptr)
        : SwChatBubbleItemDelegate(parent) {}

protected:
    SwChatBubbleRole bubbleRoleForIndex(const SwModelIndex& index) const override {
        SW_UNUSED(index)
        return SwChatBubbleRole::User;
    }
};

class SwChatBotItemDelegate : public SwChatBubbleItemDelegate {
    SW_OBJECT(SwChatBotItemDelegate, SwChatBubbleItemDelegate)

public:
    explicit SwChatBotItemDelegate(SwObject* parent = nullptr)
        : SwChatBubbleItemDelegate(parent) {}

protected:
    SwChatBubbleRole bubbleRoleForIndex(const SwModelIndex& index) const override {
        SW_UNUSED(index)
        return SwChatBubbleRole::Bot;
    }
};

