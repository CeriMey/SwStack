#pragma once

/**
 * @file src/media/encoder/SwVideoEncoder.h
 * @brief Common live video encoder interface.
 */

#include "core/types/SwString.h"
#include "media/SwVideoFrame.h"
#include "media/SwVideoPacket.h"
#include "media/encoder/SwVideoEncoderConfig.h"

class SwVideoEncoder {
public:
    virtual ~SwVideoEncoder() = default;

    virtual SwString name() const = 0;
    virtual bool configure(const SwVideoEncoderConfig& config) = 0;
    virtual bool encodeFrame(const SwVideoFrame& frame, SwVideoPacket& outPacket) = 0;
    virtual void flush() {}
    virtual void requestKeyFrame() {}
    virtual bool setTargetBitrateKbps(uint32_t bitrateKbps) {
        m_targetBitrateKbps = bitrateKbps;
        return true;
    }

    uint32_t targetBitrateKbps() const {
        return m_targetBitrateKbps;
    }

protected:
    uint32_t m_targetBitrateKbps{0};
};
