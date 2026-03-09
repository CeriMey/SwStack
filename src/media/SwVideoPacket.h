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
 * @file src/media/SwVideoPacket.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwVideoPacket in the CoreSw media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the video packet interface. The declarations exposed
 * here define the stable surface that adjacent code can rely on while the implementation remains
 * free to evolve behind the header.
 *
 * The main declarations in this header are SwVideoPacket.
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
 * @brief Declares the packet type exchanged between compressed-video pipeline stages.
 *
 * SwVideoPacket represents one encoded payload together with timing metadata,
 * key-frame information, and optional raw-format metadata when the packet
 * directly carries uncompressed frame bytes. It is the transport unit expected
 * by decoders and media sources in the current video stack.
 */

#include "media/SwVideoTypes.h"
#include "core/types/SwByteArray.h"

#include <cstdint>
#include <utility>

/**
 * @brief Stores an encoded or raw chunk of video data with decoding metadata.
 */
class SwVideoPacket {
public:
    enum class Codec {
        Unknown,
        RawRGB,
        RawBGR,
        RawRGBA,
        RawBGRA,
        H264,
        H265,
        VP8,
        VP9,
        AV1,
        MotionJPEG
    };

    /**
     * @brief Constructs a `SwVideoPacket` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVideoPacket() = default;

    /**
     * @brief Constructs a `SwVideoPacket` instance.
     * @param codec Value passed to the method.
     * @param payload Value passed to the method.
     * @param pts Value passed to the method.
     * @param dts Value passed to the method.
     * @param keyFrame Value passed to the method.
     * @param keyFrame Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwVideoPacket(Codec codec,
                  SwByteArray payload,
                  std::int64_t pts = -1,
                  std::int64_t dts = -1,
                  bool keyFrame = false)
        : m_codec(codec),
          m_payload(std::move(payload)),
          m_pts(pts),
          m_dts(dts),
          m_isKeyFrame(keyFrame) {}

    /**
     * @brief Returns the current codec.
     * @return The current codec.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    Codec codec() const { return m_codec; }
    /**
     * @brief Sets the codec.
     * @param codec Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setCodec(Codec codec) { m_codec = codec; }

    /**
     * @brief Returns the current payload.
     * @return The current payload.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwByteArray& payload() const { return m_payload; }
    /**
     * @brief Returns the current payload.
     * @return The current payload.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwByteArray& payload() { return m_payload; }

    /**
     * @brief Sets the payload.
     * @param payload Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPayload(const SwByteArray& payload) { m_payload = payload; }
    /**
     * @brief Sets the payload.
     * @param m_payload Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPayload(SwByteArray&& payload) { m_payload = std::move(payload); }

    /**
     * @brief Returns the current pts.
     * @return The current pts.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::int64_t pts() const { return m_pts; }
    /**
     * @brief Returns the current dts.
     * @return The current dts.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::int64_t dts() const { return m_dts; }
    /**
     * @brief Sets the pts.
     * @param pts Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPts(std::int64_t pts) { m_pts = pts; }
    /**
     * @brief Sets the dts.
     * @param dts Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDts(std::int64_t dts) { m_dts = dts; }

    /**
     * @brief Returns whether the object reports key Frame.
     * @return `true` when the object reports key Frame; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isKeyFrame() const { return m_isKeyFrame; }
    /**
     * @brief Sets the key Frame.
     * @param keyFrame Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setKeyFrame(bool keyFrame) { m_isKeyFrame = keyFrame; }

    /**
     * @brief Sets the raw Format.
     * @param info Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setRawFormat(const SwVideoFormatInfo& info) { m_rawFormat = info; }
    /**
     * @brief Returns the current raw Format.
     * @return The current raw Format.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    const SwVideoFormatInfo& rawFormat() const { return m_rawFormat; }
    /**
     * @brief Performs the `carriesRawFrame` operation.
     * @return `true` on success; otherwise `false`.
     */
    bool carriesRawFrame() const { return m_rawFormat.isValid(); }

private:
    Codec m_codec{Codec::Unknown};
    SwByteArray m_payload;
    std::int64_t m_pts{-1};
    std::int64_t m_dts{-1};
    bool m_isKeyFrame{false};
    SwVideoFormatInfo m_rawFormat{};
};
