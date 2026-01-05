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

#include "Sw.h"
#include "SwGraphicsTypes.h"

class SwGradient {
public:
    SwGradient() = default;
    SwGradient(const SwPointF& start, const SwPointF& end, const SwColor& a, const SwColor& b)
        : m_start(start), m_end(end), m_color0(a), m_color1(b) {}

    SwPointF start() const { return m_start; }
    SwPointF finalStop() const { return m_end; }
    SwColor color0() const { return m_color0; }
    SwColor color1() const { return m_color1; }

private:
    SwPointF m_start{};
    SwPointF m_end{};
    SwColor m_color0{0, 0, 0};
    SwColor m_color1{255, 255, 255};
};

class SwBrush {
public:
    enum Style {
        NoBrush,
        SolidPattern,
        LinearGradientPattern
    };

    SwBrush() = default;
    explicit SwBrush(const SwColor& c) : m_style(SolidPattern), m_color(c) {}
    explicit SwBrush(const SwGradient& g) : m_style(LinearGradientPattern), m_gradient(g) {}

    Style style() const { return m_style; }

    void setColor(const SwColor& c) {
        m_style = SolidPattern;
        m_color = c;
    }
    SwColor color() const { return m_color; }

    void setGradient(const SwGradient& g) {
        m_style = LinearGradientPattern;
        m_gradient = g;
    }
    SwGradient gradient() const { return m_gradient; }

private:
    Style m_style{NoBrush};
    SwColor m_color{0, 0, 0};
    SwGradient m_gradient{};
};

