#pragma once
/***************************************************************************************************
 * SwCreatorWorkspace - mid panel container around the canvas.
 *
 * Responsibilities:
 * - Provide a background and padding around the canvas.
 **************************************************************************************************/

#include "SwWidget.h"

class SwCreatorFormCanvas;

class SwCreatorWorkspace : public SwWidget {
    SW_OBJECT(SwCreatorWorkspace, SwWidget)

public:
    explicit SwCreatorWorkspace(SwWidget* parent = nullptr);

    SwCreatorFormCanvas* canvas() const;

protected:
    void resizeEvent(ResizeEvent* event) override;
    void paintEvent(PaintEvent* event) override;

private:
    void updateLayout_();

    SwCreatorFormCanvas* m_canvas{nullptr};
};

