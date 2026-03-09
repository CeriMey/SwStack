#pragma once

/**
 * @file src/core/gui/graphics/SwGraphicsTypes.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwGraphicsTypes in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the graphics types interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwPointF, SwSizeF, SwRectF, SwLineF, SwPolygonF, and
 * SwTransform.
 *
 * Type-oriented declarations here establish shared vocabulary for the surrounding subsystem so
 * multiple components can exchange data and configuration without ad-hoc conventions.
 *
 * Graphics-facing declarations here define the data flow from high-level UI state to lower-level
 * rendering backends.
 *
 */

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

/**
 * @file
 * @brief Defines the geometry primitives shared by the graphics subsystem.
 *
 * This header plays the same foundational role as a small 2D math module. It
 * provides points, sizes, rectangles, lines, polygons, and supporting helpers
 * that are reused by painting code, scene graphs, event payloads, and view
 * coordinate conversion logic.
 *
 * The types are deliberately plain and header-only so they can be embedded in
 * higher-level classes without additional allocation, virtual dispatch, or
 * platform dependencies.
 */

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef SW_PI
#define SW_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// SwPointF
// ---------------------------------------------------------------------------
/**
 * @brief Represents a point in double-precision 2D space.
 */
struct SwPointF {
    double x{0.0};
    double y{0.0};

    /**
     * @brief Constructs a `SwPointF` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointF() = default;
    /**
     * @brief Constructs a `SwPointF` instance.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPointF(double px, double py) : x(px), y(py) {}

    /**
     * @brief Performs the `manhattanLength` operation.
     * @param y Vertical coordinate.
     * @return The requested manhattan Length.
     */
    double manhattanLength() const { return std::abs(x) + std::abs(y); }
    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isNull() const { return x == 0.0 && y == 0.0; }

    /**
     * @brief Performs the `operator+=` operation.
     * @param o Value passed to the method.
     * @return The requested operator +=.
     */
    SwPointF& operator+=(const SwPointF& o) { x += o.x; y += o.y; return *this; }
    /**
     * @brief Performs the `operator-=` operation.
     * @param o Value passed to the method.
     * @return The requested operator -=.
     */
    SwPointF& operator-=(const SwPointF& o) { x -= o.x; y -= o.y; return *this; }
    /**
     * @brief Performs the `operator*=` operation.
     * @param f Value passed to the method.
     * @return The requested operator *=.
     */
    SwPointF& operator*=(double f) { x *= f; y *= f; return *this; }
    /**
     * @brief Performs the `operator/=` operation.
     * @param f Value passed to the method.
     * @return The requested operator /=.
     */
    SwPointF& operator/=(double f) { x /= f; y /= f; return *this; }

    friend SwPointF operator+(const SwPointF& a, const SwPointF& b) { return {a.x + b.x, a.y + b.y}; }
    friend SwPointF operator-(const SwPointF& a, const SwPointF& b) { return {a.x - b.x, a.y - b.y}; }
    friend SwPointF operator*(const SwPointF& p, double f) { return {p.x * f, p.y * f}; }
    friend SwPointF operator*(double f, const SwPointF& p) { return {p.x * f, p.y * f}; }
    friend bool operator==(const SwPointF& a, const SwPointF& b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(const SwPointF& a, const SwPointF& b) { return !(a == b); }

    /**
     * @brief Performs the `dotProduct` operation.
     * @param a Value passed to the method.
     * @param b Value passed to the method.
     * @return The requested dot Product.
     */
    static double dotProduct(const SwPointF& a, const SwPointF& b) { return a.x * b.x + a.y * b.y; }
};

struct SwRectF;

inline SwRectF swBoundingRectOfPoints(const SwPointF* points, int count);
inline double swDistance(const SwPointF& a, const SwPointF& b);

// ---------------------------------------------------------------------------
// SwSizeF
// ---------------------------------------------------------------------------
/**
 * @brief Represents a width/height pair in double-precision space.
 */
struct SwSizeF {
    double w{-1.0};
    double h{-1.0};

    /**
     * @brief Constructs a `SwSizeF` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwSizeF() = default;
    /**
     * @brief Constructs a `SwSizeF` instance.
     * @param pw Value passed to the method.
     * @param ph Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwSizeF(double pw, double ph) : w(pw), h(ph) {}

    /**
     * @brief Returns the current width.
     * @return The current width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double width() const { return w; }
    /**
     * @brief Returns the current height.
     * @return The current height.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double height() const { return h; }
    /**
     * @brief Sets the width.
     * @param pw Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWidth(double pw) { w = pw; }
    /**
     * @brief Sets the height.
     * @param ph Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHeight(double ph) { h = ph; }

    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEmpty() const { return w <= 0.0 || h <= 0.0; }
    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isNull() const { return w == 0.0 && h == 0.0; }
    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const { return w >= 0.0 && h >= 0.0; }

    /**
     * @brief Performs the `transpose` operation.
     */
    void transpose() { double t = w; w = h; h = t; }
    /**
     * @brief Returns the current transposed.
     * @return The current transposed.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSizeF transposed() const { return {h, w}; }

    /**
     * @brief Performs the `expandedTo` operation.
     * @param h Height value.
     * @return The requested expanded To.
     */
    SwSizeF expandedTo(const SwSizeF& o) const { return {std::max(w, o.w), std::max(h, o.h)}; }
    /**
     * @brief Performs the `boundedTo` operation.
     * @param h Height value.
     * @return The requested bounded To.
     */
    SwSizeF boundedTo(const SwSizeF& o) const { return {std::min(w, o.w), std::min(h, o.h)}; }

    /**
     * @brief Performs the `operator+=` operation.
     * @param o Value passed to the method.
     * @return The requested operator +=.
     */
    SwSizeF& operator+=(const SwSizeF& o) { w += o.w; h += o.h; return *this; }
    /**
     * @brief Performs the `operator-=` operation.
     * @param o Value passed to the method.
     * @return The requested operator -=.
     */
    SwSizeF& operator-=(const SwSizeF& o) { w -= o.w; h -= o.h; return *this; }
    /**
     * @brief Performs the `operator*=` operation.
     * @param f Value passed to the method.
     * @return The requested operator *=.
     */
    SwSizeF& operator*=(double f) { w *= f; h *= f; return *this; }
    /**
     * @brief Performs the `operator/=` operation.
     * @param f Value passed to the method.
     * @return The requested operator /=.
     */
    SwSizeF& operator/=(double f) { w /= f; h /= f; return *this; }

    friend bool operator==(const SwSizeF& a, const SwSizeF& b) { return a.w == b.w && a.h == b.h; }
    friend bool operator!=(const SwSizeF& a, const SwSizeF& b) { return !(a == b); }
};

// ---------------------------------------------------------------------------
// SwRectF
// ---------------------------------------------------------------------------
/**
 * @brief Represents an axis-aligned rectangle using double-precision geometry.
 */
struct SwRectF {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    /**
     * @brief Constructs a `SwRectF` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRectF() = default;
    /**
     * @brief Constructs a `SwRectF` instance.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     * @param w Width value.
     * @param h Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRectF(double px, double py, double w, double h) : x(px), y(py), width(w), height(h) {}
    /**
     * @brief Constructs a `SwRectF` instance.
     * @param topLeft Value passed to the method.
     * @param h Height value.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRectF(const SwPointF& topLeft, const SwSizeF& size) : x(topLeft.x), y(topLeft.y), width(size.w), height(size.h) {}
    /**
     * @brief Constructs a `SwRectF` instance.
     * @param topLeft Value passed to the method.
     * @param y Vertical coordinate.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwRectF(const SwPointF& topLeft, const SwPointF& bottomRight)
        : x(topLeft.x), y(topLeft.y), width(bottomRight.x - topLeft.x), height(bottomRight.y - topLeft.y) {}

    /**
     * @brief Returns the current left.
     * @return The current left.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double left() const { return x; }
    /**
     * @brief Returns the current top.
     * @return The current top.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double top() const { return y; }
    /**
     * @brief Returns the current right.
     * @return The current right.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double right() const { return x + width; }
    /**
     * @brief Returns the current bottom.
     * @return The current bottom.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double bottom() const { return y + height; }

    /**
     * @brief Returns the current top Left.
     * @return The current top Left.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF topLeft() const { return {x, y}; }
    /**
     * @brief Returns the current top Right.
     * @return The current top Right.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF topRight() const { return {x + width, y}; }
    /**
     * @brief Returns the current bottom Left.
     * @return The current bottom Left.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF bottomLeft() const { return {x, y + height}; }
    /**
     * @brief Returns the current bottom Right.
     * @return The current bottom Right.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF bottomRight() const { return {x + width, y + height}; }
    /**
     * @brief Returns the current center.
     * @return The current center.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwPointF center() const { return {x + width * 0.5, y + height * 0.5}; }

    /**
     * @brief Returns the current size.
     * @return The current size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwSizeF size() const { return {width, height}; }
    /**
     * @brief Sets the size.
     * @param s Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setSize(const SwSizeF& s) { width = s.w; height = s.h; }

    /**
     * @brief Sets the left.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLeft(double v) { double d = v - x; x = v; width -= d; }
    /**
     * @brief Sets the top.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTop(double v) { double d = v - y; y = v; height -= d; }
    /**
     * @brief Sets the right.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRight(double v) { width = v - x; }
    /**
     * @brief Sets the bottom.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBottom(double v) { height = v - y; }
    /**
     * @brief Sets the top Left.
     * @param y Vertical coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTopLeft(const SwPointF& p) { setLeft(p.x); setTop(p.y); }
    /**
     * @brief Sets the bottom Right.
     * @param y Vertical coordinate.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setBottomRight(const SwPointF& p) { setRight(p.x); setBottom(p.y); }

    /**
     * @brief Sets the x.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setX(double v) { x = v; }
    /**
     * @brief Sets the y.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setY(double v) { y = v; }
    /**
     * @brief Sets the width.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWidth(double v) { width = v; }
    /**
     * @brief Sets the height.
     * @param v Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setHeight(double v) { height = v; }

    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isEmpty() const { return width <= 0.0 || height <= 0.0; }
    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isNull() const { return width == 0.0 && height == 0.0; }
    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const { return width > 0.0 && height > 0.0; }

    /**
     * @brief Returns the current normalized.
     * @return The current normalized.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF normalized() const {
        SwRectF r = *this;
        if (r.width < 0.0) { r.x += r.width; r.width = -r.width; }
        if (r.height < 0.0) { r.y += r.height; r.height = -r.height; }
        return r;
    }

    /**
     * @brief Performs the `contains` operation.
     * @param p Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwPointF& p) const {
        const SwRectF r = normalized();
        return p.x >= r.left() && p.x <= r.right() && p.y >= r.top() && p.y <= r.bottom();
    }

    /**
     * @brief Performs the `contains` operation.
     * @param other Value passed to the method.
     * @return `true` when the object reports contains; otherwise `false`.
     */
    bool contains(const SwRectF& other) const {
        const SwRectF a = normalized();
        const SwRectF b = other.normalized();
        return b.left() >= a.left() && b.right() <= a.right()
            && b.top() >= a.top() && b.bottom() <= a.bottom();
    }

    /**
     * @brief Performs the `intersects` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool intersects(const SwRectF& other) const {
        const SwRectF a = normalized();
        const SwRectF b = other.normalized();
        return a.left() < b.right() && a.right() > b.left()
            && a.top() < b.bottom() && a.bottom() > b.top();
    }

    /**
     * @brief Performs the `intersected` operation.
     * @param other Value passed to the method.
     * @return The requested intersected.
     */
    SwRectF intersected(const SwRectF& other) const {
        const SwRectF a = normalized();
        const SwRectF b = other.normalized();
        double l = std::max(a.left(), b.left());
        double t = std::max(a.top(), b.top());
        double r = std::min(a.right(), b.right());
        double bo = std::min(a.bottom(), b.bottom());
        if (l >= r || t >= bo) return {};
        return {l, t, r - l, bo - t};
    }

    /**
     * @brief Performs the `united` operation.
     * @param other Value passed to the method.
     * @return The requested united.
     */
    SwRectF united(const SwRectF& other) const {
        if (isEmpty()) return other.normalized();
        if (other.isEmpty()) return normalized();
        const SwRectF a = normalized();
        const SwRectF b = other.normalized();
        double l = std::min(a.left(), b.left());
        double t = std::min(a.top(), b.top());
        double r = std::max(a.right(), b.right());
        double bo = std::max(a.bottom(), b.bottom());
        return {l, t, r - l, bo - t};
    }

    /**
     * @brief Performs the `translate` operation.
     * @param dx Value passed to the method.
     * @param dy Value passed to the method.
     */
    void translate(double dx, double dy) { x += dx; y += dy; }
    /**
     * @brief Performs the `translate` operation.
     * @param offset Value passed to the method.
     */
    void translate(const SwPointF& offset) { x += offset.x; y += offset.y; }
    /**
     * @brief Performs the `translated` operation.
     * @param dx Value passed to the method.
     * @param dy Value passed to the method.
     * @return The requested translated.
     */
    SwRectF translated(double dx, double dy) const { return {x + dx, y + dy, width, height}; }
    /**
     * @brief Performs the `translated` operation.
     * @param offset Value passed to the method.
     * @return The requested translated.
     */
    SwRectF translated(const SwPointF& offset) const { return {x + offset.x, y + offset.y, width, height}; }

    /**
     * @brief Performs the `moveCenter` operation.
     * @param p Value passed to the method.
     */
    void moveCenter(const SwPointF& p) { x = p.x - width * 0.5; y = p.y - height * 0.5; }
    /**
     * @brief Performs the `moveTopLeft` operation.
     * @param p Value passed to the method.
     */
    void moveTopLeft(const SwPointF& p) { x = p.x; y = p.y; }
    /**
     * @brief Performs the `moveTo` operation.
     * @param px Value passed to the method.
     * @param py Value passed to the method.
     */
    void moveTo(double px, double py) { x = px; y = py; }
    /**
     * @brief Performs the `moveTo` operation.
     * @param p Value passed to the method.
     */
    void moveTo(const SwPointF& p) { x = p.x; y = p.y; }

    /**
     * @brief Performs the `adjust` operation.
     * @param dx1 Value passed to the method.
     * @param dy1 Value passed to the method.
     * @param dx2 Value passed to the method.
     * @param dy2 Value passed to the method.
     */
    void adjust(double dx1, double dy1, double dx2, double dy2) {
        x += dx1; y += dy1; width += dx2 - dx1; height += dy2 - dy1;
    }
    /**
     * @brief Performs the `adjusted` operation.
     * @param dx1 Value passed to the method.
     * @param dy1 Value passed to the method.
     * @param dx2 Value passed to the method.
     * @param dy2 Value passed to the method.
     * @return The requested adjusted.
     */
    SwRectF adjusted(double dx1, double dy1, double dx2, double dy2) const {
        return {x + dx1, y + dy1, width + dx2 - dx1, height + dy2 - dy1};
    }

    friend bool operator==(const SwRectF& a, const SwRectF& b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    friend bool operator!=(const SwRectF& a, const SwRectF& b) { return !(a == b); }
};

// ---------------------------------------------------------------------------
// SwLineF
// ---------------------------------------------------------------------------
/**
 * @brief Represents a line segment between two points.
 */
struct SwLineF {
    SwPointF p1{};
    SwPointF p2{};

    /**
     * @brief Constructs a `SwLineF` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwLineF() = default;
    /**
     * @brief Constructs a `SwLineF` instance.
     * @param a Value passed to the method.
     * @param b Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwLineF(const SwPointF& a, const SwPointF& b) : p1(a), p2(b) {}
    /**
     * @brief Constructs a `SwLineF` instance.
     * @param x1 Value passed to the method.
     * @param y1 Value passed to the method.
     * @param x2 Value passed to the method.
     * @param y2 Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwLineF(double x1, double y1, double x2, double y2) : p1(x1, y1), p2(x2, y2) {}

    /**
     * @brief Performs the `length` operation.
     * @param p2 Value passed to the method.
     * @return The current length value.
     */
    double length() const { return swDistance(p1, p2); }
    /**
     * @brief Returns the current dx.
     * @return The current dx.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double dx() const { return p2.x - p1.x; }
    /**
     * @brief Returns the current dy.
     * @return The current dy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double dy() const { return p2.y - p1.y; }
    /**
     * @brief Performs the `angle` operation.
     * @return The requested angle.
     */
    double angle() const { return std::atan2(-dy(), dx()) * 180.0 / SW_PI; }
    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isNull() const { return p1 == p2; }

    /**
     * @brief Performs the `center` operation.
     * @param y Vertical coordinate.
     * @return The requested center.
     */
    SwPointF center() const { return {(p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5}; }
    /**
     * @brief Performs the `pointAt` operation.
     * @param y Vertical coordinate.
     * @return The requested point At.
     */
    SwPointF pointAt(double t) const { return {p1.x + (p2.x - p1.x) * t, p1.y + (p2.y - p1.y) * t}; }

    /**
     * @brief Sets the length.
     * @param len Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLength(double len) {
        double l = length();
        if (l > 1e-12) { double f = len / l; p2 = {p1.x + dx() * f, p1.y + dy() * f}; }
    }

    /**
     * @brief Performs the `normalVector` operation.
     * @return The requested normal Vector.
     */
    SwLineF normalVector() const { return {p1, {p1.x + dy(), p1.y - dx()}}; }
    /**
     * @brief Returns the current unit Vector.
     * @return The current unit Vector.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwLineF unitVector() const {
        double l = length();
        if (l < 1e-12) return *this;
        return {p1, {p1.x + dx() / l, p1.y + dy() / l}};
    }

    /**
     * @brief Performs the `translate` operation.
     * @param offset Value passed to the method.
     */
    void translate(const SwPointF& offset) { p1 += offset; p2 += offset; }
    /**
     * @brief Performs the `translate` operation.
     * @param dx Value passed to the method.
     */
    void translate(double dx, double dy) { translate(SwPointF(dx, dy)); }
    /**
     * @brief Performs the `translated` operation.
     * @param offset Value passed to the method.
     * @return The requested translated.
     */
    SwLineF translated(const SwPointF& offset) const { return {p1 + offset, p2 + offset}; }
};

// ---------------------------------------------------------------------------
// SwPolygonF
// ---------------------------------------------------------------------------
struct SwPolygonF {
    std::vector<SwPointF> points;

    /**
     * @brief Constructs a `SwPolygonF` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPolygonF() = default;
    /**
     * @brief Constructs a `SwPolygonF` instance.
     * @param pts Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPolygonF(std::initializer_list<SwPointF> pts) : points(pts) {}
    /**
     * @brief Constructs a `SwPolygonF` instance.
     * @param pts Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwPolygonF(const std::vector<SwPointF>& pts) : points(pts) {}

    /**
     * @brief Performs the `append` operation.
     * @param p Value passed to the method.
     */
    void append(const SwPointF& p) { points.push_back(p); }
    /**
     * @brief Performs the `size` operation.
     * @return The current size value.
     */
    int size() const { return static_cast<int>(points.size()); }
    /**
     * @brief Performs the `count` operation.
     * @return The current count value.
     */
    int count() const { return size(); }
    /**
     * @brief Returns whether the object reports empty.
     * @return `true` when the object reports empty; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isEmpty() const { return points.empty(); }
    /**
     * @brief Clears the current object state.
     */
    void clear() { points.clear(); }
    /**
     * @brief Performs the `at` operation.
     * @param i Value passed to the method.
     * @return The requested at.
     */
    const SwPointF& at(int i) const { return points[static_cast<size_t>(i)]; }
    /**
     * @brief Performs the `first` operation.
     * @return The requested first.
     */
    const SwPointF& first() const { return points.front(); }
    /**
     * @brief Performs the `last` operation.
     * @return The requested last.
     */
    const SwPointF& last() const { return points.back(); }
    /**
     * @brief Performs the `operator[]` operation.
     * @param i Value passed to the method.
     * @return The requested operator [].
     */
    SwPointF& operator[](int i) { return points[static_cast<size_t>(i)]; }
    /**
     * @brief Performs the `operator[]` operation.
     * @param i Value passed to the method.
     * @return The requested operator [].
     */
    const SwPointF& operator[](int i) const { return points[static_cast<size_t>(i)]; }

    /**
     * @brief Returns the current bounding Rect.
     * @return The current bounding Rect.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwRectF boundingRect() const {
        if (points.empty()) return {};
        return swBoundingRectOfPoints(points.data(), static_cast<int>(points.size()));
    }

    /**
     * @brief Performs the `containsPoint` operation.
     * @param p Value passed to the method.
     * @return `true` when the object reports contains Point; otherwise `false`.
     */
    bool containsPoint(const SwPointF& p) const {
        int n = static_cast<int>(points.size());
        if (n < 3) return false;
        bool inside = false;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            if (((points[i].y > p.y) != (points[j].y > p.y)) &&
                (p.x < (points[j].x - points[i].x) * (p.y - points[i].y) / (points[j].y - points[i].y) + points[i].x)) {
                inside = !inside;
            }
        }
        return inside;
    }

    /**
     * @brief Performs the `translated` operation.
     * @param dx Value passed to the method.
     * @param dy Value passed to the method.
     * @return The requested translated.
     */
    SwPolygonF translated(double dx, double dy) const {
        SwPolygonF r;
        r.points.reserve(points.size());
        for (const auto& p : points) r.append({p.x + dx, p.y + dy});
        return r;
    }
    /**
     * @brief Performs the `translated` operation.
     * @param y Vertical coordinate.
     * @return The requested translated.
     */
    SwPolygonF translated(const SwPointF& offset) const { return translated(offset.x, offset.y); }
    /**
     * @brief Performs the `translate` operation.
     * @param dx Value passed to the method.
     * @param points Value passed to the method.
     */
    void translate(double dx, double dy) { for (auto& p : points) { p.x += dx; p.y += dy; } }
    /**
     * @brief Performs the `translate` operation.
     * @param y Vertical coordinate.
     */
    void translate(const SwPointF& offset) { translate(offset.x, offset.y); }
};

// ---------------------------------------------------------------------------
// Free helpers
// ---------------------------------------------------------------------------
inline SwRectF swBoundingRectOfPoints(const SwPointF* points, int count) {
    if (!points || count <= 0) return {};
    double minX = points[0].x, maxX = points[0].x;
    double minY = points[0].y, maxY = points[0].y;
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

// ---------------------------------------------------------------------------
// SwTransform  (QTransform equivalent — 3x3 affine / projective matrix)
//
//   | m11  m12  m13 |     Mapping: x' = m11*x + m21*y + m31
//   | m21  m22  m23 |              y' = m12*x + m22*y + m32
//   | m31  m32  m33 |              w' = m13*x + m23*y + m33
//                                  result = (x'/w', y'/w')
// ---------------------------------------------------------------------------
class SwTransform {
public:
    enum TransformationType { TxNone, TxTranslate, TxScale, TxRotate, TxShear, TxProject };

    /**
     * @brief Constructs a `SwTransform` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwTransform()
        : m_11(1), m_12(0), m_13(0)
        , m_21(0), m_22(1), m_23(0)
        , m_31(0), m_32(0), m_33(1) {}

    /**
     * @brief Constructs a `SwTransform` instance.
     * @param m11 Value passed to the method.
     * @param m12 Value passed to the method.
     * @param m13 Value passed to the method.
     * @param m21 Value passed to the method.
     * @param m22 Value passed to the method.
     * @param m23 Value passed to the method.
     * @param m31 Value passed to the method.
     * @param m32 Value passed to the method.
     * @param m33 Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwTransform(double m11, double m12, double m13,
                double m21, double m22, double m23,
                double m31, double m32, double m33)
        : m_11(m11), m_12(m12), m_13(m13)
        , m_21(m21), m_22(m22), m_23(m23)
        , m_31(m31), m_32(m32), m_33(m33) {}

    /**
     * @brief Constructs a `SwTransform` instance.
     * @param m11 Value passed to the method.
     * @param m12 Value passed to the method.
     * @param m21 Value passed to the method.
     * @param m22 Value passed to the method.
     * @param dx Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwTransform(double m11, double m12,
                double m21, double m22,
                double dx, double dy)
        : m_11(m11), m_12(m12), m_13(0)
        , m_21(m21), m_22(m22), m_23(0)
        , m_31(dx), m_32(dy), m_33(1) {}

    // Element access
    /**
     * @brief Returns the current m11.
     * @return The current m11.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m11() const { return m_11; }
    /**
     * @brief Returns the current m12.
     * @return The current m12.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m12() const { return m_12; }
    /**
     * @brief Returns the current m13.
     * @return The current m13.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m13() const { return m_13; }
    /**
     * @brief Returns the current m21.
     * @return The current m21.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m21() const { return m_21; }
    /**
     * @brief Returns the current m22.
     * @return The current m22.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m22() const { return m_22; }
    /**
     * @brief Returns the current m23.
     * @return The current m23.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m23() const { return m_23; }
    /**
     * @brief Returns the current m31.
     * @return The current m31.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m31() const { return m_31; }
    /**
     * @brief Returns the current m32.
     * @return The current m32.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m32() const { return m_32; }
    /**
     * @brief Returns the current m33.
     * @return The current m33.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double m33() const { return m_33; }
    /**
     * @brief Returns the current dx.
     * @return The current dx.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double dx() const { return m_31; }
    /**
     * @brief Returns the current dy.
     * @return The current dy.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double dy() const { return m_32; }

    /**
     * @brief Sets the matrix.
     * @param m11 Value passed to the method.
     * @param m12 Value passed to the method.
     * @param m13 Value passed to the method.
     * @param m21 Value passed to the method.
     * @param m22 Value passed to the method.
     * @param m23 Value passed to the method.
     * @param m31 Value passed to the method.
     * @param m32 Value passed to the method.
     * @param m33 Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMatrix(double m11, double m12, double m13,
                   double m21, double m22, double m23,
                   double m31, double m32, double m33) {
        m_11 = m11; m_12 = m12; m_13 = m13;
        m_21 = m21; m_22 = m22; m_23 = m23;
        m_31 = m31; m_32 = m32; m_33 = m33;
    }

    // Identity / type queries
    /**
     * @brief Returns whether the object reports identity.
     * @return `true` when the object reports identity; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isIdentity() const {
        return m_11 == 1 && m_12 == 0 && m_13 == 0
            && m_21 == 0 && m_22 == 1 && m_23 == 0
            && m_31 == 0 && m_32 == 0 && m_33 == 1;
    }
    /**
     * @brief Returns whether the object reports affine.
     * @return `true` when the object reports affine; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isAffine() const { return m_13 == 0 && m_23 == 0 && m_33 == 1; }
    /**
     * @brief Returns whether the object reports translating.
     * @return `true` when the object reports translating; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isTranslating() const { return m_31 != 0 || m_32 != 0; }
    /**
     * @brief Returns whether the object reports rotating.
     * @return `true` when the object reports rotating; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isRotating() const { return m_12 != 0 || m_21 != 0; }
    /**
     * @brief Returns whether the object reports scaling.
     * @return `true` when the object reports scaling; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isScaling() const { return m_11 != 1 || m_22 != 1; }

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    TransformationType type() const {
        if (m_13 != 0 || m_23 != 0 || m_33 != 1) return TxProject;
        if (m_12 != 0 || m_21 != 0) {
            double d1 = m_11 - m_22;
            double d2 = m_12 + m_21;
            if (std::abs(d1) < 1e-12 && std::abs(d2) < 1e-12) return TxRotate;
            return TxShear;
        }
        if (m_11 != 1 || m_22 != 1) return TxScale;
        if (m_31 != 0 || m_32 != 0) return TxTranslate;
        return TxNone;
    }

    /**
     * @brief Returns the current determinant.
     * @return The current determinant.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double determinant() const {
        return m_11 * (m_22 * m_33 - m_23 * m_32)
             - m_12 * (m_21 * m_33 - m_23 * m_31)
             + m_13 * (m_21 * m_32 - m_22 * m_31);
    }

    /**
     * @brief Returns whether the object reports invertible.
     * @return `true` when the object reports invertible; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isInvertible() const { return std::abs(determinant()) > 1e-12; }

    /**
     * @brief Resets the object to a baseline state.
     * @param this Value passed to the method.
     */
    void reset() { *this = SwTransform(); }

    // Chainable mutators (post-multiply style, same as Qt)
    /**
     * @brief Performs the `translate` operation.
     * @param tdx Value passed to the method.
     * @param tdy Value passed to the method.
     * @return The requested translate.
     */
    SwTransform& translate(double tdx, double tdy) {
        m_31 += m_11 * tdx + m_21 * tdy;
        m_32 += m_12 * tdx + m_22 * tdy;
        m_33 += m_13 * tdx + m_23 * tdy;
        return *this;
    }

    /**
     * @brief Performs the `scale` operation.
     * @param sx Value passed to the method.
     * @param sy Value passed to the method.
     * @return The requested scale.
     */
    SwTransform& scale(double sx, double sy) {
        m_11 *= sx; m_12 *= sx; m_13 *= sx;
        m_21 *= sy; m_22 *= sy; m_23 *= sy;
        return *this;
    }

    /**
     * @brief Performs the `rotate` operation.
     * @param angleDegrees Value passed to the method.
     * @return The requested rotate.
     */
    SwTransform& rotate(double angleDegrees) {
        double rad = angleDegrees * SW_PI / 180.0;
        double c = std::cos(rad);
        double s = std::sin(rad);
        double t11 = m_11 * c + m_21 * s;
        double t12 = m_12 * c + m_22 * s;
        double t13 = m_13 * c + m_23 * s;
        double t21 = -m_11 * s + m_21 * c;
        double t22 = -m_12 * s + m_22 * c;
        double t23 = -m_13 * s + m_23 * c;
        m_11 = t11; m_12 = t12; m_13 = t13;
        m_21 = t21; m_22 = t22; m_23 = t23;
        return *this;
    }

    /**
     * @brief Performs the `shear` operation.
     * @param sh Value passed to the method.
     * @param sv Value passed to the method.
     * @return The requested shear.
     */
    SwTransform& shear(double sh, double sv) {
        double t11 = m_11 + m_21 * sv;
        double t12 = m_12 + m_22 * sv;
        double t13 = m_13 + m_23 * sv;
        double t21 = m_11 * sh + m_21;
        double t22 = m_12 * sh + m_22;
        double t23 = m_13 * sh + m_23;
        m_11 = t11; m_12 = t12; m_13 = t13;
        m_21 = t21; m_22 = t22; m_23 = t23;
        return *this;
    }

    // Mapping
    /**
     * @brief Performs the `map` operation.
     * @param p Value passed to the method.
     * @return The requested map.
     */
    SwPointF map(const SwPointF& p) const {
        double fx = m_11 * p.x + m_21 * p.y + m_31;
        double fy = m_12 * p.x + m_22 * p.y + m_32;
        if (!isAffine()) {
            double fw = m_13 * p.x + m_23 * p.y + m_33;
            if (std::abs(fw) > 1e-12) { fx /= fw; fy /= fw; }
        }
        return {fx, fy};
    }

    /**
     * @brief Performs the `map` operation.
     * @param x Horizontal coordinate.
     * @param y Vertical coordinate.
     * @param mx Value passed to the method.
     * @param my Value passed to the method.
     */
    void map(double x, double y, double* mx, double* my) const {
        SwPointF r = map(SwPointF(x, y));
        if (mx) *mx = r.x;
        if (my) *my = r.y;
    }

    /**
     * @brief Performs the `map` operation.
     * @param p2 Value passed to the method.
     * @return The requested map.
     */
    SwLineF map(const SwLineF& l) const { return {map(l.p1), map(l.p2)}; }

    /**
     * @brief Performs the `map` operation.
     * @param poly Value passed to the method.
     * @return The requested map.
     */
    SwPolygonF map(const SwPolygonF& poly) const {
        SwPolygonF result;
        result.points.reserve(poly.points.size());
        for (const auto& p : poly.points) result.append(map(p));
        return result;
    }

    /**
     * @brief Performs the `mapRect` operation.
     * @param r Value passed to the method.
     * @return The requested map Rect.
     */
    SwRectF mapRect(const SwRectF& r) const {
        SwPointF c[4] = {
            map({r.x, r.y}), map({r.x + r.width, r.y}),
            map({r.x + r.width, r.y + r.height}), map({r.x, r.y + r.height})
        };
        return swBoundingRectOfPoints(c, 4);
    }

    /**
     * @brief Performs the `mapToPolygon` operation.
     * @param r Value passed to the method.
     * @return The requested map To Polygon.
     */
    SwPolygonF mapToPolygon(const SwRectF& r) const {
        return {{map({r.x, r.y}), map({r.x + r.width, r.y}),
                 map({r.x + r.width, r.y + r.height}), map({r.x, r.y + r.height})}};
    }

    // Inverse
    /**
     * @brief Performs the `inverted` operation.
     * @param invertible Value passed to the method.
     * @return The requested inverted.
     */
    SwTransform inverted(bool* invertible = nullptr) const {
        double det = determinant();
        if (std::abs(det) < 1e-12) {
            if (invertible) *invertible = false;
            return {};
        }
        if (invertible) *invertible = true;
        double inv = 1.0 / det;
        return SwTransform(
            (m_22 * m_33 - m_23 * m_32) * inv, (m_13 * m_32 - m_12 * m_33) * inv, (m_12 * m_23 - m_13 * m_22) * inv,
            (m_23 * m_31 - m_21 * m_33) * inv, (m_11 * m_33 - m_13 * m_31) * inv, (m_13 * m_21 - m_11 * m_23) * inv,
            (m_21 * m_32 - m_22 * m_31) * inv, (m_12 * m_31 - m_11 * m_32) * inv, (m_11 * m_22 - m_12 * m_21) * inv
        );
    }

    /**
     * @brief Returns the current transposed.
     * @return The current transposed.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwTransform transposed() const {
        return {m_11, m_21, m_31, m_12, m_22, m_32, m_13, m_23, m_33};
    }

    /**
     * @brief Returns the current adjoint.
     * @return The current adjoint.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwTransform adjoint() const {
        return {
            m_22*m_33 - m_23*m_32, m_13*m_32 - m_12*m_33, m_12*m_23 - m_13*m_22,
            m_23*m_31 - m_21*m_33, m_11*m_33 - m_13*m_31, m_13*m_21 - m_11*m_23,
            m_21*m_32 - m_22*m_31, m_12*m_31 - m_11*m_32, m_11*m_22 - m_12*m_21
        };
    }

    // Operators
    /**
     * @brief Performs the `operator*` operation.
     * @param o Value passed to the method.
     * @return The requested operator *.
     */
    SwTransform operator*(const SwTransform& o) const {
        return {
            m_11*o.m_11 + m_12*o.m_21 + m_13*o.m_31,
            m_11*o.m_12 + m_12*o.m_22 + m_13*o.m_32,
            m_11*o.m_13 + m_12*o.m_23 + m_13*o.m_33,
            m_21*o.m_11 + m_22*o.m_21 + m_23*o.m_31,
            m_21*o.m_12 + m_22*o.m_22 + m_23*o.m_32,
            m_21*o.m_13 + m_22*o.m_23 + m_23*o.m_33,
            m_31*o.m_11 + m_32*o.m_21 + m_33*o.m_31,
            m_31*o.m_12 + m_32*o.m_22 + m_33*o.m_32,
            m_31*o.m_13 + m_32*o.m_23 + m_33*o.m_33
        };
    }
    /**
     * @brief Performs the `operator*=` operation.
     * @param o Value passed to the method.
     * @return The requested operator *=.
     */
    SwTransform& operator*=(const SwTransform& o) { *this = *this * o; return *this; }
    /**
     * @brief Performs the `operator==` operation.
     * @param o Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwTransform& o) const {
        return m_11==o.m_11 && m_12==o.m_12 && m_13==o.m_13
            && m_21==o.m_21 && m_22==o.m_22 && m_23==o.m_23
            && m_31==o.m_31 && m_32==o.m_32 && m_33==o.m_33;
    }
    /**
     * @brief Performs the `operator!=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwTransform& o) const { return !(*this == o); }

    // Static factories
    /**
     * @brief Performs the `fromTranslate` operation.
     * @param tdx Value passed to the method.
     * @param tdy Value passed to the method.
     * @return The requested from Translate.
     */
    static SwTransform fromTranslate(double tdx, double tdy) { return {1,0,0, 0,1,0, tdx,tdy,1}; }
    /**
     * @brief Performs the `fromScale` operation.
     * @param sx Value passed to the method.
     * @param sy Value passed to the method.
     * @return The requested from Scale.
     */
    static SwTransform fromScale(double sx, double sy) { return {sx,0,0, 0,sy,0, 0,0,1}; }
    /**
     * @brief Performs the `fromRotate` operation.
     * @param angleDegrees Value passed to the method.
     * @return The requested from Rotate.
     */
    static SwTransform fromRotate(double angleDegrees) { SwTransform t; t.rotate(angleDegrees); return t; }
    /**
     * @brief Performs the `fromShear` operation.
     * @param sh Value passed to the method.
     * @param sv Value passed to the method.
     * @return The requested from Shear.
     */
    static SwTransform fromShear(double sh, double sv) { SwTransform t; t.shear(sh, sv); return t; }

private:
    double m_11, m_12, m_13;
    double m_21, m_22, m_23;
    double m_31, m_32, m_33;
};
