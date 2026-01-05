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

class SwPen {
public:
    SwPen() = default;
    explicit SwPen(const SwColor& c, int w = 1) : m_color(c), m_width(w) {}

    void setColor(const SwColor& c) { m_color = c; }
    SwColor color() const { return m_color; }

    void setWidth(int w) { m_width = w; }
    int width() const { return m_width; }

    bool isCosmetic() const { return m_cosmetic; }
    void setCosmetic(bool on) { m_cosmetic = on; }

private:
    SwColor m_color{0, 0, 0};
    int m_width{1};
    bool m_cosmetic{false};
};

