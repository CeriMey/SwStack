#pragma once

/**
 * @file src/core/gui/graphics/SwPixmap.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwPixmap in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the pixmap interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwPixmap.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
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
 * @brief Declares SwPixmap, a lightweight GUI-facing image handle.
 *
 * SwPixmap wraps SwImage behind an API that matches the expectations of higher
 * level widgets, styles, icons, and graphics items. In this stack the type is a
 * small value wrapper rather than a platform-owned GPU resource, which keeps it
 * easy to copy, store, and serialize inside UI code.
 */

#include "graphics/SwImage.h"

/**
 * @brief Stores an image in the pixmap role expected by GUI components.
 *
 * The class mostly forwards to the underlying SwImage while providing a clearer
 * semantic boundary between raw image buffers and assets used for presentation.
 */
class SwPixmap {
public:
    /**
     * @brief Constructs a `SwPixmap` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPixmap() = default;
    /**
     * @brief Constructs a `SwPixmap` instance.
     * @param img Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwPixmap(const SwImage& img) : m_img(img) {}
    /**
     * @brief Constructs a `SwPixmap` instance.
     * @param w Width value.
     * @param Format_ARGB32 Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwPixmap(int w, int h) : m_img(w, h, SwImage::Format_ARGB32) {}

    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isNull() const { return m_img.isNull(); }
    /**
     * @brief Performs the `width` operation.
     * @return The current width value.
     */
    int width() const { return m_img.width(); }
    /**
     * @brief Performs the `height` operation.
     * @return The current height value.
     */
    int height() const { return m_img.height(); }

    /**
     * @brief Returns the current to Image.
     * @return The current to Image.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwImage toImage() const { return m_img; }
    /**
     * @brief Returns the current image.
     * @return The current image.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwImage& image() { return m_img; }
    /**
     * @brief Returns the current image.
     * @return The current image.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwImage& image() const { return m_img; }

    /**
     * @brief Performs the `load` operation on the associated resource.
     * @param filePath Path of the target file.
     * @return `true` on success; otherwise `false`.
     */
    bool load(const SwString& filePath) { return m_img.load(filePath); }
    /**
     * @brief Performs the `save` operation on the associated resource.
     * @param filePath Path of the target file.
     * @return `true` on success; otherwise `false`.
     */
    bool save(const SwString& filePath) const { return m_img.save(filePath); }

private:
    SwImage m_img;
};
