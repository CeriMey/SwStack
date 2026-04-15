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

#include <cstdint>
#include <vector>

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#elif SW_PLATFORM_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "platform/x11/SwX11Painter.h"
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

#if defined(_WIN32)
        RenderBuffer_& buf = RenderBuffer_::instance();
        if (!buf.ensure(rect.width, rect.height)) {
            return false;
        }

        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp || !guiApp->platformIntegration()) {
            return false;
        }

        SwScopedPlatformPainter painter(guiApp->platformIntegration(),
                                        SwMakePlatformPaintEvent(SwPlatformSize{rect.width, rect.height},
                                                                 buf.dc,
                                                                 nullptr,
                                                                 nullptr,
                                                                 SwPlatformRect{0, 0, rect.width, rect.height}));
        if (!painter) {
            return false;
        }
        painter->clear(SwColor{255, 255, 255});
        PaintEvent paintEvent(painter.asPainter(), rect);
        SwCoreApplication::sendEvent(widget, &paintEvent);
        painter->finalize();
        painter->flush();

        const int strideBytes = rect.width * 4;
        SwByteArray png = encodePngRgb24(static_cast<const std::uint8_t*>(buf.bits),
                                         rect.width,
                                         rect.height,
                                         strideBytes);
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
#elif SW_PLATFORM_X11
        return savePngX11_(widget, filePath, rect.width, rect.height);
#else
        SW_UNUSED(filePath)
        return false;
#endif
    }

private:
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

    static std::uint32_t crc32Update(std::uint32_t crc,
                                     const std::uint8_t* data,
                                     std::uint32_t length) {
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

        static const std::uint32_t ADLER_MOD = 65521U;
        static const size_t ADLER_NMAX = 5552U;
        std::uint32_t adlerA = 1;
        std::uint32_t adlerB = 0;
        size_t adlerAcc = 0;

        auto adlerUpdateBuf = [&](const std::uint8_t* data, size_t len) {
            while (len > 0) {
                const size_t room = ADLER_NMAX - adlerAcc;
                const size_t n = (len < room) ? len : room;
                for (size_t i = 0; i < n; ++i) {
                    adlerA += data[i];
                    adlerB += adlerA;
                }
                data += n;
                len -= n;
                adlerAcc += n;
                if (adlerAcc >= ADLER_NMAX) {
                    adlerA %= ADLER_MOD;
                    adlerB %= ADLER_MOD;
                    adlerAcc = 0;
                }
            }
        };

        SwByteArray block;
        block.reserve(65535);

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

        SwByteArray rowBuf;
        rowBuf.resize(static_cast<size_t>(rowBytes));

        for (int y = 0; y < height; ++y) {
            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(rowBuf.data());
            dst[0] = 0;

            const std::uint8_t* row = bgrxPixels + (y * strideBytes);
            std::uint8_t* rgb = dst + 1;
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* px = row + (x * 4);
                rgb[x * 3 + 0] = px[2];
                rgb[x * 3 + 1] = px[1];
                rgb[x * 3 + 2] = px[0];
            }

            adlerUpdateBuf(dst, static_cast<size_t>(rowBytes));
            appendToBlock(rowBuf.constData(), static_cast<size_t>(rowBytes));
        }

        deflateStoreFlush(out, block, true);

        if (adlerAcc > 0) {
            adlerA %= ADLER_MOD;
            adlerB %= ADLER_MOD;
        }

        const std::uint32_t adler = (adlerB << 16) | adlerA;
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
        appendU8(ihdr, 8);
        appendU8(ihdr, 2);
        appendU8(ihdr, 0);
        appendU8(ihdr, 0);
        appendU8(ihdr, 0);
        appendChunk(out, "IHDR", ihdr);

        SwByteArray idat = encodeZlibStoreRgb24(bgrxPixels, width, height, strideBytes);
        appendChunk(out, "IDAT", idat);

        SwByteArray iend;
        appendChunk(out, "IEND", iend);

        return out;
    }

#if defined(_WIN32)
    struct RenderBuffer_ {
        HDC      dc{nullptr};
        HBITMAP  bitmap{nullptr};
        HBITMAP  prevBitmap{nullptr};
        void*    bits{nullptr};
        int      width{0};
        int      height{0};

        static RenderBuffer_& instance() {
            static RenderBuffer_ buf;
            return buf;
        }

        bool ensure(int w, int h) {
            if (dc && width == w && height == h) {
                return true; // reuse existing buffer
            }
            release();
            HDC screenDc = GetDC(nullptr);
            if (!screenDc) return false;
            dc = CreateCompatibleDC(screenDc);
            if (!dc) { ReleaseDC(nullptr, screenDc); return false; }

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = w;
            bmi.bmiHeader.biHeight      = -h; // top-down
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            bitmap = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            ReleaseDC(nullptr, screenDc);
            if (!bitmap || !bits) { release(); return false; }

            prevBitmap = static_cast<HBITMAP>(SelectObject(dc, bitmap));
            width = w;
            height = h;
            return true;
        }

        void release() {
            if (dc) {
                if (prevBitmap) SelectObject(dc, prevBitmap);
                if (bitmap)     DeleteObject(bitmap);
                DeleteDC(dc);
            }
            dc = nullptr; bitmap = nullptr; prevBitmap = nullptr;
            bits = nullptr; width = 0; height = 0;
        }

        ~RenderBuffer_() { release(); }
    };
#endif

#if SW_PLATFORM_X11
    static int maskShiftX11_(unsigned long mask) {
        int shift = 0;
        while (mask != 0 && (mask & 1UL) == 0UL) {
            mask >>= 1U;
            ++shift;
        }
        return shift;
    }

    static int maskBitsX11_(unsigned long mask) {
        int bits = 0;
        while (mask != 0) {
            bits += static_cast<int>(mask & 1UL);
            mask >>= 1U;
        }
        return bits;
    }

    static std::uint8_t expandMaskComponentX11_(unsigned long value, int bits) {
        if (bits <= 0) {
            return 0U;
        }
        const unsigned long maxValue = (bits >= static_cast<int>(sizeof(unsigned long) * 8U))
                                           ? ~0UL
                                           : ((1UL << bits) - 1UL);
        if (maxValue == 0UL) {
            return 0U;
        }
        return static_cast<std::uint8_t>((value * 255UL + (maxValue / 2UL)) / maxValue);
    }

    static bool copyPixmapToBgrxX11_(Display* display,
                                     ::Pixmap pixmap,
                                     int width,
                                     int height,
                                     std::vector<std::uint8_t>& outPixels) {
        if (!display || pixmap == 0 || width <= 0 || height <= 0) {
            return false;
        }

        XImage* image =
            XGetImage(display, pixmap, 0, 0, static_cast<unsigned int>(width), static_cast<unsigned int>(height), AllPlanes, ZPixmap);
        if (!image) {
            return false;
        }

        outPixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U, 0U);
        const bool directLittle24 =
            (image->byte_order == LSBFirst) &&
            (image->bits_per_pixel == 24 || image->bits_per_pixel == 32) &&
            image->bytes_per_line >= (width * ((image->bits_per_pixel + 7) / 8));
        const bool directBig32 =
            (image->byte_order == MSBFirst) &&
            (image->bits_per_pixel == 32) &&
            image->bytes_per_line >= (width * 4);
        const int redShift = maskShiftX11_(image->red_mask);
        const int greenShift = maskShiftX11_(image->green_mask);
        const int blueShift = maskShiftX11_(image->blue_mask);
        const int redBits = maskBitsX11_(image->red_mask);
        const int greenBits = maskBitsX11_(image->green_mask);
        const int blueBits = maskBitsX11_(image->blue_mask);
        const int sourceBytesPerPixel = (image->bits_per_pixel + 7) / 8;

        for (int y = 0; y < height; ++y) {
            const unsigned char* row =
                reinterpret_cast<const unsigned char*>(image->data + (y * image->bytes_per_line));
            for (int x = 0; x < width; ++x) {
                std::uint8_t* dst =
                    outPixels.data() +
                    ((static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4U);
                if (directLittle24) {
                    const unsigned char* src = row + (x * sourceBytesPerPixel);
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 0xFFU;
                    continue;
                }
                if (directBig32) {
                    const unsigned char* src = row + (x * 4);
                    dst[0] = src[3];
                    dst[1] = src[2];
                    dst[2] = src[1];
                    dst[3] = 0xFFU;
                    continue;
                }

                const unsigned long pixel = XGetPixel(image, x, y);
                dst[2] =
                    expandMaskComponentX11_((pixel & image->red_mask) >> redShift, redBits);
                dst[1] =
                    expandMaskComponentX11_((pixel & image->green_mask) >> greenShift, greenBits);
                dst[0] =
                    expandMaskComponentX11_((pixel & image->blue_mask) >> blueShift, blueBits);
                dst[3] = 0xFFU;
            }
        }

        XDestroyImage(image);
        return true;
    }

    static bool savePngX11_(SwWidgetInterface* widget,
                            const SwString& filePath,
                            int width,
                            int height) {
        auto* concrete = dynamic_cast<SwWidget*>(widget);
        if (!concrete) {
            return false;
        }

        const SwWidget* nativeOwner = concrete;
        SwWidgetPlatformHandle nativeHandle = nativeOwner->platformHandle();
        while (!nativeHandle && nativeOwner) {
            nativeOwner = dynamic_cast<const SwWidget*>(nativeOwner->parent());
            if (nativeOwner) {
                nativeHandle = nativeOwner->platformHandle();
            }
        }

        Display* display = reinterpret_cast<Display*>(nativeHandle.nativeDisplay);
        ::Window window = reinterpret_cast<::Window>(nativeHandle.nativeHandle);
        if (!display || window == 0) {
            return false;
        }

        SwGuiApplication* guiApp = SwGuiApplication::instance(false);
        if (!guiApp || !guiApp->platformIntegration()) {
            return false;
        }

        const int screen = DefaultScreen(display);
        const unsigned int pixmapWidth = static_cast<unsigned int>(width);
        const unsigned int pixmapHeight = static_cast<unsigned int>(height);
        ::Pixmap snapshotPixmap =
            XCreatePixmap(display,
                          window,
                          pixmapWidth,
                          pixmapHeight,
                          static_cast<unsigned int>(DefaultDepth(display, screen)));
        if (snapshotPixmap == 0) {
            return false;
        }

        bool painted = false;
        {
            SwScopedPlatformPainter painter(guiApp->platformIntegration(),
                                            SwMakePlatformPaintEvent(SwPlatformSize{width, height},
                                                                     reinterpret_cast<void*>(snapshotPixmap),
                                                                     nullptr,
                                                                     display,
                                                                     SwPlatformRect{0, 0, width, height}));
            if (painter) {
                painter->clear(SwColor{255, 255, 255});
                PaintEvent paintEvent(painter.asPainter(), SwRect{0, 0, width, height});
                SwCoreApplication::sendEvent(widget, &paintEvent);
                painter->finalize();
                painter->flush();
                painted = true;
            }
        }

        std::vector<std::uint8_t> pixels;
        bool copied = false;
        if (painted) {
            XSync(display, False);
            const ::Pixmap renderedPixmap = SwX11Painter::sharedBackbufferHandle(display, snapshotPixmap);
            copied = copyPixmapToBgrxX11_(display,
                                          renderedPixmap != 0 ? renderedPixmap : snapshotPixmap,
                                          width,
                                          height,
                                          pixels);
        }

        SwX11Painter::releaseSharedBackbuffer(display, snapshotPixmap);
        XFreePixmap(display, snapshotPixmap);

        if (!copied || pixels.empty()) {
            return false;
        }

        const SwByteArray png = encodePngRgb24(pixels.data(), width, height, width * 4);
        if (png.size() == 0) {
            return false;
        }

        SwFile out(filePath);
        if (!out.openBinary(SwFile::Write)) {
            return false;
        }
        const bool ok = out.write(png);
        out.close();
        return ok;
    }
#endif
};
