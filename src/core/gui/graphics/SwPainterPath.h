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

/**
 * @file src/core/gui/graphics/SwPainterPath.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwPainterPath in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the painter path interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwPainterPath.
 *
 * Directory and path declarations here usually define normalization, lookup, traversal, and
 * platform-neutral path manipulation rules that other modules can depend on.
 *
 * Graphics-facing declarations here define the data flow from high-level UI state to lower-level
 * rendering backends.
 *
 */


/***************************************************************************************************
 * SwPainterPath: Qt-like QPainterPath — moveTo/lineTo/cubicTo/quadTo/arcTo + boolean ops.
 **************************************************************************************************/

/**
 * @file
 * @brief Declares the vector path builder used by painting and scene geometry.
 *
 * SwPainterPath records reusable vector outlines made of line segments, Bezier
 * curves, arcs, and convenience shapes. The same path object can later be
 * consumed for rasterization, clipping, hit testing, bounding-box queries, or
 * scene selection checks.
 *
 * Within the stack this type is the canonical representation for complex 2D
 * geometry. It therefore sits at the intersection of the painter API, the
 * retained-mode scene graph, and shape-based interaction code.
 */

#include "SwGraphicsTypes.h"

#include <vector>

namespace { inline double swClamp_(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); } }

/**
 * @brief Stores a sequence of vector drawing commands and derived shape helpers.
 */
class SwPainterPath {
public:
    enum ElementType {
        MoveToElement,
        LineToElement,
        CurveToElement,          // cubic control point 1
        CurveToDataElement,      // cubic control point 2 / end point
        CloseSubpathElement
    };

    enum FillRule {
        OddEvenFill,
        WindingFill
    };

    struct Element {
        ElementType type{MoveToElement};
        SwPointF p{};

        /**
         * @brief Constructs a `Element` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Element() {}
        /**
         * @brief Constructs a `Element` instance.
         * @param typeValue Value passed to the method.
         * @param pointValue Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        Element(ElementType typeValue, const SwPointF& pointValue)
            : type(typeValue), p(pointValue) {}

        /**
         * @brief Returns whether the object reports move To.
         * @return `true` when the object reports move To; otherwise `false`.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        bool isMoveTo() const { return type == MoveToElement; }
        /**
         * @brief Returns whether the object reports line To.
         * @return `true` when the object reports line To; otherwise `false`.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        bool isLineTo() const { return type == LineToElement; }
        /**
         * @brief Returns whether the object reports curve To.
         * @return `true` when the object reports curve To; otherwise `false`.
         *
         * @details The returned value reflects the state currently stored by the instance.
         */
        bool isCurveTo() const { return type == CurveToElement; }
    };

    /**
     * @brief Constructs a `SwPainterPath` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPainterPath() = default;
    /**
     * @brief Constructs a `SwPainterPath` instance.
     * @param startPoint Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwPainterPath(const SwPointF& startPoint) { moveTo(startPoint); }

    // -------------------------------------------------------------------
    // Path construction
    // -------------------------------------------------------------------
    /**
     * @brief Performs the `moveTo` operation.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     */
    void moveTo(double x, double y) {
        m_elements.push_back(Element{MoveToElement, SwPointF(x, y)});
    }
    /**
     * @brief Performs the `moveTo` operation.
     * @param y Vertical coordinate.
     */
    void moveTo(const SwPointF& p) { moveTo(p.x, p.y); }

    /**
     * @brief Performs the `lineTo` operation.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     */
    void lineTo(double x, double y) {
        if (m_elements.empty()) { moveTo(x, y); return; }
        m_elements.push_back(Element{LineToElement, SwPointF(x, y)});
    }
    /**
     * @brief Performs the `lineTo` operation.
     * @param y Vertical coordinate.
     */
    void lineTo(const SwPointF& p) { lineTo(p.x, p.y); }

    /**
     * @brief Performs the `cubicTo` operation.
     * @param c1x Value passed to the method.
     * @param c1y Value passed to the method.
     * @param c2x Value passed to the method.
     * @param c2y Value passed to the method.
     * @param ex Value passed to the method.
     * @param ey Value passed to the method.
     */
    void cubicTo(double c1x, double c1y, double c2x, double c2y, double ex, double ey) {
        if (m_elements.empty()) moveTo(0, 0);
        m_elements.push_back(Element{CurveToElement, SwPointF(c1x, c1y)});
        m_elements.push_back(Element{CurveToDataElement, SwPointF(c2x, c2y)});
        m_elements.push_back(Element{CurveToDataElement, SwPointF(ex, ey)});
    }
    /**
     * @brief Performs the `cubicTo` operation.
     * @param c1 Value passed to the method.
     * @param c2 Value passed to the method.
     * @param end Value passed to the method.
     */
    void cubicTo(const SwPointF& c1, const SwPointF& c2, const SwPointF& end) {
        cubicTo(c1.x, c1.y, c2.x, c2.y, end.x, end.y);
    }

    /**
     * @brief Performs the `quadTo` operation.
     * @param cx Value passed to the method.
     * @param cy Value passed to the method.
     * @param ex Value passed to the method.
     * @param ey Value passed to the method.
     */
    void quadTo(double cx, double cy, double ex, double ey) {
        if (m_elements.empty()) moveTo(0, 0);
        SwPointF p0 = currentPosition();
        double c1x = p0.x + 2.0 / 3.0 * (cx - p0.x);
        double c1y = p0.y + 2.0 / 3.0 * (cy - p0.y);
        double c2x = ex + 2.0 / 3.0 * (cx - ex);
        double c2y = ey + 2.0 / 3.0 * (cy - ey);
        cubicTo(c1x, c1y, c2x, c2y, ex, ey);
    }
    /**
     * @brief Performs the `quadTo` operation.
     * @param ctrl Value passed to the method.
     * @param y Vertical coordinate.
     */
    void quadTo(const SwPointF& ctrl, const SwPointF& end) { quadTo(ctrl.x, ctrl.y, end.x, end.y); }

    /**
     * @brief Performs the `arcTo` operation.
     * @param rect Rectangle used by the operation.
     * @param startAngle Value passed to the method.
     * @param sweepLength Value passed to the method.
     */
    void arcTo(const SwRectF& rect, double startAngle, double sweepLength) {
        if (rect.isEmpty()) return;
        double cx = rect.x + rect.width * 0.5;
        double cy = rect.y + rect.height * 0.5;
        double rx = rect.width * 0.5;
        double ry = rect.height * 0.5;

        int segments = std::max(1, static_cast<int>(std::ceil(std::abs(sweepLength) / 90.0)));
        double segAngle = sweepLength / segments;

        double curAngle = startAngle;
        for (int i = 0; i < segments; ++i) {
            double a1 = curAngle * SW_PI / 180.0;
            double a2 = (curAngle + segAngle) * SW_PI / 180.0;

            SwPointF p0(cx + rx * std::cos(a1), cy - ry * std::sin(a1));
            SwPointF p3(cx + rx * std::cos(a2), cy - ry * std::sin(a2));

            double alpha = std::sin(segAngle * SW_PI / 180.0) *
                           (std::sqrt(4.0 + 3.0 * std::pow(std::tan((segAngle * SW_PI / 180.0) * 0.5), 2)) - 1.0) / 3.0;

            SwPointF c1(p0.x - alpha * (-rx * std::sin(a1)),
                        p0.y - alpha * (-ry * (-std::cos(a1))));
            SwPointF c2(p3.x + alpha * (-rx * std::sin(a2)),
                        p3.y + alpha * (-ry * (-std::cos(a2))));

            if (i == 0 && m_elements.empty()) {
                moveTo(p0);
            } else if (i == 0) {
                lineTo(p0);
            }
            cubicTo(c1, c2, p3);
            curAngle += segAngle;
        }
    }

    /**
     * @brief Performs the `arcMoveTo` operation.
     * @param rect Rectangle used by the operation.
     * @param angle Value passed to the method.
     */
    void arcMoveTo(const SwRectF& rect, double angle) {
        double cx = rect.x + rect.width * 0.5;
        double cy = rect.y + rect.height * 0.5;
        double rx = rect.width * 0.5;
        double ry = rect.height * 0.5;
        double rad = angle * SW_PI / 180.0;
        moveTo(cx + rx * std::cos(rad), cy - ry * std::sin(rad));
    }

    /**
     * @brief Closes the subpath handled by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void closeSubpath() {
        m_elements.push_back(Element{CloseSubpathElement, SwPointF()});
    }

    // -------------------------------------------------------------------
    // Convenience shapes
    // -------------------------------------------------------------------
    /**
     * @brief Adds the specified rect.
     * @param rect Rectangle used by the operation.
     */
    void addRect(const SwRectF& rect) {
        moveTo(rect.x, rect.y);
        lineTo(rect.x + rect.width, rect.y);
        lineTo(rect.x + rect.width, rect.y + rect.height);
        lineTo(rect.x, rect.y + rect.height);
        closeSubpath();
    }
    /**
     * @brief Adds the specified rect.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param w Width value.
     */
    void addRect(double x, double y, double w, double h) { addRect(SwRectF(x, y, w, h)); }

    /**
     * @brief Adds the specified ellipse.
     * @param rect Rectangle used by the operation.
     */
    void addEllipse(const SwRectF& rect) {
        double cx = rect.x + rect.width * 0.5;
        double cy = rect.y + rect.height * 0.5;
        double rx = rect.width * 0.5;
        double ry = rect.height * 0.5;
        double k = 0.5522847498;
        moveTo(cx + rx, cy);
        cubicTo(cx + rx, cy - ry * k, cx + rx * k, cy - ry, cx, cy - ry);
        cubicTo(cx - rx * k, cy - ry, cx - rx, cy - ry * k, cx - rx, cy);
        cubicTo(cx - rx, cy + ry * k, cx - rx * k, cy + ry, cx, cy + ry);
        cubicTo(cx + rx * k, cy + ry, cx + rx, cy + ry * k, cx + rx, cy);
        closeSubpath();
    }
    /**
     * @brief Adds the specified ellipse.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param w Width value.
     */
    void addEllipse(double x, double y, double w, double h) { addEllipse(SwRectF(x, y, w, h)); }
    /**
     * @brief Adds the specified ellipse.
     * @param center Value passed to the method.
     * @param rx Value passed to the method.
     * @param ry Value passed to the method.
     */
    void addEllipse(const SwPointF& center, double rx, double ry) {
        addEllipse(SwRectF(center.x - rx, center.y - ry, rx * 2, ry * 2));
    }

    /**
     * @brief Adds the specified rounded Rect.
     * @param rect Rectangle used by the operation.
     * @param xRadius Value passed to the method.
     * @param yRadius Value passed to the method.
     */
    void addRoundedRect(const SwRectF& rect, double xRadius, double yRadius) {
        double x = rect.x, y = rect.y, w = rect.width, h = rect.height;
        double rx = std::min(xRadius, w * 0.5);
        double ry = std::min(yRadius, h * 0.5);
        double k = 0.5522847498;
        moveTo(x + rx, y);
        lineTo(x + w - rx, y);
        cubicTo(x + w - rx + rx * k, y, x + w, y + ry - ry * k, x + w, y + ry);
        lineTo(x + w, y + h - ry);
        cubicTo(x + w, y + h - ry + ry * k, x + w - rx + rx * k, y + h, x + w - rx, y + h);
        lineTo(x + rx, y + h);
        cubicTo(x + rx - rx * k, y + h, x, y + h - ry + ry * k, x, y + h - ry);
        lineTo(x, y + ry);
        cubicTo(x, y + ry - ry * k, x + rx - rx * k, y, x + rx, y);
        closeSubpath();
    }

    /**
     * @brief Adds the specified polygon.
     * @param polygon Value passed to the method.
     */
    void addPolygon(const SwPolygonF& polygon) {
        if (polygon.isEmpty()) return;
        moveTo(polygon.at(0));
        for (int i = 1; i < polygon.size(); ++i) lineTo(polygon.at(i));
    }

    /**
     * @brief Adds the specified path.
     * @param other Value passed to the method.
     */
    void addPath(const SwPainterPath& other) {
        m_elements.insert(m_elements.end(), other.m_elements.begin(), other.m_elements.end());
    }

    /**
     * @brief Performs the `connectPath` operation.
     * @param other Value passed to the method.
     */
    void connectPath(const SwPainterPath& other) {
        if (other.isEmpty()) return;
        if (!m_elements.empty() && other.m_elements.front().type == MoveToElement) {
            lineTo(other.m_elements.front().p);
            for (size_t i = 1; i < other.m_elements.size(); ++i)
                m_elements.push_back(other.m_elements[i]);
        } else {
            addPath(other);
        }
    }

    // -------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------
    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isEmpty() const { return m_elements.empty(); }
    /**
     * @brief Performs the `elementCount` operation.
     * @return The requested element Count.
     */
    int elementCount() const { return static_cast<int>(m_elements.size()); }
    /**
     * @brief Performs the `elementAt` operation.
     * @param i Value passed to the method.
     * @return The requested element At.
     */
    const Element& elementAt(int i) const { return m_elements[static_cast<size_t>(i)]; }

    /**
     * @brief Returns the current elements.
     * @return The current elements.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const std::vector<Element>& elements() const { return m_elements; }

    /**
     * @brief Clears the current object state.
     */
    void clear() { m_elements.clear(); m_fillRule = OddEvenFill; }

    /**
     * @brief Sets the fill Rule.
     * @param rule Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFillRule(FillRule rule) { m_fillRule = rule; }
    /**
     * @brief Returns the current fill Rule.
     * @return The current fill Rule.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    FillRule fillRule() const { return m_fillRule; }

    /**
     * @brief Returns the current current Position.
     * @return The current current Position.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF currentPosition() const {
        for (int i = static_cast<int>(m_elements.size()) - 1; i >= 0; --i) {
            if (m_elements[i].type != CloseSubpathElement)
                return m_elements[i].p;
        }
        return {};
    }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const {
        return controlPointRect();
    }

    /**
     * @brief Returns the current control Point Rect.
     * @return The current control Point Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF controlPointRect() const {
        bool has = false;
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        for (const auto& e : m_elements) {
            if (e.type == CloseSubpathElement) continue;
            if (!has) {
                minX = maxX = e.p.x;
                minY = maxY = e.p.y;
                has = true;
            } else {
                minX = std::min(minX, e.p.x);
                maxX = std::max(maxX, e.p.x);
                minY = std::min(minY, e.p.y);
                maxY = std::max(maxY, e.p.y);
            }
        }
        if (!has) return {};
        return {minX, minY, maxX - minX, maxY - minY};
    }

    /**
     * @brief Performs the `contains` operation.
     * @param point Point used by the operation.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwPointF& point) const {
        SwPolygonF poly = toFillPolygon();
        return poly.containsPoint(point);
    }

    /**
     * @brief Performs the `contains` operation.
     * @param rect Rectangle used by the operation.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwRectF& rect) const {
        SwPolygonF poly = toFillPolygon();
        SwPointF corners[4] = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
        for (int i = 0; i < 4; ++i) {
            if (!poly.containsPoint(corners[i])) return false;
        }
        return true;
    }

    /**
     * @brief Performs the `intersects` operation.
     * @param rect Rectangle used by the operation.
     * @return `true` on success; otherwise `false`.
     */
    bool intersects(const SwRectF& rect) const {
        return boundingRect().intersects(rect) && !intersected_(rect).isEmpty();
    }

    // -------------------------------------------------------------------
    // Flatten path to polygon (for hit-testing and rendering)
    // -------------------------------------------------------------------
    /**
     * @brief Performs the `toFillPolygon` operation.
     * @param transform Value passed to the method.
     * @return The requested to Fill Polygon.
     */
    SwPolygonF toFillPolygon(const SwTransform& transform = SwTransform()) const {
        SwPolygonF poly;
        flattenTo_(poly, transform);
        return poly;
    }

    /**
     * @brief Performs the `toFillPolygon` operation.
     * @param int Value passed to the method.
     * @return The requested to Fill Polygon.
     */
    SwPolygonF toFillPolygon(int /* ignored flatness */) const {
        return toFillPolygon();
    }

    /**
     * @brief Performs the `toSubpathPolygons` operation.
     * @param transform Value passed to the method.
     * @return The requested to Subpath Polygons.
     */
    std::vector<SwPolygonF> toSubpathPolygons(const SwTransform& transform = SwTransform()) const {
        std::vector<SwPolygonF> result;
        SwPolygonF current;
        for (size_t i = 0; i < m_elements.size(); ++i) {
            const auto& e = m_elements[i];
            if (e.type == MoveToElement) {
                if (!current.isEmpty()) result.push_back(current);
                current.clear();
                current.append(transform.map(e.p));
            } else if (e.type == LineToElement) {
                current.append(transform.map(e.p));
            } else if (e.type == CurveToElement && i + 2 < m_elements.size()) {
                SwPointF prev = current.isEmpty() ? SwPointF() : current.last();
                flattenCubic_(prev, e.p, m_elements[i+1].p, m_elements[i+2].p, current, transform);
                i += 2;
            } else if (e.type == CloseSubpathElement) {
                if (!current.isEmpty() && current.size() > 1) {
                    current.append(current.first());
                }
                result.push_back(current);
                current.clear();
            }
        }
        if (!current.isEmpty()) result.push_back(current);
        return result;
    }

    // -------------------------------------------------------------------
    // Translate / transform
    // -------------------------------------------------------------------
    /**
     * @brief Performs the `translate` operation.
     * @param dx Value passed to the method.
     * @param dy Value passed to the method.
     */
    void translate(double dx, double dy) {
        for (auto& e : m_elements) {
            if (e.type != CloseSubpathElement) { e.p.x += dx; e.p.y += dy; }
        }
    }
    /**
     * @brief Performs the `translate` operation.
     * @param y Vertical coordinate.
     */
    void translate(const SwPointF& offset) { translate(offset.x, offset.y); }

    /**
     * @brief Performs the `translated` operation.
     * @param dx Value passed to the method.
     * @param dy Value passed to the method.
     * @return The requested translated.
     */
    SwPainterPath translated(double dx, double dy) const {
        SwPainterPath r = *this;
        r.translate(dx, dy);
        return r;
    }
    /**
     * @brief Performs the `translated` operation.
     * @param y Vertical coordinate.
     * @return The requested translated.
     */
    SwPainterPath translated(const SwPointF& offset) const { return translated(offset.x, offset.y); }

    // -------------------------------------------------------------------
    // Boolean operations (approximate, via polygon)
    // -------------------------------------------------------------------
    /**
     * @brief Performs the `united` operation.
     * @param other Value passed to the method.
     * @return The requested united.
     */
    SwPainterPath united(const SwPainterPath& other) const {
        SwPainterPath result = *this;
        result.addPath(other);
        return result;
    }

    /**
     * @brief Performs the `intersected` operation.
     * @return The requested intersected.
     */
    SwPainterPath intersected(const SwPainterPath& /*other*/) const {
        // Full polygon boolean intersection is complex; return bounding rect intersection.
        return intersected_(boundingRect());
    }

    /**
     * @brief Performs the `subtracted` operation.
     * @return The requested subtracted.
     */
    SwPainterPath subtracted(const SwPainterPath& /*other*/) const {
        // Stub: full boolean subtraction requires a polygon clipper.
        return *this;
    }

    /**
     * @brief Returns the current simplified.
     * @return The current simplified.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath simplified() const {
        return *this;
    }

    // -------------------------------------------------------------------
    // Reverse
    // -------------------------------------------------------------------
    /**
     * @brief Returns the current to Reversed.
     * @return The current to Reversed.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPainterPath toReversed() const {
        SwPainterPath result;
        auto polys = toSubpathPolygons();
        for (auto& poly : polys) {
            if (poly.isEmpty()) continue;
            result.moveTo(poly.last());
            for (int i = poly.size() - 2; i >= 0; --i)
                result.lineTo(poly.at(i));
        }
        return result;
    }

    // -------------------------------------------------------------------
    // Length / percent
    // -------------------------------------------------------------------
    /**
     * @brief Returns the current length.
     * @return The current length.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double length() const {
        double total = 0;
        SwPointF prev;
        bool hasPrev = false;
        for (const auto& e : m_elements) {
            if (e.type == CloseSubpathElement) continue;
            if (hasPrev && (e.type == LineToElement || e.type == CurveToElement)) {
                total += swDistance(prev, e.p);
            }
            prev = e.p;
            hasPrev = true;
        }
        return total;
    }

    /**
     * @brief Performs the `percentAtLength` operation.
     * @param len Value passed to the method.
     * @return The requested percent At Length.
     */
    double percentAtLength(double len) const {
        double total = length();
        if (total < 1e-12) return 0;
        return swClamp_(len / total, 0.0, 1.0);
    }

    /**
     * @brief Performs the `pointAtPercent` operation.
     * @param t Value passed to the method.
     * @return The requested point At Percent.
     */
    SwPointF pointAtPercent(double t) const {
        t = swClamp_(t, 0.0, 1.0);
        double total = length();
        double target = total * t;
        double accum = 0;
        SwPointF prev;
        bool hasPrev = false;
        for (const auto& e : m_elements) {
            if (e.type == CloseSubpathElement) continue;
            if (hasPrev && (e.type == LineToElement || e.type == CurveToElement)) {
                double segLen = swDistance(prev, e.p);
                if (accum + segLen >= target && segLen > 1e-12) {
                    double f = (target - accum) / segLen;
                    return {prev.x + (e.p.x - prev.x) * f, prev.y + (e.p.y - prev.y) * f};
                }
                accum += segLen;
            }
            prev = e.p;
            hasPrev = true;
        }
        return currentPosition();
    }

    /**
     * @brief Performs the `angleAtPercent` operation.
     * @param t Value passed to the method.
     * @return The requested angle At Percent.
     */
    double angleAtPercent(double t) const {
        double eps = 0.001;
        SwPointF p1 = pointAtPercent(std::max(0.0, t - eps));
        SwPointF p2 = pointAtPercent(std::min(1.0, t + eps));
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        return std::atan2(-dy, dx) * 180.0 / SW_PI;
    }

    /**
     * @brief Performs the `slopeAtPercent` operation.
     * @param t Value passed to the method.
     * @return The requested slope At Percent.
     */
    double slopeAtPercent(double t) const {
        double a = angleAtPercent(t) * SW_PI / 180.0;
        double c = std::cos(a);
        if (std::abs(c) < 1e-12) return 1e12;
        return std::sin(a) / c;
    }

    // -------------------------------------------------------------------
    // Swap / comparison
    // -------------------------------------------------------------------
    /**
     * @brief Performs the `swap` operation.
     * @param m_fillRule Value passed to the method.
     */
    void swap(SwPainterPath& other) { m_elements.swap(other.m_elements); std::swap(m_fillRule, other.m_fillRule); }

    /**
     * @brief Performs the `operator==` operation.
     * @param o Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwPainterPath& o) const {
        return m_fillRule == o.m_fillRule && m_elements.size() == o.m_elements.size();
    }
    /**
     * @brief Performs the `operator!=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwPainterPath& o) const { return !(*this == o); }

    // flattenCubic_ is public for external rendering (e.g. SwGraphicsPathItem)
    /**
     * @brief Performs the `flattenCubic_` operation.
     * @param p0 Value passed to the method.
     * @param p1 Value passed to the method.
     * @param p2 Value passed to the method.
     * @param p3 Value passed to the method.
     * @param out Value passed to the method.
     * @param t Value passed to the method.
     * @param mapP0 Value passed to the method.
     * @param depth Value passed to the method.
     * @return The requested flatten Cubic.
     */
    static void flattenCubic_(const SwPointF& p0, const SwPointF& p1, const SwPointF& p2, const SwPointF& p3,
                              SwPolygonF& out, const SwTransform& t, bool mapP0 = false, int depth = 0);

private:
    std::vector<Element> m_elements;
    FillRule m_fillRule{OddEvenFill};

    void flattenTo_(SwPolygonF& out, const SwTransform& t) const {
        SwPointF start;
        for (size_t i = 0; i < m_elements.size(); ++i) {
            const auto& e = m_elements[i];
            if (e.type == MoveToElement) {
                start = e.p;
                out.append(t.map(e.p));
            } else if (e.type == LineToElement) {
                out.append(t.map(e.p));
            } else if (e.type == CurveToElement && i + 2 < m_elements.size()) {
                SwPointF prev = out.isEmpty() ? SwPointF() : out.last();
                flattenCubic_(prev, e.p, m_elements[i+1].p, m_elements[i+2].p, out, t, false);
                i += 2;
            } else if (e.type == CloseSubpathElement) {
                out.append(t.map(start));
            }
        }
    }

    SwPainterPath intersected_(const SwRectF& rect) const {
        SwRectF br = boundingRect();
        SwRectF inter = br.intersected(rect);
        if (inter.isEmpty()) return {};
        SwPainterPath r;
        r.addRect(inter);
        return r;
    }
};

// Inline definition of flattenCubic_ (public static, used by SwGraphicsPathItem paint)
inline void SwPainterPath::flattenCubic_(const SwPointF& p0, const SwPointF& p1,
                                         const SwPointF& p2, const SwPointF& p3,
                                         SwPolygonF& out, const SwTransform& t,
                                         bool mapP0, int depth) {
    if (depth > 8) { out.append(t.map(p3)); return; }
    double dx = p3.x - p0.x, dy = p3.y - p0.y;
    double d = std::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx)
             + std::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);
    if (d * d <= 0.25 * (dx * dx + dy * dy)) {
        if (mapP0) out.append(t.map(p0));
        out.append(t.map(p3));
        return;
    }
    SwPointF p01{(p0.x+p1.x)*0.5, (p0.y+p1.y)*0.5};
    SwPointF p12{(p1.x+p2.x)*0.5, (p1.y+p2.y)*0.5};
    SwPointF p23{(p2.x+p3.x)*0.5, (p2.y+p3.y)*0.5};
    SwPointF p012{(p01.x+p12.x)*0.5, (p01.y+p12.y)*0.5};
    SwPointF p123{(p12.x+p23.x)*0.5, (p12.y+p23.y)*0.5};
    SwPointF pm{(p012.x+p123.x)*0.5, (p012.y+p123.y)*0.5};
    flattenCubic_(p0, p01, p012, pm, out, t, mapP0, depth + 1);
    flattenCubic_(pm, p123, p23, p3, out, t, false, depth + 1);
}
