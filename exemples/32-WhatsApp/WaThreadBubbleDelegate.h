#pragma once

#include "SwStyledItemDelegate.h"

#include "chatbubble/SwChatBubbleTheme.h"
#include "chatbubble/SwChatBubbleTypes.h"
#include "graphics/SwImage.h"

#include <string>
#include <unordered_map>

class WaLocalStore;

struct WaThreadBubbleColumns {
    int text{0};
    int role{1};
    int kind{2};
    int meta{3};
    int reaction{4};
    int payload{5};
    int status{6};
};

class WaThreadBubbleDelegate final : public SwStyledItemDelegate {
    SW_OBJECT(WaThreadBubbleDelegate, SwStyledItemDelegate)

public:
    explicit WaThreadBubbleDelegate(SwObject* parent = nullptr);

    void setStore(WaLocalStore* store) { m_store = store; }
    WaLocalStore* store() const { return m_store; }

    void setTheme(const SwChatBubbleTheme& theme) { m_theme = theme; }
    const SwChatBubbleTheme& theme() const { return m_theme; }

    void setColumns(const WaThreadBubbleColumns& columns) { m_columns = columns; }
    WaThreadBubbleColumns columns() const { return m_columns; }

    void setDefaultImage(const SwImage& image) { m_defaultImage = image; }
    void setDefaultVideo(const SwImage& image) { m_defaultVideo = image; }

    SwSize sizeHint(const SwStyleOptionViewItem& option, const SwModelIndex& index) const override;
    void paint(SwPainter* painter, const SwStyleOptionViewItem& option, const SwModelIndex& index) const override;

private:
    SwChatBubbleRole bubbleRoleForIndex_(const SwModelIndex& index) const;
    SwChatBubbleMessage messageForIndex_(const SwModelIndex& index) const;

    const SwImage* thumbnailFor_(const SwModelIndex& index) const;

    WaLocalStore* m_store{nullptr};
    SwChatBubbleTheme m_theme{swChatBubbleWhatsAppTheme()};
    WaThreadBubbleColumns m_columns;

    SwImage m_defaultImage;
    SwImage m_defaultVideo;

    // Cache thumbnails by resolved absolute path (string).
    mutable std::unordered_map<std::string, SwImage> m_thumbCache;
};
