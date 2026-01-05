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

#pragma once

/***************************************************************************************************
 * SwPainterPath (QPainterPath-like): minimal path container (move/line/close).
 **************************************************************************************************/

#include "SwGraphicsTypes.h"

#include <vector>

class SwPainterPath {
public:
    enum ElementType {
        MoveToElement,
        LineToElement,
        CloseSubpathElement
    };

    struct Element {
        ElementType type{MoveToElement};
        SwPointF p{};
    };

    void moveTo(double x, double y) {
        m_elements.push_back(Element{MoveToElement, SwPointF(x, y)});
    }

    void lineTo(double x, double y) {
        if (m_elements.empty()) {
            moveTo(x, y);
            return;
        }
        m_elements.push_back(Element{LineToElement, SwPointF(x, y)});
    }

    void closeSubpath() {
        m_elements.push_back(Element{CloseSubpathElement, SwPointF()});
    }

    void addRect(const SwRectF& rect) {
        moveTo(rect.x, rect.y);
        lineTo(rect.x + rect.width, rect.y);
        lineTo(rect.x + rect.width, rect.y + rect.height);
        lineTo(rect.x, rect.y + rect.height);
        closeSubpath();
    }

    bool isEmpty() const { return m_elements.empty(); }

    const std::vector<Element>& elements() const { return m_elements; }

    SwRectF boundingRect() const {
        bool has = false;
        SwPointF pts[1] = {};
        SwRectF bounds{};
        for (const Element& e : m_elements) {
            if (e.type == CloseSubpathElement) {
                continue;
            }
            pts[0] = e.p;
            if (!has) {
                bounds = SwRectF(e.p.x, e.p.y, 0.0, 0.0);
                has = true;
            } else {
                const SwRectF r = swBoundingRectOfPoints(pts, 1);
                const double minX = std::min(bounds.left(), r.left());
                const double minY = std::min(bounds.top(), r.top());
                const double maxX = std::max(bounds.right(), r.right());
                const double maxY = std::max(bounds.bottom(), r.bottom());
                bounds = SwRectF(minX, minY, maxX - minX, maxY - minY);
            }
        }
        return bounds;
    }

private:
    std::vector<Element> m_elements;
};

