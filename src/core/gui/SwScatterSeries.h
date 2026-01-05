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

#include "SwAbstractSeries.h"

#include <algorithm>

class SwScatterSeries : public SwAbstractSeries {
    SW_OBJECT(SwScatterSeries, SwAbstractSeries)

public:
    explicit SwScatterSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    SeriesType type() const override { return SeriesType::Scatter; }

    void setMarkerSize(int size) {
        size = std::max(2, std::min(24, size));
        if (m_markerSize == size) {
            return;
        }
        m_markerSize = size;
        updated();
    }

    int markerSize() const { return m_markerSize; }

private:
    int m_markerSize{7};
};

