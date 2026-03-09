#include "SwCreatorPalettePage.h"

#include "SwCreatorPaletteItem.h"

#include <algorithm>

SwCreatorPalettePage::SwCreatorPalettePage(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwCreatorPalettePage { background-color: rgb(255, 255, 255); border-width: 0px; border-radius: 0px; }");
}

void SwCreatorPalettePage::refreshLayout() {
    updateLayout_();
    update();
}

void SwCreatorPalettePage::setEntries(const std::vector<SwCreatorPaletteEntry>& entries) {
    m_entries = entries;
    rebuildButtons_();
}

void SwCreatorPalettePage::setFilterText(const SwString& text) {
    const SwString t = text.trimmed();
    if (m_filter == t) {
        return;
    }
    m_filter = t;
    updateLayout_();
    update();
}

void SwCreatorPalettePage::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorPalettePage::newParentEvent(SwObject* parent) {
    SwWidget::newParentEvent(parent);

    SwWidget* nextParent = dynamic_cast<SwWidget*>(parent);
    if (m_resizeParent == nextParent) {
        return;
    }

    if (m_resizeParent) {
        SwObject::disconnect(m_resizeParent, this);
    }
    m_resizeParent = nextParent;
    if (m_resizeParent) {
        SwObject::connect(m_resizeParent, &SwWidget::resized, this, &SwCreatorPalettePage::onParentResized_);
    }

    refreshLayout();
}

SwSize SwCreatorPalettePage::sizeHint() const {
    int visibleCount = 0;
    for (const auto& e : m_entries) {
        if (entryVisible_(e)) {
            ++visibleCount;
        }
    }

    int contentH = m_pad * 2;
    if (visibleCount > 0) {
        contentH += visibleCount * m_itemHeight + std::max(0, visibleCount - 1) * m_itemGap;
    }
    return SwSize{width(), std::max(0, contentH)};
}

SwSize SwCreatorPalettePage::minimumSizeHint() const {
    return sizeHint();
}

void SwCreatorPalettePage::rebuildButtons_() {
    for (SwCreatorPaletteItem* b : m_buttons) {
        delete b;
    }
    m_buttons.clear();

    m_buttons.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        auto* b = new SwCreatorPaletteItem(e, this);
        b->resize(200, m_itemHeight);
        SwObject::connect(b, &SwCreatorPaletteItem::clicked, this, [this, b, e](const SwCreatorPaletteEntry&) {
            for (auto* other : m_buttons) {
                if (other) {
                    other->setSelected(other == b);
                }
            }
            entryActivated(e);
        });
        SwObject::connect(b, &SwCreatorPaletteItem::dragStarted, this, [this, b, e](const SwCreatorPaletteEntry&) {
            for (auto* other : m_buttons) {
                if (other) {
                    other->setSelected(other == b);
                }
            }
            entryDragStarted(e);
        });
        SwObject::connect(b,
                          &SwCreatorPaletteItem::dragMoved,
                          this,
                          [this, e](const SwCreatorPaletteEntry&, int x, int y) { entryDragMoved(e, x, y); });
        SwObject::connect(b,
                          &SwCreatorPaletteItem::dragDropped,
                          this,
                          [this, e](const SwCreatorPaletteEntry&, int x, int y) { entryDropped(e, x, y); });
        m_buttons.push_back(b);
    }

    updateLayout_();
    update();
}

bool SwCreatorPalettePage::entryVisible_(const SwCreatorPaletteEntry& e) const {
    if (m_filter.isEmpty()) {
        return true;
    }
    const SwString needle = m_filter.toLower();
    const SwString a = e.displayName.toLower();
    const SwString b = e.className.toLower();
    return a.contains(needle) || b.contains(needle);
}

void SwCreatorPalettePage::onParentResized_(int, int) {
    refreshLayout();
}

void SwCreatorPalettePage::updateLayout_() {
    if (m_inUpdateLayout) {
        return;
    }
    m_inUpdateLayout = true;
    SwRect r = rect();

    const SwWidget* parentWidget = dynamic_cast<SwWidget*>(parent());
    const int targetW = parentWidget ? std::max(0, parentWidget->rect().width) : r.width;

    int y = m_pad;
    int visibleCount = 0;
    for (size_t i = 0; i < m_entries.size() && i < m_buttons.size(); ++i) {
        const auto& e = m_entries[i];
        SwCreatorPaletteItem* b = m_buttons[i];
        if (!b) {
            continue;
        }
        const bool visible = entryVisible_(e);
        if (!visible) {
            b->hide();
            continue;
        }
        b->show();
        b->move(m_pad, y);
        b->resize(std::max(0, targetW - 2 * m_pad), m_itemHeight);
        y += m_itemHeight + m_itemGap;
        ++visibleCount;
    }

    const int contentH = std::max(0, m_pad * 2 + visibleCount * m_itemHeight + std::max(0, visibleCount - 1) * m_itemGap);
    if (contentH != r.height || targetW != r.width) {
        resize(targetW, contentH);
    }
    m_inUpdateLayout = false;
}
