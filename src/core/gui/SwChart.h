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

#include "SwAbstractSeries.h"
#include "SwString.h"
#include "SwValueAxis.h"
#include "SwVector.h"

class SwChart : public SwObject {
    SW_OBJECT(SwChart, SwObject)

public:
    explicit SwChart(SwObject* parent = nullptr)
        : SwObject(parent) {
        m_axisX = new SwValueAxis(this);
        m_axisY = new SwValueAxis(this);

        SwObject::connect(m_axisX, &SwValueAxis::changed, this, &SwChart::onAxisChanged_);
        SwObject::connect(m_axisY, &SwValueAxis::changed, this, &SwChart::onAxisChanged_);
    }

    SwValueAxis* axisX() const { return m_axisX; }
    SwValueAxis* axisY() const { return m_axisY; }

    const SwVector<SwAbstractSeries*>& series() const { return m_series; }

    void setTitle(const SwString& title) {
        if (m_title == title) {
            return;
        }
        m_title = title;
        changed();
    }

    SwString title() const { return m_title; }

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

    void removeAllSeries() {
        while (!m_series.isEmpty()) {
            removeSeries(m_series.back());
        }
    }

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

