#pragma once

#include "SwString.h"
#include "graphics/SwImage.h"

class WaMediaImageLoader {
public:
    // Loads and scales to the exact target size (best-effort).
    static SwImage loadThumbnail(const SwString& absoluteFilePath, int targetW, int targetH);

    // Loads the image and scales it down to fit within maxW/maxH while preserving aspect ratio.
    // If maxW/maxH are <= 0, returns the decoded image size.
    static SwImage loadImageFit(const SwString& absoluteFilePath, int maxW, int maxH);
};

