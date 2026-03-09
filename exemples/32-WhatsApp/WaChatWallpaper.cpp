#include "WaChatWallpaper.h"

#include "SwPainter.h"

WaChatWallpaper::WaChatWallpaper(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwWidget { background-color: rgb(239, 234, 226); border-width: 0px; }");
    setFocusPolicy(FocusPolicyEnum::NoFocus);
}

void WaChatWallpaper::paintEvent(PaintEvent* event) {
    if (!isVisibleInHierarchy()) {
        return;
    }
    SwPainter* painter = event ? event->painter() : nullptr;
    if (!painter) {
        return;
    }

    const SwRect rect = this->rect();
    painter->pushClipRect(rect);
    const SwColor bg{239, 234, 226};
    painter->fillRect(rect, bg, bg, 0);

    // Light doodle pattern (very subtle).
    const SwColor ink{224, 220, 211};
    const int step = 84;
    for (int y = rect.y - (rect.y % step); y < rect.y + rect.height; y += step) {
        for (int x = rect.x - (rect.x % step); x < rect.x + rect.width; x += step) {
            painter->drawEllipse(SwRect{x + 16, y + 18, 14, 14}, ink, 1);
            painter->drawLine(x + 44, y + 20, x + 64, y + 20, ink, 1);
            painter->drawLine(x + 54, y + 12, x + 54, y + 28, ink, 1);
            painter->fillRoundedRect(SwRect{x + 18, y + 46, 18, 12}, 4, bg, ink, 1);
            painter->fillRoundedRect(SwRect{x + 48, y + 48, 14, 10}, 4, bg, ink, 1);
        }
    }

    // Paint children on top of the wallpaper.
    const SwRect& paintRect = event->paintRect();
    for (SwObject* objChild : children()) {
        auto* child = dynamic_cast<SwWidget*>(objChild);
        if (!child) {
            continue;
        }
        const SwRect childRect = child->geometry();
        if (child->isVisibleInHierarchy() && rectsIntersect(paintRect, childRect)) {
            paintChild_(event, child);
        }
    }
    painter->popClipRect();
}

