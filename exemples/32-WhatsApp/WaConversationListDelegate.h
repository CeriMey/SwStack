#pragma once

#include "SwStyledItemDelegate.h"

class WaConversationListDelegate : public SwStyledItemDelegate {
    SW_OBJECT(WaConversationListDelegate, SwStyledItemDelegate)

public:
    explicit WaConversationListDelegate(SwObject* parent = nullptr);

    void paint(SwPainter* painter,
               const SwStyleOptionViewItem& option,
               const SwModelIndex& index) const override;
};

