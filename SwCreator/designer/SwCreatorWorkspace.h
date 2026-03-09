#pragma once
/***************************************************************************************************
 * SwCreatorWorkspace - mid panel container around the canvas.
 *
 * Responsibilities:
 * - Provide a background and padding around the canvas.
 **************************************************************************************************/

#include "SwWidget.h"

class SwCreatorFormCanvas;
class SwScrollArea;
class SwWidget;

class SwCreatorWorkspace : public SwWidget {
    SW_OBJECT(SwCreatorWorkspace, SwWidget)

public:
    explicit SwCreatorWorkspace(SwWidget* parent = nullptr);

    SwCreatorFormCanvas* canvas() const;
    void refreshGeometry();

protected:
    void resizeEvent(ResizeEvent* event) override;
    void paintEvent(PaintEvent* event) override;

private:
    void updateLayout_();
    void updateCanvasHostGeometry_();

    SwScrollArea* m_scrollArea{nullptr};
    SwWidget* m_canvasHost{nullptr};
    SwCreatorFormCanvas* m_canvas{nullptr};
};
