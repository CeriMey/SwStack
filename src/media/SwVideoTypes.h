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

#include <array>
#include <cstddef>
#include <cstdint>

enum class SwVideoPixelFormat {
    Unknown,
    Gray8,
    RGB24,
    BGR24,
    RGBA32,
    BGRA32,
    NV12,
    YUV420P
};

enum class SwVideoColorSpace {
    Undefined,
    BT601,
    BT709,
    BT2020
};

enum class SwVideoRotation {
    Identity,
    Rotate90,
    Rotate180,
    Rotate270
};

/**
 * @brief Describes how a pixel format is laid out in memory.
 */
struct SwVideoFormatInfo {
    SwVideoPixelFormat format{SwVideoPixelFormat::Unknown};
    int width{0};
    int height{0};
    int planeCount{0};
    std::array<int, 4> stride{};
    std::array<int, 4> planeHeights{};
    std::array<std::size_t, 4> planeOffsets{};
    std::size_t dataSize{0};

    bool isValid() const {
        return format != SwVideoPixelFormat::Unknown && width > 0 && height > 0;
    }

    bool isPlanar() const {
        return planeCount > 1;
    }

    bool isPacked() const {
        return planeCount <= 1;
    }
};

/**
 * @brief Computes format information for a pixel format/size combination.
 */
inline SwVideoFormatInfo SwDescribeVideoFormat(SwVideoPixelFormat format, int width, int height) {
    SwVideoFormatInfo info;
    info.format = format;
    info.width = width;
    info.height = height;

    if (width <= 0 || height <= 0) {
        return info;
    }

    auto setPacked = [&](int bytesPerPixel) {
        info.planeCount = 1;
        info.stride[0] = width * bytesPerPixel;
        info.planeHeights[0] = height;
        info.planeOffsets[0] = 0;
        info.dataSize = static_cast<std::size_t>(info.stride[0]) * info.planeHeights[0];
    };

    auto setPlanar420 = [&](bool interleavedUV) {
        info.planeCount = interleavedUV ? 2 : 3;
        const int chromaWidth = (width + 1) / 2;
        const int chromaHeight = (height + 1) / 2;
        info.stride[0] = width;
        info.planeHeights[0] = height;
        info.planeOffsets[0] = 0;
        if (interleavedUV) {
            info.stride[1] = chromaWidth * 2;
            info.planeHeights[1] = chromaHeight;
            info.planeOffsets[1] = static_cast<std::size_t>(info.stride[0]) * info.planeHeights[0];
            info.dataSize =
                info.planeOffsets[1] + static_cast<std::size_t>(info.stride[1]) * info.planeHeights[1];
        } else {
            info.stride[1] = chromaWidth;
            info.planeHeights[1] = chromaHeight;
            info.planeOffsets[1] = static_cast<std::size_t>(info.stride[0]) * info.planeHeights[0];
            info.stride[2] = chromaWidth;
            info.planeHeights[2] = chromaHeight;
            info.planeOffsets[2] =
                info.planeOffsets[1] + static_cast<std::size_t>(info.stride[1]) * info.planeHeights[1];
            info.dataSize =
                info.planeOffsets[2] + static_cast<std::size_t>(info.stride[2]) * info.planeHeights[2];
        }
    };

    switch (format) {
    case SwVideoPixelFormat::Gray8:
        setPacked(1);
        break;
    case SwVideoPixelFormat::RGB24:
    case SwVideoPixelFormat::BGR24:
        setPacked(3);
        break;
    case SwVideoPixelFormat::RGBA32:
    case SwVideoPixelFormat::BGRA32:
        setPacked(4);
        break;
    case SwVideoPixelFormat::NV12:
        setPlanar420(true);
        break;
    case SwVideoPixelFormat::YUV420P:
        setPlanar420(false);
        break;
    default:
        info.planeCount = 0;
        info.dataSize = 0;
        break;
    }
    return info;
}

inline int SwVideoPlaneCount(SwVideoPixelFormat format) {
    switch (format) {
    case SwVideoPixelFormat::NV12: return 2;
    case SwVideoPixelFormat::YUV420P: return 3;
    case SwVideoPixelFormat::Unknown: return 0;
    default: return 1;
    }
}

inline bool SwVideoFormatIsRGB(SwVideoPixelFormat format) {
    return format == SwVideoPixelFormat::RGB24 || format == SwVideoPixelFormat::BGR24 ||
           format == SwVideoPixelFormat::RGBA32 || format == SwVideoPixelFormat::BGRA32;
}

inline bool SwVideoFormatNeedsAlpha(SwVideoPixelFormat format) {
    return format == SwVideoPixelFormat::RGBA32 || format == SwVideoPixelFormat::BGRA32;
}
