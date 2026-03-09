#pragma once

/**
 * @file src/core/gui/graphics/SwImage.h
 * @ingroup core_graphics
 * @brief Declares the public interface exposed by SwImage in the CoreSw graphics layer.
 *
 * This header belongs to the CoreSw graphics layer. It provides geometry types, painting
 * primitives, images, scene-graph helpers, and rendering support consumed by widgets and views.
 *
 * Within that layer, this file focuses on the image interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwImage.
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
 * @brief Defines SwImage, the graphics stack's in-memory raster image container.
 *
 * SwImage is the lowest-level image value type used by the GUI layer. It owns a
 * contiguous pixel buffer, exposes direct scanline access, and provides compact
 * BMP-based load/save helpers so the rest of the stack can exchange pixels
 * without relying on an external image backend.
 *
 * The implementation is intentionally narrow: it currently standardizes on a
 * single 32-bit ARGB format. That keeps memory layout, serialization, and
 * painting integration predictable for callers such as SwPixmap, icon helpers,
 * scene items, snapshot tools, and custom widgets.
 */

#include "SwString.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

/**
 * @brief Owns a 32-bit pixel buffer together with basic image I/O helpers.
 *
 * The class behaves like a regular value type. Copying a SwImage duplicates the
 * underlying pixel storage, which makes it easy to pass images between widgets
 * and rendering helpers without sharing lifetime-sensitive backend objects.
 */
class SwImage {
public:
    enum Format {
        Format_Invalid,
        Format_ARGB32
    };

    /**
     * @brief Constructs a `SwImage` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwImage() = default;
    /**
     * @brief Constructs a `SwImage` instance.
     * @param w Width value.
     * @param h Height value.
     * @param fmt Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwImage(int w, int h, Format fmt = Format_ARGB32) {
        create(w, h, fmt);
    }

    /**
     * @brief Returns whether the object reports null.
     * @return `true` when the object reports null; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isNull() const { return m_width <= 0 || m_height <= 0 || m_pixels.empty(); }

    /**
     * @brief Returns the current width.
     * @return The current width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int width() const { return m_width; }
    /**
     * @brief Returns the current height.
     * @return The current height.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int height() const { return m_height; }
    /**
     * @brief Returns the current bytes Per Line.
     * @return The current bytes Per Line.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int bytesPerLine() const { return m_width > 0 ? m_width * 4 : 0; }
    /**
     * @brief Returns the current format.
     * @return The current format.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Format format() const { return m_format; }

    /**
     * @brief Creates the requested create.
     * @param w Width value.
     * @param h Height value.
     * @param fmt Value passed to the method.
     */
    void create(int w, int h, Format fmt = Format_ARGB32) {
        m_width = w;
        m_height = h;
        m_format = fmt;
        if (m_width <= 0 || m_height <= 0 || m_format != Format_ARGB32) {
            m_width = 0;
            m_height = 0;
            m_format = Format_Invalid;
            m_pixels.clear();
            return;
        }
        m_pixels.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), 0x00000000u);
    }

    /**
     * @brief Performs the `fill` operation.
     * @param argb Value passed to the method.
     */
    void fill(std::uint32_t argb) {
        std::fill(m_pixels.begin(), m_pixels.end(), argb);
    }

    /**
     * @brief Performs the `bits` operation.
     * @return The requested bits.
     */
    std::uint32_t* bits() { return m_pixels.empty() ? nullptr : m_pixels.data(); }
    /**
     * @brief Performs the `constBits` operation.
     * @return The requested const Bits.
     */
    const std::uint32_t* constBits() const { return m_pixels.empty() ? nullptr : m_pixels.data(); }

    /**
     * @brief Performs the `scanLine` operation.
     * @param y Vertical coordinate.
     * @return The requested scan Line.
     */
    std::uint32_t* scanLine(int y) {
        if (y < 0 || y >= m_height || m_width <= 0) {
            return nullptr;
        }
        return m_pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(m_width);
    }

    /**
     * @brief Performs the `constScanLine` operation.
     * @param y Vertical coordinate.
     * @return The requested const Scan Line.
     */
    const std::uint32_t* constScanLine(int y) const {
        if (y < 0 || y >= m_height || m_width <= 0) {
            return nullptr;
        }
        return m_pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(m_width);
    }

    /**
     * @brief Performs the `save` operation on the associated resource.
     * @param filePath Path of the target file.
     * @return `true` on success; otherwise `false`.
     */
    bool save(const SwString& filePath) const {
        if (isNull()) {
            return false;
        }
        const std::string path = filePath.toStdString();
        std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }

        // 32-bit BMP, top-down (negative height), BI_RGB.
        const std::uint32_t fileHeaderSize = 14;
        const std::uint32_t infoHeaderSize = 40;
        const std::uint32_t pixelBytes = static_cast<std::uint32_t>(m_width * m_height * 4);
        const std::uint32_t fileSize = fileHeaderSize + infoHeaderSize + pixelBytes;

        auto writeU16 = [&](std::uint16_t v) { out.put(static_cast<char>(v & 0xFF)); out.put(static_cast<char>((v >> 8) & 0xFF)); };
        auto writeU32 = [&](std::uint32_t v) {
            out.put(static_cast<char>(v & 0xFF));
            out.put(static_cast<char>((v >> 8) & 0xFF));
            out.put(static_cast<char>((v >> 16) & 0xFF));
            out.put(static_cast<char>((v >> 24) & 0xFF));
        };
        auto writeS32 = [&](std::int32_t v) { writeU32(static_cast<std::uint32_t>(v)); };

        // BITMAPFILEHEADER
        out.put('B');
        out.put('M');
        writeU32(fileSize);
        writeU16(0);
        writeU16(0);
        writeU32(fileHeaderSize + infoHeaderSize);

        // BITMAPINFOHEADER
        writeU32(infoHeaderSize);
        writeS32(static_cast<std::int32_t>(m_width));
        writeS32(static_cast<std::int32_t>(-m_height)); // top-down
        writeU16(1);                                    // planes
        writeU16(32);                                   // bpp
        writeU32(0);                                    // BI_RGB
        writeU32(pixelBytes);
        writeS32(2835); // 72 DPI
        writeS32(2835);
        writeU32(0); // colors used
        writeU32(0); // important

        // Pixels: stored as 0xAARRGGBB in memory -> little-endian BGRA, matches BMP 32bpp.
        out.write(reinterpret_cast<const char*>(constBits()), static_cast<std::streamsize>(pixelBytes));
        out.flush();
        return out.good();
    }

    /**
     * @brief Performs the `load` operation on the associated resource.
     * @param filePath Path of the target file.
     * @return `true` on success; otherwise `false`.
     */
    bool load(const SwString& filePath) {
        const std::string path = filePath.toStdString();
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in.is_open()) {
            return false;
        }

        auto readU16 = [&]() -> std::uint16_t {
            const std::uint8_t b0 = static_cast<std::uint8_t>(in.get());
            const std::uint8_t b1 = static_cast<std::uint8_t>(in.get());
            return static_cast<std::uint16_t>(b0 | (static_cast<std::uint16_t>(b1) << 8));
        };
        auto readU32 = [&]() -> std::uint32_t {
            const std::uint8_t b0 = static_cast<std::uint8_t>(in.get());
            const std::uint8_t b1 = static_cast<std::uint8_t>(in.get());
            const std::uint8_t b2 = static_cast<std::uint8_t>(in.get());
            const std::uint8_t b3 = static_cast<std::uint8_t>(in.get());
            return static_cast<std::uint32_t>(b0) |
                   (static_cast<std::uint32_t>(b1) << 8) |
                   (static_cast<std::uint32_t>(b2) << 16) |
                   (static_cast<std::uint32_t>(b3) << 24);
        };
        auto readS32 = [&]() -> std::int32_t { return static_cast<std::int32_t>(readU32()); };

        const char b0 = static_cast<char>(in.get());
        const char b1 = static_cast<char>(in.get());
        if (b0 != 'B' || b1 != 'M') {
            return false;
        }
        (void)readU32(); // file size
        (void)readU16(); // reserved1
        (void)readU16(); // reserved2
        const std::uint32_t dataOffset = readU32();

        const std::uint32_t headerSize = readU32();
        if (headerSize < 40) {
            return false;
        }
        const std::int32_t w = readS32();
        const std::int32_t h = readS32();
        const std::uint16_t planes = readU16();
        const std::uint16_t bpp = readU16();
        const std::uint32_t compression = readU32();
        const std::uint32_t imageSize = readU32();
        (void)readS32(); // ppm x
        (void)readS32(); // ppm y
        (void)readU32(); // colors used
        (void)readU32(); // important

        if (planes != 1 || bpp != 32 || compression != 0) {
            return false;
        }
        if (w <= 0 || h == 0) {
            return false;
        }

        const bool topDown = h < 0;
        const int absH = topDown ? -h : h;

        create(static_cast<int>(w), absH, Format_ARGB32);
        if (isNull()) {
            return false;
        }

        in.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);
        if (!in.good()) {
            return false;
        }

        const std::uint32_t expectedBytes = static_cast<std::uint32_t>(m_width * m_height * 4);
        const std::uint32_t bytesToRead = imageSize != 0 ? std::min(imageSize, expectedBytes) : expectedBytes;

        std::vector<std::uint8_t> tmp(bytesToRead);
        in.read(reinterpret_cast<char*>(tmp.data()), static_cast<std::streamsize>(bytesToRead));
        if (!in.good()) {
            return false;
        }

        // Copy rows. Data is BGRA in file. Our storage is 0xAARRGGBB -> BGRA in memory, so memcpy.
        const std::uint32_t rowBytes = static_cast<std::uint32_t>(m_width * 4);
        const std::uint32_t rows = static_cast<std::uint32_t>(m_height);
        for (std::uint32_t y = 0; y < rows; ++y) {
            const std::uint32_t srcY = topDown ? y : (rows - 1 - y);
            const std::uint8_t* src = tmp.data() + srcY * rowBytes;
            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(scanLine(static_cast<int>(y)));
            std::copy(src, src + rowBytes, dst);
        }

        return true;
    }

private:
    int m_width{0};
    int m_height{0};
    Format m_format{Format_Invalid};
    std::vector<std::uint32_t> m_pixels;
};
