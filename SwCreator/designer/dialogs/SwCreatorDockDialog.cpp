#include "SwCreatorDockDialog.h"

#include "SwWidgetPlatformAdapter.h"

#include <algorithm>

namespace {
bool rectContainsPoint(const SwRect& r, int x, int y) {
    return x >= r.x && x <= (r.x + r.width) && y >= r.y && y <= (r.y + r.height);
}
} // namespace

class SwCreatorDockDialog::DockOverlay final : public SwWidget {
    SW_OBJECT(DockOverlay, SwWidget)

public:
    DockOverlay(SwCreatorDockDialog* owner, SwWidget* root, SwWidget* parent = nullptr)
        : SwWidget(parent)
        , m_owner(owner)
        , m_root(root) {
        setStyleSheet("SwWidget { background-color: rgba(0,0,0,0); border-width: 0px; }");
        setFocusPolicy(FocusPolicyEnum::Strong);
        setCursor(CursorType::Arrow);
    }

    void mousePressEvent(MouseEvent* event) override {
        if (!event || !m_owner) {
            return;
        }

        if (m_owner->closeOnOutsideClick()) {
            const SwRect dr = m_owner->frameGeometry();
            if (!rectContainsPoint(dr, event->x(), event->y())) {
                m_owner->closeDocked();
            }
        }

        if (m_root) {
            MouseEvent forwarded(EventType::MousePressEvent, event->x(), event->y(), event->button());
            static_cast<SwWidgetInterface*>(m_root)->mousePressEvent(&forwarded);
        }

        event->accept();
    }

    void keyPressEvent(KeyEvent* event) override {
        if (!event || !m_owner) {
            return;
        }
        if (m_owner->closeOnOutsideClick() && SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
            m_owner->closeDocked();
            event->accept();
            return;
        }
        SwWidget::keyPressEvent(event);
    }

private:
    SwCreatorDockDialog* m_owner{nullptr};
    SwWidget* m_root{nullptr};
};

SwCreatorDockDialog::SwCreatorDockDialog(SwWidget* parent)
    : SwDialog(parent) {
    setModal(false);
    setUseNativeWindow(false);
}

void SwCreatorDockDialog::setCloseOnOutsideClick(bool on) {
    m_closeOnOutsideClick = on;
}

bool SwCreatorDockDialog::closeOnOutsideClick() const {
    return m_closeOnOutsideClick;
}

bool SwCreatorDockDialog::isDockedOpen() const {
    return m_dockedOpen;
}

void SwCreatorDockDialog::openDocked(SwObject* startForRoot, const SwRect& anchorRect, DockSide side, int gapPx) {
    if (gapPx < 0) {
        gapPx = 0;
    }

    setModal(false);
    setUseNativeWindow(false);
    m_dockedOpen = true;
    m_anchorRect = anchorRect;
    m_side = side;
    m_gapPx = gapPx;

    SwWidget* root = findRootWidget_(startForRoot ? startForRoot : this);
    if (!root) {
        show();
        move(anchorRect.x, anchorRect.y);
        update();
        return;
    }

    ensureOverlay_(root);
    updateOverlayGeometry_();

    if (parent() != m_overlay) {
        setParent(m_overlay);
    }

    show();
    reposition_();
    update();
}

void SwCreatorDockDialog::closeDocked() {
    m_dockedOpen = false;
    hide();
    if (m_overlay) {
        m_overlay->hide();
    }
}

void SwCreatorDockDialog::resizeEvent(ResizeEvent* event) {
    SwDialog::resizeEvent(event);
    reposition_();
}

void SwCreatorDockDialog::keyPressEvent(KeyEvent* event) {
    if (!event) {
        return;
    }

    if (m_dockedOpen && SwWidgetPlatformAdapter::isEscapeKey(event->key())) {
        closeDocked();
        event->accept();
        return;
    }

    SwDialog::keyPressEvent(event);
}

SwWidget* SwCreatorDockDialog::findRootWidget_(SwObject* start) {
    SwWidget* lastWidget = nullptr;
    for (SwObject* p = start; p; p = p->parent()) {
        if (auto* w = dynamic_cast<SwWidget*>(p)) {
            lastWidget = w;
        }
    }
    return lastWidget;
}

void SwCreatorDockDialog::ensureOverlay_(SwWidget* root) {
    if (!root) {
        return;
    }
    m_root = root;

    if (!m_overlay) {
        m_overlay = new DockOverlay(this, m_root, m_root);
    }

    if (m_overlay->parent() != m_root) {
        m_overlay->setParent(m_root);
    }

    m_overlay->move(0, 0);
    m_overlay->resize(m_root->width(), m_root->height());
    m_overlay->show();
    m_overlay->update();

    if (m_overlayRootConnected != m_root) {
        m_overlayRootConnected = m_root;
        SwWidget* expectedRoot = m_root;
        SwObject::connect(m_root, &SwWidget::resized, this, [this, expectedRoot](int w, int h) {
            if (m_root != expectedRoot) {
                return;
            }
            if (m_overlay) {
                m_overlay->move(0, 0);
                m_overlay->resize(w, h);
            }
            reposition_();
        });
    }
}

void SwCreatorDockDialog::updateOverlayGeometry_() {
    if (!m_overlay || !m_root) {
        return;
    }
    m_overlay->move(0, 0);
    m_overlay->resize(m_root->width(), m_root->height());
    m_overlay->show();
}

void SwCreatorDockDialog::reposition_() {
    if (!m_root) {
        return;
    }

    const int w = width();
    const int h = height();

    const int maxX = std::max(0, m_root->width() - w - 2);
    const int maxY = std::max(0, m_root->height() - h - 2);

    DockSide resolvedSide = m_side;
    if (resolvedSide == DockSide::Auto) {
        const int desiredRightX = m_anchorRect.x + m_anchorRect.width + m_gapPx;
        if (desiredRightX + w <= m_root->width()) {
            resolvedSide = DockSide::Right;
        } else {
            resolvedSide = DockSide::Left;
        }
    }

    int x = m_anchorRect.x;
    if (resolvedSide == DockSide::Right) {
        x = m_anchorRect.x + m_anchorRect.width + m_gapPx;
    } else if (resolvedSide == DockSide::Left) {
        x = m_anchorRect.x - w - m_gapPx;
    }

    int y = m_anchorRect.y;

    x = std::max(2, std::min(maxX, x));
    y = std::max(2, std::min(maxY, y));

    move(x, y);
}

