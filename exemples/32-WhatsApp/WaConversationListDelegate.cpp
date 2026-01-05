#include "WaConversationListDelegate.h"

#include "SwPainter.h"

WaConversationListDelegate::WaConversationListDelegate(SwObject* parent)
    : SwStyledItemDelegate(parent) {}

void WaConversationListDelegate::paint(SwPainter* painter,
                                       const SwStyleOptionViewItem& option,
                                       const SwModelIndex& index) const {
    SW_UNUSED(index)
    if (!painter) {
        return;
    }

    // WhatsApp-like row backgrounds.
    if (option.selected) {
        const SwColor fill{233, 237, 239}; // #e9edef
        painter->fillRect(option.rect, fill, fill, 0);
    } else if (option.hovered) {
        const SwColor fill{245, 246, 246}; // subtle hover
        painter->fillRect(option.rect, fill, fill, 0);
    }

    // Divider (skip the avatar area to avoid visual clutter)
    const SwColor divider{233, 237, 239};
    const int y = option.rect.y + option.rect.height - 1;
    painter->drawLine(option.rect.x + 76, y, option.rect.x + option.rect.width, y, divider, 1);
}

