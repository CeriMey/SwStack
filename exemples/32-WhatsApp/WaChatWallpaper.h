#pragma once

#include "SwWidget.h"

class WaChatWallpaper final : public SwWidget {
    SW_OBJECT(WaChatWallpaper, SwWidget)

public:
    explicit WaChatWallpaper(SwWidget* parent = nullptr);

protected:
    void paintEvent(PaintEvent* event) override;
};

