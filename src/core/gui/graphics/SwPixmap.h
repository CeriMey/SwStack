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

#include "graphics/SwImage.h"

class SwPixmap {
public:
    SwPixmap() = default;
    explicit SwPixmap(const SwImage& img) : m_img(img) {}
    SwPixmap(int w, int h) : m_img(w, h, SwImage::Format_ARGB32) {}

    bool isNull() const { return m_img.isNull(); }
    int width() const { return m_img.width(); }
    int height() const { return m_img.height(); }

    SwImage toImage() const { return m_img; }
    SwImage& image() { return m_img; }
    const SwImage& image() const { return m_img; }

    bool load(const SwString& filePath) { return m_img.load(filePath); }
    bool save(const SwString& filePath) const { return m_img.save(filePath); }

private:
    SwImage m_img;
};

