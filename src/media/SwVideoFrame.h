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

#include "media/SwVideoTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

class SwVideoFrame {
public:
    SwVideoFrame() = default;

    explicit SwVideoFrame(const SwVideoFormatInfo& info)
        : m_info(info) {
        if (m_info.isValid() && m_info.dataSize > 0) {
            allocateStorage(m_info.dataSize);
        }
    }

    static SwVideoFrame allocate(int width, int height, SwVideoPixelFormat format) {
        return SwVideoFrame(SwDescribeVideoFormat(format, width, height));
    }

    static SwVideoFrame fromCopy(int width,
                                 int height,
                                 SwVideoPixelFormat format,
                                 const void* data,
                                 std::size_t size) {
        SwVideoFrame frame = allocate(width, height, format);
        if (!data || size == 0 || !frame.isValid()) {
            return frame;
        }
        const std::size_t copyLen = std::min(size, frame.m_info.dataSize);
        std::memcpy(frame.m_bytes, data, copyLen);
        return frame;
    }

    static SwVideoFrame wrapExternal(const SwVideoFormatInfo& info,
                                     std::shared_ptr<void> external,
                                     std::size_t size,
                                     uint8_t* dataPtr = nullptr) {
        SwVideoFrame frame;
        frame.m_info = info;
        frame.m_buffer = std::move(external);
        frame.m_bytes = dataPtr ? dataPtr : static_cast<uint8_t*>(frame.m_buffer.get());
        frame.m_bufferSize = size;
        return frame;
    }

    bool isValid() const {
        return m_info.isValid() && m_bytes != nullptr;
    }

    int width() const { return m_info.width; }
    int height() const { return m_info.height; }
    SwVideoPixelFormat pixelFormat() const { return m_info.format; }

    int planeCount() const { return m_info.planeCount; }

    uint8_t* planeData(std::size_t planeIndex) {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return nullptr;
        }
        return m_bytes + m_info.planeOffsets[planeIndex];
    }

    const uint8_t* planeData(std::size_t planeIndex) const {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return nullptr;
        }
        return m_bytes + m_info.planeOffsets[planeIndex];
    }

    int planeStride(std::size_t planeIndex) const {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return 0;
        }
        return m_info.stride[planeIndex];
    }

    int planeHeight(std::size_t planeIndex) const {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return 0;
        }
        return m_info.planeHeights[planeIndex];
    }

    void setTimestamp(std::int64_t value) { m_pts = value; }
    std::int64_t timestamp() const { return m_pts; }

    void setColorSpace(SwVideoColorSpace cs) { m_colorSpace = cs; }
    SwVideoColorSpace colorSpace() const { return m_colorSpace; }

    void setRotation(SwVideoRotation rotation) { m_rotation = rotation; }
    SwVideoRotation rotation() const { return m_rotation; }

    void setAspectRatio(double ratio) { m_aspectRatio = ratio; }
    double aspectRatio() const { return m_aspectRatio; }

    const SwVideoFormatInfo& formatInfo() const { return m_info; }

    void clear() {
        m_buffer.reset();
        m_bytes = nullptr;
        m_bufferSize = 0;
        m_info = {};
    }

    void fill(uint8_t value) {
        if (!isValid()) {
            return;
        }
        std::memset(m_bytes, value, m_info.dataSize);
    }

    std::shared_ptr<void> buffer() const { return m_buffer; }
    std::size_t bufferSize() const { return m_bufferSize; }

private:
    void allocateStorage(std::size_t bytes) {
        if (bytes == 0) {
            return;
        }
        auto deleter = [](void* ptr) {
            delete[] static_cast<uint8_t*>(ptr);
        };
        m_buffer = std::shared_ptr<void>(static_cast<void*>(new uint8_t[bytes]), deleter);
        m_bytes = static_cast<uint8_t*>(m_buffer.get());
        m_bufferSize = bytes;
    }

    SwVideoFormatInfo m_info{};
    std::shared_ptr<void> m_buffer;
    uint8_t* m_bytes{nullptr};
    std::size_t m_bufferSize{0};
    std::int64_t m_pts{0};
    SwVideoColorSpace m_colorSpace{SwVideoColorSpace::Undefined};
    SwVideoRotation m_rotation{SwVideoRotation::Identity};
    double m_aspectRatio{0.0};
};
