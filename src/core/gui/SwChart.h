#pragma once

/**
 * @file
 * @ingroup core_gui
 * @brief Declares `SwChart`, the in-memory chart model used by chart widgets.
 *
 * `SwChart` owns the series and presentation state that describe a chart scene.
 * Views such as `SwChartView` consume this object to render axes, titles, legends,
 * and data series without coupling the drawing code to a specific dataset type.
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



#include "SwAbstractSeries.h"
#include "SwString.h"
#include "SwValueAxis.h"
#include "SwVector.h"

class SwChart : public SwObject {
    SW_OBJECT(SwChart, SwObject)

public:
    /**
     * @brief Constructs a `SwChart` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwChart(SwObject* parent = nullptr)
        : SwObject(parent) {
        m_axisX = new SwValueAxis(this);
        m_axisY = new SwValueAxis(this);

        SwObject::connect(m_axisX, &SwValueAxis::changed, this, &SwChart::onAxisChanged_);
        SwObject::connect(m_axisY, &SwValueAxis::changed, this, &SwChart::onAxisChanged_);
    }

    /**
     * @brief Returns the current axis X.
     * @return The current axis X.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwValueAxis* axisX() const { return m_axisX; }
    /**
     * @brief Returns the current axis Y.
     * @return The current axis Y.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwValueAxis* axisY() const { return m_axisY; }

    /**
     * @brief Returns the current series.
     * @return The current series.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwVector<SwAbstractSeries*>& series() const { return m_series; }

    /**
     * @brief Sets the title.
     * @param title Title text applied by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTitle(const SwString& title) {
        if (m_title == title) {
            return;
        }
        m_title = title;
        changed();
    }

    /**
     * @brief Returns the current title.
     * @return The current title.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString title() const { return m_title; }

    /**
     * @brief Adds the specified series.
     * @param serie Value passed to the method.
     */
    void addSeries(SwAbstractSeries* serie) {
        if (!serie) {
            return;
        }
        if (indexOfSeries_(serie) >= 0) {
            return;
        }

        if (serie->parent() != this) {
            serie->setParent(this);
        }

        m_series.push_back(serie);
        SwObject::connect(serie, &SwAbstractSeries::updated, this, &SwChart::onSeriesChanged_);
        seriesAdded(serie);
        changed();
    }

    /**
     * @brief Removes the specified series.
     * @param serie Value passed to the method.
     */
    void removeSeries(SwAbstractSeries* serie) {
        if (!serie) {
            return;
        }
        const int idx = indexOfSeries_(serie);
        if (idx < 0) {
            return;
        }

        SwObject::disconnect(serie, &SwAbstractSeries::updated, this, &SwChart::onSeriesChanged_);
        m_series.removeAt(idx);

        if (serie->parent() == this) {
            serie->setParent(nullptr);
        }

        seriesRemoved(serie);
        changed();
    }

    /**
     * @brief Removes the specified all Series.
     */
    void removeAllSeries() {
        while (!m_series.isEmpty()) {
            removeSeries(m_series.back());
        }
    }

    /**
     * @brief Creates the requested default Axes.
     */
    void createDefaultAxes() {
        if (m_axisX) {
            m_axisX->setAutoRange(true);
        }
        if (m_axisY) {
            m_axisY->setAutoRange(true);
        }
        changed();
    }

    DECLARE_SIGNAL_VOID(changed);
    DECLARE_SIGNAL(seriesAdded, SwAbstractSeries*);
    DECLARE_SIGNAL(seriesRemoved, SwAbstractSeries*);

private:
    int indexOfSeries_(SwAbstractSeries* serie) const {
        for (int i = 0; i < m_series.size(); ++i) {
            if (m_series[i] == serie) {
                return i;
            }
        }
        return -1;
    }

    void onSeriesChanged_() {
        changed();
    }

    void onAxisChanged_() {
        changed();
    }

    SwString m_title{};
    SwValueAxis* m_axisX{nullptr};
    SwValueAxis* m_axisY{nullptr};
    SwVector<SwAbstractSeries*> m_series{};
};
