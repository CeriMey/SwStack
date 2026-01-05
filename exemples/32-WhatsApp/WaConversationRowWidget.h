#pragma once

#include "SwWidget.h"

#include "Sw.h"
#include "SwString.h"

class SwLabel;
class WaAvatarCircle;

class WaConversationRowWidget final : public SwWidget {
    SW_OBJECT(WaConversationRowWidget, SwWidget)

public:
    WaConversationRowWidget(const SwString& initial,
                            const SwColor& color,
                            const SwString& title,
                            const SwString& preview,
                            const SwString& timeText,
                            int unreadCount,
                            SwWidget* parent = nullptr);

    void setPreviewText(const SwString& preview);
    void setTimeText(const SwString& timeText);
    void setUnreadCount(int unread);

    SwRect sizeHint() const override;

protected:
    void resizeEvent(ResizeEvent* event) override;

private:
    static int clampInt_(int value, int minValue, int maxValue);
    void updateLayout_();

    WaAvatarCircle* m_avatar{nullptr};
    SwLabel* m_title{nullptr};
    SwLabel* m_preview{nullptr};
    SwLabel* m_time{nullptr};
    SwLabel* m_badge{nullptr};
};
