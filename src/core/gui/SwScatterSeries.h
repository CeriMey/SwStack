
/**
 * @file
 * @ingroup core_gui
 * @brief Declares the scatter-plot series type used by charts.
 *
 * `SwScatterSeries` stores independent XY markers without connecting them by lines. The
 * chart renderer uses this series when the visual focus is on point distribution, density,
 * or correlation rather than interpolation between samples.
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

#pragma once



#include "SwAbstractSeries.h"

#include <algorithm>

class SwScatterSeries : public SwAbstractSeries {
    SW_OBJECT(SwScatterSeries, SwAbstractSeries)

public:
    /**
     * @brief Constructs a `SwScatterSeries` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwScatterSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SeriesType type() const override { return SeriesType::Scatter; }

    /**
     * @brief Sets the marker Size.
     * @param size Size value used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setMarkerSize(int size) {
        size = std::max(2, std::min(24, size));
        if (m_markerSize == size) {
            return;
        }
        m_markerSize = size;
        updated();
    }

    /**
     * @brief Returns the current marker Size.
     * @return The current marker Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int markerSize() const { return m_markerSize; }

private:
    int m_markerSize{7};
};
