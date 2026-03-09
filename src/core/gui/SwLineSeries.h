
/**
 * @file
 * @ingroup core_gui
 * @brief Declares the standard XY line series used by charts.
 *
 * `SwLineSeries` stores ordered points that chart views render as connected straight
 * segments. It provides the simplest plotted-series model for trends, telemetry, and other
 * continuously sampled data displayed against chart axes.
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

class SwLineSeries : public SwAbstractSeries {
    SW_OBJECT(SwLineSeries, SwAbstractSeries)

public:
    /**
     * @brief Constructs a `SwLineSeries` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    explicit SwLineSeries(SwObject* parent = nullptr)
        : SwAbstractSeries(parent) {}

    /**
     * @brief Returns the current type.
     * @return The current type.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SeriesType type() const override { return SeriesType::Line; }
};
