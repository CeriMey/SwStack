#include "WaThreadBubbleDelegate.h"

#include "WaDemoAssets.h"
#include "WaLocalStore.h"
#include "WaMediaImageLoader.h"

#include "chatbubble/SwChatBubble.h"

#include <algorithm>

WaThreadBubbleDelegate::WaThreadBubbleDelegate(SwObject* parent)
    : SwStyledItemDelegate(parent) {}

SwChatBubbleRole WaThreadBubbleDelegate::bubbleRoleForIndex_(const SwModelIndex& index) const {
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

SwChatBubbleMessage WaThreadBubbleDelegate::messageForIndex_(const SwModelIndex& index) const {
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
    msg.role = bubbleRoleForIndex_(index);

    // Kind
    SwString kind;
    if (m_columns.kind >= 0 && m_columns.kind < cols) {
        kind = index.model()->data(index.model()->index(r, m_columns.kind), SwItemDataRole::DisplayRole).toString();
    }
    if (kind == "image" || kind == "video") {
        msg.kind = SwChatMessageKind::Image;
    } else {
        msg.kind = SwChatMessageKind::Text;
    }

    // Meta
    if (m_columns.meta >= 0 && m_columns.meta < cols) {
        msg.meta = index.model()->data(index.model()->index(r, m_columns.meta), SwItemDataRole::DisplayRole).toString();
    }

    // Reaction
    if (m_columns.reaction >= 0 && m_columns.reaction < cols) {
        msg.reaction = index.model()->data(index.model()->index(r, m_columns.reaction), SwItemDataRole::DisplayRole).toString();
    }

    // Status (only meaningful for outgoing messages; still safe to set for all rows)
    if (m_columns.status >= 0 && m_columns.status < cols) {
        const SwString status =
            index.model()->data(index.model()->index(r, m_columns.status), SwItemDataRole::DisplayRole).toString().toLower();
        if (status == "sent") {
            msg.status = SwChatMessageStatus::Sent;
        } else if (status == "delivered") {
            msg.status = SwChatMessageStatus::Delivered;
        } else if (status == "read") {
            msg.status = SwChatMessageStatus::Read;
        } else {
            msg.status = SwChatMessageStatus::Unset;
        }
    }

    return msg;
}

const SwImage* WaThreadBubbleDelegate::thumbnailFor_(const SwModelIndex& index) const {
    if (!index.isValid() || !index.model()) {
        return nullptr;
    }

    const int r = index.row();
    const int cols = index.model()->columnCount();
    if (m_columns.kind < 0 || m_columns.kind >= cols || m_columns.payload < 0 || m_columns.payload >= cols) {
        return nullptr;
    }

    const SwString kind = index.model()->data(index.model()->index(r, m_columns.kind), SwItemDataRole::DisplayRole).toString();
    const SwString payload = index.model()->data(index.model()->index(r, m_columns.payload), SwItemDataRole::DisplayRole).toString();

    if (kind == "video") {
        if (m_defaultVideo.isNull()) {
            // Lazily initialize a default 16:9 thumb.
            const_cast<WaThreadBubbleDelegate*>(this)->m_defaultVideo = WaDemoAssets::makeVideoThumb(420, 236, 16);
        }
        return m_defaultVideo.isNull() ? nullptr : &m_defaultVideo;
    }

    if (kind != "image") {
        return nullptr;
    }

    if (payload.isEmpty()) {
        if (m_defaultImage.isNull()) {
            const_cast<WaThreadBubbleDelegate*>(this)->m_defaultImage = WaDemoAssets::makeFakeScreenshotThumb(420, 236, 16);
        }
        return m_defaultImage.isNull() ? nullptr : &m_defaultImage;
    }

    SwString abs = payload;
    if (m_store) {
        abs = m_store->resolveMediaPath(payload);
    }

    const std::string key = abs.toStdString();
    auto it = m_thumbCache.find(key);
    if (it != m_thumbCache.end()) {
        return it->second.isNull() ? nullptr : &it->second;
    }

    SwImage thumb = WaMediaImageLoader::loadThumbnail(abs, 420, 236);
    if (thumb.isNull()) {
        thumb = WaDemoAssets::makeFakeScreenshotThumb(420, 236, 16);
    }

    m_thumbCache.emplace(key, std::move(thumb));
    it = m_thumbCache.find(key);
    if (it == m_thumbCache.end()) {
        return nullptr;
    }
    return it->second.isNull() ? nullptr : &it->second;
}

SwSize WaThreadBubbleDelegate::sizeHint(const SwStyleOptionViewItem& option, const SwModelIndex& index) const {
    if (!index.isValid() || !index.model()) {
        return SwSize{0, 0};
    }
    const int rowW = option.rect.width > 0 ? option.rect.width : 800;
    SwChatBubbleMessage msg = messageForIndex_(index);
    return SwChatBubble::sizeHintForRow(rowW, msg, m_theme);
}

void WaThreadBubbleDelegate::paint(SwPainter* painter, const SwStyleOptionViewItem& option, const SwModelIndex& index) const {
    if (!painter || !index.isValid() || !index.model()) {
        return;
    }

    SwChatBubbleMessage msg = messageForIndex_(index);
    if (msg.kind == SwChatMessageKind::Image) {
        msg.image = thumbnailFor_(index);
        if (!msg.image || msg.image->isNull()) {
            msg.image = m_defaultImage.isNull() ? nullptr : &m_defaultImage;
        }
    }

    SwChatBubble::paintRow(painter, option.rect, msg, m_theme);
}
