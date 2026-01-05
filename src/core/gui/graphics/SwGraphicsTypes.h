#pragma once
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#include <algorithm>
#include <cmath>

struct SwPointF {
    double x{0.0};
    double y{0.0};

    SwPointF() = default;
    SwPointF(double px, double py) : x(px), y(py) {}
};

struct SwRectF {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    SwRectF() = default;
    SwRectF(double px, double py, double w, double h) : x(px), y(py), width(w), height(h) {}

    double left() const { return x; }
    double top() const { return y; }
    double right() const { return x + width; }
    double bottom() const { return y + height; }

    bool isEmpty() const { return width <= 0.0 || height <= 0.0; }

    SwRectF normalized() const {
        SwRectF r = *this;
        if (r.width < 0.0) {
            r.x += r.width;
            r.width = -r.width;
        }
        if (r.height < 0.0) {
            r.y += r.height;
            r.height = -r.height;
        }
        return r;
    }

    bool contains(const SwPointF& p) const {
        const SwRectF r = normalized();
        return p.x >= r.left() && p.x <= r.right() && p.y >= r.top() && p.y <= r.bottom();
    }

    void translate(double dx, double dy) {
        x += dx;
        y += dy;
    }
};

struct SwLineF {
    SwPointF p1{};
    SwPointF p2{};

    SwLineF() = default;
    SwLineF(const SwPointF& a, const SwPointF& b) : p1(a), p2(b) {}
    SwLineF(double x1, double y1, double x2, double y2) : p1(x1, y1), p2(x2, y2) {}
};

inline SwRectF swBoundingRectOfPoints(const SwPointF* points, int count) {
    if (!points || count <= 0) {
        return {};
    }
    double minX = points[0].x;
    double maxX = points[0].x;
    double minY = points[0].y;
    double maxY = points[0].y;
    for (int i = 1; i < count; ++i) {
        minX = std::min(minX, points[i].x);
        maxX = std::max(maxX, points[i].x);
        minY = std::min(minY, points[i].y);
        maxY = std::max(maxY, points[i].y);
    }
    return SwRectF(minX, minY, maxX - minX, maxY - minY);
}

inline double swDistance(const SwPointF& a, const SwPointF& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

