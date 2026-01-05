#include "WaDemoAssets.h"

#include <cstdint>
#include <algorithm>

static int clampInt_(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static std::uint32_t packArgb_(int a, int r, int g, int b) {
    const std::uint32_t aa = static_cast<std::uint32_t>(clampInt_(a, 0, 255));
    const std::uint32_t rr = static_cast<std::uint32_t>(clampInt_(r, 0, 255));
    const std::uint32_t gg = static_cast<std::uint32_t>(clampInt_(g, 0, 255));
    const std::uint32_t bb = static_cast<std::uint32_t>(clampInt_(b, 0, 255));
    return (aa << 24) | (rr << 16) | (gg << 8) | bb;
}

SwImage WaDemoAssets::makeFakeScreenshotThumb(int w, int h, int radius) {
    SwImage img(w, h, SwImage::Format_ARGB32);
    if (img.isNull()) {
        return img;
    }

    radius = clampInt_(radius, 0, (w < h ? w : h) / 2);
    const int r2 = radius * radius;

    const std::uint32_t bg = packArgb_(255, 244, 244, 245);
    const std::uint32_t header = packArgb_(255, 229, 231, 235);
    const std::uint32_t frame = packArgb_(255, 209, 213, 219);
    const std::uint32_t dark = packArgb_(255, 10, 16, 24);
    const std::uint32_t dark2 = packArgb_(255, 18, 24, 32);
    const std::uint32_t green = packArgb_(255, 34, 197, 94);
    const std::uint32_t blue = packArgb_(255, 59, 130, 246);
    const std::uint32_t grayText = packArgb_(255, 203, 213, 225);

    // Background fill.
    for (int y = 0; y < h; ++y) {
        std::uint32_t* row = img.scanLine(y);
        if (!row) {
            continue;
        }
        for (int x = 0; x < w; ++x) {
            std::uint32_t px = bg;

            // Rounded alpha mask.
            if (radius > 0) {
                bool outside = false;
                if (x < radius && y < radius) {
                    const int dx = radius - 1 - x;
                    const int dy = radius - 1 - y;
                    outside = (dx * dx + dy * dy) > r2;
                } else if (x >= w - radius && y < radius) {
                    const int dx = x - (w - radius);
                    const int dy = radius - 1 - y;
                    outside = (dx * dx + dy * dy) > r2;
                } else if (x < radius && y >= h - radius) {
                    const int dx = radius - 1 - x;
                    const int dy = y - (h - radius);
                    outside = (dx * dx + dy * dy) > r2;
                } else if (x >= w - radius && y >= h - radius) {
                    const int dx = x - (w - radius);
                    const int dy = y - (h - radius);
                    outside = (dx * dx + dy * dy) > r2;
                }
                if (outside) {
                    px = 0x00000000u;
                }
            }

            row[x] = px;
        }
    }

    auto isTransparentAt = [&](int x, int y) -> bool {
        const std::uint32_t* row = img.constScanLine(y);
        if (!row) {
            return true;
        }
        return row[x] == 0x00000000u;
    };

    // Frame border.
    for (int y = 0; y < h; ++y) {
        std::uint32_t* row = img.scanLine(y);
        if (!row) continue;
        for (int x = 0; x < w; ++x) {
            if (isTransparentAt(x, y)) {
                continue;
            }
            const bool border = (x == 0) || (y == 0) || (x == w - 1) || (y == h - 1);
            if (border) {
                row[x] = frame;
            }
        }
    }

    // Header bar.
    const int headerH = clampInt_(h / 7, 16, 22);
    for (int y = 1; y < headerH; ++y) {
        std::uint32_t* row = img.scanLine(y);
        if (!row) continue;
        for (int x = 1; x < w - 1; ++x) {
            if (isTransparentAt(x, y)) {
                continue;
            }
            row[x] = header;
        }
    }

    // "Window" buttons (3 small dots).
    auto putDot = [&](int cx, int cy, int rad, std::uint32_t color) {
        for (int yy = cy - rad; yy <= cy + rad; ++yy) {
            if (yy < 0 || yy >= h) continue;
            std::uint32_t* row = img.scanLine(yy);
            if (!row) continue;
            for (int xx = cx - rad; xx <= cx + rad; ++xx) {
                if (xx < 0 || xx >= w) continue;
                if (isTransparentAt(xx, yy)) continue;
                const int dx = xx - cx;
                const int dy = yy - cy;
                if (dx * dx + dy * dy <= rad * rad) {
                    row[xx] = color;
                }
            }
        }
    };
    const int dotY = headerH / 2 + 1;
    putDot(12, dotY, 3, packArgb_(255, 239, 68, 68));
    putDot(24, dotY, 3, packArgb_(255, 234, 179, 8));
    putDot(36, dotY, 3, packArgb_(255, 34, 197, 94));

    // Dark code area.
    const int bodyY = headerH + 2;
    const int bodyH = h - bodyY - 3;
    for (int y = bodyY; y < bodyY + bodyH; ++y) {
        std::uint32_t* row = img.scanLine(y);
        if (!row) continue;
        for (int x = 2; x < w - 2; ++x) {
            if (isTransparentAt(x, y)) {
                continue;
            }
            row[x] = ((y - bodyY) % 2 == 0) ? dark : dark2;
        }
    }

    // "Text" lines (rectangles).
    auto fillLine = [&](int x0, int y0, int lw, int lh, std::uint32_t c) {
        for (int yy = y0; yy < y0 + lh; ++yy) {
            if (yy < 0 || yy >= h) continue;
            std::uint32_t* row = img.scanLine(yy);
            if (!row) continue;
            for (int xx = x0; xx < x0 + lw; ++xx) {
                if (xx < 0 || xx >= w) continue;
                if (isTransparentAt(xx, yy)) continue;
                row[xx] = c;
            }
        }
    };

    int yCursor = bodyY + 10;
    const int lh = 6;
    const int gap = 10;
    for (int i = 0; i < 6; ++i) {
        const int x0 = 14;
        const int baseW = w - 28;
        int w1 = baseW - (i * 22);
        if (w1 < 80) w1 = 80;
        fillLine(x0, yCursor, w1, lh, grayText);
        if (i == 1) {
            fillLine(x0, yCursor + lh + 6, baseW - 140, lh, blue);
        }
        if (i == 3) {
            fillLine(x0, yCursor + lh + 6, baseW - 200, lh, green);
        }
        yCursor += gap;
        if (yCursor > bodyY + bodyH - 20) {
            break;
        }
    }

    return img;
}

SwImage WaDemoAssets::makeVideoThumb(int w, int h, int radius) {
    SwImage img(w, h, SwImage::Format_ARGB32);
    if (img.isNull()) {
        return img;
    }

    radius = clampInt_(radius, 0, (w < h ? w : h) / 2);
    const int r2 = radius * radius;

    const std::uint32_t bg0 = packArgb_(255, 12, 18, 28);
    const std::uint32_t bg1 = packArgb_(255, 18, 26, 38);
    const std::uint32_t frame = packArgb_(255, 209, 213, 219);
    const std::uint32_t bar = packArgb_(255, 236, 239, 241);

    // Background (subtle vertical gradient) + rounded mask.
    for (int y = 0; y < h; ++y) {
        std::uint32_t* row = img.scanLine(y);
        if (!row) {
            continue;
        }
        const float t = (h > 1) ? (static_cast<float>(y) / static_cast<float>(h - 1)) : 0.0f;
        const int rr = static_cast<int>(((1.0f - t) * 12.0f) + (t * 18.0f));
        const int gg = static_cast<int>(((1.0f - t) * 18.0f) + (t * 26.0f));
        const int bb = static_cast<int>(((1.0f - t) * 28.0f) + (t * 38.0f));
        const std::uint32_t bg = packArgb_(255, rr, gg, bb);

        for (int x = 0; x < w; ++x) {
            std::uint32_t px = bg;

            if (radius > 0) {
                bool outside = false;
                if (x < radius && y < radius) {
                    const int dx = radius - 1 - x;
                    const int dy = radius - 1 - y;
                    outside = (dx * dx + dy * dy) > r2;
                } else if (x >= w - radius && y < radius) {
                    const int dx = x - (w - radius);
                    const int dy = radius - 1 - y;
                    outside = (dx * dx + dy * dy) > r2;
                } else if (x < radius && y >= h - radius) {
                    const int dx = radius - 1 - x;
                    const int dy = y - (h - radius);
                    outside = (dx * dx + dy * dy) > r2;
                } else if (x >= w - radius && y >= h - radius) {
                    const int dx = x - (w - radius);
                    const int dy = y - (h - radius);
                    outside = (dx * dx + dy * dy) > r2;
                }
                if (outside) {
                    px = 0x00000000u;
                }
            }

            row[x] = px;
        }
    }

    auto isTransparentAt = [&](int x, int y) -> bool {
        const std::uint32_t* row = img.constScanLine(y);
        if (!row) {
            return true;
        }
        return row[x] == 0x00000000u;
    };

    // Frame border.
    for (int y = 0; y < h; ++y) {
        std::uint32_t* row = img.scanLine(y);
        if (!row) continue;
        for (int x = 0; x < w; ++x) {
            if (isTransparentAt(x, y)) {
                continue;
            }
            const bool border = (x == 0) || (y == 0) || (x == w - 1) || (y == h - 1);
            if (border) {
                row[x] = frame;
            }
        }
    }

    // Bottom progress bar.
    const int barH = clampInt_(h / 18, 6, 10);
    for (int y = h - barH - 10; y < h - 10; ++y) {
        if (y < 0 || y >= h) continue;
        std::uint32_t* row = img.scanLine(y);
        if (!row) continue;
        for (int x = 16; x < w - 16; ++x) {
            if (isTransparentAt(x, y)) continue;
            row[x] = bar;
        }
    }
    // Progress fill (left part).
    const int fillW = (w - 32) / 4;
    const std::uint32_t fill = packArgb_(255, 59, 130, 246);
    for (int y = h - barH - 10; y < h - 10; ++y) {
        if (y < 0 || y >= h) continue;
        std::uint32_t* row = img.scanLine(y);
        if (!row) continue;
        for (int x = 16; x < 16 + fillW; ++x) {
            if (x < 0 || x >= w) continue;
            if (isTransparentAt(x, y)) continue;
            row[x] = fill;
        }
    }

    // Play button (circle + triangle).
    const int cx = w / 2;
    const int cy = h / 2;
    const int circleR = clampInt_((w < h ? w : h) / 6, 16, 42);
    const std::uint32_t circleFill = packArgb_(170, 0, 0, 0);
    const std::uint32_t circleBorder = packArgb_(210, 255, 255, 255);
    for (int yy = cy - circleR; yy <= cy + circleR; ++yy) {
        if (yy < 0 || yy >= h) continue;
        std::uint32_t* row = img.scanLine(yy);
        if (!row) continue;
        for (int xx = cx - circleR; xx <= cx + circleR; ++xx) {
            if (xx < 0 || xx >= w) continue;
            if (isTransparentAt(xx, yy)) continue;
            const int dx = xx - cx;
            const int dy = yy - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 <= circleR * circleR) {
                const bool border = d2 >= (circleR - 2) * (circleR - 2);
                row[xx] = border ? circleBorder : circleFill;
            }
        }
    }

    const int triH = (circleR * 9) / 10;
    const int triW = (circleR * 9) / 10;
    const int leftX = cx - triW / 4;
    const int rightX = cx + triW / 2;
    const int topY = cy - triH / 2;
    const int botY = cy + triH / 2;
    const std::uint32_t triFill = packArgb_(255, 255, 255, 255);

    for (int yy = topY; yy <= botY; ++yy) {
        if (yy < 0 || yy >= h) continue;
        std::uint32_t* row = img.scanLine(yy);
        if (!row) continue;
        if (yy == topY || yy == botY) continue;

        int xEnd = leftX;
        if (yy <= cy) {
            const float t = static_cast<float>(yy - topY) / std::max(1.0f, static_cast<float>(cy - topY));
            xEnd = static_cast<int>(leftX + (rightX - leftX) * t);
        } else {
            const float t = static_cast<float>(botY - yy) / std::max(1.0f, static_cast<float>(botY - cy));
            xEnd = static_cast<int>(leftX + (rightX - leftX) * t);
        }

        for (int xx = leftX; xx <= xEnd; ++xx) {
            if (xx < 0 || xx >= w) continue;
            if (isTransparentAt(xx, yy)) continue;
            row[xx] = triFill;
        }
    }

    return img;
}
