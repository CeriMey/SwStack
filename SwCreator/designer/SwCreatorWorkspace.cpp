#include "SwCreatorWorkspace.h"

#include "SwCreatorFormCanvas.h"
#include "theme/SwCreatorTheme.h"
#include "SwPainter.h"
#include "SwScrollArea.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kWorkspacePadding = 24;
}

SwCreatorWorkspace::SwCreatorWorkspace(SwWidget* parent)
    : SwWidget(parent) {
    const auto& th = SwCreatorTheme::current();
    const SwString wsBg = SwCreatorTheme::rgb(th.workspaceBg);

    setStyleSheet("SwCreatorWorkspace { background-color: " + wsBg + "; border-width: 0px; }");

    m_scrollArea = new SwScrollArea(this);
    m_scrollArea->setFrameShape(SwFrame::Shape::NoFrame);
    m_scrollArea->setLineWidth(0);
    m_scrollArea->setStyleSheet("SwScrollArea { background-color: " + wsBg + "; border-width: 0px; }");

    m_canvasHost = new SwWidget();
    m_canvasHost->setStyleSheet("SwWidget { background-color: " + wsBg + "; border-width: 0px; }");
    m_scrollArea->setWidget(m_canvasHost);

    m_canvas = new SwCreatorFormCanvas(m_canvasHost);

    SwObject::connect(m_canvas, &SwWidget::resized, this, [this](int, int) {
        updateCanvasHostGeometry_();
    });

    updateCanvasHostGeometry_();
}

SwCreatorFormCanvas* SwCreatorWorkspace::canvas() const {
    return m_canvas;
}

SwSize SwCreatorWorkspace::minimumSizeHint() const {
    SwSize hint = SwWidget::minimumSizeHint();
    const SwSize canvasMin = m_canvas ? m_canvas->minimumSizeHint() : SwSize{0, 0};
    int framePad = 0;
    if (m_scrollArea) {
        const int borderWidth = m_scrollArea->lineWidth() > 0 ? m_scrollArea->lineWidth() : 1;
        framePad = std::max(0, borderWidth) * 2;
    }

    hint.width = std::max(hint.width, canvasMin.width + 2 * kWorkspacePadding + framePad);
    hint.height = std::max(hint.height, canvasMin.height + 2 * kWorkspacePadding + framePad);
    return hint;
}

void SwCreatorWorkspace::refreshGeometry() {
    updateLayout_();
    if (!m_scrollArea) {
        return;
    }

    // Mirror the "splitter moved a bit" path: force one real size change on the viewport host so
    // descendant coordinates are recomputed from the settled workspace width.
    const int w = m_scrollArea->width();
    const int h = m_scrollArea->height();
    if (w > 0 && h > 0) {
        m_scrollArea->resize(w + 1, h);
        m_scrollArea->resize(w, h);
    }
    updateCanvasHostGeometry_();
}

void SwCreatorWorkspace::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void SwCreatorWorkspace::paintEvent(PaintEvent* event) {
    SwWidget::paintEvent(event);
}

void SwCreatorWorkspace::updateLayout_() {
    if (!m_scrollArea) {
        return;
    }

    const SwRect r = rect();
    m_scrollArea->move(0, 0);
    m_scrollArea->resize(r.width, r.height);
    updateCanvasHostGeometry_();
}

void SwCreatorWorkspace::updateCanvasHostGeometry_() {
    if (!m_scrollArea || !m_canvasHost || !m_canvas) {
        return;
    }

    constexpr int kScrollBarThickness = 14;

    const int canvasW = m_canvas->width() + (2 * kWorkspacePadding);
    const int canvasH = m_canvas->height() + (2 * kWorkspacePadding);

    int viewportW = std::max(0, m_scrollArea->width());
    int viewportH = std::max(0, m_scrollArea->height());
    bool showH = false;
    bool showV = false;

    for (int pass = 0; pass < 2; ++pass) {
        showV = canvasH > viewportH;
        showH = canvasW > viewportW;
        viewportW = std::max(0, m_scrollArea->width() - (showV ? kScrollBarThickness : 0));
        viewportH = std::max(0, m_scrollArea->height() - (showH ? kScrollBarThickness : 0));
    }

    const int hostW = std::max(canvasW, viewportW);
    const int hostH = std::max(canvasH, viewportH);

    m_canvasHost->resize(hostW, hostH);
    m_canvas->move(kWorkspacePadding, kWorkspacePadding);
    m_scrollArea->refreshLayout();
}
