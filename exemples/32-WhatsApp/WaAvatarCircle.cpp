#include "WaAvatarCircle.h"

#include "SwFont.h"
#include "SwPainter.h"

WaAvatarCircle::WaAvatarCircle(const SwString& initial, const SwColor& color, SwWidget* parent)
    : SwWidget(parent)
    , m_initial(initial)
    , m_color(color) {
    setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
    setFocusPolicy(FocusPolicyEnum::NoFocus);
    setFont(SwFont(L"Segoe UI", 11, SemiBold));
    resize(40, 40);
}

void WaAvatarCircle::setInitial(const SwString& initial) {
    m_initial = initial;
    update();
}

void WaAvatarCircle::setColor(const SwColor& color) {
    m_color = color;
    update();
}

void WaAvatarCircle::paintEvent(PaintEvent* event) {
    SwPainter* painter = event ? event->painter() : nullptr;
    if (!painter) {
        return;
    }

    const SwRect r = getRect();
    const int radius = (r.width < r.height ? r.width : r.height) / 2;
    painter->fillRoundedRect(r, radius, m_color, m_color, 0);
    painter->drawText(r,
                      m_initial,
                      DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                      SwColor{255, 255, 255},
                      getFont());
}

