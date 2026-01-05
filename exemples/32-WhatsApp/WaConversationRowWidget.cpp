#include "WaConversationRowWidget.h"

#include "SwFont.h"
#include "SwLabel.h"

#include "WaAvatarCircle.h"

WaConversationRowWidget::WaConversationRowWidget(const SwString& initial,
                                                 const SwColor& color,
                                                 const SwString& title,
                                                 const SwString& preview,
                                                 const SwString& timeText,
                                                 int unreadCount,
                                                 SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    setFocusPolicy(FocusPolicyEnum::NoFocus);

    m_avatar = new WaAvatarCircle(initial, color, this);

    m_title = new SwLabel(title, this);
    m_title->setFont(SwFont(L"Segoe UI", 11, SemiBold));
    m_title->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(17, 27, 33); font-size: 15px; }");

    m_preview = new SwLabel(preview, this);
    m_preview->setFont(SwFont(L"Segoe UI", 10, Normal));
    m_preview->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(102, 119, 129); font-size: 13px; }");

    m_time = new SwLabel(timeText, this);
    m_time->setFont(SwFont(L"Segoe UI", 9, Normal));
    m_time->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(102, 119, 129); font-size: 12px; }");
    m_time->setAlignment(DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));

    m_badge = new SwLabel("", this);
    m_badge->setAlignment(DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine));
    m_badge->setStyleSheet(R"(
        SwLabel {
            background-color: rgb(37, 211, 102);
            border-width: 0px;
            border-radius: 10px;
            color: rgb(255, 255, 255);
            font-size: 11px;
            padding: 0px;
        }
    )");

    setUnreadCount(unreadCount);
    resize(420, 72);
}

void WaConversationRowWidget::setPreviewText(const SwString& preview) {
    if (!m_preview) {
        return;
    }
    m_preview->setText(preview);
}

void WaConversationRowWidget::setTimeText(const SwString& timeText) {
    if (!m_time) {
        return;
    }
    m_time->setText(timeText);
}

void WaConversationRowWidget::setUnreadCount(int unread) {
    if (!m_badge) {
        return;
    }
    if (unread <= 0) {
        if (m_time) {
            m_time->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(102, 119, 129); font-size: 12px; }");
        }
        m_badge->setVisible(false);
        return;
    }
    if (m_time) {
        m_time->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(0, 168, 132); font-size: 12px; }");
    }
    m_badge->setVisible(true);
    m_badge->setText(SwString::number(unread));
}

SwRect WaConversationRowWidget::sizeHint() const {
    return SwRect{0, 0, 10000, 72};
}

void WaConversationRowWidget::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

int WaConversationRowWidget::clampInt_(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void WaConversationRowWidget::updateLayout_() {
    const SwRect r = getRect();
    const int w = r.width;
    const int h = r.height;

    const int padX = 14;
    const int avatarSize = 48;

    if (m_avatar) {
        m_avatar->move(r.x + padX, r.y + (h - avatarSize) / 2);
        m_avatar->resize(avatarSize, avatarSize);
    }

    int right = r.x + w - padX;

    const int timeW = 66;
    if (m_time) {
        m_time->move(right - timeW, r.y + 12);
        m_time->resize(timeW, 18);
    }

    if (m_badge) {
        const int badgeW = 22;
        const int badgeH = 20;
        m_badge->move(right - badgeW, r.y + h - 12 - badgeH);
        m_badge->resize(badgeW, badgeH);
    }

    const int leftText = r.x + padX + avatarSize + 12;
    const int textW = clampInt_((right - timeW - 12) - leftText, 0, 10000);

    if (m_title) {
        m_title->move(leftText, r.y + 10);
        m_title->resize(textW, 22);
    }
    if (m_preview) {
        m_preview->move(leftText, r.y + 34);
        m_preview->resize(textW, 18);
    }
}
