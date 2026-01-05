#pragma once

#include "graphics/SwImage.h"

class WaDemoAssets {
public:
    static SwImage makeFakeScreenshotThumb(int w, int h, int radius);
    static SwImage makeVideoThumb(int w, int h, int radius);
};
