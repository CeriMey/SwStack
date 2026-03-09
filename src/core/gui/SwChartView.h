#pragma once

/**
 * @file src/core/gui/SwChartView.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwChartView in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the chart view interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwChartView.
 *
 * View-oriented declarations here mainly describe how underlying state is projected into a visual
 * or interactive surface, including how refresh, selection, or presentation concerns are exposed
 * at the API boundary.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
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

#include "SwAreaSeries.h"
#include "SwBarSeries.h"
#include "SwCandlestickSeries.h"
#include "SwChart.h"
#include "SwFont.h"
#include "SwPainter.h"
#include "SwPieSeries.h"
#include "SwScatterSeries.h"
#include "SwStepLineSeries.h"
#include "SwSplineSeries.h"
#include "SwWidget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

class SwChartView : public SwWidget {
    SW_OBJECT(SwChartView, SwWidget)

public:
    /**
     * @brief Constructs a `SwChartView` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwChartView(SwWidget* parent = nullptr)
        : SwWidget(parent) {
        resize(460, 280);
        setFont(SwFont(L"Segoe UI", 10, Medium));
        setFocusPolicy(FocusPolicyEnum::Strong);

        setStyleSheet(R"(
            SwChartView {
                background-color: rgb(255, 255, 255);
                plot-area-color: rgb(250, 250, 250);
                grid-color: rgb(226, 232, 240);
                axis-color: rgb(148, 163, 184);
                text-color: rgb(24, 28, 36);
                border-color: rgb(226, 232, 240);
                border-width: 1px;
            }
        )");

        setChart(new SwChart(this));
        setEnableMouseZoom(true);
        setEnableMousePan(true);
    }

    /**
     * @brief Returns the current chart.
     * @return The current chart.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwChart* chart() const { return m_chart; }

    /**
     * @brief Sets the chart.
     * @param chart Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setChart(SwChart* chart) {
        if (m_chart == chart) {
            return;
        }

        if (m_chart) {
            SwObject::disconnect(m_chart, &SwChart::changed, this, &SwChartView::onChartChanged_);
        }

        m_chart = chart;
        if (m_chart && m_chart->parent() != this) {
            m_chart->setParent(this);
        }

        if (m_chart) {
            SwObject::connect(m_chart, &SwChart::changed, this, &SwChartView::onChartChanged_);
        }

        update();
    }

    /**
     * @brief Returns whether the object reports mouse Zoom Enabled.
     * @return `true` when the object reports mouse Zoom Enabled; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isMouseZoomEnabled() const { return m_enableMouseZoom; }
    /**
     * @brief Sets the enable Mouse Zoom.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEnableMouseZoom(bool enable) { m_enableMouseZoom = enable; }

    /**
     * @brief Returns whether the object reports mouse Pan Enabled.
     * @return `true` when the object reports mouse Pan Enabled; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isMousePanEnabled() const { return m_enableMousePan; }
    /**
     * @brief Sets the enable Mouse Pan.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEnableMousePan(bool enable) { m_enableMousePan = enable; }

    /**
     * @brief Sets the enable Play Mode.
     * @param enable Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setEnablePlayMode(bool enable) { m_enablePlayMode = enable; }
    /**
     * @brief Returns whether the object reports play Mode Enabled.
     * @return `true` when the object reports play Mode Enabled; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isPlayModeEnabled() const { return m_enablePlayMode; }

    // Convenience wrappers.
    /**
     * @brief Sets the title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitle(const SwString& title) {
        if (m_chart) {
            m_chart->setTitle(title);
        }
    }

    /**
     * @brief Adds the specified series.
     * @param serie Value passed to the method.
     */
    void addSeries(SwAbstractSeries* serie) {
        if (m_chart) {
            m_chart->addSeries(serie);
        }
    }

    /**
     * @brief Removes the specified series.
     * @param serie Value passed to the method.
     */
    void removeSeries(SwAbstractSeries* serie) {
        if (m_chart) {
            m_chart->removeSeries(serie);
        }
    }

    /**
     * @brief Creates the requested default Axes.
     */
    void createDefaultAxes() {
        if (m_chart) {
            m_chart->createDefaultAxes();
        }
    }

    /**
     * @brief Performs the `centerView` operation.
     */
    void centerView() {
        if (!m_chart) {
            return;
        }
        if (m_chart->axisX()) {
            m_chart->axisX()->applyNiceNumbers();
        }
        if (m_chart->axisY()) {
            m_chart->axisY()->applyNiceNumbers();
        }
    }

    /**
     * @brief Sets the xRange.
     * @param start Value passed to the method.
     * @param end Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setXRange(double start, double end) {
        if (m_chart && m_chart->axisX()) {
            m_chart->axisX()->setRange(start, end);
        }
    }

    /**
     * @brief Sets the yRange.
     * @param start Value passed to the method.
     * @param end Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setYRange(double start, double end) {
        if (m_chart && m_chart->axisY()) {
            m_chart->axisY()->setRange(start, end);
        }
    }

    /**
     * @brief Performs the `zoomIn` operation.
     */
    void zoomIn() { zoom_(1.1); }
    /**
     * @brief Performs the `zoomOut` operation.
     */
    void zoomOut() { zoom_(1.0 / 1.1); }

protected:
    /**
     * @brief Handles the paint Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void paintEvent(PaintEvent* event) override {
        SwPainter* painter = event ? event->painter() : nullptr;
        if (!painter) {
            return;
        }

        const SwRect bounds = rect();
        if (bounds.width <= 2 || bounds.height <= 2) {
            return;
        }

        SwColor background = resolveColor_("background-color", SwColor{255, 255, 255});
        SwColor plotColor = resolveColor_("plot-area-color", SwColor{250, 250, 250});
        SwColor gridColor = resolveColor_("grid-color", SwColor{226, 232, 240});
        SwColor axisColor = resolveColor_("axis-color", SwColor{148, 163, 184});
        SwColor textColor = resolveColor_("text-color", SwColor{24, 28, 36});
        SwColor borderColor = resolveColor_("border-color", SwColor{226, 232, 240});
        const int borderWidth = std::max(0, std::min(6, resolveInt_("border-width", 1)));

        painter->fillRect(bounds, background, borderColor, borderWidth);

        if (shouldRenderPie_()) {
            const SwRect pieRect = pieRect_();
            if (pieRect.width <= 2 || pieRect.height <= 2) {
                return;
            }
            painter->fillRect(pieRect, plotColor, borderColor, 1);
            drawTitle_(painter, bounds, textColor);
            painter->pushClipRect(pieRect);
            drawPie_(painter, pieRect, plotColor, textColor);
            painter->popClipRect();
            return;
        }

        const SwRect plot = plotRect_();
        if (plot.width <= 2 || plot.height <= 2) {
            return;
        }

        painter->fillRect(plot, plotColor, borderColor, 1);

        double xMin = 0.0;
        double xMax = 1.0;
        double yMin = 0.0;
        double yMax = 1.0;
        computeRanges_(xMin, xMax, yMin, yMax);

        const int xTicks = effectiveTickCount_(m_chart ? m_chart->axisX() : nullptr);
        const int yTicks = effectiveTickCount_(m_chart ? m_chart->axisY() : nullptr);

        drawGrid_(painter, plot, xTicks, yTicks, gridColor);
        drawAxes_(painter, plot, axisColor);
        drawAxisLabels_(painter, plot, xMin, xMax, yMin, yMax, xTicks, yTicks, textColor);
        drawTitle_(painter, bounds, textColor);
        drawLegend_(painter, bounds, textColor);

        painter->pushClipRect(plot);
        drawSeries_(painter, plot, xMin, xMax, yMin, yMax);
        painter->popClipRect();
    }

    /**
     * @brief Handles the mouse Press Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mousePressEvent(MouseEvent* event) override {
        if (!event || !isVisibleInHierarchy()) {
            return;
        }

        if (event->button() == SwMouseButton::Middle && m_enableMousePan) {
            const SwRect plot = plotRect_();
            if (pointInRect_(event->x(), event->y(), plot)) {
                m_panning = true;
                m_lastPanX = event->x();
                m_lastPanY = event->y();
                setCursor(CursorType::SizeAll);
                event->accept();
                return;
            }
        }

        SwWidget::mousePressEvent(event);
    }

    /**
     * @brief Handles the mouse Move Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseMoveEvent(MouseEvent* event) override {
        if (!event || !isVisibleInHierarchy()) {
            return;
        }

        if (m_panning && m_enableMousePan) {
            const SwRect plot = plotRect_();
            if (plot.width > 0 && plot.height > 0) {
                const int dx = event->x() - m_lastPanX;
                const int dy = event->y() - m_lastPanY;
                panByPixels_(dx, dy, plot);
                m_lastPanX = event->x();
                m_lastPanY = event->y();
                event->accept();
                return;
            }
        }

        SwWidget::mouseMoveEvent(event);
    }

    /**
     * @brief Handles the mouse Release Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseReleaseEvent(MouseEvent* event) override {
        if (!event || !isVisibleInHierarchy()) {
            return;
        }

        if (m_panning) {
            m_panning = false;
            setCursor(CursorType::Arrow);
            event->accept();
        }

        SwWidget::mouseReleaseEvent(event);
    }

    /**
     * @brief Handles the mouse Double Click Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void mouseDoubleClickEvent(MouseEvent* event) override {
        if (!event || !isVisibleInHierarchy()) {
            return;
        }
        if (event->button() == SwMouseButton::Left) {
            createDefaultAxes();
            event->accept();
            return;
        }
        SwWidget::mouseDoubleClickEvent(event);
    }

    /**
     * @brief Handles the wheel Event forwarded by the framework.
     * @param event Event object forwarded by the framework.
     *
     * @details Override this hook when the default framework behavior needs to be extended or replaced.
     */
    void wheelEvent(WheelEvent* event) override {
        if (!event || !isVisibleInHierarchy()) {
            return;
        }
        if (!m_enableMouseZoom) {
            return;
        }
        const SwRect plot = plotRect_();
        if (!pointInRect_(event->x(), event->y(), plot)) {
            return;
        }

        const double factor = event->delta() > 0 ? 1.1 : (1.0 / 1.1);
        zoomAt_(factor, event->x(), event->y(), plot);
        event->accept();
    }

private:
    void onChartChanged_() {
        if (m_handlingChartChange) {
            return;
        }

        m_handlingChartChange = true;
        if (m_enablePlayMode) {
            applyPlayMode_();
        }
        m_handlingChartChange = false;

        update();
    }

    static bool pointInRect_(int px, int py, const SwRect& r) {
        return px >= r.x && px < (r.x + r.width) && py >= r.y && py < (r.y + r.height);
    }

    SwRect plotRect_() const {
        const SwRect bounds = rect();

        const int left = 56;
        const int right = 18;
        const int top = hasTitle_() ? 34 : 16;
        const int bottom = 36;

        SwRect plot;
        plot.x = bounds.x + left;
        plot.y = bounds.y + top;
        plot.width = std::max(0, bounds.width - left - right);
        plot.height = std::max(0, bounds.height - top - bottom);
        return plot;
    }

    SwRect pieRect_() const {
        const SwRect bounds = rect();

        const int left = 16;
        const int right = 16;
        const int top = hasTitle_() ? 34 : 16;
        const int bottom = 16;

        SwRect plot;
        plot.x = bounds.x + left;
        plot.y = bounds.y + top;
        plot.width = std::max(0, bounds.width - left - right);
        plot.height = std::max(0, bounds.height - top - bottom);
        return plot;
    }

    bool hasTitle_() const {
        return m_chart && !m_chart->title().isEmpty();
    }

    int effectiveTickCount_(SwValueAxis* axis) const {
        if (!axis) {
            return 5;
        }
        return std::max(2, axis->tickCount());
    }

    SwColor resolveColor_(const std::string& property, const SwColor& fallback) {
        StyleSheet* sheet = getToolSheet();
        if (!sheet) {
            return fallback;
        }
        auto selectors = classHierarchy();
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            const SwString value = sheet->getStyleProperty(selector, property);
            if (value.isEmpty()) {
                continue;
            }
            float alpha = 1.0f;
            try {
                SwColor c = sheet->parseColor(value, &alpha);
                if (alpha <= 0.0f) {
                    return fallback;
                }
                return c;
            } catch (...) {
                return fallback;
            }
        }
        return fallback;
    }

    int resolveInt_(const std::string& property, int fallback) {
        StyleSheet* sheet = getToolSheet();
        if (!sheet) {
            return fallback;
        }
        auto selectors = classHierarchy();
        for (const SwString& selector : selectors) {
            if (selector.isEmpty()) {
                continue;
            }
            SwString swProp = sheet->getStyleProperty(selector, property);
            if (swProp.isEmpty()) {
                continue;
            }
            std::string value = swProp.toStdString();
            value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                return !(std::isdigit(c) || c == '-' || c == '+');
            }), value.end());
            if (value.empty()) {
                continue;
            }
            try {
                return std::stoi(value);
            } catch (...) {
                return fallback;
            }
        }
        return fallback;
    }

    void computeRanges_(double& xMin, double& xMax, double& yMin, double& yMax) const {
        if (!m_chart) {
            xMin = 0.0;
            xMax = 1.0;
            yMin = 0.0;
            yMax = 1.0;
            return;
        }

        const SwValueAxis* axisX = m_chart->axisX();
        const SwValueAxis* axisY = m_chart->axisY();

        const bool autoX = !axisX || axisX->isAutoRange();
        const bool autoY = !axisY || axisY->isAutoRange();

        if (!autoX) {
            xMin = axisX->min();
            xMax = axisX->max();
        }
        if (!autoY) {
            yMin = axisY->min();
            yMax = axisY->max();
        }

        if (autoX || autoY) {
            double minX = std::numeric_limits<double>::infinity();
            double maxX = -std::numeric_limits<double>::infinity();
            double minY = std::numeric_limits<double>::infinity();
            double maxY = -std::numeric_limits<double>::infinity();
            bool hasPoint = false;

            for (int s = 0; s < m_chart->series().size(); ++s) {
                SwAbstractSeries* serie = m_chart->series()[s];
                if (!serie || !serie->isVisible() || !serie->isXYSeries()) {
                    continue;
                }
                serie->computeBounds(minX, maxX, minY, maxY, hasPoint);
            }

            if (hasPoint) {
                if (autoX) {
                    xMin = minX;
                    xMax = maxX;
                }
                if (autoY) {
                    yMin = minY;
                    yMax = maxY;
                }
            }
        }

        if (!std::isfinite(xMin) || !std::isfinite(xMax) || xMin == xMax) {
            xMin = 0.0;
            xMax = 1.0;
        }
        if (!std::isfinite(yMin) || !std::isfinite(yMax) || yMin == yMax) {
            yMin = 0.0;
            yMax = 1.0;
        }

        if (xMax - xMin < 1e-12) {
            xMax = xMin + 1.0;
        }
        if (yMax - yMin < 1e-12) {
            yMax = yMin + 1.0;
        }
    }

    void drawGrid_(SwPainter* painter, const SwRect& plot, int xTicks, int yTicks, const SwColor& gridColor) const {
        if (!painter) {
            return;
        }
        if (xTicks < 2 || yTicks < 2) {
            return;
        }

        for (int i = 0; i < xTicks; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(xTicks - 1);
            const int x = plot.x + static_cast<int>(std::round(t * plot.width));
            painter->drawLine(x, plot.y, x, plot.y + plot.height, gridColor, 1);
        }

        for (int i = 0; i < yTicks; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(yTicks - 1);
            const int y = plot.y + static_cast<int>(std::round(t * plot.height));
            painter->drawLine(plot.x, y, plot.x + plot.width, y, gridColor, 1);
        }
    }

    void drawAxes_(SwPainter* painter, const SwRect& plot, const SwColor& axisColor) const {
        if (!painter) {
            return;
        }
        painter->drawRect(plot, axisColor, 1);
    }

    void drawAxisLabels_(SwPainter* painter,
                         const SwRect& plot,
                         double xMin,
                         double xMax,
                         double yMin,
                         double yMax,
                         int xTicks,
                         int yTicks,
                         const SwColor& textColor) const {
        if (!painter) {
            return;
        }

        const SwFont font = getFont();
        const int labelPad = 6;

        for (int i = 0; i < xTicks; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(xTicks - 1);
            const double value = xMin + (xMax - xMin) * t;
            const int x = plot.x + static_cast<int>(std::round(t * plot.width));
            const SwRect r{x - 40, plot.y + plot.height + labelPad, 80, 22};
            painter->drawText(r,
                              SwString::number(value, 2),
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::Top | DrawTextFormat::SingleLine),
                              textColor,
                              font);
        }

        for (int i = 0; i < yTicks; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(yTicks - 1);
            const double value = yMax - (yMax - yMin) * t;
            const int y = plot.y + static_cast<int>(std::round(t * plot.height));
            const SwRect r{plot.x - 52, y - 10, 46, 20};
            painter->drawText(r,
                              SwString::number(value, 2),
                              DrawTextFormats(DrawTextFormat::Right | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              font);
        }
    }

    void drawTitle_(SwPainter* painter, const SwRect& bounds, const SwColor& textColor) const {
        if (!painter || !m_chart) {
            return;
        }
        const SwString title = m_chart->title();
        if (title.isEmpty()) {
            return;
        }
        const SwRect r{bounds.x + 12, bounds.y + 8, bounds.width - 24, 22};
        painter->drawText(r,
                          title,
                          DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                          textColor,
                          getFont());
    }

    void drawLegend_(SwPainter* painter, const SwRect& bounds, const SwColor& textColor) const {
        if (!painter || !m_chart) {
            return;
        }
        if (!m_showLegend) {
            return;
        }

        int x = bounds.x + bounds.width - 12;
        const int y = bounds.y + 10;
        const int h = 18;
        const int sw = 10;
        const int gap = 8;

        for (int i = m_chart->series().size() - 1; i >= 0; --i) {
            SwAbstractSeries* serie = m_chart->series()[i];
            if (!serie) {
                continue;
            }
            const SwString name = serie->name().isEmpty() ? SwString("series") : serie->name();
            const int w = static_cast<int>(name.size()) * 7 + sw + gap + 8;
            x -= w;

            const SwRect swatch{x, y + 4, sw, sw};
            painter->fillRect(swatch, serie->color(), serie->color(), 0);

            const SwRect label{x + sw + gap, y, w - (sw + gap), h};
            painter->drawText(label,
                              name,
                              DrawTextFormats(DrawTextFormat::Left | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              getFont());

            x -= 10;
            if (x < bounds.x + 12) {
                break;
            }
        }
    }

    void drawSeries_(SwPainter* painter,
                     const SwRect& plot,
                     double xMin,
                     double xMax,
                     double yMin,
                     double yMax) const {
        if (!painter || !m_chart) {
            return;
        }

        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0) {
            return;
        }

        for (int s = 0; s < m_chart->series().size(); ++s) {
            SwAbstractSeries* serie = m_chart->series()[s];
            if (!serie || !serie->isVisible() || !serie->isXYSeries()) {
                continue;
            }

            if (serie->type() == SwAbstractSeries::SeriesType::Candlestick) {
                const SwCandlestickSeries* candles = dynamic_cast<const SwCandlestickSeries*>(serie);
                if (candles) {
                    drawCandlesticks_(painter, plot, xMin, xMax, yMin, yMax, *candles);
                }
                continue;
            }

            const SwVector<SwPointF>& pts = serie->points();
            if (pts.size() <= 0) {
                continue;
            }

            if (serie->type() == SwAbstractSeries::SeriesType::Bar) {
                const SwBarSeries* bars = dynamic_cast<const SwBarSeries*>(serie);
                drawBars_(painter,
                          plot,
                          xMin,
                          xMax,
                          yMin,
                          yMax,
                          pts,
                          bars ? bars->barWidth() : 0.0,
                          bars ? bars->baseline() : 0.0,
                          serie->color());
                continue;
            }

            if (serie->type() == SwAbstractSeries::SeriesType::Area) {
                const SwAreaSeries* area = dynamic_cast<const SwAreaSeries*>(serie);
                const double baseline = area ? area->baseline() : 0.0;
                const SwColor fillColor = area && area->hasFillColor() ? area->fillColor() : lighten_(serie->color(), 0.65);
                const int borderWidth = area ? area->borderWidth() : 2;
                drawArea_(painter,
                          plot,
                          xMin,
                          xMax,
                          yMin,
                          yMax,
                          pts,
                          baseline,
                          fillColor,
                          serie->color(),
                          borderWidth);
                continue;
            }

            if (serie->type() == SwAbstractSeries::SeriesType::Scatter) {
                const SwScatterSeries* scatter = dynamic_cast<const SwScatterSeries*>(serie);
                const int marker = scatter ? scatter->markerSize() : 7;
                for (int i = 0; i < pts.size(); ++i) {
                    const SwPointF p = pts[i];
                    const int px = plot.x + static_cast<int>(std::round(((p.x - xMin) / xSpan) * plot.width));
                    const int py = plot.y + plot.height - static_cast<int>(std::round(((p.y - yMin) / ySpan) * plot.height));
                    const SwRect dot{px - marker / 2, py - marker / 2, marker, marker};
                    painter->fillEllipse(dot, serie->color(), serie->color(), 0);
                }
                continue;
            }

            if (serie->type() == SwAbstractSeries::SeriesType::StepLine) {
                drawStepLine_(painter, plot, xMin, xMax, yMin, yMax, pts, serie->color(), 2);
                continue;
            }

            if (serie->type() == SwAbstractSeries::SeriesType::Spline) {
                drawSpline_(painter, plot, xMin, xMax, yMin, yMax, pts, serie->color(), 2);
                continue;
            }

            // Default: line series.
            SwPointF prev = pts[0];
            int prevX = plot.x + static_cast<int>(std::round(((prev.x - xMin) / xSpan) * plot.width));
            int prevY = plot.y + plot.height - static_cast<int>(std::round(((prev.y - yMin) / ySpan) * plot.height));

            for (int i = 1; i < pts.size(); ++i) {
                const SwPointF p = pts[i];
                const int x = plot.x + static_cast<int>(std::round(((p.x - xMin) / xSpan) * plot.width));
                const int y = plot.y + plot.height - static_cast<int>(std::round(((p.y - yMin) / ySpan) * plot.height));
                painter->drawLine(prevX, prevY, x, y, serie->color(), 2);
                prevX = x;
                prevY = y;
            }
        }
    }

    void applyPlayMode_() {
        if (!m_chart || !m_chart->axisX() || m_chart->axisX()->isAutoRange()) {
            return;
        }
        const double maxX = lastVisibleX_();
        if (!std::isfinite(maxX)) {
            return;
        }

        const double currentMax = m_chart->axisX()->max();
        const double currentMin = m_chart->axisX()->min();
        if (!std::isfinite(currentMax) || !std::isfinite(currentMin)) {
            return;
        }

        if (maxX > currentMax) {
            const double shift = maxX - currentMax;
            m_chart->axisX()->setRange(currentMin + shift, currentMax + shift);
        }
    }

    double lastVisibleX_() const {
        if (!m_chart) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        double maxX = -std::numeric_limits<double>::infinity();
        bool has = false;
        for (int s = 0; s < m_chart->series().size(); ++s) {
            SwAbstractSeries* serie = m_chart->series()[s];
            if (!serie || !serie->isVisible()) {
                continue;
            }
            double x = 0.0;
            if (serie->lastX(x)) {
                maxX = std::max(maxX, x);
                has = true;
            }
        }
        return has ? maxX : std::numeric_limits<double>::quiet_NaN();
    }

    bool shouldRenderPie_() const {
        if (!m_chart) {
            return false;
        }

        bool hasPie = false;
        bool hasCartesian = false;
        for (int s = 0; s < m_chart->series().size(); ++s) {
            SwAbstractSeries* serie = m_chart->series()[s];
            if (!serie || !serie->isVisible()) {
                continue;
            }
            if (serie->type() == SwAbstractSeries::SeriesType::Pie) {
                hasPie = true;
                continue;
            }
            if (serie->isXYSeries()) {
                hasCartesian = true;
            }
        }
        return hasPie && !hasCartesian;
    }

    static SwColor lighten_(const SwColor& c, double amount) {
        amount = std::max(0.0, std::min(1.0, amount));
        auto lerp = [&](int a, int b) -> int {
            const double v = static_cast<double>(a) + (static_cast<double>(b - a) * amount);
            return std::max(0, std::min(255, static_cast<int>(std::lround(v))));
        };
        return SwColor{lerp(c.r, 255), lerp(c.g, 255), lerp(c.b, 255)};
    }

    static SwColor darken_(const SwColor& c, double amount) {
        amount = std::max(0.0, std::min(1.0, amount));
        auto lerp = [&](int a, int b) -> int {
            const double v = static_cast<double>(a) + (static_cast<double>(b - a) * amount);
            return std::max(0, std::min(255, static_cast<int>(std::lround(v))));
        };
        return SwColor{lerp(c.r, 0), lerp(c.g, 0), lerp(c.b, 0)};
    }

    static double clampBaseline_(double baseline, double yMin, double yMax) {
        if (!std::isfinite(baseline)) {
            return yMin;
        }
        if (baseline < yMin) {
            return yMin;
        }
        if (baseline > yMax) {
            return yMax;
        }
        return baseline;
    }

    static double autoBandWidthFromXs_(std::vector<double> xs, double fallbackSpan) {
        if (xs.size() <= 1) {
            return fallbackSpan * 0.1;
        }
        std::sort(xs.begin(), xs.end());
        double minDx = std::numeric_limits<double>::infinity();
        for (size_t i = 1; i < xs.size(); ++i) {
            const double dx = xs[i] - xs[i - 1];
            if (dx > 0.0 && std::isfinite(dx)) {
                minDx = std::min(minDx, dx);
            }
        }
        if (!std::isfinite(minDx) || minDx <= 0.0) {
            return fallbackSpan * 0.1;
        }
        return minDx * 0.8;
    }

    static double autoBandWidthFromPoints_(const SwVector<SwPointF>& pts, double fallbackSpan) {
        std::vector<double> xs;
        xs.reserve(static_cast<size_t>(pts.size()));
        for (int i = 0; i < pts.size(); ++i) {
            if (std::isfinite(pts[i].x)) {
                xs.push_back(pts[i].x);
            }
        }
        return autoBandWidthFromXs_(std::move(xs), fallbackSpan);
    }

    static void drawBars_(SwPainter* painter,
                          const SwRect& plot,
                          double xMin,
                          double xMax,
                          double yMin,
                          double yMax,
                          const SwVector<SwPointF>& pts,
                          double widthX,
                          double baseline,
                          const SwColor& fill) {
        if (!painter) {
            return;
        }
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0) {
            return;
        }

        if (!(widthX > 0.0)) {
            widthX = autoBandWidthFromPoints_(pts, xSpan);
        }
        if (widthX <= 0.0) {
            return;
        }

        const double base = clampBaseline_(baseline, yMin, yMax);
        const int py0 = plot.y + plot.height - static_cast<int>(std::round(((base - yMin) / ySpan) * plot.height));

        int halfW = static_cast<int>(std::round((widthX / xSpan) * plot.width * 0.5));
        halfW = std::max(1, std::min(plot.width / 2, halfW));

        const SwColor border = darken_(fill, 0.35);

        for (int i = 0; i < pts.size(); ++i) {
            const SwPointF p = pts[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            const int px = plot.x + static_cast<int>(std::round(((p.x - xMin) / xSpan) * plot.width));
            const int py = plot.y + plot.height - static_cast<int>(std::round(((p.y - yMin) / ySpan) * plot.height));

            const int x = px - halfW;
            const int w = halfW * 2;

            const int top = std::min(py, py0);
            const int bottom = std::max(py, py0);
            const int h = std::max(1, bottom - top);

            painter->fillRect(SwRect{x, top, w, h}, fill, border, 1);
        }
    }

    static void drawArea_(SwPainter* painter,
                          const SwRect& plot,
                          double xMin,
                          double xMax,
                          double yMin,
                          double yMax,
                          const SwVector<SwPointF>& pts,
                          double baseline,
                          const SwColor& fill,
                          const SwColor& stroke,
                          int strokeWidth) {
        if (!painter) {
            return;
        }
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0) {
            return;
        }

        int first = -1;
        int last = -1;
        for (int i = 0; i < pts.size(); ++i) {
            const SwPointF p = pts[i];
            if (std::isfinite(p.x) && std::isfinite(p.y)) {
                first = i;
                break;
            }
        }
        for (int i = pts.size() - 1; i >= 0; --i) {
            const SwPointF p = pts[i];
            if (std::isfinite(p.x) && std::isfinite(p.y)) {
                last = i;
                break;
            }
        }
        if (first < 0 || last < 0 || last <= first) {
            return;
        }

        const double base = clampBaseline_(baseline, yMin, yMax);
        const int baseY = plot.y + plot.height - static_cast<int>(std::round(((base - yMin) / ySpan) * plot.height));

        std::vector<SwPoint> poly;
        poly.reserve(static_cast<size_t>(last - first + 3));

        const int firstX = plot.x + static_cast<int>(std::round(((pts[first].x - xMin) / xSpan) * plot.width));
        const int lastX = plot.x + static_cast<int>(std::round(((pts[last].x - xMin) / xSpan) * plot.width));
        poly.push_back(SwPoint{firstX, baseY});

        for (int i = first; i <= last; ++i) {
            const SwPointF p = pts[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            const int px = plot.x + static_cast<int>(std::round(((p.x - xMin) / xSpan) * plot.width));
            const int py = plot.y + plot.height - static_cast<int>(std::round(((p.y - yMin) / ySpan) * plot.height));
            poly.push_back(SwPoint{px, py});
        }

        poly.push_back(SwPoint{lastX, baseY});

        if (poly.size() >= 3) {
            painter->fillPolygon(poly.data(), static_cast<int>(poly.size()), fill, fill, 0);
        }

        const int w = std::max(1, strokeWidth);
        int prevX = 0;
        int prevY = 0;
        bool hasPrev = false;
        for (int i = first; i <= last; ++i) {
            const SwPointF p = pts[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            const int x = plot.x + static_cast<int>(std::round(((p.x - xMin) / xSpan) * plot.width));
            const int y = plot.y + plot.height - static_cast<int>(std::round(((p.y - yMin) / ySpan) * plot.height));
            if (hasPrev) {
                painter->drawLine(prevX, prevY, x, y, stroke, w);
            }
            prevX = x;
            prevY = y;
            hasPrev = true;
        }
    }

    static void drawStepLine_(SwPainter* painter,
                              const SwRect& plot,
                              double xMin,
                              double xMax,
                              double yMin,
                              double yMax,
                              const SwVector<SwPointF>& pts,
                              const SwColor& color,
                              int width) {
        if (!painter) {
            return;
        }
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0) {
            return;
        }

        int prevX = 0;
        int prevY = 0;
        bool hasPrev = false;
        for (int i = 0; i < pts.size(); ++i) {
            const SwPointF p = pts[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            const int x = plot.x + static_cast<int>(std::round(((p.x - xMin) / xSpan) * plot.width));
            const int y = plot.y + plot.height - static_cast<int>(std::round(((p.y - yMin) / ySpan) * plot.height));
            if (!hasPrev) {
                prevX = x;
                prevY = y;
                hasPrev = true;
                continue;
            }
            painter->drawLine(prevX, prevY, x, prevY, color, width);
            painter->drawLine(x, prevY, x, y, color, width);
            prevX = x;
            prevY = y;
        }
    }

    static void drawSpline_(SwPainter* painter,
                            const SwRect& plot,
                            double xMin,
                            double xMax,
                            double yMin,
                            double yMax,
                            const SwVector<SwPointF>& pts,
                            const SwColor& color,
                            int width) {
        if (!painter) {
            return;
        }
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0) {
            return;
        }

        std::vector<SwPointF> pxy;
        pxy.reserve(static_cast<size_t>(pts.size()));
        for (int i = 0; i < pts.size(); ++i) {
            const SwPointF p = pts[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                continue;
            }
            const double px = static_cast<double>(plot.x) + ((p.x - xMin) / xSpan) * static_cast<double>(plot.width);
            const double py = static_cast<double>(plot.y) + static_cast<double>(plot.height) -
                              ((p.y - yMin) / ySpan) * static_cast<double>(plot.height);
            pxy.push_back(SwPointF(px, py));
        }
        if (pxy.size() < 2) {
            return;
        }

        auto catmullRom = [](double p0, double p1, double p2, double p3, double t) -> double {
            const double t2 = t * t;
            const double t3 = t2 * t;
            return 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
                          (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
        };

        const int segs = 12;
        SwPointF prev = pxy[0];
        for (size_t i = 0; i + 1 < pxy.size(); ++i) {
            const SwPointF p0 = (i == 0) ? pxy[i] : pxy[i - 1];
            const SwPointF p1 = pxy[i];
            const SwPointF p2 = pxy[i + 1];
            const SwPointF p3 = (i + 2 < pxy.size()) ? pxy[i + 2] : pxy[i + 1];

            for (int s = 1; s <= segs; ++s) {
                const double t = static_cast<double>(s) / static_cast<double>(segs);
                const double x = catmullRom(p0.x, p1.x, p2.x, p3.x, t);
                const double y = catmullRom(p0.y, p1.y, p2.y, p3.y, t);
                const SwPointF cur(x, y);
                painter->drawLine(static_cast<int>(std::lround(prev.x)),
                                  static_cast<int>(std::lround(prev.y)),
                                  static_cast<int>(std::lround(cur.x)),
                                  static_cast<int>(std::lround(cur.y)),
                                  color,
                                  width);
                prev = cur;
            }
        }
    }

    static void drawCandlesticks_(SwPainter* painter,
                                  const SwRect& plot,
                                  double xMin,
                                  double xMax,
                                  double yMin,
                                  double yMax,
                                  const SwCandlestickSeries& series) {
        if (!painter) {
            return;
        }

        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0) {
            return;
        }
        if (series.candles().isEmpty()) {
            return;
        }

        double widthX = series.candleWidth();
        if (!(widthX > 0.0)) {
            std::vector<double> xs;
            xs.reserve(static_cast<size_t>(series.candles().size()));
            for (int i = 0; i < series.candles().size(); ++i) {
                const double x = series.candles()[i].x;
                if (std::isfinite(x)) {
                    xs.push_back(x);
                }
            }
            widthX = autoBandWidthFromXs_(std::move(xs), xSpan) * 0.85;
        }
        if (widthX <= 0.0) {
            return;
        }

        int halfW = static_cast<int>(std::round((widthX / xSpan) * plot.width * 0.5));
        halfW = std::max(1, std::min(plot.width / 2, halfW));

        for (int i = 0; i < series.candles().size(); ++i) {
            const SwCandlestickSeries::Candle& c = series.candles()[i];
            if (!std::isfinite(c.x) || !std::isfinite(c.open) || !std::isfinite(c.close) || !std::isfinite(c.low) ||
                !std::isfinite(c.high)) {
                continue;
            }

            const int px = plot.x + static_cast<int>(std::round(((c.x - xMin) / xSpan) * plot.width));
            const int pyHigh = plot.y + plot.height - static_cast<int>(std::round(((c.high - yMin) / ySpan) * plot.height));
            const int pyLow = plot.y + plot.height - static_cast<int>(std::round(((c.low - yMin) / ySpan) * plot.height));
            const int pyOpen = plot.y + plot.height - static_cast<int>(std::round(((c.open - yMin) / ySpan) * plot.height));
            const int pyClose = plot.y + plot.height - static_cast<int>(std::round(((c.close - yMin) / ySpan) * plot.height));

            const bool up = c.close >= c.open;
            const SwColor body = up ? series.increasingColor() : series.decreasingColor();
            const SwColor border = darken_(body, 0.35);

            painter->drawLine(px, pyHigh, px, pyLow, border, 1);

            const int top = std::min(pyOpen, pyClose);
            const int bottom = std::max(pyOpen, pyClose);
            const int h = std::max(1, bottom - top);
            painter->fillRect(SwRect{px - halfW, top, halfW * 2, h}, body, border, 1);
        }
    }

    void drawPie_(SwPainter* painter, const SwRect& rect, const SwColor& background, const SwColor& textColor) const {
        if (!painter || !m_chart) {
            return;
        }

        const SwPieSeries* pie = nullptr;
        for (int s = 0; s < m_chart->series().size(); ++s) {
            SwAbstractSeries* serie = m_chart->series()[s];
            if (!serie || !serie->isVisible()) {
                continue;
            }
            if (serie->type() == SwAbstractSeries::SeriesType::Pie) {
                pie = dynamic_cast<const SwPieSeries*>(serie);
                if (pie) {
                    break;
                }
            }
        }
        if (!pie) {
            return;
        }

        const double total = pie->totalValue();
        if (!(total > 0.0)) {
            painter->drawText(rect,
                              "No data",
                              DrawTextFormats(DrawTextFormat::Center | DrawTextFormat::VCenter | DrawTextFormat::SingleLine),
                              textColor,
                              getFont());
            return;
        }

        const int cx = rect.x + rect.width / 2;
        const int cy = rect.y + rect.height / 2;
        const int radius = std::max(10, std::min(rect.width, rect.height) / 2 - 12);

        constexpr double kPi = 3.14159265358979323846;
        double angle = pie->startAngleDegrees() * (kPi / 180.0);

        for (int i = 0; i < pie->slices().size(); ++i) {
            const SwPieSeries::Slice& slice = pie->slices()[i];
            if (!(slice.value > 0.0) || !std::isfinite(slice.value)) {
                continue;
            }

            const double span = (slice.value / total) * (kPi * 2.0);
            const double mid = angle + span * 0.5;

            const double explode = slice.exploded ? std::max(0.0, std::min(0.3, slice.explodeFactor)) : 0.0;
            const int ox = static_cast<int>(std::lround(std::cos(mid) * static_cast<double>(radius) * explode));
            const int oy = static_cast<int>(std::lround(std::sin(mid) * static_cast<double>(radius) * explode));

            const int segs = std::max(10, static_cast<int>(std::lround((span / (kPi * 2.0)) * 80.0)));
            std::vector<SwPoint> poly;
            poly.reserve(static_cast<size_t>(segs) + 2);
            poly.push_back(SwPoint{cx + ox, cy + oy});

            for (int s = 0; s <= segs; ++s) {
                const double t = static_cast<double>(s) / static_cast<double>(segs);
                const double a = angle + span * t;
                const int px = cx + ox + static_cast<int>(std::lround(std::cos(a) * static_cast<double>(radius)));
                const int py = cy + oy + static_cast<int>(std::lround(std::sin(a) * static_cast<double>(radius)));
                poly.push_back(SwPoint{px, py});
            }

            painter->fillPolygon(poly.data(),
                                 static_cast<int>(poly.size()),
                                 slice.color,
                                 SwColor{255, 255, 255},
                                 1);

            angle += span;
        }

        const double hole = pie->holeSize();
        if (hole > 0.0) {
            const int inner = std::max(0, static_cast<int>(std::lround(static_cast<double>(radius) * hole)));
            painter->fillEllipse(SwRect{cx - inner, cy - inner, inner * 2, inner * 2}, background, background, 0);
        }
    }

    void panByPixels_(int dx, int dy, const SwRect& plot) {
        if (!m_chart || !m_chart->axisX() || !m_chart->axisY()) {
            return;
        }
        if (m_chart->axisX()->isAutoRange() || m_chart->axisY()->isAutoRange()) {
            return;
        }

        const double xMin = m_chart->axisX()->min();
        const double xMax = m_chart->axisX()->max();
        const double yMin = m_chart->axisY()->min();
        const double yMax = m_chart->axisY()->max();
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0 || plot.width <= 0 || plot.height <= 0) {
            return;
        }

        const double dxData = -(static_cast<double>(dx) / static_cast<double>(plot.width)) * xSpan;
        const double dyData = (static_cast<double>(dy) / static_cast<double>(plot.height)) * ySpan;

        m_chart->axisX()->setRange(xMin + dxData, xMax + dxData);
        m_chart->axisY()->setRange(yMin + dyData, yMax + dyData);
    }

    void zoom_(double factor) {
        const SwRect plot = plotRect_();
        zoomAt_(factor, plot.x + plot.width / 2, plot.y + plot.height / 2, plot);
    }

    void zoomAt_(double factor, int anchorX, int anchorY, const SwRect& plot) {
        if (!m_chart || !m_chart->axisX() || !m_chart->axisY()) {
            return;
        }
        if (factor <= 0.0 || !std::isfinite(factor)) {
            return;
        }

        if (m_chart->axisX()->isAutoRange() || m_chart->axisY()->isAutoRange()) {
            return;
        }

        const double xMin = m_chart->axisX()->min();
        const double xMax = m_chart->axisX()->max();
        const double yMin = m_chart->axisY()->min();
        const double yMax = m_chart->axisY()->max();
        const double xSpan = xMax - xMin;
        const double ySpan = yMax - yMin;
        if (xSpan <= 0.0 || ySpan <= 0.0 || plot.width <= 0 || plot.height <= 0) {
            return;
        }

        const double rx = static_cast<double>(anchorX - plot.x) / static_cast<double>(plot.width);
        const double ry = 1.0 - (static_cast<double>(anchorY - plot.y) / static_cast<double>(plot.height));

        const double anchorDataX = xMin + rx * xSpan;
        const double anchorDataY = yMin + ry * ySpan;

        const double newSpanX = xSpan / factor;
        const double newSpanY = ySpan / factor;

        if (!std::isfinite(newSpanX) || !std::isfinite(newSpanY) || newSpanX <= 0.0 || newSpanY <= 0.0) {
            return;
        }

        const double newMinX = anchorDataX - rx * newSpanX;
        const double newMaxX = newMinX + newSpanX;
        const double newMinY = anchorDataY - ry * newSpanY;
        const double newMaxY = newMinY + newSpanY;

        m_chart->axisX()->setRange(newMinX, newMaxX);
        m_chart->axisY()->setRange(newMinY, newMaxY);
    }

    SwChart* m_chart{nullptr};
    bool m_enableMouseZoom{false};
    bool m_enableMousePan{false};
    bool m_enablePlayMode{false};
    bool m_showLegend{true};

    bool m_panning{false};
    int m_lastPanX{0};
    int m_lastPanY{0};

    bool m_handlingChartChange{false};
};

