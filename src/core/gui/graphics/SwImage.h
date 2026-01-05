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

#include "SwString.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class SwImage {
public:
    enum Format {
        Format_Invalid,
        Format_ARGB32
    };

    SwImage() = default;
    SwImage(int w, int h, Format fmt = Format_ARGB32) {
        create(w, h, fmt);
    }

    bool isNull() const { return m_width <= 0 || m_height <= 0 || m_pixels.empty(); }

    int width() const { return m_width; }
    int height() const { return m_height; }
    int bytesPerLine() const { return m_width > 0 ? m_width * 4 : 0; }
    Format format() const { return m_format; }

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

    void fill(std::uint32_t argb) {
        std::fill(m_pixels.begin(), m_pixels.end(), argb);
    }

    std::uint32_t* bits() { return m_pixels.empty() ? nullptr : m_pixels.data(); }
    const std::uint32_t* constBits() const { return m_pixels.empty() ? nullptr : m_pixels.data(); }

    std::uint32_t* scanLine(int y) {
        if (y < 0 || y >= m_height || m_width <= 0) {
            return nullptr;
        }
        return m_pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(m_width);
    }

    const std::uint32_t* constScanLine(int y) const {
        if (y < 0 || y >= m_height || m_width <= 0) {
            return nullptr;
        }
        return m_pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(m_width);
    }

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

