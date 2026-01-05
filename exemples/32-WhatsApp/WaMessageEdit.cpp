#include "WaMessageEdit.h"

#include "SwScrollBar.h"
#include "SwWidgetPlatformAdapter.h"

#include <algorithm>

WaMessageEdit::WaMessageEdit(const SwString& placeholderText, SwWidget* parent)
    : SwPlainTextEdit(parent) {
    setPlaceholderText(placeholderText);
    setUndoRedoEnabled(true);

    m_scrollBar = new SwScrollBar(SwScrollBar::Orientation::Vertical, this);
    m_scrollBar->hide();

    SwObject::connect(m_scrollBar, &SwScrollBar::valueChanged, [this](int value) {
        if (m_syncingScrollBar) {
            return;
        }

        const int visible = visibleLines_();
        const int maxFirst = std::max(0, m_lines.size() - visible);
        m_firstVisibleLine = clampInt(value, 0, maxFirst);
        update();
    });

    SwObject::connect(this, &SwPlainTextEdit::textChanged, [this]() { updateScrollBar_(); });

    updateScrollBar_();
}

void WaMessageEdit::setMaxLines(int lines) {
    m_maxLines = std::max(1, lines);
    updateScrollBar_();
}

int WaMessageEdit::preferredHeight() {
    StyleSheet* sheet = getToolSheet();

    SwColor border{220, 224, 232};
    int borderWidth = 1;
    int radius = 12;
    resolveBorder(sheet, border, borderWidth, radius);

    const Padding pad = resolvePadding(sheet);

    const int lh = lineHeightPx();
    if (lh <= 0) {
        return std::max(m_minHeightPx, height());
    }

    const int lineCount = std::max(1, m_lines.size());
    const int clampedLines = std::min(m_maxLines, lineCount);

    const int innerH = clampedLines * lh;
    const int outer = innerH + (2 * borderWidth) + pad.top + pad.bottom;
    return std::max(m_minHeightPx, outer);
}

void WaMessageEdit::paintEvent(PaintEvent* event) {
    SwPlainTextEdit::paintEvent(event);
    paintChildren(event);
}

void WaMessageEdit::resizeEvent(ResizeEvent* event) {
    SwPlainTextEdit::resizeEvent(event);
    updateScrollBar_();
}

void WaMessageEdit::wheelEvent(WheelEvent* event) {
    SwPlainTextEdit::wheelEvent(event);
    updateScrollBar_();
}

void WaMessageEdit::keyPressEvent(KeyEvent* event) {
    if (!event) {
        return;
    }
    if (!getFocus()) {
        SwPlainTextEdit::keyPressEvent(event);
        return;
    }

    if (SwWidgetPlatformAdapter::isReturnKey(event->key()) && !event->isShiftPressed()) {
        submitted();
        event->accept();
        return;
    }

    SwPlainTextEdit::keyPressEvent(event);
    updateScrollBar_();
}

int WaMessageEdit::visibleLines_() {
    const int lh = lineHeightPx();
    if (lh <= 0) {
        return 1;
    }

    const SwRect bounds = getRect();
    StyleSheet* sheet = getToolSheet();

    SwColor border{220, 224, 232};
    int borderWidth = 1;
    int radius = 12;
    resolveBorder(sheet, border, borderWidth, radius);

    const Padding pad = resolvePadding(sheet);

    SwRect inner = bounds;
    inner.x += borderWidth + pad.left;
    inner.y += borderWidth + pad.top;
    inner.width = std::max(0, inner.width - 2 * borderWidth - (pad.left + pad.right));
    inner.height = std::max(0, inner.height - 2 * borderWidth - (pad.top + pad.bottom));

    return std::max(1, inner.height / lh);
}

void WaMessageEdit::updateScrollBarGeometry_() {
    if (!m_scrollBar) {
        return;
    }

    const SwRect bounds = getRect();
    if (bounds.width <= 0 || bounds.height <= 0) {
        return;
    }

    const int sbW = 10;
    const int margin = 4;
    const int x = bounds.x + bounds.width - sbW - margin;
    const int y = bounds.y + margin;
    const int h = std::max(0, bounds.height - 2 * margin);
    m_scrollBar->move(x, y);
    m_scrollBar->resize(sbW, h);
}

void WaMessageEdit::updateScrollBar_() {
    if (!m_scrollBar) {
        return;
    }

    const int visible = visibleLines_();
    const int maxFirst = std::max(0, m_lines.size() - visible);

    m_syncingScrollBar = true;
    m_scrollBar->setRange(0, maxFirst);
    m_scrollBar->setPageStep(visible);
    m_scrollBar->setValue(m_firstVisibleLine);
    m_syncingScrollBar = false;

    m_scrollBar->setVisible(maxFirst > 0);
    updateScrollBarGeometry_();
}
