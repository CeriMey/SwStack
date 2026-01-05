#include "WaIconButton.h"

#include "SwFont.h"
#include "SwPainter.h"
#include "SwString.h"

WaIconButton::WaIconButton(Kind kind, SwWidget* parent)
    : SwWidget(parent)
    , m_kind(kind) {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    setFocusPolicy(FocusPolicyEnum::NoFocus);
    setCursor(CursorType::Hand);
    resize(40, 40);
}

void WaIconButton::setKind(Kind kind) {
    if (m_kind == kind) {
        return;
    }
    m_kind = kind;
    update();
}

void WaIconButton::setActive(bool on) {
    if (m_active == on) {
        return;
    }
    m_active = on;
    update();
}

void WaIconButton::setBadgeCount(int count) {
    if (count < 0) {
        count = 0;
    }
    if (m_badgeCount == count) {
        return;
    }
    m_badgeCount = count;
    update();
}

void WaIconButton::paintEvent(PaintEvent* event) {
    if (!isVisibleInHierarchy()) {
        return;
    }
    SwPainter* painter = event ? event->painter() : nullptr;
    if (!painter) {
        return;
    }

    const SwRect r = getRect();
    const int radius = (r.width < r.height ? r.width : r.height) / 2;

    const bool pressed = m_pressed;
    const bool hovered = getHover();

    SwColor surface{255, 255, 255};
    SwColor hoverBg{240, 242, 245};
    SwColor activeBg{233, 237, 239};

    bool paintBg = false;
    SwColor bg = surface;

    SwColor iconColor = m_active ? SwColor{0, 168, 132} : SwColor{84, 101, 111};

    if (m_kind == Kind::Send) {
        paintBg = true;
        iconColor = SwColor{255, 255, 255};
        if (pressed) {
            bg = SwColor{0, 150, 115};
        } else if (hovered) {
            bg = SwColor{0, 160, 125};
        } else {
            bg = SwColor{0, 168, 132};
        }
    } else if (m_active) {
        paintBg = true;
        bg = activeBg;
    } else if (pressed || hovered) {
        paintBg = true;
        bg = hoverBg;
    }

    if (paintBg) {
        painter->fillRoundedRect(r, radius, bg, bg, 0);
    }

    paintIcon_(painter, r, m_kind, iconColor, paintBg ? bg : surface);

    if (m_badgeCount > 0) {
        const int badgeSize = 18;
        SwRect badge{r.x + r.width - badgeSize - 2, r.y + 2, badgeSize, badgeSize};
        const SwColor badgeFill{37, 211, 102};
        painter->fillRoundedRect(badge, badgeSize / 2, badgeFill, badgeFill, 0);
        painter->drawText(badge,
                          SwString::number(m_badgeCount),
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          SwColor{255, 255, 255},
                          SwFont(L"Segoe UI", 8, SemiBold));
    }
}

void WaIconButton::mousePressEvent(MouseEvent* event) {
    if (!event) {
        return;
    }
    if (!isPointInside(event->x(), event->y())) {
        SwWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    update();
    event->accept();
}

void WaIconButton::mouseReleaseEvent(MouseEvent* event) {
    if (!event) {
        return;
    }
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    if (wasPressed && isPointInside(event->x(), event->y())) {
        emit clicked();
        event->accept();
        return;
    }
    SwWidget::mouseReleaseEvent(event);
}

int WaIconButton::clampInt_(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void WaIconButton::paintIcon_(SwPainter* painter, const SwRect& r, Kind kind, const SwColor& color, const SwColor& bg) {
    if (!painter) {
        return;
    }

    const int s = (r.width < r.height ? r.width : r.height);
    const int cx = r.x + r.width / 2;
    const int cy = r.y + r.height / 2;
    const int stroke = 2;

    auto drawDot = [&](int x, int y) {
        SwRect d{x - 2, y - 2, 4, 4};
        painter->fillEllipse(d, color, color, 0);
    };

    switch (static_cast<int>(kind)) {
    default:
    case static_cast<int>(Kind::Menu): {
        drawDot(cx, cy - 6);
        drawDot(cx, cy);
        drawDot(cx, cy + 6);
        break;
    }
    case static_cast<int>(Kind::Search): {
        const int rad = s / 6;
        SwRect circ{cx - rad, cy - rad, rad * 2, rad * 2};
        painter->drawEllipse(circ, color, stroke);
        painter->drawLine(cx + rad - 1, cy + rad - 1, cx + rad + 7, cy + rad + 7, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Plus): {
        const int len = s / 5;
        painter->drawLine(cx, cy - len, cx, cy + len, color, stroke);
        painter->drawLine(cx - len, cy, cx + len, cy, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Emoji): {
        const int rad = s / 5;
        SwRect face{cx - rad, cy - rad, rad * 2, rad * 2};
        painter->drawEllipse(face, color, stroke);
        SwRect eye1{cx - rad / 2 - 3, cy - 3, 4, 4};
        SwRect eye2{cx + rad / 2 - 1, cy - 3, 4, 4};
        painter->fillEllipse(eye1, color, color, 0);
        painter->fillEllipse(eye2, color, color, 0);
        painter->drawLine(cx - rad / 2, cy + rad / 3, cx + rad / 2, cy + rad / 3, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Mic): {
        const int w = s / 4;
        const int h = s / 3;
        SwRect body{cx - w / 2, cy - h / 2, w, h};
        painter->fillRoundedRect(body, w / 2, bg, color, stroke);
        painter->drawLine(cx, cy + h / 2, cx, cy + h / 2 + 6, color, stroke);
        painter->drawLine(cx - 6, cy + h / 2 + 6, cx + 6, cy + h / 2 + 6, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Send): {
        painter->drawLine(cx - 10, cy, cx + 10, cy, color, stroke);
        painter->drawLine(cx + 10, cy, cx + 2, cy - 8, color, stroke);
        painter->drawLine(cx + 10, cy, cx + 2, cy + 8, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Video): {
        const int w = s / 3;
        const int h = s / 4;
        SwRect cam{cx - w / 2 - 4, cy - h / 2, w, h};
        painter->fillRoundedRect(cam, 3, bg, color, stroke);
        painter->drawLine(cam.x + cam.width, cy - 4, cam.x + cam.width + 10, cy - 9, color, stroke);
        painter->drawLine(cam.x + cam.width, cy + 4, cam.x + cam.width + 10, cy + 9, color, stroke);
        painter->drawLine(cam.x + cam.width + 10, cy - 9, cam.x + cam.width + 10, cy + 9, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Phone): {
        painter->drawLine(cx - 8, cy - 6, cx - 2, cy - 12, color, stroke);
        painter->drawLine(cx + 8, cy + 6, cx + 2, cy + 12, color, stroke);
        painter->drawLine(cx - 2, cy - 12, cx + 2, cy - 8, color, stroke);
        painter->drawLine(cx + 2, cy + 12, cx - 2, cy + 8, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Chats): {
        SwRect bubble{cx - 12, cy - 10, 24, 18};
        painter->fillRoundedRect(bubble, 5, bg, color, stroke);
        painter->drawLine(cx - 4, cy + 8, cx - 10, cy + 12, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Status): {
        const int rad = s / 5;
        SwRect ring{cx - rad, cy - rad, rad * 2, rad * 2};
        painter->drawEllipse(ring, color, stroke);
        painter->drawLine(cx, cy - rad, cx, cy - rad - 6, color, stroke);
        break;
    }
    case static_cast<int>(Kind::Settings): {
        const int rad = s / 6;
        SwRect ring{cx - rad, cy - rad, rad * 2, rad * 2};
        painter->drawEllipse(ring, color, stroke);
        painter->drawLine(cx - rad - 6, cy, cx - rad + 2, cy, color, stroke);
        painter->drawLine(cx + rad - 2, cy, cx + rad + 6, cy, color, stroke);
        painter->drawLine(cx, cy - rad - 6, cx, cy - rad + 2, color, stroke);
        painter->drawLine(cx, cy + rad - 2, cx, cy + rad + 6, color, stroke);
        break;
    }
    case static_cast<int>(Kind::NewChat): {
        SwRect bubble{cx - 12, cy - 10, 24, 18};
        painter->fillRoundedRect(bubble, 5, bg, color, stroke);
        painter->drawLine(cx - 4, cy + 8, cx - 10, cy + 12, color, stroke);
        painter->drawLine(cx + 6, cy - 2, cx + 6, cy + 6, color, stroke);
        painter->drawLine(cx + 2, cy + 2, cx + 10, cy + 2, color, stroke);
        break;
    }
    }
}
