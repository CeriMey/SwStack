#include "SwCreatorPaletteItem.h"

#include "SwDragDrop.h"
#include "SwPainter.h"
#include "SwWidgetPlatformAdapter.h"
#include "designer/SwCreatorSystemDragDrop.h"
#include "theme/SwCreatorTheme.h"

#include <algorithm>
#include <cstdlib>

SwCreatorPaletteItem::SwCreatorPaletteItem(const SwCreatorPaletteEntry& entry, SwWidget* parent)
    : SwWidget(parent)
    , m_entry(entry) {
    setCursor(CursorType::Hand);
    setFocusPolicy(FocusPolicyEnum::NoFocus);
    resize(220, 24);
    setStyleSheet("SwCreatorPaletteItem { background-color: rgba(0,0,0,0); border-width: 0px; }");
}

SwCreatorPaletteEntry SwCreatorPaletteItem::entry() const {
    return m_entry;
}

SwWidget* SwCreatorPaletteItem::rootWidget_() const {
    const SwWidget* root = this;
    while (const auto* parentWidget = dynamic_cast<const SwWidget*>(root->parent())) {
        root = parentWidget;
    }
    return const_cast<SwWidget*>(root);
}

SwPoint SwCreatorPaletteItem::rootWindowClientPos_(int localX, int localY) const {
    SwWidget* root = rootWidget_();
    if (!root) {
        return SwPoint{localX, localY};
    }
    return mapTo(root, SwPoint{localX, localY});
}

void SwCreatorPaletteItem::setSelected(bool on) {
    if (m_selected == on) {
        return;
    }
    m_selected = on;
    update();
}

bool SwCreatorPaletteItem::isSelected() const {
    return m_selected;
}

void SwCreatorPaletteItem::paintEvent(PaintEvent* event) {
    SwPainter* painter = event ? event->painter() : nullptr;
    if (!painter) {
        return;
    }

    const auto& th = SwCreatorTheme::current();
    const SwRect r = rect();

    SwColor text = th.textPrimary;

    bool paintBg = false;
    SwColor bg = th.surface2;

    if (m_selected) {
        bg = th.selectionBg;
        paintBg = true;
    } else if (m_pressed) {
        bg = th.pressedBg;
        paintBg = true;
    } else if (getHover()) {
        bg = th.hoverBg;
        paintBg = true;
    }

    if (paintBg) {
        painter->fillRect(r, bg, bg, 0);
    }

    const int pad = 10;
    const int iconSize = 16;
    SwRect iconRect{r.x + pad, r.y + (r.height - iconSize) / 2, iconSize, iconSize};
    drawIcon_(painter, iconRect, m_entry);

    SwRect textRect = r;
    textRect.x = iconRect.x + iconRect.width + 10;
    textRect.width = std::max(0, r.width - (textRect.x - r.x) - pad);
    painter->drawText(textRect,
                      m_entry.displayName,
                      DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                      text,
                      getFont());
}

void SwCreatorPaletteItem::mousePressEvent(MouseEvent* event) {
    if (!event) {
        return;
    }
    if (!getEnable() || !isPointInside(event->x(), event->y())) {
        SwWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressX = event->x();
    m_pressY = event->y();
    update();
    event->accept();
}

void SwCreatorPaletteItem::mouseReleaseEvent(MouseEvent* event) {
    if (!event) {
        return;
    }

    const bool wasPressed = m_pressed;
    const bool wasDragging = m_dragging;
    m_pressed = false;
    m_dragging = false;
    update();

    if (wasDragging && getEnable()) {
        const SwPoint rootPos = rootWindowClientPos_(event->x(), event->y());
        SwDragDrop::instance().end();
        dragDropped(m_entry, rootPos.x, rootPos.y);
        event->accept();
        return;
    }

    if (wasPressed && getEnable() && isPointInside(event->x(), event->y())) {
        clicked(m_entry);
        event->accept();
        return;
    }

    SwWidget::mouseReleaseEvent(event);
}

void SwCreatorPaletteItem::mouseMoveEvent(MouseEvent* event) {
    SwWidget::mouseMoveEvent(event);
    if (!event) {
        return;
    }
    if (!m_pressed || !getEnable()) {
        return;
    }

    const int dx = std::abs(event->x() - m_pressX);
    const int dy = std::abs(event->y() - m_pressY);
    const int threshold = 6;
    if (!m_dragging && (dx + dy) >= threshold) {
        m_dragging = true;
        dragStarted(m_entry);

        SwCreatorSystemDragDrop::Payload payload;
        payload.className = m_entry.className;
        payload.isLayout = m_entry.isLayout;
        if (SwCreatorSystemDragDrop::startDrag(payload)) {
            // System drag loop handled the operation (drop or cancel). We must reset our state
            // because we may not receive a clean MouseRelease event afterward.
            m_pressed = false;
            m_dragging = false;
            // The OLE drag loop consumed the mouse release, so the framework's logical mouse
            // grab (set when we accepted the original press) is still active. Clear it so that
            // the next click is dispatched normally to whatever widget is under the cursor.
            mouseGrabberWidget_() = nullptr;
            mouseGrabButtons_() = 0;
            update();
            event->accept();
            return;
        }

        // Fallback: internal overlay-driven drag (non-Windows or OLE unavailable).
        const SwPoint rootPos = rootWindowClientPos_(event->x(), event->y());
        SwDragDrop::instance().begin(nativeWindowHandle(), m_entry.displayName, getFont(), rootPos.x, rootPos.y, true);
    }
    if (m_dragging) {
        const SwPoint rootPos = rootWindowClientPos_(event->x(), event->y());
        SwDragDrop::instance().updatePosition(rootPos.x, rootPos.y);
        dragMoved(m_entry, rootPos.x, rootPos.y);
        event->accept();
    }
}

void SwCreatorPaletteItem::drawIcon_(SwPainter* painter, const SwRect& rect, const SwCreatorPaletteEntry& entry) {
    if (!painter) {
        return;
    }

    const auto& th = SwCreatorTheme::current();
    const SwColor stroke = th.textSecondary;
    const SwColor fill = th.surface4;

    painter->fillRoundedRect(rect, 4, fill, stroke, 1);

    const std::string cls = entry.className.toStdString();
    const int x = rect.x;
    const int y = rect.y;
    const int w = rect.width;
    const int h = rect.height;

    auto line = [&](int x1, int y1, int x2, int y2) { painter->drawLine(x1, y1, x2, y2, stroke, 2); };
    auto box = [&](int bx, int by, int bw, int bh) { painter->drawRect(SwRect{bx, by, bw, bh}, stroke, 2); };

    if (entry.isLayout) {
        // Simple layout glyphs.
        if (cls == "SwVerticalLayout") {
            box(x + 3, y + 3, w - 6, h - 6);
            line(x + 5, y + 7, x + w - 5, y + 7);
            line(x + 5, y + h / 2, x + w - 5, y + h / 2);
            return;
        }
        if (cls == "SwHorizontalLayout") {
            box(x + 3, y + 3, w - 6, h - 6);
            line(x + 7, y + 5, x + 7, y + h - 5);
            line(x + w / 2, y + 5, x + w / 2, y + h - 5);
            return;
        }
        if (cls == "SwGridLayout") {
            box(x + 3, y + 3, w - 6, h - 6);
            line(x + w / 2, y + 4, x + w / 2, y + h - 4);
            line(x + 4, y + h / 2, x + w - 4, y + h / 2);
            return;
        }
        if (cls == "SwFormLayout") {
            box(x + 3, y + 3, w - 6, h - 6);
            line(x + 6, y + 6, x + w - 6, y + 6);
            line(x + 6, y + h / 2, x + w - 6, y + h / 2);
            line(x + w / 2, y + 6, x + w / 2, y + h - 6);
            return;
        }
    }

    if (cls == "SwPushButton" || cls == "SwToolButton") {
        painter->fillRoundedRect(SwRect{x + 3, y + 4, w - 6, h - 8}, 4, th.surface3, stroke, 2);
        return;
    }
    if (cls == "SwLabel") {
        line(x + 4, y + 6, x + w - 4, y + 6);
        line(x + 4, y + 10, x + w - 8, y + 10);
        return;
    }
    if (cls == "SwLineEdit") {
        painter->fillRoundedRect(SwRect{x + 3, y + 5, w - 6, h - 10}, 3, th.surface1, stroke, 2);
        line(x + 6, y + 8, x + 6, y + h - 8);
        return;
    }
    if (cls == "SwCheckBox") {
        box(x + 4, y + 4, w - 8, h - 8);
        line(x + 5, y + h / 2, x + w / 2, y + h - 5);
        line(x + w / 2, y + h - 5, x + w - 5, y + 5);
        return;
    }
    if (cls == "SwRadioButton") {
        painter->drawEllipse(SwRect{x + 4, y + 4, w - 8, h - 8}, stroke, 2);
        painter->fillEllipse(SwRect{x + 7, y + 7, w - 14, h - 14}, stroke, stroke, 0);
        return;
    }
    if (cls == "SwComboBox") {
        painter->fillRoundedRect(SwRect{x + 3, y + 5, w - 6, h - 10}, 3, th.surface1, stroke, 2);
        line(x + w - 7, y + 7, x + w - 4, y + 10);
        line(x + w - 4, y + 10, x + w - 7, y + 13);
        return;
    }
    if (cls == "SwProgressBar") {
        box(x + 3, y + 6, w - 6, h - 12);
        painter->fillRect(SwRect{x + 4, y + 7, (w - 8) / 2, h - 14}, th.accentSecondary, th.accentSecondary, 0);
        return;
    }
    if (cls == "SwPlainTextEdit" || cls == "SwTextEdit") {
        box(x + 3, y + 3, w - 6, h - 6);
        line(x + 5, y + 6, x + w - 5, y + 6);
        line(x + 5, y + 10, x + w - 9, y + 10);
        return;
    }
    if (cls == "SwTabWidget") {
        box(x + 3, y + 5, w - 6, h - 8);
        box(x + 3, y + 3, w / 2, 5);
        return;
    }
    if (cls == "SwGroupBox") {
        box(x + 3, y + 4, w - 6, h - 7);
        line(x + 5, y + 6, x + w / 2, y + 6);
        return;
    }
    if (cls == "SwScrollArea") {
        box(x + 3, y + 3, w - 6, h - 6);
        line(x + w - 5, y + 4, x + w - 5, y + h - 4);
        return;
    }
    if (cls == "SwSplitter") {
        line(x + w / 2, y + 3, x + w / 2, y + h - 3);
        line(x + w / 2 - 2, y + h / 2 - 2, x + w / 2 + 2, y + h / 2 - 2);
        line(x + w / 2 - 2, y + h / 2 + 2, x + w / 2 + 2, y + h / 2 + 2);
        return;
    }
    if (cls == "SwSlider") {
        line(x + 4, y + h / 2, x + w - 4, y + h / 2);
        painter->fillEllipse(SwRect{x + w / 2 - 3, y + h / 2 - 3, 6, 6}, stroke, stroke, 0);
        return;
    }
    if (cls == "SwSpinBox" || cls == "SwDoubleSpinBox") {
        box(x + 3, y + 4, w - 6, h - 8);
        line(x + w - 6, y + 5, x + w - 6, y + h - 5);
        line(x + w - 5, y + 7, x + w - 3, y + 9);
        line(x + w - 3, y + 9, x + w - 5, y + 11);
        return;
    }
    if (cls == "SwTableWidget" || cls == "SwTableView") {
        box(x + 3, y + 3, w - 6, h - 6);
        line(x + w / 2, y + 4, x + w / 2, y + h - 4);
        line(x + 4, y + h / 2, x + w - 4, y + h / 2);
        return;
    }
    if (cls == "SwTreeWidget" || cls == "SwTreeView") {
        box(x + 3, y + 3, w - 6, h - 6);
        line(x + 6, y + 6, x + 6, y + h - 6);
        line(x + 6, y + 6, x + w - 6, y + 6);
        line(x + 6, y + h / 2, x + w - 8, y + h / 2);
        return;
    }
}
