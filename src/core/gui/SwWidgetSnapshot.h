#pragma once

/**
 * @file src/core/gui/SwWidgetSnapshot.h
 * @ingroup core_gui
 * @brief Declares the public interface exposed by SwWidgetSnapshot in the CoreSw GUI layer.
 *
 * This header belongs to the CoreSw GUI layer. It defines widgets, dialogs, models, delegates,
 * styling helpers, and application integration for the native UI stack.
 *
 * Within that layer, this file focuses on the widget snapshot interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwWidgetSnapshot.
 *
 * Widget-oriented declarations here usually capture persistent UI state, input handling, layout
 * participation, and paint-time behavior while keeping platform-specific rendering details behind
 * lower layers.
 *
 * GUI-facing declarations here are expected to cooperate with event delivery, layout, painting,
 * focus, and parent-child ownership rules.
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

#include "SwWidget.h"

#include "core/io/SwFile.h"
#include "core/types/SwByteArray.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <cstdint>
#endif

class SwWidgetSnapshot {
public:
    /**
     * @brief Performs the `savePng` operation on the associated resource.
     * @param widget Widget associated with the operation.
     * @param filePath Path of the target file.
     * @return The requested png.
     */
    static bool savePng(SwWidgetInterface* widget, const SwString& filePath) {
        if (!widget) {
            return false;
        }

#if defined(_WIN32)
        SwRect rect = widget->frameGeometry();
        if (auto* concrete = dynamic_cast<SwWidget*>(widget)) {
            rect = concrete->rect();
        } else {
            rect.x = 0;
            rect.y = 0;
        }
        if (rect.width <= 0 || rect.height <= 0) {
            return false;
        }

        HDC screenDc = GetDC(nullptr);
        if (!screenDc) {
            return false;
        }

        HDC memDc = CreateCompatibleDC(screenDc);
        if (!memDc) {
            ReleaseDC(nullptr, screenDc);
            return false;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = rect.width;
        bmi.bmiHeader.biHeight = -rect.height; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) {
            if (dib) {
                DeleteObject(dib);
            }
            DeleteDC(memDc);
            ReleaseDC(nullptr, screenDc);
            return false;
        }

        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDc, dib));

        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp || !guiApp->platformIntegration()) {
            SelectObject(memDc, oldBitmap);
            DeleteObject(dib);
            DeleteDC(memDc);
            ReleaseDC(nullptr, screenDc);
            return false;
        }

        SwScopedPlatformPainter painter(guiApp->platformIntegration(),
                                        SwMakePlatformPaintEvent(SwPlatformSize{rect.width, rect.height},
                                                                 memDc,
                                                                 nullptr,
                                                                 nullptr,
                                                                 SwPlatformRect{0, 0, rect.width, rect.height}));
        if (!painter) {
            SelectObject(memDc, oldBitmap);
            DeleteObject(dib);
            DeleteDC(memDc);
            ReleaseDC(nullptr, screenDc);
            return false;
        }

        painter->clear(SwColor{255, 255, 255});

        PaintEvent paintEvent(painter.asPainter(), rect);
        SwCoreApplication::sendEvent(widget, &paintEvent);
        painter->finalize();
        painter->flush();

        const int strideBytes = rect.width * 4;
        SwByteArray png = encodePngRgb24(static_cast<const std::uint8_t*>(bits),
                                         rect.width,
                                         rect.height,
                                         strideBytes);

        SelectObject(memDc, oldBitmap);
        DeleteObject(dib);
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);

        if (png.size() == 0) {
            return false;
        }

        SwFile out(filePath);
        if (!out.openBinary(SwFile::Write)) {
            return false;
        }
        bool ok = out.write(png);
        out.close();
        return ok;
#else
        SW_UNUSED(filePath)
        return false;
#endif
    }

private:
#if defined(_WIN32)
    static void appendU8(SwByteArray& out, std::uint8_t value) {
        out.append(static_cast<char>(value));
    }

    static void appendU16LE(SwByteArray& out, std::uint16_t value) {
        appendU8(out, static_cast<std::uint8_t>(value & 0xFF));
        appendU8(out, static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }

    static void appendU32BE(SwByteArray& out, std::uint32_t value) {
        appendU8(out, static_cast<std::uint8_t>((value >> 24) & 0xFF));
        appendU8(out, static_cast<std::uint8_t>((value >> 16) & 0xFF));
        appendU8(out, static_cast<std::uint8_t>((value >> 8) & 0xFF));
        appendU8(out, static_cast<std::uint8_t>(value & 0xFF));
    }

    static std::uint32_t crc32TableEntry(std::uint32_t index) {
        std::uint32_t c = index;
        for (int k = 0; k < 8; ++k) {
            if (c & 1U) {
                c = 0xEDB88320U ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        return c;
    }

    static const std::uint32_t* crc32Table() {
        static std::uint32_t table[256];
        static bool initialized = false;
        if (!initialized) {
            for (std::uint32_t i = 0; i < 256; ++i) {
                table[i] = crc32TableEntry(i);
            }
            initialized = true;
        }
        return table;
    }

    static std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* data, std::uint32_t length) {
        const std::uint32_t* table = crc32Table();
        std::uint32_t c = crc;
        for (std::uint32_t i = 0; i < length; ++i) {
            c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
        }
        return c;
    }

    static void appendChunk(SwByteArray& out, const char type[4], const SwByteArray& data) {
        appendU32BE(out, static_cast<std::uint32_t>(data.size()));

        out.append(type, 4);
        if (data.size() > 0 && data.constData()) {
            out.append(data.constData(), data.size());
        }

        std::uint32_t crc = 0xFFFFFFFFU;
        crc = crc32Update(crc, reinterpret_cast<const std::uint8_t*>(type), 4);
        if (data.size() > 0 && data.constData()) {
            crc = crc32Update(crc,
                              reinterpret_cast<const std::uint8_t*>(data.constData()),
                              static_cast<std::uint32_t>(data.size()));
        }
        crc ^= 0xFFFFFFFFU;
        appendU32BE(out, crc);
    }

    static void adler32Update(std::uint32_t& a, std::uint32_t& b, std::uint8_t byte) {
        static const std::uint32_t mod = 65521U;
        a += byte;
        if (a >= mod) {
            a -= mod;
        }
        b += a;
        b %= mod;
    }

    static void deflateStoreFlush(SwByteArray& out, SwByteArray& block, bool finalBlock) {
        appendU8(out, finalBlock ? 0x01 : 0x00);

        std::uint16_t len = static_cast<std::uint16_t>(block.size());
        std::uint16_t nlen = static_cast<std::uint16_t>(~len);

        appendU16LE(out, len);
        appendU16LE(out, nlen);

        if (block.size() > 0 && block.constData()) {
            out.append(block.constData(), block.size());
        }
        block.clear();
    }

    static SwByteArray encodeZlibStoreRgb24(const std::uint8_t* bgrxPixels,
                                           int width,
                                           int height,
                                           int strideBytes) {
        SwByteArray out;

        const int rowBytes = 1 + width * 3;
        const size_t rawBytes = static_cast<size_t>(height) * static_cast<size_t>(rowBytes);
        const size_t blocks = (rawBytes + 65534u) / 65535u;
        const size_t estimated = 2u + rawBytes + (blocks * 5u) + 4u + 64u;
        out.reserve(estimated);

        appendU8(out, 0x78);
        appendU8(out, 0x01);

        std::uint32_t adlerA = 1;
        std::uint32_t adlerB = 0;

        SwByteArray block;
        block.reserve(65535);

        SwByteArray rowBuf;
        rowBuf.reserve(static_cast<size_t>(rowBytes));

        auto adlerUpdateBuffer = [&](const char* data, size_t len) {
            if (!data || len == 0) {
                return;
            }
            for (size_t i = 0; i < len; ++i) {
                adler32Update(adlerA, adlerB, static_cast<std::uint8_t>(data[i]));
            }
        };

        auto appendToBlock = [&](const char* data, size_t len) {
            const char* p = data;
            size_t remaining = len;
            while (remaining > 0) {
                size_t space = 65535u - block.size();
                if (space == 0u) {
                    deflateStoreFlush(out, block, false);
                    space = 65535u;
                }

                const size_t chunk = (remaining < space) ? remaining : space;
                block.append(p, chunk);
                p += chunk;
                remaining -= chunk;

                if (block.size() == 65535u) {
                    deflateStoreFlush(out, block, false);
                }
            }
        };

        for (int y = 0; y < height; ++y) {
            rowBuf.resize(static_cast<size_t>(rowBytes));
            char* dst = rowBuf.data();
            dst[0] = 0; // filter type: None

            const std::uint8_t* row = bgrxPixels + (y * strideBytes);
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* px = row + (x * 4);
                const size_t base = static_cast<size_t>(1 + x * 3);
                dst[base + 0] = static_cast<char>(px[2]); // R
                dst[base + 1] = static_cast<char>(px[1]); // G
                dst[base + 2] = static_cast<char>(px[0]); // B
            }

            adlerUpdateBuffer(rowBuf.constData(), rowBuf.size());
            appendToBlock(rowBuf.constData(), rowBuf.size());
        }

        deflateStoreFlush(out, block, true);

        std::uint32_t adler = (adlerB << 16) | adlerA;
        appendU32BE(out, adler);
        return out;
    }

    static SwByteArray encodePngRgb24(const std::uint8_t* bgrxPixels,
                                      int width,
                                      int height,
                                      int strideBytes) {
        if (!bgrxPixels || width <= 0 || height <= 0 || strideBytes <= 0) {
            return SwByteArray();
        }

        SwByteArray out;

        static const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        out.append(reinterpret_cast<const char*>(signature), 8);

        SwByteArray ihdr;
        appendU32BE(ihdr, static_cast<std::uint32_t>(width));
        appendU32BE(ihdr, static_cast<std::uint32_t>(height));
        appendU8(ihdr, 8); // bit depth
        appendU8(ihdr, 2); // color type: truecolor RGB
        appendU8(ihdr, 0); // compression
        appendU8(ihdr, 0); // filter
        appendU8(ihdr, 0); // interlace
        appendChunk(out, "IHDR", ihdr);

        SwByteArray idat = encodeZlibStoreRgb24(bgrxPixels, width, height, strideBytes);
        appendChunk(out, "IDAT", idat);

        SwByteArray iend;
        appendChunk(out, "IEND", iend);

        return out;
    }
#endif
};
