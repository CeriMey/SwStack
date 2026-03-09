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

/**
 * @file src/media/SwVideoFrame.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwVideoFrame in the CoreSw media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the video frame interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwVideoFrame.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


/**
 * @file
 * @brief Declares the owning container used for decoded or raw video frame data.
 *
 * SwVideoFrame packages pixel storage together with format metadata such as
 * dimensions, plane layout, stride, color space, rotation, presentation time,
 * and aspect ratio. The class can either allocate its own backing store or wrap
 * externally managed memory through a shared owner handle.
 */

#include "media/SwVideoTypes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

/**
 * @brief Stores a video frame plus the metadata needed to interpret its planes.
 *
 * The class is designed to travel between sources, decoders, converters, and
 * rendering widgets without forcing every stage to agree on a single ownership
 * model. Internal storage and externally wrapped buffers are both supported.
 */
class SwVideoFrame {
public:
    /**
     * @brief Constructs a `SwVideoFrame` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVideoFrame() = default;

    /**
     * @brief Constructs a `SwVideoFrame` instance.
     * @param info Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwVideoFrame(const SwVideoFormatInfo& info)
        : m_info(info) {
        if (m_info.isValid() && m_info.dataSize > 0) {
            allocateStorage(m_info.dataSize);
        }
    }

    /**
     * @brief Performs the `allocate` operation.
     * @param width Width value.
     * @param height Height value.
     * @param format Value passed to the method.
     * @return The requested allocate.
     */
    static SwVideoFrame allocate(int width, int height, SwVideoPixelFormat format) {
        return SwVideoFrame(SwDescribeVideoFormat(format, width, height));
    }

    /**
     * @brief Performs the `fromCopy` operation.
     * @param width Width value.
     * @param height Height value.
     * @param format Value passed to the method.
     * @param data Value passed to the method.
     * @param size Size value used by the operation.
     * @return The requested from Copy.
     */
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

    /**
     * @brief Performs the `wrapExternal` operation.
     * @param info Value passed to the method.
     * @param external Value passed to the method.
     * @param size Size value used by the operation.
     * @param dataPtr Value passed to the method.
     * @return The requested wrap External.
     */
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

    /**
     * @brief Returns whether the object reports valid.
     * @return `true` when the object reports valid; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isValid() const {
        return m_info.isValid() && m_bytes != nullptr;
    }

    /**
     * @brief Returns the current width.
     * @return The current width.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int width() const { return m_info.width; }
    /**
     * @brief Returns the current height.
     * @return The current height.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int height() const { return m_info.height; }
    /**
     * @brief Returns the current pixel Format.
     * @return The current pixel Format.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwVideoPixelFormat pixelFormat() const { return m_info.format; }

    /**
     * @brief Returns the current plane Count.
     * @return The current plane Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int planeCount() const { return m_info.planeCount; }

    /**
     * @brief Performs the `planeData` operation.
     * @param planeIndex Value passed to the method.
     * @return The requested plane Data.
     */
    uint8_t* planeData(std::size_t planeIndex) {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return nullptr;
        }
        return m_bytes + m_info.planeOffsets[planeIndex];
    }

    /**
     * @brief Performs the `planeData` operation.
     * @param planeIndex Value passed to the method.
     * @return The requested plane Data.
     */
    const uint8_t* planeData(std::size_t planeIndex) const {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return nullptr;
        }
        return m_bytes + m_info.planeOffsets[planeIndex];
    }

    /**
     * @brief Performs the `planeStride` operation.
     * @param planeIndex Value passed to the method.
     * @return The requested plane Stride.
     */
    int planeStride(std::size_t planeIndex) const {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return 0;
        }
        return m_info.stride[planeIndex];
    }

    /**
     * @brief Performs the `planeHeight` operation.
     * @param planeIndex Value passed to the method.
     * @return The requested plane Height.
     */
    int planeHeight(std::size_t planeIndex) const {
        if (!isValid() || planeIndex >= static_cast<std::size_t>(planeCount())) {
            return 0;
        }
        return m_info.planeHeights[planeIndex];
    }

    /**
     * @brief Sets the timestamp.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setTimestamp(std::int64_t value) { m_pts = value; }
    /**
     * @brief Returns the current timestamp.
     * @return The current timestamp.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::int64_t timestamp() const { return m_pts; }

    /**
     * @brief Sets the color Space.
     * @param cs Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setColorSpace(SwVideoColorSpace cs) { m_colorSpace = cs; }
    /**
     * @brief Returns the current color Space.
     * @return The current color Space.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwVideoColorSpace colorSpace() const { return m_colorSpace; }

    /**
     * @brief Sets the rotation.
     * @param rotation Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRotation(SwVideoRotation rotation) { m_rotation = rotation; }
    /**
     * @brief Returns the current rotation.
     * @return The current rotation.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwVideoRotation rotation() const { return m_rotation; }

    /**
     * @brief Sets the aspect Ratio.
     * @param ratio Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setAspectRatio(double ratio) { m_aspectRatio = ratio; }
    /**
     * @brief Returns the current aspect Ratio.
     * @return The current aspect Ratio.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    double aspectRatio() const { return m_aspectRatio; }

    /**
     * @brief Returns the current format Info.
     * @return The current format Info.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwVideoFormatInfo& formatInfo() const { return m_info; }

    /**
     * @brief Clears the current object state.
     */
    void clear() {
        m_buffer.reset();
        m_bytes = nullptr;
        m_bufferSize = 0;
        m_info = {};
    }

    /**
     * @brief Performs the `fill` operation.
     * @param value Value passed to the method.
     */
    void fill(uint8_t value) {
        if (!isValid()) {
            return;
        }
        std::memset(m_bytes, value, m_info.dataSize);
    }

    /**
     * @brief Returns the current buffer.
     * @return The current buffer.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::shared_ptr<void> buffer() const { return m_buffer; }
    /**
     * @brief Returns the current buffer Size.
     * @return The current buffer Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
