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
#include "core/types/SwByteArray.h"

#include <cstdint>
#include <utility>

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

    SwVideoPacket() = default;

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

    Codec codec() const { return m_codec; }
    void setCodec(Codec codec) { m_codec = codec; }

    const SwByteArray& payload() const { return m_payload; }
    SwByteArray& payload() { return m_payload; }

    void setPayload(const SwByteArray& payload) { m_payload = payload; }
    void setPayload(SwByteArray&& payload) { m_payload = std::move(payload); }

    std::int64_t pts() const { return m_pts; }
    std::int64_t dts() const { return m_dts; }
    void setPts(std::int64_t pts) { m_pts = pts; }
    void setDts(std::int64_t dts) { m_dts = dts; }

    bool isKeyFrame() const { return m_isKeyFrame; }
    void setKeyFrame(bool keyFrame) { m_isKeyFrame = keyFrame; }

    void setRawFormat(const SwVideoFormatInfo& info) { m_rawFormat = info; }
    const SwVideoFormatInfo& rawFormat() const { return m_rawFormat; }
    bool carriesRawFrame() const { return m_rawFormat.isValid(); }

private:
    Codec m_codec{Codec::Unknown};
    SwByteArray m_payload;
    std::int64_t m_pts{-1};
    std::int64_t m_dts{-1};
    bool m_isKeyFrame{false};
    SwVideoFormatInfo m_rawFormat{};
};
