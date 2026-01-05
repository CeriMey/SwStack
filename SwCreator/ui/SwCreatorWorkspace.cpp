#include "SwCreatorWorkspace.h"

#include "SwCreatorFormCanvas.h"

#include <algorithm>

SwCreatorWorkspace::SwCreatorWorkspace(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwCreatorWorkspace { background-color: rgb(241, 245, 249); border-width: 0px; }");
    m_canvas = new SwCreatorFormCanvas(this);
}

SwCreatorFormCanvas* SwCreatorWorkspace::canvas() const {
    return m_canvas;
}

void SwCreatorWorkspace::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorWorkspace::paintEvent(PaintEvent* event) {
    SwWidget::paintEvent(event);
    if (!event || !event->painter() || !isVisibleInHierarchy()) {
        return;
    }

    for (SwObject* obj : getChildren()) {
        auto* child = dynamic_cast<SwWidget*>(obj);
        if (child && child->isVisibleInHierarchy()) {
            static_cast<SwWidgetInterface*>(child)->paintEvent(event);
        }
    }
}

void SwCreatorWorkspace::updateLayout_() {
    if (!m_canvas) {
        return;
    }
    const SwRect r = getRect();
    const int pad = 18;
    m_canvas->move(r.x + pad, r.y + pad);
    m_canvas->resize(std::max(0, r.width - 2 * pad), std::max(0, r.height - 2 * pad));
}

