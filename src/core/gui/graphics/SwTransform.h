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

#include "SwGraphicsTypes.h"

#include <cmath>

class SwTransform {
public:
    SwTransform() = default;

    static SwTransform fromTranslate(double dx, double dy) {
        SwTransform t;
        t.m31 = dx;
        t.m32 = dy;
        return t;
    }

    static SwTransform fromScale(double sx, double sy) {
        SwTransform t;
        t.m11 = sx;
        t.m22 = sy;
        return t;
    }

    static SwTransform fromRotateDegrees(double degrees) {
        const double radians = degrees * (3.14159265358979323846 / 180.0);
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        SwTransform t;
        t.m11 = c;
        t.m12 = s;
        t.m21 = -s;
        t.m22 = c;
        return t;
    }

    SwTransform translated(double dx, double dy) const {
        return *this * fromTranslate(dx, dy);
    }

    SwTransform scaled(double sx, double sy) const {
        return *this * fromScale(sx, sy);
    }

    SwTransform rotated(double degrees) const {
        return *this * fromRotateDegrees(degrees);
    }

    SwPointF map(const SwPointF& p) const {
        const double x = m11 * p.x + m21 * p.y + m31;
        const double y = m12 * p.x + m22 * p.y + m32;
        const double w = m13 * p.x + m23 * p.y + m33;
        if (w != 0.0 && w != 1.0) {
            return SwPointF(x / w, y / w);
        }
        return SwPointF(x, y);
    }

    SwRectF mapRect(const SwRectF& rect) const {
        SwPointF pts[4] = {
            map(SwPointF(rect.x, rect.y)),
            map(SwPointF(rect.x + rect.width, rect.y)),
            map(SwPointF(rect.x, rect.y + rect.height)),
            map(SwPointF(rect.x + rect.width, rect.y + rect.height)),
        };
        return swBoundingRectOfPoints(pts, 4);
    }

    SwTransform operator*(const SwTransform& o) const {
        SwTransform r;
        r.m11 = m11 * o.m11 + m12 * o.m21 + m13 * o.m31;
        r.m12 = m11 * o.m12 + m12 * o.m22 + m13 * o.m32;
        r.m13 = m11 * o.m13 + m12 * o.m23 + m13 * o.m33;

        r.m21 = m21 * o.m11 + m22 * o.m21 + m23 * o.m31;
        r.m22 = m21 * o.m12 + m22 * o.m22 + m23 * o.m32;
        r.m23 = m21 * o.m13 + m22 * o.m23 + m23 * o.m33;

        r.m31 = m31 * o.m11 + m32 * o.m21 + m33 * o.m31;
        r.m32 = m31 * o.m12 + m32 * o.m22 + m33 * o.m32;
        r.m33 = m31 * o.m13 + m32 * o.m23 + m33 * o.m33;
        return r;
    }

    double m11{1.0}, m12{0.0}, m13{0.0};
    double m21{0.0}, m22{1.0}, m23{0.0};
    double m31{0.0}, m32{0.0}, m33{1.0};
};

