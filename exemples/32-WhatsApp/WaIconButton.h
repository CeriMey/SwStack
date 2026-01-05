#pragma once

#include "SwWidget.h"

class WaIconButton final : public SwWidget {
    SW_OBJECT(WaIconButton, SwWidget)

public:
    enum class Kind {
        Chats,
        Phone,
        Status,
        Settings,
        NewChat,
        Menu,
        Video,
        Search,
        Plus,
        Emoji,
        Mic,
        Send
    };

    explicit WaIconButton(Kind kind, SwWidget* parent = nullptr);

    void setKind(Kind kind);
    Kind kind() const { return m_kind; }

    void setActive(bool on);
    bool isActive() const { return m_active; }

    void setBadgeCount(int count);
    int badgeCount() const { return m_badgeCount; }

signals:
    DECLARE_SIGNAL_VOID(clicked);

protected:
    void paintEvent(PaintEvent* event) override;
    void mousePressEvent(MouseEvent* event) override;
    void mouseReleaseEvent(MouseEvent* event) override;

private:
    static int clampInt_(int value, int minValue, int maxValue);
    static void paintIcon_(SwPainter* painter, const SwRect& r, Kind kind, const SwColor& color, const SwColor& bg);

    Kind m_kind{Kind::Menu};
    bool m_active{false};
    int m_badgeCount{0};
    bool m_pressed{false};
};
